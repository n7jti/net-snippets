#include <winsock2.h>
#include <windows.h>
#include <shellapi.h>
#include <ws2tcpip.h>
#include <cstdlib>
#include <atomic>
#include <iostream>
#include <wil/resource.h>

namespace {
// Default TCP port the echo server listens on if no port argument is provided.
constexpr int kDefaultPort = 7000;
// Size of the buffer used to receive and echo data from each client.
constexpr int kEchoBufferSizeBytes = 1024;
// Set to false by the console control handler to signal the server loop to stop.
std::atomic_bool g_keep_running = true;

// Parsed command-line options.
struct command_options_t {
    bool show_help;
    int port;
};

// Holds the listening socket for the server's lifetime.
struct server_context_t {
    wil::unique_socket listen_socket;
};

// Console control handler invoked by Windows when the user presses Ctrl+C, Ctrl+Break,
// or closes the console window. Signals the server loop to stop gracefully.
BOOL WINAPI on_console_ctrl(DWORD control_type) {
    if (control_type == CTRL_C_EVENT || control_type == CTRL_BREAK_EVENT || control_type == CTRL_CLOSE_EVENT) {
        g_keep_running = false;
        return TRUE;
    }

    // Return FALSE for unhandled events to pass them to the next handler in the chain.
    return FALSE;
}

// Prints usage information to the console.
void print_usage() {
    std::wcout << L"Usage: echo-server.exe [-p|--port <port>] [<port>]\n";
    std::wcout << L"Starts a simple TCP echo server bound to all interfaces.\n";
    std::wcout << L"  -p, --port <port>  TCP port to listen on (default: " << kDefaultPort << L")\n";
    std::wcout << L"  -h, --help, /?     Show this help message\n";
}

// Parses a port number string and validates it is in the valid TCP port range (1-65535).
// Returns true and sets *port_out on success, or false if the string is not a valid port number.
static bool parse_port(const wchar_t* str, int* port_out) {
    wchar_t* end = nullptr;
    long parsed = wcstol(str, &end, 10);
    if (end == str || *end != L'\0' || parsed < 1 || parsed > 65535) {
        return false;
    }
    *port_out = static_cast<int>(parsed);
    return true;
}

// Parses the command line into options. Accepts an optional port number, either as a bare
// positional argument or via the -p/--port flag, as well as a help flag.
// Uses a WIL smart pointer so the argv buffer returned by CommandLineToArgvW is always freed.
// Returns ERROR_SUCCESS on success, or a Windows error code on failure.
int parse_command_line(command_options_t* options) {
    int argc = 0;
    wil::unique_hlocal_ptr<PWSTR[]> argv(CommandLineToArgvW(GetCommandLineW(), &argc));
    if (!argv) {
        return GetLastError();
    }

    options->show_help = false;
    options->port = kDefaultPort;

    for (int i = 1; i < argc; ++i) {
        if (_wcsicmp(argv[i], L"-h") == 0 || _wcsicmp(argv[i], L"--help") == 0 || _wcsicmp(argv[i], L"/?") == 0) {
            options->show_help = true;
        } else if (_wcsicmp(argv[i], L"-p") == 0 || _wcsicmp(argv[i], L"--port") == 0) {
            // -p/--port requires a following argument containing the port number.
            if (i + 1 >= argc) {
                std::wcerr << L"Missing port value after " << argv[i] << L".\n";
                return ERROR_BAD_ARGUMENTS;
            }
            ++i;
            if (!parse_port(argv[i], &options->port)) {
                std::cerr << "Invalid port. Use a number from 1 to 65535.\n";
                return ERROR_BAD_ARGUMENTS;
            }
        } else {
            // Treat an unrecognized argument as a bare positional port number.
            if (!parse_port(argv[i], &options->port)) {
                std::wcerr << L"Unrecognized argument: " << argv[i] << L". Use -h for help.\n";
                return ERROR_BAD_ARGUMENTS;
            }
        }
    }

    return ERROR_SUCCESS;
}

// Creates a TCP listening socket bound to all interfaces on the given port, and stores it
// in context->listen_socket. Returns ERROR_SUCCESS on success, or a Winsock error code on failure.
//
// The process follows four steps required to prepare a server socket:
//   1. getaddrinfo  - resolve the local address and port into a sockaddr structure
//   2. socket       - allocate a socket file descriptor with the desired protocol
//   3. bind         - associate the socket with the local address and port
//   4. listen       - mark the socket as passive and ready to accept incoming connections
int create_listen_socket(server_context_t* context, int port) {
    // Step 1: getaddrinfo
    // getaddrinfo() translates a host name / service name pair into a linked list of addrinfo
    // structures, each containing a sockaddr ready to pass to bind() or connect().
    //
    // The hints structure controls which results are returned:
    //   ai_family   = AF_INET      - request IPv4 addresses only
    //   ai_socktype = SOCK_STREAM  - request stream (TCP) sockets, not datagrams (UDP)
    //   ai_protocol = IPPROTO_TCP  - be explicit about TCP (redundant with SOCK_STREAM but clear)
    //   ai_flags    = AI_PASSIVE   - with a null node name, resolve to the wildcard address
    //                               (INADDR_ANY / 0.0.0.0), meaning accept on all interfaces
    addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    // getaddrinfo requires the port as a decimal string, not an integer.
    char port_string[16] = {0};
    _snprintf_s(port_string, sizeof(port_string), _TRUNCATE, "%d", port);

    // Passing nullptr as the node name combined with AI_PASSIVE yields a bind address of 0.0.0.0.
    addrinfo* bind_info_raw = nullptr;
    int getaddrinfo_result = getaddrinfo(nullptr, port_string, &hints, &bind_info_raw);
    if (getaddrinfo_result != 0) {
        return getaddrinfo_result;
    }
    wil::unique_addrinfo bind_info(bind_info_raw);

    // Step 2: socket
    // socket() allocates a new socket and returns a handle (file descriptor) for it.
    // The three parameters must be consistent with each other and are taken directly from
    // the addrinfo result so they are guaranteed to be a valid combination:
    //   ai_family   - address family (AF_INET for IPv4)
    //   ai_socktype - socket type (SOCK_STREAM for a reliable, ordered byte stream)
    //   ai_protocol - protocol (IPPROTO_TCP)
    // At this point the socket exists in the OS but is not yet associated with any address.
    // wil::unique_socket closes the socket automatically on every error path unless ownership is moved.
    wil::unique_socket listen_socket(::socket(bind_info.get()->ai_family, bind_info.get()->ai_socktype, bind_info.get()->ai_protocol));
    if (!listen_socket) {
        return WSAGetLastError();
    }

    // Step 3: bind
    // bind() assigns a local address and port to the socket. Until bind() is called the OS
    // has not reserved the port; after bind() succeeds, no other process can bind the same
    // address/port combination (unless SO_REUSEADDR is set).
    // ai_addr and ai_addrlen are the sockaddr and its size, filled in by getaddrinfo().
    if (::bind(listen_socket.get(), bind_info.get()->ai_addr, static_cast<int>(bind_info.get()->ai_addrlen)) == SOCKET_ERROR) {
        return WSAGetLastError();
    }

    // Step 4: listen
    // listen() transitions the socket from an unconnected state into a passive listening state.
    // After this call the OS will start queuing incoming TCP connection requests.
    // SOMAXCONN lets the OS choose the maximum backlog queue length (typically 128-200 on Windows).
    if (::listen(listen_socket.get(), SOMAXCONN) == SOCKET_ERROR) {
        return WSAGetLastError();
    }

    context->listen_socket = std::move(listen_socket);
    return ERROR_SUCCESS;
}

// Runs the echo loop for a single connected client: receives data and sends it back verbatim.
// Loops until the client disconnects, a non-timeout error occurs, or g_keep_running is cleared.
// Returns ERROR_SUCCESS on clean disconnect or shutdown, or a Winsock error code on failure.
int run_echo_session(SOCKET client_socket) {
    // Use a short receive timeout so the loop can periodically check g_keep_running
    // rather than blocking indefinitely on recv().
    DWORD recv_timeout_ms = 1000;
    if (setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&recv_timeout_ms), sizeof(recv_timeout_ms)) == SOCKET_ERROR) {
        return WSAGetLastError();
    }

    char buffer[kEchoBufferSizeBytes];
    while (g_keep_running.load()) {
        int received = recv(client_socket, buffer, sizeof(buffer), 0);
        // A return value of 0 means the client has closed the connection gracefully.
        if (received == 0) {
            return ERROR_SUCCESS;
        }

        if (received < 0) {
            int recv_error = WSAGetLastError();
            // A timeout is not an error — loop again to check g_keep_running.
            if (recv_error == WSAETIMEDOUT) {
                continue;
            }

            return recv_error;
        }

        // send() may not send all bytes in one call, so loop until all received bytes are echoed back.
        int sent_total = 0;
        while (sent_total < received) {
            int sent = send(client_socket, buffer + sent_total, received - sent_total, 0);
            if (sent == SOCKET_ERROR) {
                return WSAGetLastError();
            }

            sent_total += sent;
        }
    }

    return ERROR_SUCCESS;
}

