# Build and debug with CMake in VS Code (Ctrl+Shift+B and F5)

This sets up the `grpc_helloworld` folder so that:

| Key | What it does |
|---|---|
| **Ctrl+Shift+B** | Configures (if needed) and builds all targets with CMake, as a Debug build |
| **F5** | Builds, then launches the selected program under gdb with breakpoints |
| **F7** | CMake Tools' own "CMake: Build" (same result as Ctrl+Shift+B) |

The setup is stored in the project's `.vscode/` folder, so it survives closing
VS Code, restarting it, and rebooting the machine. Nothing depends on your
shell, your PATH, or VS Code's remembered UI state.

## 0. Prerequisites (WSL / Linux)

- VS Code opened on **this folder** (`prj/grpc_helloworld`), as a WSL/remote window.
- Extensions installed in the WSL side: **CMake Tools** (`ms-vscode.cmake-tools`)
  and **C/C++** (`ms-vscode.cpptools`, provides the gdb debug adapter).
- `cmake`, `g++` and `gdb` installed (`which cmake g++ gdb`).
- gRPC, protobuf and abseil installed together under `~/.local/grpc`
  (see the README for building them).

## 1. Why a plain build can fail here (the persistent part)

Under WSL, your **Windows PATH is appended to the Linux PATH**, so it contains
folders such as `/mnt/c/Users/<you>/anaconda3/Library/bin`. CMake's
`find_package()` searches PATH entries, so it can find Windows copies of
protobuf and abseil, mixed with the Linux gRPC in `~/.local/grpc`. The result is
a version mismatch and a failed build. Which copy wins depends on how VS Code
was started, so it can work one day and fail the next.

The cure is to tell CMake explicitly where the libraries are and to ignore
`/mnt/c`. Put that in a file inside the project (step 3), never in the shell.
A value typed into a terminal, or cached in `build/CMakeCache.txt`, is not
durable.

## 2. Create the `.vscode` folder

Create `prj/grpc_helloworld/.vscode/` and add the five files below. If a file
already exists, merge the keys in instead of replacing it.

