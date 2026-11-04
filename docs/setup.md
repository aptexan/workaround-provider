# workaround-provider: Developer Setup

Platforms: **Linux (Ubuntu 24.04+)** and **macOS**. Everything goes under `~/.local` unless noted.

**WSL (Ubuntu):** follow the Linux steps, except skip step 2: install VSCode on Windows plus the **WSL** extension, then run `code .` from the WSL shell. In step 10, add the keybinding in the Windows VSCode.

## 1. Base tools

**Linux** (system-wide; required for compiler and debugger):
```sh
sudo apt update
sudo apt install -y build-essential git gdb curl
```

**macOS**:
```sh
xcode-select --install
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"   # Homebrew, skip if installed
```

## 2. VSCode

**Linux** (tarball, no root):
```sh
mkdir -p ~/.local/vscode ~/.local/bin
curl -L "https://code.visualstudio.com/sha/download?build=stable&os=linux-x64" | tar -xz -C ~/.local/vscode --strip-components=1
ln -sf ~/.local/vscode/bin/code ~/.local/bin/code
```

**macOS**: download from https://code.visualstudio.com/Download, unzip, drag `Visual Studio Code.app` to `~/Applications`.
Then in VSCode: `Cmd+Shift+P` → **Shell Command: Install 'code' command in PATH**.

## 3. CMake (3.22 or newer)

**Linux**:
```sh
curl -LO https://github.com/Kitware/CMake/releases/download/v3.31.6/cmake-3.31.6-linux-x86_64.tar.gz
mkdir -p ~/.local/cmake && tar -xzf cmake-3.31.6-linux-x86_64.tar.gz -C ~/.local/cmake --strip-components=1
ln -sf ~/.local/cmake/bin/* ~/.local/bin/
```

**macOS**:
```sh
brew install cmake
```

Add to `~/.bashrc` (Linux) or `~/.zshrc` (macOS), then open a new shell:
```sh
export PATH="$HOME/.local/bin:$PATH"
```
Check: `cmake --version`.

## 4. gRPC, Protobuf, Abseil

