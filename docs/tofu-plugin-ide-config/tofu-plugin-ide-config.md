# tofu-plugin-ide-config — VS Code IDE setup for the C++ OpenTofu plugin

Session name: **tofu-plugin-ide-config**
Project: `~/tpc` (CMake target `tofu-plugin-cpp`, C++23, gRPC 1.82 / protobuf 35 in `~/.local/grpc`)
Last updated: 2026-10-23
This folder is **self-contained**: everything needed to rebuild the setup on another machine is
in here (see section 0). Scope is the **VS Code IDE setup only** (build, debug, IntelliSense,
extensions). Plugin code, the protocol and the proto files are out of scope and are not in this
bundle. The reasoning behind the setup is in `reference/vscode-cmake-build-debug-setup.md`.

---

## 0. What is in this folder (backup bundle)

```
tofu-plugin-ide-config/
├── tofu-plugin-ide-config.md        this document
├── vscode/                          the 5 files for <project>/.vscode/ (copy as-is)
│   ├── settings.json  cmake-variants.json  tasks.json  launch.json  extensions.json
├── project/                         only the build files the IDE setup depends on
│   ├── .gitignore  CMakeLists.txt   (includes the two fixes from section 2)
│   └── cmake/common.cmake
├── reference/
│   └── vscode-cmake-build-debug-setup.md   original setup guide with reasoning
└── session-history/
    └── eb1960de-58fb-49f0-9d07-cd795e4038ff.jsonl   raw Claude session transcript
```

`CMakeLists.txt` expects `proto/<name>.proto` and `src/tofu-plugin.cpp` from the real project; they
are not included. The first CMake configure fails without them, so restore into an existing
project folder, or copy your own `proto/` and `src/` in first. The generated header is named after
the proto file (`<proto-stem>.grpc.pb.h`), so the `#include` in the .cpp must match it.

### Restore on a new machine (quick path)
```bash
mkdir -p ~/tpc && cd ~/tpc && git init 2>/dev/null
B=~/claude-sessions/tofu-provider-cpp/tofu-plugin-ide-config
cp -r $B/project/. .                 # CMakeLists.txt, cmake/, .gitignore (add your own proto/ and src/)
mkdir -p .vscode && cp $B/vscode/*.json .vscode/
# then follow section 3.1 (tools + extensions) and section 5 (select GCC kit)
```

### Restore the Claude session itself (so `claude --resume` works)
```bash
mkdir -p ~/.claude/projects/-home-<user>-tpc      # directory name = project path with / replaced by -
cp $B/session-history/*.jsonl ~/.claude/projects/-home-<user>-tpc/
cd ~/tpc && claude --resume
```
The project path must match the new machine's path (`/home/<user>/tpc`), or the session will not
appear in the picker. If it does not, read this document instead; it carries the same state.
The transcript is a raw log: treat it as private.

---

## 1. TL;DR

The IDE side is **finished**. Red squiggles on `#include` lines are gone, Ctrl+Shift+B builds,
and F5 is configured to build and run under gdb. The last open item is a **one-time click in
VS Code that cannot be put in a file**: select the GCC kit (section 5).

The plugin source itself (`src/tofu-plugin.cpp`) is **the user's to fix**. Do not edit it
unless asked. Only IDE and header-resolution issues are in scope for Claude.

---

## 2. What was done, in order

| # | Problem | Cause | Fix |
|---|---|---|---|
| 1 | Red squiggle on `#include <grpcpp/...>` (line 19) | `build/` did not exist, so clangd had no `compile_commands.json` and knew no include paths | Ran a CMake configure (command in section 6). That created `build/compile_commands.json` |
| 2 | `helloworld.grpc.pb.h` not found | The generated header is named after the proto file: `tofu-plugin.grpc.pb.h` | Changed the `#else` include in `src/tofu-plugin.cpp` to `tofu-plugin.grpc.pb.h`. The `#ifdef BAZEL_BUILD` include was left as the original |
| 3 | `.proto` files had no highlighting | No extension | Installed `zxh404.vscode-proto3` on **WSL** and on the **Windows host**, and added it to `.vscode/extensions.json` |
| 4 | F5 "build fails", no error shown | CMake Tools had **no kit selected**, so it never started. Log said `Not starting CMake driver: no kit selected` | Not fixable from a file. User must run **CMake: Select a Kit** (section 5). The command-line build was verified to work |
| 5 | F5 depended on picking a launch target | `${command:cmake.launchTargetPath}` is empty until you pick one | Added a first launch entry with a fixed path to `build/tofu-plugin-cpp` |
| 6 | `build/` showed up as untracked | No `.gitignore` | Added `.gitignore` with `build/` and `.cache/` |

