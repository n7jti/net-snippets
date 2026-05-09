# net-snippets

Windows networking snippets built as small standalone console executables.

## Included executables

- `interface-enum.exe` (C): Enumerates network interfaces and prints MAC address, all unicast IP addresses, and whether media is attached.
- `echo-server.exe` (C++): Minimal TCP echo server using Winsock.

## Build in VSCode (Windows)

1. Install **CMake Tools** and **C/C++** extensions in VSCode.
2. Open this repository in VSCode.
3. Run task **Build (CMake)**.
4. Built executables are written to `build/bin/`.

## Command usage

- `interface-enum.exe`
- `echo-server.exe [port]` (default port: `7000`, stop with `Ctrl+C`)
