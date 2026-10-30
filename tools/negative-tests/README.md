# Negative tests: do the checks notice a broken provider?

The normal checks in `tools/` (`check-startup.sh`, `check-health.sh`, `check-streams.sh`, `check-tofu.sh`) are meant to go red when the provider is wrong. A check that can never go red is worse than none, because it gives false confidence. `negative-check.sh` proves that they can.

```sh
tools/negative-tests/negative-check.sh        # takes about 4 minutes
```

## What it does

1. Copies the project to a temporary folder and builds it there. The copy is deleted at the end, even if you press Ctrl-C.
2. Runs the four normal checks on the **unbroken** copy. They must all be green. If not, "red" later would mean nothing, so the script reports it as a failure.
3. For each entry in the table below it breaks one feature in the copy's `src/tofu-plugin.cpp` by replacing one line of text, rebuilds, and runs the check that is supposed to notice. That check must go **red**.
4. Prints one `PASS`/`FAIL` line per step and a summary. Exit status 0 only if everything passed.

Two things are checked for every break, so a broken break cannot pass silently:
- the broken copy still **builds** (otherwise a missing binary would also make a check go red);
- the text to replace occurs **exactly once** in the source (if someone edits the provider so it no longer matches, the script fails loudly instead of testing nothing).

## What it guarantees: your project is not touched, not even slightly

- The only thing read from your project is a copy (`tar`); every build, edit and test run happens in the temporary folder.
- Everything else is private to that folder too: temporary files and logs (`TMPDIR`), the OpenTofu configuration (`TF_CLI_CONFIG_FILE`, so `~/.tofurc` is not read or changed) and OpenTofu's working folder (a copy of your `*.tf` files, so `check-tofu.sh` deletes state files there, not in your real `TOFU_DIR`).
- It **proves** it. Before it starts and again at the end it fingerprints your whole project folder, including `build/`: every file's content, size and modification time (only `.git` is left out, because plain `git status` rewrites its index). The last line of the output is a check that the two fingerprints are equal. The fingerprint function was tried on a scratch folder and does detect a content change, a same-size content change, a modification-time-only change, a change in `build/`, and a new file.
- Checked from outside as well on 2026-10-30: a separate fingerprint of `~/tpc` (with `build/`) and of `~/wpe`, taken before and after a full run, was identical.

Because of that last check, **do not edit or rebuild the project while it runs**; your own change would be reported as a difference.

## The breaks

| What is broken in the copy | Check that must go red |
| --- | --- |
| health name `plugin` not registered | `check-health.sh` |
| open streams not cancelled on shutdown | `check-streams.sh` |
| stdio service not registered | `check-streams.sh` |
| `Shutdown` service not registered | `check-streams.sh` and `check-tofu.sh` |
| cookie check disabled | `check-startup.sh` |
| protocol version always 5 | `check-startup.sh` |
| port range ignored | `check-startup.sh` |
| handshake line missing its last `\|` | `check-startup.sh` |
| a log line written to stdout | `check-startup.sh` |

The `check-tofu.sh` cases are skipped (the script says `SKIP`) if `tofu` or `TOFU_DIR/tofu.tf` is missing. For those cases the script writes a private OpenTofu config pointing at the **copy**, via `TF_CLI_CONFIG_FILE`, so your `~/.tofurc` is not used and not changed. `check-tofu.sh` still deletes `terraform.tfstate*` in `TOFU_DIR`, as it always does.

## Adding a break

Add one line at the end of `negative-check.sh`:

```sh
broken_provider "what is broken" check-something.sh 'exact line in src/tofu-plugin.cpp' 'what to replace it with'
```

Keep the replaced text on **one line**, and make the replacement still compile. Then run the script and see the new line go `PASS`.

## Options

- `CMAKE_PREFIX_PATH=...` if your gRPC is not in `~/.local/grpc` (the default used in `BUILDING.md`).
- `PROVIDER` is set by the script itself; don't set it.
- `TOFU_DIR=...` as for `check-tofu.sh`.

## How the script itself was checked

It was run with two bogus entries: a "break" that replaces a line with itself (so the check stays green), and one whose text is not in the source. Both were reported as `FAIL` and the exit status was 1, as they should be. Real run on 2026-10-30: unbroken copy green on all four checks, all nine breaks (ten cases) went red, and the untouched-project check passed: `passed 25, failed 0`.
