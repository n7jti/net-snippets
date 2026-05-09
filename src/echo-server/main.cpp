#include <winsock2.h>
#include <windows.h>
#include <shellapi.h>
#include <ws2tcpip.h>
#include <cstdlib>
#include <atomic>
#include <iostream>

namespace {
constexpr int kDefaultPort = 7000;
constexpr int kEchoBufferSizeBytes = 1024;
std::atomic_bool g_keep_running = true;

struct command_options_t {
    bool show_help;
    int port;
};

struct server_context_t {
    WSADATA wsa_data;
    SOCKET listen_socket;
};

BOOL WINAPI on_console_ctrl(DWORD control_type) {
    if (control_type == CTRL_C_EVENT || control_type == CTRL_BREAK_EVENT || control_type == CTRL_CLOSE_EVENT) {
        g_keep_running = false;
        return TRUE;
    }

    return FALSE;
}

void print_usage() {
    std::wcout << L"Usage: echo-server.exe [port]\n";
    std::wcout << L"Starts a simple TCP echo server bound to all interfaces.\n";
}

int parse_command_line(command_options_t* options, LPWSTR** argv_out) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) {
        return GetLastError();
    }

    options->show_help = false;
    options->port = kDefaultPort;

    if (argc > 2) {
        LocalFree(argv);
        return ERROR_BAD_ARGUMENTS;
    }

    if (argc == 2) {
        if (_wcsicmp(argv[1], L"-h") == 0 || _wcsicmp(argv[1], L"--help") == 0 || _wcsicmp(argv[1], L"/?") == 0) {
            options->show_help = true;
        } else {
            wchar_t* end = nullptr;
            long parsed = wcstol(argv[1], &end, 10);
            if (end == argv[1] || *end != L'\0' || parsed < 1 || parsed > 65535) {
                LocalFree(argv);
                return ERROR_BAD_ARGUMENTS;
            }

            options->port = static_cast<int>(parsed);
        }
    }

    *argv_out = argv;
    return ERROR_SUCCESS;
}

int initialize_network_stack(server_context_t* context) {
    context->listen_socket = INVALID_SOCKET;
    int startup_result = WSAStartup(MAKEWORD(2, 2), &context->wsa_data);
    if (startup_result != 0) {
        return startup_result;
    }

    return ERROR_SUCCESS;
}

void teardown_network_stack(server_context_t* context) {
    if (context->listen_socket != INVALID_SOCKET) {
        closesocket(context->listen_socket);
        context->listen_socket = INVALID_SOCKET;
    }

    WSACleanup();
}

int create_listen_socket(server_context_t* context, int port) {
    addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    char port_string[16] = {0};
    _snprintf_s(port_string, sizeof(port_string), _TRUNCATE, "%d", port);

    addrinfo* bind_info = nullptr;
    int getaddrinfo_result = getaddrinfo(nullptr, port_string, &hints, &bind_info);
    if (getaddrinfo_result != 0) {
        return getaddrinfo_result;
    }

    SOCKET listen_socket = socket(bind_info->ai_family, bind_info->ai_socktype, bind_info->ai_protocol);
    if (listen_socket == INVALID_SOCKET) {
        int socket_error = WSAGetLastError();
        freeaddrinfo(bind_info);
        return socket_error;
    }

    if (bind(listen_socket, bind_info->ai_addr, static_cast<int>(bind_info->ai_addrlen)) == SOCKET_ERROR) {
        int bind_error = WSAGetLastError();
        freeaddrinfo(bind_info);
        closesocket(listen_socket);
        return bind_error;
    }

    freeaddrinfo(bind_info);

    if (listen(listen_socket, SOMAXCONN) == SOCKET_ERROR) {
        int listen_error = WSAGetLastError();
        closesocket(listen_socket);
        return listen_error;
    }

    context->listen_socket = listen_socket;
    return ERROR_SUCCESS;
}

int run_echo_session(SOCKET client_socket) {
    DWORD recv_timeout_ms = 1000;
    if (setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&recv_timeout_ms), sizeof(recv_timeout_ms)) == SOCKET_ERROR) {
        return WSAGetLastError();
    }

    char buffer[kEchoBufferSizeBytes];
    while (g_keep_running.load()) {
        int received = recv(client_socket, buffer, sizeof(buffer), 0);
        if (received == 0) {
            return ERROR_SUCCESS;
        }

        if (received < 0) {
            int recv_error = WSAGetLastError();
            if (recv_error == WSAETIMEDOUT) {
                continue;
            }

            return recv_error;
        }

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

int run_server_loop(server_context_t* context) {
    while (g_keep_running.load()) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(context->listen_socket, &read_fds);

        timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int select_result = select(0, &read_fds, nullptr, nullptr, &timeout);
        if (select_result == SOCKET_ERROR) {
            return WSAGetLastError();
        }

        if (select_result == 0) {
            continue;
        }

        SOCKET client_socket = accept(context->listen_socket, nullptr, nullptr);
        if (client_socket == INVALID_SOCKET) {
            return WSAGetLastError();
        }

        std::cout << "Client connected.\n";
        int session_result = run_echo_session(client_socket);
        closesocket(client_socket);
        std::cout << "Client disconnected.\n";

        if (session_result != ERROR_SUCCESS) {
            return session_result;
        }
    }

    return ERROR_SUCCESS;
}
}

int wmain() {
    command_options_t options;
    LPWSTR* argv = nullptr;
    int parse_result = parse_command_line(&options, &argv);
    if (parse_result != ERROR_SUCCESS) {
        std::cerr << "Command line parse failed with error " << parse_result << ".\n";
        return parse_result;
    }

    int exit_code = ERROR_SUCCESS;
    if (options.show_help) {
        print_usage();
    } else {
        server_context_t context = {};
        int setup_result = initialize_network_stack(&context);
        if (setup_result != ERROR_SUCCESS) {
            std::cerr << "WSAStartup failed with error " << setup_result << ".\n";
            exit_code = setup_result;
        } else if (!SetConsoleCtrlHandler(on_console_ctrl, TRUE)) {
            exit_code = static_cast<int>(GetLastError());
            std::cerr << "SetConsoleCtrlHandler failed with error " << exit_code << ".\n";
            teardown_network_stack(&context);
        } else {
            int listen_result = create_listen_socket(&context, options.port);
            if (listen_result != ERROR_SUCCESS) {
                std::cerr << "Socket setup failed with error " << listen_result << ".\n";
                exit_code = listen_result;
            } else {
                std::cout << "Echo server listening on port " << options.port << "\n";
                std::cout << "Press Ctrl+C to stop.\n";

                int process_result = run_server_loop(&context);
                if (process_result != ERROR_SUCCESS) {
                    std::cerr << "Server loop failed with error " << process_result << ".\n";
                    exit_code = process_result;
                }
            }

            SetConsoleCtrlHandler(on_console_ctrl, FALSE);
            teardown_network_stack(&context);
        }
    }

    LocalFree(argv);
    return exit_code;
}
