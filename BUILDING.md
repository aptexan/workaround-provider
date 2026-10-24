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
install gRPC with its dependencies into a local prefix. Use
`$HOME/.local/grpc` as the guide's `MY_INSTALL_DIR` to match this repository's
VS Code settings. Use the same compiler toolchain for the dependencies and provider.

After installing the dependencies, configure and build the provider:

```sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="$HOME/.local/grpc" \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build --parallel 4
```

If your dependencies are installed elsewhere, substitute their installation
prefix for `CMAKE_PREFIX_PATH`. For dependencies in standard system locations,
you may be able to omit that option. Installing only `protoc` is insufficient:
the build explicitly requires the Protobuf and gRPC CMake config packages.

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

Run the newly built executable:

```sh
./build/terraform-provider-workaround --port=0
```

It should print a listening message and a provider handshake resembling
`1|5|tcp|127.0.0.1:<port>|grpc|`, then wait for RPCs. Press Ctrl+C to stop it.
This checks startup only, not OpenTofu integration. The current implementation
requires `TF_DISABLE_PLUGIN_TLS=1` when used with OpenTofu, as noted in the source.

## VS Code

Open the repository folder and install the recommended CMake Tools extension.
Select a compiler kit and a Debug or Release variant, then run **CMake: Configure**
and **CMake: Build** (or Ctrl+Shift+B). The checked-in settings use Unix Makefiles,
`build/`, and the dependency prefix `$HOME/.local/grpc`; adjust the prefix for a
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
- **WSL picks up Windows dependencies:** Add `-DCMAKE_IGNORE_PREFIX_PATH=/mnt/c`
  when configuring, as the checked-in VS Code settings do.

Leave `GRPC_AS_SUBMODULE` and `GRPC_FETCHCONTENT` unset. The inherited helper file
contains these options, but the first assumes an external source-tree layout and
the second uses a placeholder gRPC tag. Neither is configured for this repository;
use installed dependencies as described above.
