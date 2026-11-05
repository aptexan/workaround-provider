# Building workaround-provider

Setup and build steps are in **[docs/setup.md](docs/setup.md)**; this page only holds what error messages and other files point to.

- Tools, CMake, gRPC: sections 1 to 4. gRPC goes in `~/.local/grpc-openssl`; to build it from source see **4.1**.
- Build: section 6 (`cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`, then `cmake --build build --parallel 4`).
- Check: `tools/run-all.sh`, described in `tools/TESTING.md`.
- Or let Claude Code do all of it with the setup skill (last section of `docs/setup.md`).

## gRPC must be built against OpenSSL

OpenTofu encrypts its connection to every provider and presents a certificate with a P-521 key. The TLS server must accept ECDSA with SHA-512 for that. **OpenSSL does; gRPC's bundled BoringSSL does not**, and the gRPC quick start builds BoringSSL by default. A provider built against it would start and print a correct handshake line, and OpenTofu would then fail with `error reading server preface: EOF` or `Plugin did not respond`. So the provider refuses to compile against it (`gRPC must be built against OpenSSL 3`). How to build gRPC against OpenSSL: `docs/setup.md`, section 4.1. A gRPC in `~/.local/grpc` (the quick start's default folder) is not usable here.

## Troubleshooting

- **`gRPC must be built against OpenSSL 3` (compile error):** your gRPC uses BoringSSL. Build gRPC as in `docs/setup.md` 4.1, then `rm -rf build/CMakeCache.txt build/CMakeFiles` and configure again.
- **Hundreds of `Protobuf C++ gencode is built with an incompatible version` errors:** `build/` remembers packages from two places. Run `rm -rf build/CMakeCache.txt build/CMakeFiles`, then configure again.
- **`found in different installations` (CMake error):** same fix, and pass one `-DCMAKE_PREFIX_PATH=<prefix>`.
- **`Could not find ... Protobuf` or `gRPC`:** gRPC is not in `~/.local/grpc-openssl`. Pass `-DCMAKE_PREFIX_PATH=<its folder>`.
- **WSL:** the Windows folders (`/mnt/c`, inherited `PATH`) are ignored automatically; nothing to do.
