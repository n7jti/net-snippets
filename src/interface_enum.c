#include <winsock2.h>
#include <windows.h>
#include <shellapi.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <wchar.h>
#include <stdio.h>
#include <stdlib.h>

enum { INITIAL_ADAPTER_BUFFER_SIZE_BYTES = 15 * 1024 };

static void print_usage(void) {
    wprintf(L"Usage: interface-enum.exe\n");
    wprintf(L"Enumerates network interfaces, MAC addresses, IP addresses, and media state.\n");
}

static void print_mac(const BYTE* address, ULONG length) {
    if (address == NULL || length == 0) {
        wprintf(L"(none)");
        return;
    }

    for (ULONG i = 0; i < length; ++i) {
        wprintf(L"%02X", address[i]);
        if (i + 1 < length) {
            wprintf(L":");
        }
    }
}

int wmain(void) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == NULL) {
        fwprintf(stderr, L"Command line parsing failed.\n");
        return 1;
    }

    if (argc > 1 && (_wcsicmp(argv[1], L"-h") == 0 || _wcsicmp(argv[1], L"--help") == 0 || _wcsicmp(argv[1], L"/?") == 0)) {
        print_usage();
        LocalFree(argv);
        return 0;
    }

    ULONG buffer_size = INITIAL_ADAPTER_BUFFER_SIZE_BYTES;
    IP_ADAPTER_ADDRESSES* adapters = (IP_ADAPTER_ADDRESSES*)malloc(buffer_size);
    if (adapters == NULL) {
        fwprintf(stderr, L"Memory allocation failed.\n");
        LocalFree(argv);
        return 1;
    }

    ULONG result = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, NULL, adapters, &buffer_size);
    if (result == ERROR_BUFFER_OVERFLOW) {
        free(adapters);
        adapters = (IP_ADAPTER_ADDRESSES*)malloc(buffer_size);
        if (adapters == NULL) {
            fwprintf(stderr, L"Memory allocation failed.\n");
            LocalFree(argv);
            return 1;
        }
        result = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, NULL, adapters, &buffer_size);
    }

    if (result != NO_ERROR) {
        fwprintf(stderr, L"GetAdaptersAddresses failed with error %lu.\n", result);
        free(adapters);
        LocalFree(argv);
        return 1;
    }

    for (IP_ADAPTER_ADDRESSES* adapter = adapters; adapter != NULL; adapter = adapter->Next) {
        wprintf(L"Interface: %ls\n", adapter->FriendlyName != NULL ? adapter->FriendlyName : L"(unknown)");
        wprintf(L"  MAC: ");
        print_mac(adapter->PhysicalAddress, adapter->PhysicalAddressLength);
        wprintf(L"\n");
        wprintf(L"  Media attached: %ls\n", adapter->OperStatus == IfOperStatusUp ? L"yes" : L"no");

        if (adapter->FirstUnicastAddress == NULL) {
            wprintf(L"  IP Addresses: (none)\n");
        } else {
            wprintf(L"  IP Addresses:\n");
            for (IP_ADAPTER_UNICAST_ADDRESS* ua = adapter->FirstUnicastAddress; ua != NULL; ua = ua->Next) {
                char ip_address_string[NI_MAXHOST] = {0};
                int rc = getnameinfo(
                    ua->Address.lpSockaddr,
                    (int)ua->Address.iSockaddrLength,
                    ip_address_string,
                    sizeof(ip_address_string),
                    NULL,
                    0,
                    NI_NUMERICHOST);

                if (rc == 0) {
                    wprintf(L"    - %S\n", ip_address_string);
                }
            }
        }

        wprintf(L"\n");
    }

    free(adapters);
    LocalFree(argv);
    return 0;
}