// Accepts and processes client connections sequentially until g_keep_running is cleared.
// Uses select() with a 1-second timeout to remain responsive to shutdown signals without
// blocking forever on accept(). Returns ERROR_SUCCESS on clean shutdown, or a Winsock error code on failure.
int run_server_loop(server_context_t* context) {
    while (g_keep_running.load()) {
        // Use select() to wait for an incoming connection with a timeout so we can
        // periodically re-check g_keep_running without blocking indefinitely.
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(context->listen_socket.get(), &read_fds);

        timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int select_result = select(0, &read_fds, nullptr, nullptr, &timeout);
        if (select_result == SOCKET_ERROR) {
            return WSAGetLastError();
        }

        // A result of 0 means the timeout elapsed with no incoming connection; loop again.
        if (select_result == 0) {
            continue;
        }

        // Wrap each accepted client socket in wil::unique_socket so it is always closed,
        // even if the session exits early because of an error.
        wil::unique_socket client_socket(::accept(context->listen_socket.get(), nullptr, nullptr));
        if (!client_socket) {
            return WSAGetLastError();
        }

        std::cout << "Client connected.\n";
        int session_result = run_echo_session(client_socket.get());
        std::cout << "Client disconnected.\n";

        if (session_result != ERROR_SUCCESS) {
            return session_result;
        }
    }

    return ERROR_SUCCESS;
}
}

