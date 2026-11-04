# Building workaround-provider

This project builds a C++ OpenTofu provider with CMake. Run the project commands
below from the repository directory containing `CMakeLists.txt`.

## Prerequisites

- CMake **3.22 or newer**.
- C and C++ compilers, with **C++23** support enabled for C++.
- A build tool such as Make or Ninja.
- Installed **gRPC**, **Protobuf**, and **Abseil** development libraries, including
  their CMake package configuration files.
- The Protobuf compiler (`protoc`) and gRPC C++ generator (`grpc_cpp_plugin`).
- gRPC built against **OpenSSL 3**, not gRPC's bundled BoringSSL. See
  [gRPC must be built against OpenSSL](#grpc-must-be-built-against-openssl) below. This
  is the one requirement that the gRPC quick start does not give you by default.

For reference on installing gRPC and its dependencies, see the official
[gRPC C++ quick start](https://grpc.io/docs/languages/cpp/quickstart/).

Use a compatible set of dependency libraries and code generators from the same
installation. The repository does not pin dependency versions. Abseil must provide
the `absl::check`, `absl::flags`, `absl::flags_parse`, `absl::log`, and
`absl::log_initialize` CMake targets used by the project.

OpenTofu is only needed to use the provider, not to compile it.

### macOS with Homebrew

Install the Xcode Command Line Tools if you do not already have Xcode or its tools:

```sh
xcode-select --install
```

With Homebrew installed, install the build dependencies:

```sh
brew install cmake grpc protobuf abseil
```

The [Homebrew gRPC formula](https://formulae.brew.sh/formula/grpc) supplies
`grpc_cpp_plugin` and depends on Protobuf and Abseil.

Configure and build:

```sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="$(brew --prefix)" \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build --parallel 4
```

### Linux / WSL

On Debian or Ubuntu, install the basic build tools:

```sh
sudo apt update
sudo apt install build-essential cmake git autoconf libtool pkg-config
```

Check that your distribution's CMake is at least 3.22 and its C++ compiler supports
C++23. These packages alone do not install the gRPC dependencies.

For the dependency libraries, follow the installation section of the official
[gRPC C++ quick start](https://grpc.io/docs/languages/cpp/quickstart/). Build and
install gRPC with its dependencies into a local prefix, **with the one change described in
[gRPC must be built against OpenSSL](#grpc-must-be-built-against-openssl)**. Use
`$HOME/.local/grpc-openssl` as the guide's `MY_INSTALL_DIR` to match this repository's
VS Code settings (or another prefix, passed as `CMAKE_PREFIX_PATH`). Use the same compiler
toolchain for the dependencies and provider.

After installing the dependencies, configure and build the provider:

```sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="$HOME/.local/grpc-openssl" \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build --parallel 4
```

If your dependencies are installed elsewhere, substitute their installation
prefix for `CMAKE_PREFIX_PATH`. For dependencies in standard system locations,
you may be able to omit that option. Installing only `protoc` is insufficient:
the build explicitly requires the Protobuf and gRPC CMake config packages.

## gRPC must be built against OpenSSL

OpenTofu secures its connection to every provider with mutual TLS: both sides present a
certificate. The provider supports that, and it only works if gRPC uses **OpenSSL 3** for
TLS. If your gRPC uses its bundled **BoringSSL**, which is what the gRPC quick start builds by
default, compiling `src/server-cert.cpp` stops with
`gRPC must be built against OpenSSL 3 ...`.

### Why

- The certificate OpenTofu makes for itself has a **P-521** key (go-plugin's `generateCert`).
- For OpenTofu's TLS client (Go) to use that certificate, the server must list the signature
  type "ECDSA with SHA-512" among the ones it accepts from a client. BoringSSL's server does
  not. OpenSSL's does, and so does Go's own TLS server, which is why Go providers never see
  this.
- Without it, OpenTofu sends no certificate, the provider closes the connection, and OpenTofu
  reports `error reading server preface: EOF` and `Plugin did not respond`, even though the
  provider started and printed a correct handshake line.
- This was reproduced with the `openssl` and `curl` command line tools against the provider
  built with BoringSSL: a P-256 client certificate works, a P-521 one is dropped. With gRPC
  built against OpenSSL 3, real OpenTofu runs `plan`, `apply` and `destroy` with encryption on.
- Plain text (`TF_DISABLE_PLUGIN_TLS=1`) works with either library.

### Which gRPC installs are fine

- Built from source with `-DgRPC_SSL_PROVIDER=package` (recipe below). Verified.
- Packaged gRPC from Homebrew, apt or conda-forge is normally built against the system
  OpenSSL, but that has **not** been checked here. The compile-time check above tells you
  straight away if yours is not.
- Built from source following the quick start unchanged (`gRPC_SSL_PROVIDER` defaults to
  `module`, the bundled BoringSSL): **not** fine.

### What building gRPC against OpenSSL needs

On Debian or Ubuntu (versions that were used are in brackets):

| Dependency | Why |
| --- | --- |
| `build-essential` (C and C++ compilers, `make`) | compiling. Use the same compiler for gRPC and for the provider |
| `cmake` 3.22 or newer (4.4.0) | configuring |
| `git` | cloning gRPC and its submodules |
| `autoconf`, `libtool`, `pkg-config` | used while building the bundled dependencies |
| **`libssl-dev`**, OpenSSL 3.0 or newer (3.0.13) | **the one that makes this build different**: OpenSSL headers and libraries. The result links `libssl.so.3` and `libcrypto.so.3` |
| the gRPC source with its git submodules (v1.82.0) | gRPC itself and the dependencies it builds from source: Abseil, Protobuf, c-ares, re2 and zlib |
| network access | for the clone and the submodules |
| about 2.5 GB for the build tree and 1.4 GB for the install, and about 11 minutes on 20 cores | the build tree can be deleted afterwards |

On macOS, `brew install grpc protobuf abseil` already uses OpenSSL.

### Recipe

```sh
sudo apt install build-essential cmake git autoconf libtool pkg-config libssl-dev
git clone --recurse-submodules -b v1.82.0 --depth 1 --shallow-submodules https://github.com/grpc/grpc
cd grpc && mkdir -p cmake/build-openssl && cd cmake/build-openssl
cmake ../.. \
  -DgRPC_INSTALL=ON -DgRPC_BUILD_TESTS=OFF -DCMAKE_CXX_STANDARD=17 \
  -DgRPC_SSL_PROVIDER=package \
  -DgRPC_ABSL_PROVIDER=module -DgRPC_PROTOBUF_PROVIDER=module -DgRPC_CARES_PROVIDER=module \
  -DgRPC_RE2_PROVIDER=module -DgRPC_ZLIB_PROVIDER=module \
  -DCMAKE_INSTALL_PREFIX=$HOME/.local/grpc-openssl
cmake --build . -j"$(nproc)"
cmake --install .
```

This is the quick start's recipe with one difference, `-DgRPC_SSL_PROVIDER=package` (the other
`...PROVIDER=module` lines only spell out the defaults). The prefix is `$HOME/.local/grpc-openssl` on
purpose: this repository's VS Code settings and the setup guide use that path, and the name is a
reminder that this gRPC is not the quick start's default (BoringSSL) build. A BoringSSL-based gRPC
in `$HOME/.local/grpc` is not usable here; leave it alone. Then give the provider's build the prefix:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="$HOME/.local/grpc-openssl"
```

If `build/` was configured against a different gRPC, delete `build/CMakeCache.txt` and
`build/CMakeFiles` first; CMake remembers where it found gRPC.

## Build output and rebuilds

For the Make/Ninja builds above, the executable is:

```text
build/terraform-provider-workaround
```

CMake generates the C++ Protobuf and gRPC sources for `proto/tfplugin5.proto` and
`proto/grpc_controller.proto` in `build/`, then compiles them and
`src/tofu-plugin.cpp`. You do not need to run `protoc` manually. The root-level
`main.cpp` is not part of this target.

After editing source or protocol files, rebuild with:

```sh
cmake --build build --parallel 4
```

For an optimized build, rerun your platform's configure command with
`-DCMAKE_BUILD_TYPE=Release`, then build again. To rebuild all compiled files:

```sh
cmake --build build --clean-first --parallel 4
```

The repository also contains a root-level `terraform-provider-workaround` binary.
Building does not update that file; use the executable in `build/`. There is no
CMake install rule or automated CTest suite defined in this repository.

## Optional startup check

Run the newly built executable by hand. It refuses to start without OpenTofu's magic cookie,
so pass `--debug`:

```sh
./build/terraform-provider-workaround --debug --port=0
```

It should print a listening message and a provider handshake resembling
`1|5|tcp|127.0.0.1:<port>|grpc|`, then wait for RPCs. Press Ctrl+C to stop it.
This checks startup only, not OpenTofu integration. By hand there is no
`PLUGIN_CLIENT_CERT`, so it serves plaintext; started by OpenTofu it uses mutual TLS
unless `TF_DISABLE_PLUGIN_TLS=1` is set. To test it properly, see `tools/TESTING.md`.

## VS Code

Open the repository folder and install the recommended CMake Tools extension.
Select a compiler kit and a Debug or Release variant, then run **CMake: Configure**
and **CMake: Build** (or Ctrl+Shift+B). The checked-in settings use Unix Makefiles,
`build/`, and the dependency prefix `$HOME/.local/grpc-openssl`; adjust the prefix for a
Homebrew or other installation.

`CMAKE_EXPORT_COMPILE_COMMANDS=ON` creates `build/compile_commands.json` for clangd.
The fixed-path debug configuration still points to the old executable name
`build/tofu-plugin-cpp`. Use the CMake-target debug configuration or update the path
to `build/terraform-provider-workaround`. Both checked-in debug configurations
assume GDB; other debuggers require configuration changes.

## Troubleshooting

- **Missing `ProtobufConfig.cmake`, `protobuf-config.cmake`, or
  `gRPCConfig.cmake`:** Install the development libraries with their CMake config
  files, and set `CMAKE_PREFIX_PATH` to their installation prefix. A standalone
  compiler or runtime package is not enough.
- **Missing Abseil targets or headers:** Use a compatible, sufficiently recent
  gRPC/Protobuf/Abseil installation that provides the targets listed above.
- **Generated-code version errors or linker errors:** Check for mixed dependency
  installations, compiler toolchains, or architectures. Configure in a new build
  directory after correcting the dependency prefix to avoid cached package paths.
- **Generator or compiler changed:** Configure in a new build directory rather
  than reusing a CMake cache from another generator or compiler. Add `-G Ninja`
  to the configure command if you prefer Ninja and have installed it.
- **`gRPC must be built against OpenSSL 3` (compile error in `src/server-cert.cpp`):** The gRPC
  you pointed `CMAKE_PREFIX_PATH` at uses its bundled BoringSSL. Build gRPC against OpenSSL as
  described in [gRPC must be built against OpenSSL](#grpc-must-be-built-against-openssl). If
  `build/` was configured against another gRPC, delete `build/CMakeCache.txt` and
  `build/CMakeFiles` before configuring again.
- **OpenTofu reports `error reading server preface: EOF` or `Plugin did not respond`, and the
  provider logs `Server listening ... (pid N, mutual TLS)`:** A provider built against an
  older, BoringSSL-based gRPC (before the check above existed) cannot accept OpenTofu's
  certificate. Rebuild it against OpenSSL.
- **WSL picks up Windows dependencies:** Add `-DCMAKE_IGNORE_PREFIX_PATH=/mnt/c`
  when configuring, as the checked-in VS Code settings do.

Leave `GRPC_AS_SUBMODULE` and `GRPC_FETCHCONTENT` unset. The inherited helper file
contains these options, but the first assumes an external source-tree layout and
the second uses a placeholder gRPC tag. Neither is configured for this repository;
use installed dependencies as described above.