Earlier work, from the previous session, that this one relies on:
`.vscode/` was created from the `grpc_helloworld` doc, and two `CMakeLists.txt` bugs were fixed
(`STEM` to `NAME_WE`, and the executable source path to `src/tofu-plugin.cpp`).

---

## 3. Replicate on another machine (step by step)

### 3.1 Prerequisites
- VS Code opened on the project folder (as a WSL window if on Windows).
- Tools: `cmake`, `g++`, `gdb`, `clangd` (check with `which cmake g++ gdb clangd`).
- gRPC, protobuf and abseil installed together under `~/.local/grpc`.
- Extensions, installed **on the side where the folder is open** (WSL side for a WSL window):
  - `ms-vscode.cmake-tools` (CMake Tools)
  - `ms-vscode.cpptools` (C/C++, supplies the gdb debug adapter)
  - `llvm-vs-code-extensions.vscode-clangd` (IntelliSense)
  - `zxh404.vscode-proto3` (`.proto` highlighting)

  The `.vscode/extensions.json` file makes VS Code offer all four when the folder is opened.

Install an extension from a WSL terminal:
```bash
code --install-extension zxh404.vscode-proto3          # installs into the WSL side
```
Install it on the Windows host editor from WSL (the plain `code.cmd` route fails because of
quoting; PowerShell works):
```bash
cd /mnt/c && powershell.exe -NoProfile -Command \
  '& "$env:LOCALAPPDATA\Programs\Microsoft VS Code\bin\code.cmd" --install-extension zxh404.vscode-proto3'
```

### 3.2 Create the files
Create these five files in `<project>/.vscode/`, plus `.gitignore` at the project root.
Contents are in section 4. Nothing in them is machine- or user-specific, so they are copied
as-is.

### 3.3 One-time steps in VS Code
Follow section 5. In short: reload the window, select the **GCC** kit, check **Debug** is shown
in the status bar, then Ctrl+Shift+B.

### 3.4 Verify
```bash
# Cache points at ~/.local/grpc and Debug (any /mnt/c path means the settings were not applied)
grep -E "^(Protobuf_DIR|absl_DIR|gRPC_DIR|CMAKE_BUILD_TYPE)" build/CMakeCache.txt

# Debug symbols exist (needed for breakpoints)
readelf -S build/tofu-plugin-cpp | grep debug_info

# IntelliSense resolves every include; should end with "0 errors"
clangd --compile-commands-dir=build --check=$PWD/src/tofu-plugin.cpp
```
The `clangd --check` result counts only include and syntax errors that clangd can see. It does
not mean the program compiles.

---

## 4. Final contents of the config files

### `.vscode/settings.json`: where CMake finds things, and clangd instead of Microsoft IntelliSense
```json
{
    "cmake.sourceDirectory": "${workspaceFolder}",
    "cmake.buildDirectory": "${workspaceFolder}/build",
    "cmake.configureOnOpen": true,
    "cmake.generator": "Unix Makefiles",

    "cmake.configureSettings": {
        "CMAKE_EXPORT_COMPILE_COMMANDS": "ON",
        "CMAKE_PREFIX_PATH": "${userHome}/.local/grpc",
        "CMAKE_IGNORE_PREFIX_PATH": "/mnt/c"
    },

    "C_Cpp.intelliSenseEngine": "disabled",
    "C_Cpp.autocomplete": "disabled",
    "C_Cpp.errorSquiggles": "disabled",
    "clangd.arguments": [
        "--compile-commands-dir=${workspaceFolder}/build",
        "--background-index",
        "--header-insertion=iwyu"
    ]
}
```
Why each part matters:
- `CMAKE_PREFIX_PATH` tells CMake where gRPC lives. `${userHome}` avoids a hardcoded username.
- `CMAKE_IGNORE_PREFIX_PATH=/mnt/c` stops CMake finding Windows copies of protobuf and abseil
  that leak in through the WSL PATH. This is the line that prevents version-mismatch build
  failures. On native Linux the path does not exist, so it is harmless.
