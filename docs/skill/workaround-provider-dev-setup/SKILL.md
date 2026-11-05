---
name: workaround-provider-dev-setup
description: Use this skill to set up a developer machine for the workaround-provider C++ project (OpenTofu provider using gRPC/Protobuf) on Linux (Ubuntu 24.04+) or macOS. Checks what is already installed and only performs missing steps - base tools, VSCode, CMake, gRPC/Protobuf/Abseil, command-line build, VSCode extensions, clangd config, and F5/Ctrl+F5/Ctrl+Shift+F5 build and debug hotkeys. Triggers on "set up my dev environment", "setup workaround-provider", "install dependencies and build", "configure VSCode for this project". Run from the repo root (the folder containing CMakeLists.txt and .git).
---

# workaround-provider: automated developer setup

Do the setup that `docs/setup.md` describes manually. Run every step **check first, act only if missing**, so reruns are safe and skip finished work. Report a one-line status per step (`already installed` / `installed` / `failed`) and a short summary at the end.

## Rules

- The repo is already cloned. Never clone it. Work in the current directory. (Step 4's optional source build clones gRPC into a temp directory, not here.)
- Install into the user's home (`~/.local`) without disturbing the system. Use `sudo` only for apt packages (Linux) and tell the user before running it. If sudo needs a password, ask the user to run the command with the `! <command>` prefix.
- Prefer prebuilt binaries over building from source. Build from source only if the user asks, or (after asking) when a prebuilt gRPC fails the OpenSSL check in step 4.
- Never overwrite the user's existing config files. Merge JSON edits into them and show what changed.
- Do not use `curl | sh` for anything that is not named below.
- Stop and report on any failed step; do not continue past a failure that later steps depend on.

## Step 0: Preflight

1. Confirm cwd is the repo root: `CMakeLists.txt` and `.git` exist and `git remote -v` mentions `workaround-provider`. If not, tell the user to start Claude from the repo folder and stop.
2. Detect the platform: `uname -s` → `Linux` or `Darwin`. On Linux, read `/etc/os-release` and warn if not Ubuntu 24.04+. Detect arch with `uname -m` (`x86_64` or `aarch64`/`arm64`) and use matching download URLs.
3. Under WSL (`/proc/version` contains `microsoft`), VSCode is the Windows install used through the WSL extension: skip step 2's install, only check `code` exists in PATH, and ensure the **WSL** extension is installed on the Windows side.
4. Ensure `~/.local/bin` exists and is on PATH. If missing from PATH, append `export PATH="$HOME/.local/bin:$PATH"` to `~/.bashrc` (Linux) or `~/.zshrc` (macOS) only if the line is not already present, and export it for the current session.

## Step 1: Base tools

Check: `gcc --version`, `g++ --version`, `git --version`, `gdb --version`, `curl --version` (Linux); `xcode-select -p`, `brew --version` (macOS).

- Linux, if any missing: `sudo apt update && sudo apt install -y build-essential git gdb curl`.
- macOS, if `xcode-select -p` fails: `xcode-select --install` and wait for the user to confirm it finished. If `brew` is missing, tell the user to install Homebrew from https://brew.sh (the installer is interactive) and stop until done.
- Verify the C++ compiler accepts C++23: compile an empty file with `-std=c++23 -fsyntax-only`. If not, report the compiler version and stop.

## Step 2: VSCode

Check: `command -v code`.

- Linux, if missing:
  ```sh
  mkdir -p ~/.local/vscode ~/.local/bin
  curl -L "https://code.visualstudio.com/sha/download?build=stable&os=linux-x64" | tar -xz -C ~/.local/vscode --strip-components=1
  ln -sf ~/.local/vscode/bin/code ~/.local/bin/code
  ```
  (use `linux-arm64` on aarch64).
- macOS, if missing: download https://code.visualstudio.com/sha/download?build=stable&os=darwin-universal to a temp dir, unzip, move `Visual Studio Code.app` to `~/Applications`, then symlink `~/Applications/Visual Studio Code.app/Contents/Resources/app/bin/code` to `~/.local/bin/code`. Or `brew install --cask visual-studio-code` if the user prefers.

## Step 3: CMake ≥ 3.22

Check: `cmake --version`; parse the version and require ≥ 3.22.

- Linux, if missing/too old: download `https://github.com/Kitware/CMake/releases/download/v3.31.6/cmake-3.31.6-linux-<x86_64|aarch64>.tar.gz` to a temp dir, extract into `~/.local/cmake` with `--strip-components=1`, symlink `~/.local/cmake/bin/*` into `~/.local/bin/`.
- macOS, if missing/too old: `brew install cmake`.

## Step 4: gRPC, Protobuf, Abseil in `~/.local/grpc-openssl` (docs/setup.md section 4)

gRPC must use **OpenSSL**, not its bundled BoringSSL: OpenTofu's mutual TLS needs it, and the provider refuses to compile otherwise. The prefix is `~/.local/grpc-openssl` on purpose; a gRPC in `~/.local/grpc` (the quick start's default folder, BoringSSL) is not usable here: leave it alone.