## 3. `.vscode/settings.json`: where CMake finds things

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
    }
}
```

- `CMAKE_PREFIX_PATH` points CMake at the gRPC install. `${userHome}` expands to
  the home directory of whoever opens the folder, so no username is hardcoded.
  If gRPC is installed elsewhere, change the path (or use `${env:SOME_VAR}`).
- `CMAKE_IGNORE_PREFIX_PATH` stops CMake from looking under `/mnt/c`
  (the Windows side). **This is the line that fixes the failing build.**
- `cmake.generator` avoids a "ninja not found" probe; Makefiles are fine here.

## 4. `.vscode/cmake-variants.json`: always Debug

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

Without this file, CMake Tools can fall back to an "Unspecified" build type
after a restart. That builds with no `-g`, and breakpoints are then silently
skipped. This file makes Debug the default every time. You can still switch
to Release from the status bar.

## 5. `.vscode/tasks.json`: makes Ctrl+Shift+B build directly

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

`"isDefault": true` is what binds **Ctrl+Shift+B** to this task. Without it,
VS Code shows a picker instead of building.

## 6. `.vscode/launch.json`: F5 debugging

```json
{
    "version": "0.2.0",
    "configurations": [
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
            "miDebuggerPath": "/usr/bin/gdb",
            "setupCommands": [
                { "description": "Enable pretty-printing for gdb", "text": "-enable-pretty-printing", "ignoreFailures": true }
            ],
            "preLaunchTask": "CMake: build"
        }
    ]
}
```

- `program` uses the CMake launch target, so it follows whichever program you
  pick. This project has two: `greeter_server` and `greeter_client`.
- `preLaunchTask` rebuilds before every F5.
- Keep `stopAtEntry` at `false`; `true` pauses before `main` and looks like
  breakpoints not working.

## 7. `.vscode/extensions.json`: optional recommendation

```json
{
    "recommendations": ["ms-vscode.cmake-tools"]
}
```

## 8. Reset the old build directory (one time)

An old `build/` may have the wrong Windows paths cached in
`CMakeCache.txt`, and CMake keeps cached paths even after the settings change.
Delete it once:

```bash
cd ~/b/pracs/prj/grpc_helloworld
rm -rf build
```

`build/` is generated output, so nothing is lost.

## 9. Select the kit and launch target (one time per machine)

1. Reload the window: **Ctrl+Shift+P** > *Developer: Reload Window*.
2. If prompted for a kit, choose **GCC** (`/usr/bin/gcc`, `/usr/bin/g++`).
3. Check the status bar shows the **Debug** variant.
4. Click the launch target in the status bar (or **Ctrl+Shift+P** >
   *CMake: Set Launch/Debug Target*) and pick `greeter_server` or `greeter_client`.

## 10. Use it

- **Ctrl+Shift+B**: configure and build. Watch the *Output* > *CMake/Build* panel.
- Set a breakpoint, for example in `greeter_server.cc`, then press **F5**.

## 11. Verify it worked

Check the cache points at the Linux install:

```bash
grep -E "^(Protobuf_DIR|absl_DIR|gRPC_DIR|CMAKE_BUILD_TYPE)" build/CMakeCache.txt
```

Expected: the three `_DIR` paths are under `~/.local/grpc/...` (your home directory), and
`CMAKE_BUILD_TYPE:STRING=Debug`. Any `/mnt/c/...` path means the settings were
not applied. Redo steps 3 and 8.

Check the binary has debug symbols:

```bash
readelf -S build/greeter_server | grep debug_info
```

## 12. Persistence test

Close VS Code completely (or reboot), reopen the folder, press **Ctrl+Shift+B**,
then **F5**. It should build and hit your breakpoint without further setup.

If not, check in this order:
1. `.vscode/settings.json`: the `CMAKE_PREFIX_PATH` and `CMAKE_IGNORE_PREFIX_PATH` lines are present.
2. `.vscode/cmake-variants.json` exists and `"default"` is `"debug"`.
3. The CMake log: **Output** > *CMake/Build* shows `-DCMAKE_BUILD_TYPE:STRING=Debug` and the Linux `-DCMAKE_PREFIX_PATH`.

## Doing the same without VS Code

```bash
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_PREFIX_PATH=$HOME/.local/grpc -DCMAKE_IGNORE_PREFIX_PATH=/mnt/c \
      -DCMAKE_C_COMPILER=/usr/bin/gcc -DCMAKE_CXX_COMPILER=/usr/bin/g++
cmake --build build -j$(nproc)
```

Always pass the same compilers VS Code uses (`/usr/bin/gcc`, `/usr/bin/g++`).
If a command-line configure leaves `build/` with a different compiler (CMake
defaults to `/usr/bin/c++`), CMake Tools' next configure makes CMake wipe the
cache and reconfigure *without* your `-D` options. It then silently picks up the
Windows protobuf again and the build fails with errors such as
`expected initializer before 'final'` in `helloworld.pb.h`. Recovery: delete
`build/` and let Ctrl+Shift+B configure it from scratch.

## Red squiggles on `#include` lines (IntelliSense)

The squiggles come from the editor's language server, not from the compiler.
The Microsoft C/C++ engine knows nothing about `~/.local/grpc/include` or the
generated `build/helloworld.pb.h`, so every include looks unresolved.

Fix: use **clangd**, which reads the exact compiler flags CMake recorded in
`build/compile_commands.json`. Add these keys to `.vscode/settings.json`
(they are already in the file, after the `cmake.*` keys):

```json
"C_Cpp.intelliSenseEngine": "disabled",
"C_Cpp.autocomplete": "disabled",
"C_Cpp.errorSquiggles": "disabled",
"clangd.arguments": [
    "--compile-commands-dir=${workspaceFolder}/build",
    "--background-index",
    "--header-insertion=iwyu"
]
```