- `CMAKE_EXPORT_COMPILE_COMMANDS` writes `build/compile_commands.json`, which clangd reads. It
  holds the exact flags (`-std=c++23`, the gRPC include dir, the generated-header dir).
- The three `C_Cpp.*` settings turn off the Microsoft engine so it does not fight clangd and
  draw false squiggles.

### `.vscode/cmake-variants.json`: always Debug
```json
{
    "buildType": {
        "default": "debug",
        "choices": {
            "debug":   { "short": "Debug",   "long": "Debug info, no optimization", "buildType": "Debug" },
            "release": { "short": "Release", "long": "Optimized",                   "buildType": "Release" }
        }
    }
}
```
Without this, CMake Tools can fall back to an unspecified build type after a restart. That
builds with no `-g`, and breakpoints are then silently skipped.

### `.vscode/tasks.json`: Ctrl+Shift+B
```json
{
    "version": "2.0.0",
    "tasks": [
        {
            "type": "cmake",
            "label": "CMake: build",
            "command": "build",
            "targets": ["all"],
            "group": { "kind": "build", "isDefault": true },
            "problemMatcher": ["$gcc"],
            "detail": "Build all targets with CMake Tools (Ctrl+Shift+B)"
        }
    ]
}
```
`"isDefault": true` is what binds Ctrl+Shift+B. The `$gcc` problem matcher is what turns
compiler output into entries in the Problems panel.

### `.vscode/launch.json`: F5
Two entries. The first has a fixed path and is the one to use. The second follows whatever
CMake launch target is selected in the status bar.
```json
{
    "version": "0.2.0",
    "configurations": [
        {
            // Fixed path: F5 works without picking a CMake launch target first.
            "name": "Debug tofu-plugin-cpp",
            "type": "cppdbg",
            "request": "launch",
            "program": "${workspaceFolder}/build/tofu-plugin-cpp",
            "args": [],
            "stopAtEntry": false,
            "cwd": "${workspaceFolder}/build",
            "environment": [],
            "externalConsole": false,
            "MIMode": "gdb",
            "setupCommands": [
                { "description": "Enable pretty-printing for gdb", "text": "-enable-pretty-printing", "ignoreFailures": true }
            ],
            "preLaunchTask": "CMake: build"
        },
        {
            "name": "Debug (CMake Tools target)",
            "type": "cppdbg",
            "request": "launch",
            "program": "${command:cmake.launchTargetPath}",
            "args": [],
            "stopAtEntry": false,
            "cwd": "${command:cmake.launchTargetDirectory}",
            "environment": [],
            "externalConsole": false,
            "MIMode": "gdb",
            "setupCommands": [
                { "description": "Enable pretty-printing for gdb", "text": "-enable-pretty-printing", "ignoreFailures": true }
            ],
            "preLaunchTask": "CMake: build"
        }
    ]
}
```
Notes:
- `preLaunchTask` rebuilds before every F5.
- Keep `stopAtEntry` at `false`. `true` pauses before `main` and looks like breakpoints not
  working.
- There is no `miDebuggerPath`, so gdb is found on PATH. This keeps the file portable.
- The program is started by hand, not by OpenTofu, so the plugin handshake variables are
  missing. It may exit or complain. For handshake testing, add `args` / `environment` here.

### `.vscode/extensions.json`: recommended extensions
```json
{
    "recommendations": [
        "ms-vscode.cmake-tools",
        "ms-vscode.cpptools",
        "llvm-vs-code-extensions.vscode-clangd",
        "zxh404.vscode-proto3"
    ]
}
```

### `.gitignore` (project root)
```
build/
.cache/
```

---

## 5. The one-time step that cannot be put in a file