Check, in this order:
1. `~/.local/grpc-openssl/bin/protoc` and `bin/grpc_cpp_plugin` exist, and the CMake configs exist: `find ~/.local/grpc-openssl -name 'gRPCConfig.cmake' -o -name 'protobuf-config.cmake' -o -name 'abslConfig.cmake'` finds all three.
2. **OpenSSL check:** `grep -q 'find_package(OpenSSL' ~/.local/grpc-openssl/lib/cmake/grpc/gRPCConfig.cmake` succeeds. An OpenSSL-based install passes it; a BoringSSL-based one fails it (and has `lib/libssl.a`).

If both hold, skip. If 1 holds but 2 fails, the install is BoringSSL-based: tell the user, offer the source build below, and **ask before replacing anything**. Do not go on to step 5 with it.

- Linux, if missing (prebuilt binaries from conda-forge):
  ```sh
  curl -Ls https://micro.mamba.pm/api/micromamba/linux-64/latest | tar -xj -C ~/.local bin/micromamba    # linux-aarch64 on arm64; skip if micromamba already exists
  ~/.local/bin/micromamba create -y -p ~/.local/grpc-openssl -c conda-forge grpc-cpp libprotobuf libabseil
  ```
  If `~/.local/grpc-openssl` exists but is incomplete, ask the user before removing or replacing it.
- macOS, if missing (prebuilt bottles from Homebrew):
  ```sh
  brew install grpc protobuf abseil
  mkdir -p ~/.local && ln -sfn "$(brew --prefix)" ~/.local/grpc-openssl
  ```
  If `~/.local/grpc-openssl` already exists and is not a symlink, ask the user before changing it.
- After either install, run check 2. conda-forge's `libgrpc` and Homebrew's `grpc` declare a dependency on OpenSSL (their published dependency lists), so it normally passes. If it fails, or step 5 later stops with `gRPC must be built against OpenSSL 3`, tell the user and offer the source build.
- Optional, only on request or after such a failure (docs/setup.md section 4.1): build gRPC from source against OpenSSL. It is the gRPC quick start with one extra flag, `-DgRPC_SSL_PROVIDER=package`, and installs gRPC, Protobuf, Abseil, re2, c-ares, zlib, `protoc` and `grpc_cpp_plugin` into the prefix.
  1. Tell the user before `sudo`, then install the extra tools: Linux `sudo apt install -y autoconf libtool pkg-config libssl-dev`; macOS `brew install autoconf libtool pkg-config openssl@3`.
  2. Tell the user it takes about 11 minutes on 20 cores (longer on fewer) and 4 GB of disk. In a temp directory (`T=$(mktemp -d)`), run the long steps in the background and wait for them:
     ```sh
     git clone --recurse-submodules -b v1.82.0 --depth 1 --shallow-submodules https://github.com/grpc/grpc "$T/grpc"
     mkdir -p "$T/grpc/cmake/build" && cd "$T/grpc/cmake/build"
     cmake ../.. -DgRPC_INSTALL=ON -DgRPC_BUILD_TESTS=OFF -DCMAKE_CXX_STANDARD=17 \
       -DgRPC_SSL_PROVIDER=package \
       -DCMAKE_INSTALL_PREFIX=$HOME/.local/grpc-openssl          # macOS: also -DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)" (untested)
     cmake --build . -j"$(nproc)"                                 # macOS: -j"$(sysctl -n hw.ncpu)"
     cmake --install .
     ```
     Use the same compiler as for the provider. Return to the repo root afterwards.
  3. Run checks 1 and 2 again, then `rm -rf "$T"`.

## Step 5: Command-line build

