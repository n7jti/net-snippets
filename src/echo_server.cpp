#include <winsock2.h>
#include <windows.h>
#include <shellapi.h>
#include <ws2tcpip.h>
#include <cstdlib>
#include <iostream>

namespace {
constexpr int kDefaultPort = 7000;
constexpr int kEchoBufferSize = 1024;
}

static void print_usage() {
    std::wcout << L"Usage: echo-server.exe [port]\n";
    std::wcout << L"Starts a simple TCP echo server bound to all interfaces.\n";
}

int wmain() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) {
        std::cerr << "Command line parsing failed.\n";
        return 1;
    }

    int port = kDefaultPort;
    if (argc > 1) {
        if (_wcsicmp(argv[1], L"-h") == 0 || _wcsicmp(argv[1], L"--help") == 0 || _wcsicmp(argv[1], L"/?") == 0) {
            print_usage();
            LocalFree(argv);
            return 0;
        }

        wchar_t* end = nullptr;
        long parsed = wcstol(argv[1], &end, 10);
        if (end == argv[1] || *end != L'\0' || parsed < 1 || parsed > 65535) {
            std::cerr << "Invalid port. Must be a number between 1 and 65535.\n";
            LocalFree(argv);
            return 1;
        }
        port = static_cast<int>(parsed);
    }

    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        std::cerr << "WSAStartup failed.\n";
        LocalFree(argv);
        return 1;
    }

    addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    char port_text[16] = {0};
    _snprintf_s(port_text, sizeof(port_text), _TRUNCATE, "%d", port);

    addrinfo* bind_info = nullptr;
    if (getaddrinfo(nullptr, port_text, &hints, &bind_info) != 0) {
        std::cerr << "getaddrinfo failed.\n";
        WSACleanup();
        LocalFree(argv);
        return 1;
    }

    SOCKET listen_socket = socket(bind_info->ai_family, bind_info->ai_socktype, bind_info->ai_protocol);
    if (listen_socket == INVALID_SOCKET) {
        std::cerr << "socket creation failed.\n";
        freeaddrinfo(bind_info);
        WSACleanup();
        LocalFree(argv);
        return 1;
    }

    if (bind(listen_socket, bind_info->ai_addr, static_cast<int>(bind_info->ai_addrlen)) == SOCKET_ERROR) {
        std::cerr << "bind failed.\n";
        closesocket(listen_socket);
        freeaddrinfo(bind_info);
        WSACleanup();
        LocalFree(argv);
        return 1;
    }

    freeaddrinfo(bind_info);

    if (listen(listen_socket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "listen failed.\n";
        closesocket(listen_socket);
        WSACleanup();
        LocalFree(argv);
        return 1;
    }

    std::cout << "Echo server listening on port " << port << "\n";

    while (true) {
        SOCKET client = accept(listen_socket, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            std::cerr << "accept failed.\n";
            break;
        }

        std::cout << "Client connected.\n";

        char buffer[kEchoBufferSize];
        int received = 0;
        while ((received = recv(client, buffer, sizeof(buffer), 0)) > 0) {
            int sent_total = 0;
            while (sent_total < received) {
                int sent = send(client, buffer + sent_total, received - sent_total, 0);
                if (sent == SOCKET_ERROR) {
                    std::cerr << "send failed.\n";
                    break;
                }
                sent_total += sent;
            }
            if (sent_total < received) {
                break;
            }
        }

        closesocket(client);
        std::cout << "Client disconnected.\n";
    }

    closesocket(listen_socket);
    WSACleanup();
    LocalFree(argv);
    return 0;
}