Requirements:
- The **clangd** extension (`llvm-vs-code-extensions.vscode-clangd`) is installed.
- A CMake configure has run, so `build/compile_commands.json` exists.
  It is regenerated on every configure (`CMAKE_EXPORT_COMPILE_COMMANDS=ON`),
  and it is what contains the generated `helloworld.pb.h` include path.
  If you delete `build/`, re-run Ctrl+Shift+B, then
  **Ctrl+Shift+P** > *clangd: Restart language server*.

Check from a terminal (should end with `0 errors`):

```bash
clangd --compile-commands-dir=build --check=$PWD/greeter_server.cc
```

## Debugging the server and the client together (compound launch)

This project builds two programs, `greeter_server` and `greeter_client`. The
single entry from step 6 starts only one of them. To start both with one F5,
add two fixed entries and a **compound** to `.vscode/launch.json`.

### Why the single entry is not enough

`"program": "${command:cmake.launchTargetPath}"` expands to **one** executable:
the CMake launch target shown in the status bar. It cannot expand to two
programs, and a configuration cannot point to a compound. The relationship only
goes the other way: a compound lists configurations by name.

### Add the two entries

Inside `"configurations"`, after the existing entry (mind the commas):

```json
{
    "name": "Debug server",
    "type": "cppdbg",
    "request": "launch",
    "program": "${workspaceFolder}/build/greeter_server",
    "args": [],
    "stopAtEntry": false,
    "cwd": "${workspaceFolder}/build",
    "environment": [],
    "externalConsole": false,
    "MIMode": "gdb",
    "miDebuggerPath": "/usr/bin/gdb",
    "setupCommands": [
        { "description": "Enable pretty-printing for gdb", "text": "-enable-pretty-printing", "ignoreFailures": true }
    ]
},
{
    "name": "Debug client",
    "type": "cppdbg",
    "request": "launch",
    "program": "${workspaceFolder}/build/greeter_client",
    "args": [],
    "stopAtEntry": true,
    "cwd": "${workspaceFolder}/build",
    "environment": [],
    "externalConsole": false,
    "MIMode": "gdb",
    "miDebuggerPath": "/usr/bin/gdb",
    "setupCommands": [
        { "description": "Enable pretty-printing for gdb", "text": "-enable-pretty-printing", "ignoreFailures": true }
    ]
}
```

- Neither entry has a `preLaunchTask`. The compound builds once for both, so the
  two sessions don't run the build at the same time.
- The client has `"stopAtEntry": true` on purpose: it makes only one call and
  exits, and it would fail if it ran before the server is listening. It pauses
  at `main` until you press Continue.

### Add the compound

A **sibling** of `"configurations"`, at the top level of the file:

```json
"compounds": [
    {
        "name": "Debug server + client",
        "configurations": ["Debug server", "Debug client"],
        "preLaunchTask": "CMake: build",
        "stopAll": true
    }
]
```

- `configurations` must use the exact names of the two entries.
- `preLaunchTask` builds once, then both sessions start.
- `stopAll` ends both sessions when you stop either one.

### Use it

1. In the **Run and Debug** view, open the dropdown at the top and choose
   **Debug server + client**. F5 runs whatever the dropdown shows, so if it still
   says "Debug (CMake Tools target)" only the server starts. VS Code remembers
   the choice for this workspace.
2. Press **F5**. It builds, starts the server, and starts the client paused at `main`.
3. In the **Call Stack** panel, select the client session and press **Continue**
   (F5). The client calls the server and prints `Greeter received: Hello world`.
4. Breakpoints in either `greeter_server.cc` or `greeter_client.cc` hit as usual.

### Optional: make the compound the first entry

Add `"presentation": { "order": 1 }` to the compound, and larger numbers
(2, 3, 4) to the other entries, so the compound sorts first in the dropdown.

### Troubleshooting

| Symptom | Cause and fix |
|---|---|
| Only the server starts | The dropdown is on a single entry. Select **Debug server + client**. |
| Client never appears in Call Stack | Same cause, or a name in `configurations` does not match an entry exactly. |
| Server fails to bind port 50051 | A previous server session is still running. Stop it first, then press F5. |
| Client reports an RPC failure | It ran before the server was listening. Keep `stopAtEntry` true and press Continue after the server starts. |