// Application entry point. Parses arguments, initializes Winsock, registers the console
// control handler, creates the listen socket, and runs the server loop until shutdown.
int wmain() {
    command_options_t options;
    // Parse the command line first so we can exit early on invalid input before touching Winsock.
    int parse_result = parse_command_line(&options);
    if (parse_result != ERROR_SUCCESS) {
        std::cerr << "Command line parse failed with error " << parse_result << ".\n";
        return parse_result;
    }

    int exit_code = ERROR_SUCCESS;
    if (options.show_help) {
        // Help mode is intentionally side-effect free.
        print_usage();
    } else {
        // Winsock must be initialized before any socket calls or address resolution.
        WSADATA wsa_data = {};
        int setup_result = ::WSAStartup(MAKEWORD(2, 2), &wsa_data);
        if (setup_result != ERROR_SUCCESS) {
            std::cerr << "WSAStartup failed with error " << setup_result << ".\n";
            exit_code = setup_result;
        } else {
            // Scope-based cleanup keeps the shutdown path correct even on early returns or failures.
            // This ensures WSACleanup is always paired with a successful WSAStartup.
            const auto network_cleanup = wil::scope_exit([] {
                ::WSACleanup();
            });

            server_context_t context = {};
            // Register a console handler so Ctrl+C can stop the accept loop cleanly.
            if (!::SetConsoleCtrlHandler(on_console_ctrl, TRUE)) {
                exit_code = static_cast<int>(GetLastError());
                std::cerr << "SetConsoleCtrlHandler failed with error " << exit_code << ".\n";
            } else {
                // Restore the previous console handler registration when this scope ends.
                const auto console_handler_cleanup = wil::scope_exit([] {
                    ::SetConsoleCtrlHandler(on_console_ctrl, FALSE);
                });

                // Create, bind, and listen on the server socket before entering the accept loop.
                int listen_result = create_listen_socket(&context, options.port);
                if (listen_result != ERROR_SUCCESS) {
                    std::cerr << "Socket setup failed with error " << listen_result << ".\n";
                    exit_code = listen_result;
                } else {
                    // At this point the server is live and ready to accept connections.
                    std::cout << "Echo server listening on port " << options.port << "\n";
                    std::cout << "Press Ctrl+C to stop.\n";

                    // Run until the console handler clears the shutdown flag or an error occurs.
                    int process_result = run_server_loop(&context);
                    if (process_result != ERROR_SUCCESS) {
                        std::cerr << "Server loop failed with error " << process_result << ".\n";
                        exit_code = process_result;
                    }
                }
            }
        }
    }

    return exit_code;
}