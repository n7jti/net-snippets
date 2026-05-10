#include <winsock2.h>
#include <windows.h>
#include <shellapi.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <wchar.h>
#include <stdio.h>
#include <stdlib.h>

// Initial size of the buffer for adapter addresses. If this is too small, the code will reallocate with the required size.
#define INITIAL_ADAPTER_BUFFER_SIZE_BYTES  (15 * 1024)


// Structure to hold command line options. Currently only supports a help flag, but can be extended in the future.
typedef struct command_options_t {
    BOOL show_help;
} command_options_t;


// Function declarations

// Prints usage information to the console.
static void print_usage(void) {
    wprintf(L"Usage: interface-enum.exe\n");
    wprintf(L"Enumerates network interfaces, MAC addresses, IP addresses, and media state.\n");
}

// Converts an ADDRESS_FAMILY value to a human-readable string.
static const wchar_t* get_address_family_string(ADDRESS_FAMILY family) {
    switch (family) {
        case AF_INET:
            return L"IPv4";
        case AF_INET6:
            return L"IPv6";
        default:
            return L"Unknown";
    }
}

// Prints a MAC address in the format "XX:XX:XX:XX:XX:XX". If the address is NULL or length is 0, prints "(none)".
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

//
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

// Loads the adapter addresses using GetAdaptersAddresses. If the initial buffer is too small, it reallocates with the required size and tries again.
// On success, *adapters_out will point to a buffer containing the adapter addresses, and the caller is responsible for freeing it with free(). 
//On failure, returns a Windows error code.
static DWORD load_adapter_addresses(IP_ADAPTER_ADDRESSES** adapters_out) {
    ULONG buffer_size = INITIAL_ADAPTER_BUFFER_SIZE_BYTES;

    // Allocate an initial buffer for the adapter addresses. 
    IP_ADAPTER_ADDRESSES* adapters = (IP_ADAPTER_ADDRESSES*)malloc(buffer_size);
    if (adapters == NULL) {
        return ERROR_OUTOFMEMORY;
    }

    // Call GetAdaptersAddresses to fill the buffer. If the buffer is too small, it will return ERROR_BUFFER_OVERFLOW and set buffer_size to the required size.
    ULONG result = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, NULL, adapters, &buffer_size);
    if (result == ERROR_BUFFER_OVERFLOW) {
        free(adapters);
        adapters = (IP_ADAPTER_ADDRESSES*)malloc(buffer_size);
        if (adapters == NULL) {
            return ERROR_OUTOFMEMORY;
        }

        // Try again with the correctly sized buffer.
        result = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, NULL, adapters, &buffer_size);
    }

    // If the call still fails, clean up and return the error code.
    if (result != NO_ERROR) {
        free(adapters);
        return result;
    }

    *adapters_out = adapters;
    return ERROR_SUCCESS;
}

// Prints a formatted report of all adapter properties including interface name, MAC address,
// media state, and all unicast IP addresses with their address families.
static void emit_interface_report(const IP_ADAPTER_ADDRESSES* adapters) {
    for (const IP_ADAPTER_ADDRESSES* adapter = adapters; adapter != NULL; adapter = adapter->Next) {
        wprintf(L"Interface: %ls\n", adapter->FriendlyName != NULL ? adapter->FriendlyName : L"(unknown)");
        wprintf(L"  MAC: ");
        print_mac_address(adapter->PhysicalAddress, adapter->PhysicalAddressLength);
        wprintf(L"\n");
        wprintf(L"  Media attached: %ls\n", adapter->OperStatus == IfOperStatusUp ? L"yes" : L"no");

        // Print all unicast IP addresses, or "(none)" if the adapter has no addresses assigned.
        if (adapter->FirstUnicastAddress == NULL) {
            wprintf(L"  IP Addresses: (none)\n");
        } else {
            wprintf(L"  IP Addresses:\n");
            for (const IP_ADAPTER_UNICAST_ADDRESS* unicast = adapter->FirstUnicastAddress; unicast != NULL; unicast = unicast->Next) {
                // Convert the socket address to a numeric string using getnameinfo.
                // NI_NUMERICHOST suppresses DNS lookup and returns the raw IP address.
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
                    wprintf(L"    - [%ls] %S\n", get_address_family_string(unicast->Address.lpSockaddr->sa_family), ip_address_string);
                } else {
                    wprintf(L"    - (error: getnameinfo failed with code %d)\n", getnameinfo_result);
                }
            }
        }

        wprintf(L"\n");
    }
}

// Initializes the Winsock library. Must be called before any socket or address resolution functions.
// Returns ERROR_SUCCESS on success, or a Winsock error code on failure.
static DWORD startup(void) {
    WSADATA wsa_data;
    // Request Winsock version 2.2, the most recent and widely supported version.
    int wsa_startup_result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (wsa_startup_result != 0) {
        return (DWORD)wsa_startup_result;
    }
    return ERROR_SUCCESS;
}

// Cleans up the Winsock library. Should be called once for every successful call to startup().
static void cleanup(void) {
    WSACleanup();
}

// Loads all network adapter addresses and prints a report to the console.
// Returns ERROR_SUCCESS on success, or a Windows error code on failure.
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

// Application entry point. Parses command line arguments, initializes Winsock, runs the
// interface enumeration, and cleans up before exiting.
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
        // Winsock must be initialized before calling getnameinfo() for IP address formatting.
        DWORD startup_result = startup();
        if (startup_result != ERROR_SUCCESS) {
            fwprintf(stderr, L"Startup failed with error %lu.\n", startup_result);
            exit_code = (int)startup_result;
        } else {
            // Run the main interface enumeration logic. Even if this fails, we still need to clean up Winsock before exiting.
            DWORD run_result = run_interface_enumeration();
            if (run_result != ERROR_SUCCESS) {
                fwprintf(stderr, L"Interface enumeration failed with error %lu.\n", run_result);
                exit_code = (int)run_result;
            }
            // Always clean up Winsock after a successful startup, regardless of enumeration result.
            cleanup();
        }
    }

    LocalFree(argv);
    return exit_code;
}