Check: `build/terraform-provider-workaround` exists and `build/CMakeCache.txt` has `gRPC_DIR` and `Protobuf_DIR` under `.local/grpc-openssl` (the prefix itself is not recorded when none was given). If the executable exists and is newer than every file in `src/` and `proto/`, skip. If the cache was made with a different generator, compiler, or prefix, configure into a fresh `build/` (ask before deleting an existing one).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build --parallel 4
```
`CMakeLists.txt` finds gRPC in `~/.local/grpc-openssl` by itself and ignores Windows folders under WSL, so no prefix option is needed. (`-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` creates `build/compile_commands.json` for clangd.) Check: `./build/terraform-provider-workaround --debug --port=0` (`--debug`: the provider refuses to start by hand without OpenTofu's magic cookie) prints a handshake like `1|5|tcp|127.0.0.1:<port>|grpc|`; run it with a short timeout and kill it. If the build fails, show the first error and stop.

## Step 6: VSCode extensions

Check: `code --list-extensions`. Install only the missing ones with `code --install-extension <id>`:

- `llvm-vs-code-extensions.vscode-clangd`
- `ms-vscode.cmake-tools`
- `ms-vscode.cpptools-themes`
- `ms-vscode.cpp-devtools`
- `ms-vscode.cpptools-extension-pack`

If an ID is not found, report it and continue with the rest. Never uninstall `ms-vscode.cpptools` (the debugger needs it).

## Step 7: clangd configured, Microsoft IntelliSense off

Check `.vscode/settings.json` for each key; add only missing/different ones, preserving everything else (it may contain comments; edit text carefully rather than re-serializing):

```json
"C_Cpp.intelliSenseEngine": "disabled",
"C_Cpp.autocomplete": "disabled",
"C_Cpp.errorSquiggles": "disabled",
"cmake.configureSettings": { "CMAKE_EXPORT_COMPILE_COMMANDS": "ON", "CMAKE_PREFIX_PATH": "${userHome}/.local/grpc-openssl" },
"clangd.arguments": ["--compile-commands-dir=${workspaceFolder}/build", "--background-index", "--header-insertion=iwyu"]
```

Also ensure `.vscode/extensions.json` recommends `llvm-vs-code-extensions.vscode-clangd` and `ms-vscode.cmake-tools`, and that `ms-vscode.cpptools` is **not** in `unwantedRecommendations`.

## Step 8: No red squiggles

1. `build/compile_commands.json` must exist (step 5) and generated headers (`build/tfplugin5.pb.h`, `build/tfplugin5.grpc.pb.h`, `build/grpc_controller.pb.h`) must exist. If not, rerun step 5.
2. Run `clangd --check=src/tofu-plugin.cpp --compile-commands-dir=build` (use `~/.local/grpc-openssl/bin/clangd` or the VSCode-managed one if `clangd` is not on PATH; the extension downloads its own into the VSCode global storage). Any `error:` diagnostics (unresolved include, unknown symbol) → fix include paths/compile database and recheck. Repeat for a generated file, e.g. `build/tfplugin5.pb.cc`.
3. Tell the user to run **clangd: Restart language server** in VSCode if it is already open.
4. If problems persist, follow the `vscode-clangd-setup` skill, and `vscode_enable_cpp23` if only C++23 syntax is flagged.

## Step 9: Build and debug hotkeys

1. `.vscode/tasks.json` must have a default build task (`"type": "cmake"`, `"command": "build"`, `"group": {"kind": "build", "isDefault": true}`), label `CMake: build`. Add it if missing.
2. `.vscode/launch.json`: every `cppdbg` config's `program` must be `${workspaceFolder}/build/terraform-provider-workaround` (or `${command:cmake.launchTargetPath}`). Fix a stale `build/tofu-plugin-cpp` path. Keep `"preLaunchTask": "CMake: build"` and `"MIMode": "gdb"` on Linux; use `"lldb"` on macOS.
3. `.vscode/settings.json`: `cmake.sourceDirectory`, `cmake.buildDirectory` (`${workspaceFolder}/build`), `cmake.generator` consistent with the build in step 5.
4. User keybindings file (create `[]` if absent; **merge**, do not overwrite; skip if an identical binding already exists):
   - Linux: `~/.config/Code/User/keybindings.json`
   - macOS: `~/Library/Application Support/Code/User/keybindings.json`
   - WSL: the Windows-side `%APPDATA%\Code\User\keybindings.json` (`/mnt/c/Users/<user>/AppData/Roaming/Code/User/keybindings.json`)
   ```json
   { "key": "ctrl+shift+f5", "command": "workbench.action.tasks.build", "when": "!inDebugMode" }
   ```
   (`cmd+shift+f5` on macOS). If the file has other bindings for the same key, tell the user about the conflict instead of removing them.

| Key | Action |
|---|---|
| `Ctrl+Shift+F5` | Build via top-level CMake |
| `F5` | Run under debugger, hits breakpoints |
| `Ctrl+F5` | Run without debugger |

`F5` and `Ctrl+F5` are VSCode defaults; verify they are not remapped in the keybindings file.

## Final report

Print a table: step, status, and anything the user must do manually (restart shell, restart clangd, set CMake kit/variant in the VSCode status bar to a GCC/Clang kit + Debug, set a breakpoint in `src/tofu-plugin.cpp` and press `F5` to confirm).
