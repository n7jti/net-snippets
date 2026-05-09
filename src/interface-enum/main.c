#include <winsock2.h>
#include <windows.h>
#include <shellapi.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <wchar.h>
#include <stdio.h>
#include <stdlib.h>

enum { INITIAL_ADAPTER_BUFFER_SIZE_BYTES = 15 * 1024 };

typedef struct command_options_t {
    BOOL show_help;
} command_options_t;

static void print_usage(void) {
    wprintf(L"Usage: interface-enum.exe\n");
    wprintf(L"Enumerates network interfaces, MAC addresses, IP addresses, and media state.\n");
}

static void print_mac_address(const BYTE* address, ULONG length) {
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

static DWORD parse_command_line(command_options_t* options, LPWSTR** argv_out) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == NULL) {
        return GetLastError();
    }

    options->show_help = FALSE;
    if (argc > 1) {
        if (_wcsicmp(argv[1], L"-h") == 0 || _wcsicmp(argv[1], L"--help") == 0 || _wcsicmp(argv[1], L"/?") == 0) {
            options->show_help = TRUE;
        } else {
            LocalFree(argv);
            return ERROR_BAD_ARGUMENTS;
        }
    }

    *argv_out = argv;
    return ERROR_SUCCESS;
}

static DWORD load_adapter_addresses(IP_ADAPTER_ADDRESSES** adapters_out) {
    ULONG buffer_size = INITIAL_ADAPTER_BUFFER_SIZE_BYTES;
    IP_ADAPTER_ADDRESSES* adapters = (IP_ADAPTER_ADDRESSES*)malloc(buffer_size);
    if (adapters == NULL) {
        return ERROR_OUTOFMEMORY;
    }

    ULONG result = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, NULL, adapters, &buffer_size);
    if (result == ERROR_BUFFER_OVERFLOW) {
        free(adapters);
        adapters = (IP_ADAPTER_ADDRESSES*)malloc(buffer_size);
        if (adapters == NULL) {
            return ERROR_OUTOFMEMORY;
        }

        result = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, NULL, adapters, &buffer_size);
    }

    if (result != NO_ERROR) {
        free(adapters);
        return result;
    }

    *adapters_out = adapters;
    return ERROR_SUCCESS;
}

static void emit_interface_report(const IP_ADAPTER_ADDRESSES* adapters) {
    for (const IP_ADAPTER_ADDRESSES* adapter = adapters; adapter != NULL; adapter = adapter->Next) {
        wprintf(L"Interface: %ls\n", adapter->FriendlyName != NULL ? adapter->FriendlyName : L"(unknown)");
        wprintf(L"  MAC: ");
        print_mac_address(adapter->PhysicalAddress, adapter->PhysicalAddressLength);
        wprintf(L"\n");
        wprintf(L"  Media attached: %ls\n", adapter->OperStatus == IfOperStatusUp ? L"yes" : L"no");

        if (adapter->FirstUnicastAddress == NULL) {
            wprintf(L"  IP Addresses: (none)\n");
        } else {
            wprintf(L"  IP Addresses:\n");
            for (const IP_ADAPTER_UNICAST_ADDRESS* unicast = adapter->FirstUnicastAddress; unicast != NULL; unicast = unicast->Next) {
                char ip_address_string[NI_MAXHOST] = {0};
                int getnameinfo_result = getnameinfo(
                    unicast->Address.lpSockaddr,
                    (int)unicast->Address.iSockaddrLength,
                    ip_address_string,
                    sizeof(ip_address_string),
                    NULL,
                    0,
                    NI_NUMERICHOST);

                if (getnameinfo_result == 0) {
                    wprintf(L"    - %S\n", ip_address_string);
                }
            }
        }

        wprintf(L"\n");
    }
}

static DWORD run_interface_enumeration(void) {
    IP_ADAPTER_ADDRESSES* adapters = NULL;
    DWORD result = load_adapter_addresses(&adapters);
    if (result != ERROR_SUCCESS) {
        return result;
    }

    emit_interface_report(adapters);
    free(adapters);
    return ERROR_SUCCESS;
}

int wmain(void) {
    command_options_t options;
    LPWSTR* argv = NULL;

    DWORD parse_result = parse_command_line(&options, &argv);
    if (parse_result != ERROR_SUCCESS) {
        fwprintf(stderr, L"Command line parse failed with error %lu.\n", parse_result);
        return (int)parse_result;
    }

    int exit_code = 0;
    if (options.show_help) {
        print_usage();
    } else {
        DWORD run_result = run_interface_enumeration();
        if (run_result != ERROR_SUCCESS) {
            fwprintf(stderr, L"Interface enumeration failed with error %lu.\n", run_result);
            exit_code = (int)run_result;
        }
    }

    LocalFree(argv);
    return exit_code;
}