CMake Tools keeps the selected kit and launch target in VS Code's workspace state, not in the
project. Do this on every machine and after clearing VS Code state:

1. **Ctrl+Shift+P** > *Developer: Reload Window*.
2. **Ctrl+Shift+P** > *CMake: Select a Kit* > **GCC 13.3.0 x86_64-linux-gnu**
   (`/usr/bin/gcc`, `/usr/bin/g++`). Do **not** pick a Clang kit: the cache is configured with
   GCC and a different compiler makes CMake wipe the cache.
3. Check the status bar shows the **Debug** variant.
4. In **Run and Debug**, choose **Debug tofu-plugin-cpp** in the dropdown.
5. **Ctrl+Shift+B** to build, then set a breakpoint in `main` and press **F5**.

**Symptom of skipping step 2:** the build task "fails" with no useful message. The real reason
is in `~/.local/share/CMakeTools/log.txt`: `Not starting CMake driver: no kit selected`.

---

## 6. Doing the same without VS Code

```bash
cd ~/tpc
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
      -DCMAKE_PREFIX_PATH=$HOME/.local/grpc -DCMAKE_IGNORE_PREFIX_PATH=/mnt/c \
      -DCMAKE_C_COMPILER=/usr/bin/gcc -DCMAKE_CXX_COMPILER=/usr/bin/g++
cmake --build build -j$(nproc)
```
Always pass the same compilers VS Code uses. If a command-line configure leaves `build/` with a
different compiler, CMake Tools' next configure wipes the cache and drops your `-D` options. If
that happens, delete `build/` and let Ctrl+Shift+B configure from scratch.

---

## 7. Troubleshooting

| Symptom | Likely cause | What to do |
|---|---|---|
| Squiggles on every `#include` | No `build/compile_commands.json` | Ctrl+Shift+B (or run the section 6 configure), then *clangd: Restart language server* |
| Squiggle on one generated header | Proto renamed, so generated header name changed | Re-run configure; check the include matches `<proto-stem>.grpc.pb.h` |
| Build task fails with no message | No kit selected | Section 5, step 2 |
| F5 starts nothing / empty program path | Dropdown on the CMake-target entry with no launch target set | Pick **Debug tofu-plugin-cpp**, or run *CMake: Set Launch/Debug Target* |
| Breakpoints are grey and never hit | Build type not Debug, so no symbols | Check status bar is Debug; `readelf -S build/tofu-plugin-cpp \| grep debug_info` |
| Weird protobuf/abseil version errors | A `/mnt/c` copy was found | Check `CMakeCache.txt` has no `/mnt/c`; delete `build/` and reconfigure |
| `.proto` file has no colours | Extension not installed on the side where the window runs | Install `zxh404.vscode-proto3` there |

---

## 8. Resuming after a restart (for Claude)

Read this file first.

**Rules of engagement set by the user**
- Do not fix compile errors in `src/tofu-plugin.cpp`. The user fixes the code. Claude handles
  only IDE, header-resolution and build-tooling problems.
- Do not build-and-declare-success on the user's behalf for the plugin code. A successful build
  is not the goal here.

**State at the end of this session**
- All `.vscode/` files and `.gitignore` are in place and match section 4.
- `build/` is configured (GCC, Debug, `~/.local/grpc`) and contains `compile_commands.json`.
  It is safe to delete and regenerate.
- `zxh404.vscode-proto3` is installed on WSL (Ubuntu-24.04) and on the Windows host.
- Git: `CMakeLists.txt` has two fixes (staged), `.vscode/extensions.json` and
  `src/tofu-plugin.cpp` are modified, and `.gitignore` and `.vscode/` are new. **Nothing has
  been committed.** Commit only if the user asks.
- The only unconfirmed item is whether the user has done the kit selection (section 5) and seen
  a breakpoint hit. Ask, or check `~/.local/share/CMakeTools/log.txt` for a driver start.
- The session name `tofu-plugin-ide-config` could not be set by Claude. The user sets it with
  the `/rename` command.

**Out of scope for this session:** plugin code, the proto files and the OpenTofu protocol work.
They are tracked elsewhere; do not mix them into this topic.