Target prefix: `~/.local/grpc-openssl` (the project's VSCode settings expect this path; the name is a reminder that gRPC must use OpenSSL, see below).

**Linux** (prebuilt binaries from conda-forge, via micromamba):
```sh
curl -Ls https://micro.mamba.pm/api/micromamba/linux-64/latest | tar -xj -C ~/.local bin/micromamba
~/.local/bin/micromamba create -y -p ~/.local/grpc-openssl -c conda-forge grpc-cpp libprotobuf libabseil
```

**macOS** (prebuilt bottles from Homebrew):
```sh
brew install grpc protobuf abseil
mkdir -p ~/.local && ln -sfn "$(brew --prefix)" ~/.local/grpc-openssl
```

Check: `ls ~/.local/grpc-openssl/bin/protoc ~/.local/grpc-openssl/bin/grpc_cpp_plugin`.

**Optional: build from source** (instead of the above). Follow the install section of https://grpc.io/docs/languages/cpp/quickstart/ and set `MY_INSTALL_DIR=$HOME/.local/grpc-openssl`. Use the same compiler for the libraries and the provider.

**gRPC must use OpenSSL, not its bundled BoringSSL.** OpenTofu's mutual TLS needs it (its client certificate has a P-521 key, which a BoringSSL-based gRPC server cannot accept). If you build from source, `apt install libssl-dev` first and add `-DgRPC_SSL_PROVIDER=package` to the quick start's `cmake` command (the default, `module`, bundles BoringSSL). A BoringSSL-based install at `~/.local/grpc` (the quick start's default prefix) is not usable here; leave it alone and use `~/.local/grpc-openssl` as above. The Homebrew and conda-forge packages above are normally built against OpenSSL, but this has not been checked: if the provider's build stops with `gRPC must be built against OpenSSL 3`, build gRPC yourself. Full reasons, dependency list and commands are in `BUILDING.md`.

## 5. Get the project

```sh
git clone git@github.com:aptexan/workaround-provider.git
cd workaround-provider
```
The `.vscode/` folder in the repo holds the project's VSCode config.

## 6. Build from the command line

From the `workaround-provider` folder:
```sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="$HOME/.local/grpc-openssl" \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build --parallel 4
```
Output: `build/terraform-provider-workaround`.
Check: `./build/terraform-provider-workaround --debug --port=0` prints a handshake like `1|5|tcp|127.0.0.1:<port>|grpc|` (`--debug` is needed because the provider refuses to start by hand without OpenTofu's magic cookie). Stop with `Ctrl+C`.
Protobuf/gRPC sources are generated into `build/` automatically.

## 7. VSCode extensions

Open the folder: `code workaround-provider`. Then install:
```sh
code --install-extension llvm-vs-code-extensions.vscode-clangd
code --install-extension ms-vscode.cmake-tools
code --install-extension ms-vscode.cpptools-themes
code --install-extension ms-vscode.cpp-devtools
code --install-extension ms-vscode.cpptools-extension-pack
```
Keep Microsoft C/C++ (`ms-vscode.cpptools`) installed: the debugger needs it.

## 8. Use clangd, not Microsoft IntelliSense

The repo's `.vscode/settings.json` already contains:
```json
"C_Cpp.intelliSenseEngine": "disabled",
"C_Cpp.autocomplete": "disabled",
"C_Cpp.errorSquiggles": "disabled",
"clangd.arguments": ["--compile-commands-dir=${workspaceFolder}/build", "--background-index", "--header-insertion=iwyu"]
```
If missing, add these to the workspace `settings.json`.

## 9. No red squiggles

1. Complete step 6 first (creates `build/compile_commands.json` and the generated `*.pb.h` / `*.grpc.pb.h`).
2. `Ctrl+Shift+P` (`Cmd+Shift+P` on macOS) → **clangd: Restart language server**.
3. Open `src/tofu-plugin.cpp` and a generated file in `build/` (e.g. `tfplugin5.pb.h`). No squiggles should appear.
4. If includes or symbols are unresolved: rerun step 6 configure, then restart clangd again.
   If something remains, run the `vscode-clangd-setup` skill in Claude Code.

## 10. Build and debug hotkeys

1. Edit `.vscode/launch.json`: in config `Debug tofu-plugin-cpp` set
   `"program": "${workspaceFolder}/build/terraform-provider-workaround"`.
2. Select the kit and variant (status bar): any GCC/Clang kit, **Debug**. Run **CMake: Configure**.
3. Open keybindings: `Ctrl+Shift+P` → **Preferences: Open Keyboard Shortcuts (JSON)**. Add:
```json
[
  { "key": "ctrl+shift+f5", "command": "workbench.action.tasks.build", "when": "!inDebugMode" }
]
```
(macOS: use `cmd+shift+f5`.)

| Key | Action |
|---|---|
| `Ctrl+Shift+F5` | Build (top-level CMake, task `CMake: build`) |
| `F5` | Run under debugger; stops at breakpoints |
| `Ctrl+F5` | Run without debugger |

Check: set a breakpoint in `src/tofu-plugin.cpp`, press `F5`, execution stops on it.
If debugging fails, run the `vscode_cmake_build_and_debug` skill in Claude Code.

macOS note: GDB is not available; set `"MIMode": "lldb"` in `launch.json`.

---

## Automated alternative: Claude Code skill

Instead of steps 1–10, let Claude do them. The skill checks what is already installed and runs only the missing steps.

**Where it is:** `docs/skills/workaround-provider-dev-setup/SKILL.md` in the `workaround-provider` repo.

**Install:** Claude Code does not load skills from `docs/`, so copy the folder to your user skills once, from the repo root:
```sh
mkdir -p ~/.claude/skills
cp -r docs/skills/workaround-provider-dev-setup ~/.claude/skills/
```

**Run:**
1. Install Claude Code if needed: https://docs.claude.com/en/docs/claude-code/setup
2. Clone the repo (step 5) and start Claude from its root folder, the one containing `CMakeLists.txt` and `.git`:
   ```sh
   cd workaround-provider
   claude
   ```
3. In the Claude prompt, type `/workaround-provider-dev-setup` (or ask: "set up my dev environment").
4. Approve the commands it asks to run. If it needs `sudo` (Linux apt packages), run the command it shows with the `!` prefix, e.g. `! sudo apt install -y build-essential git gdb curl`.
5. Rerun the skill any time; finished steps are skipped.
