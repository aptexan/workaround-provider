# Testing the provider

This folder holds shell scripts that check the provider from the outside. This page explains **what each one tests, how to run it, and what a good result looks like**. It assumes you know C++ but are new to gRPC and to how OpenTofu talks to a provider.

The scripts are deliberately plain. Every check is **one line** that says what is checked and prints `PASS` or `FAIL`; on a failure it also prints what was wanted and what was seen. There is no test framework.

## 1. Quick start

```sh
# 1. build the provider (see BUILDING.md in the repository root)
# 2. run everything
tools/run-all.sh
```

That runs the checks below, **once in plaintext and once with mutual TLS** where that makes sense (118 checks in all), and ends with `ALL CHECKS PASSED` (or `SOME CHECKS FAILED`, with exit status 1). `check-tofu.sh` needs OpenTofu set up first (section 4); without it `run-all.sh` says `SKIPPED` for that one and runs the rest.

You need: `bash`, `curl` with HTTP/2 support (`curl --version` should list `HTTP2`), `openssl` (the command line tool, to make test certificates), `python3` (only to occupy a port in `check-startup.sh`), and, for `check-tofu.sh`, `tofu` on your `PATH`. The provider itself must have been built against gRPC with **OpenSSL** (see `BUILDING.md`); otherwise it does not compile.

By default the scripts look for the provider at `$HOME/tpc/build/terraform-provider-workaround`. If yours is elsewhere: `PROVIDER=/path/to/terraform-provider-workaround tools/run-all.sh`.

## 2. The background you need

### What a provider is

OpenTofu (we call it **Core**) never contains the code that creates real resources. That lives in a separate program, a **provider**, which Core starts as a child process and then talks to over **gRPC**: Core calls named functions, such as `GetSchema` or `ApplyResourceChange`, in our process over a network connection. The calls and replies are described in `.proto` files and turned into C++ classes by the protobuf compiler.

Providers are normally written in Go with a library called **go-plugin**. Ours is C++, so it has to behave exactly like a go-plugin plugin, or Core will not accept it. These scripts check that.

### The handshake (what happens before any gRPC call)

1. Core starts the provider executable and passes instructions in **environment variables**:

   | Variable | Meaning |
   | --- | --- |
   | `TF_PLUGIN_MAGIC_COOKIE` | a fixed, public string; shows the program was started by a plugin host and not by a person |
   | `PLUGIN_PROTOCOL_VERSIONS` | which provider protocol versions Core understands, for example `5,6` |
   | `PLUGIN_MIN_PORT`, `PLUGIN_MAX_PORT` | the TCP ports the plugin may listen on |
   | `PLUGIN_CLIENT_CERT` | only when encryption (mutual TLS) is on; not supported yet |

2. The provider checks them, starts a gRPC server on a local port, and prints **exactly one line on standard output**, the handshake line:

   ```
   1|5|tcp|127.0.0.1:12345|grpc|
   ```

   The fields: go-plugin protocol (always `1`), the provider protocol version we chose, network type, address, `grpc`, and an (empty, for now) certificate.
3. Core reads that line, connects to the address, and from then on everything is gRPC. Standard output must carry nothing else; diagnostics go to standard error.
4. When Core is done it calls `Shutdown` on a small go-plugin service. The provider must exit quickly: Core waits only about 2 seconds, then kills it.

Besides the provider's own service, a go-plugin server also offers a **health** service (Core's "are you alive?" ping), a **stdio** stream and a **broker** stream. Core opens the last two when it connects, and they stay open until shutdown.

### Mutual TLS and plaintext

Normally Core encrypts its connection to the provider, and **both sides prove who they are with a certificate** (that is the "mutual"). The exchange, from `PLUGIN_CLIENT_CERT` to the certificate in the handshake line, is drawn in `mtls-exchange.svg` (next to this file) and summarised here:

1. Core makes a certificate and passes it to the provider in `PLUGIN_CLIENT_CERT`.
2. The provider makes its own certificate and private key, and prints the certificate (base64 of its DER bytes, no `=` padding) as the last field of the handshake line.
3. The provider answers only clients that present **Core's** certificate. Core trusts only **the provider's** certificate from the handshake line, and expects the name `localhost`.

If `PLUGIN_CLIENT_CERT` is not set, which is what Core does when it is told `TF_DISABLE_PLUGIN_TLS=1`, the provider serves plaintext and the last field of the handshake line is empty. The scripts test both. `TLS=1` makes `check-health.sh`, `check-streams.sh` and `check-tofu.sh` run in TLS mode (they print `== mode: mutual TLS`); `check-tls.sh` is always TLS. `run-all.sh` runs everything both ways.

### Running the binary by hand

The provider refuses to start without the magic cookie. Pass `--debug` to skip that check when you start it yourself or under a debugger. Scripts that start the provider themselves already do this.

### How a gRPC call looks in these scripts

There is no gRPC client library here. A gRPC call is an HTTP/2 POST whose body is a **0x00 byte, the message length as 4 bytes, then the protobuf message**. The scripts send those bytes with `curl`, and `check-health.sh` writes them out byte by byte with a comment, so you can see exactly what is sent. A reply carries a `grpc-status` (0 = OK, 1 = CANCELLED, 5 = NOT_FOUND, 12 = UNIMPLEMENTED).

## 3. The scripts at a glance

| Script | Needs OpenTofu? | Checks | What it proves | Takes |
| --- | --- | --- | --- | --- |
| `check-build.sh` | no | 10 | the build itself is sound: `build/` uses one installation and no Windows packages, `cmake --build` works, and a fresh configure never uses Windows packages and refuses mixed installations | about 25 s |
| `check-startup.sh` | no | 19 | the provider validates what Core tells it, and refuses bad input without printing a handshake | about 13 s |
| `check-health.sh` | no | 7 | the health service answers for `plugin`; other calls behave | under 1 s |
| `check-streams.sh` | no | 6 | the stdio and broker streams stay open, don't block other calls, and are cancelled on shutdown | about 1.5 s |
| `check-tls.sh` | no | 25 | mutual TLS: only Core's certificate is accepted, and the provider's certificate has go-plugin's properties | under 1 s |
| `check-tofu.sh` | yes | 19 | the real Core runs plan, apply and destroy against the provider with no errors or warnings | under 1 s |
| `run-all.sh` | | 118 | runs all of the above; health, streams and tofu twice (`TLS=0`, `TLS=1`) | about 14 s |

`lib.sh` is a small helper the others load; it defines `expect` (the one-line check) and a few things for talking to a running provider. It is not run directly.

Each script's exit status is 0 only if every check passed.

### `check-build.sh`: is the build itself sound?

The other checks run a provider that is already built. This one checks how it is built, because a `build/` configured against the wrong packages fails with hundreds of confusing compile errors (`Protobuf C++ gencode is built with an incompatible version of ...`). It looks at your real `build/`: its CMake cache has no dependency on a Windows drive (`/mnt/...`, reached through WSL's inherited `PATH`), all dependencies come from **one** installation prefix, and `cmake --build` succeeds (incremental, so a no-op when up to date). Then, in scratch folders, it checks the project's own protections: a fresh configure with the prefix works and uses only that prefix; configuring from the command line and then the way VS Code's CMake Tools does (which asks for `/usr/bin/gcc`, so CMake deletes the cache and re-runs without the `-D` options) still finds gRPC, whichever comes first; a fresh configure with a prefix that lacks gRPC, or with no prefix, never uses a Windows package; and packages taken from two different installations are refused (skipped, and says so, when no second install with Protobuf exists on the machine). The protections themselves are in the top-level `CMakeLists.txt`.

### `check-startup.sh`: refusing bad input

Core only ever sends good values, so the refusal paths can only be tested by starting the provider by hand. The script runs it with a **nearly empty environment** (`env -i`) and uses `timeout 1`: exit status **124** means "still running after one second", so it started; **1** means it refused. The handshake line is whatever it printed on stdout.

| Group | Cases | Expected |
| --- | --- | --- |
| Cookie | none, wrong, right, `--debug` without one | none or wrong: exit 1 and **no handshake line**; right or `--debug`: starts; the handshake line has the exact form `1|5|tcp|127.0.0.1:PORT|grpc|` |
| Protocol version | offered `5,6`, `6,5`, `abc,5`; only `6`, only `4`, only `abc`, nothing | answers `5` whatever the order, junk entries skipped; with no 5 on offer, or nothing offered: exit 1 |
| Port range | a one-port range; `MIN>MAX`; `MIN=abc`; no range; `--port=34580`; range with the first port taken; range of one taken port | listens on the port in range (or the one given); skips a taken port; exit 1 when none is free or values are invalid |

Why "no handshake line" matters: Core is waiting for that line. A provider that has to refuse must do so *before* printing it, or Core would try to connect to something that is not there.

The "port taken" cases need a taken port. The script opens one with a one-line Python listener. (Not a second copy of the provider: gRPC lets servers share a port, so that would not block anything.)

### `check-health.sh`: the "are you alive?" ping

go-plugin plugins register the standard gRPC health service and mark the name `plugin` as serving. Core's ping asks it about `plugin`. A normal OpenTofu run never pings, so only this script can notice a mistake. It checks that `plugin` and the empty name both answer `SERVING` (exact reply bytes), that an unknown name gives 5, that `GetSchema` works, and that a method that does not exist gives 12.

### `check-streams.sh`: long-lived streams and shutdown

The stdio and broker calls are **streams**: one call that stays open and carries many messages. They never end by themselves, and a gRPC server waits for open calls when it shuts down, so the provider has to cancel them on purpose. The script opens both, waits a second, checks they are still open and that `GetSchema` still works meanwhile, then calls `Shutdown` and checks the provider exits **within 0.5 s** and that the stdio stream ended with status 1 (CANCELLED).

The 0.5 s limit is deliberate. Core allows about 2 s, but if the provider did *not* cancel the streams, the server would wait about 1 s for them, which would still beat 2 s and hide the bug. 0.5 s does not.

### `check-tls.sh`: who may talk to the provider

Always in TLS mode. It plays Core with `curl` and `openssl`, using a client certificate made like Core's (P-521, self-signed, CA, name `localhost`), and checks:

| Group | Checks |
| --- | --- |
| Who may talk to the provider | Core's certificate works; **no** client certificate is refused; **someone else's** client certificate is refused; plaintext to the TLS port is refused |
| What the client (Core) checks | a different trusted certificate than the one in the handshake line is refused (so the certificate presented really is the one in the line); the name must be `localhost` (`127.0.0.1` is refused) |
| TLS versions | TLS 1.2 works, TLS 1.1 is refused (go-plugin's minimum is 1.2) |
| The certificate, against go-plugin's | ECDSA P-521 key; signed with SHA-512; name `localhost`; alternative name `DNS:localhost`; CA flag; key usages Certificate Sign, Digital Signature, Key Encipherment, Key Agreement; extended usages server and client authentication; valid now and for at least 25 more years |
| Fresh key and bad input | field 6 never has `=` padding (looked at **10** certificates, because base64 pads only when the length is not a multiple of 3 and the length varies from key to key, so one certificate can look right by luck); every start makes a new certificate; a client certificate that is not PEM is refused (exit 1, no handshake) with a message naming `PLUGIN_CLIENT_CERT` |

"Refused" for a connection means curl got no reply at all. A client certificate that is wrong is refused by closing the connection, not with a gRPC error. The Go client behaves the same way, which is how Core saw the failure described in `BUILDING.md` before gRPC was built against OpenSSL.

### `check-tofu.sh`: the real thing

Runs `tofu plan`, `tofu apply -auto-approve` and `tofu destroy -auto-approve` with `TF_LOG=DEBUG`, and for each checks: exit status 0, no `[ERROR]` lines, no `[WARN]` lines, no `Error:` messages, and that every provider process Core started (`starting plugin`) also logged `plugin process exited`. Then that no provider process is left running. It also checks that Core really used mutual TLS in TLS mode (Core logs `configuring client automatic mTLS` only then) and did not in plaintext mode, so the mode asked for is the mode that ran. With `TLS=1` it leaves encryption on, as in normal use; without it, it sets `TF_DISABLE_PLUGIN_TLS=1`. This is the most important check because the other side of the conversation is the genuine OpenTofu. A provider that Core had to kill shows up as `[WARN]`/`[ERROR]` lines.

It **deletes `terraform.tfstate*` in `TOFU_DIR`** before and after, so never point it at a directory with real state. Logs go to `$TMPDIR/tofu-logs/` (`/tmp/tofu-logs/`).

## 4. One-time OpenTofu setup (only for `check-tofu.sh`)

1. Create a directory (default `~/wpe`, or set `TOFU_DIR`) with two files.

   `tofu.tf`
   ```hcl
   terraform {
     required_version = "1.13.1"        # change this if your tofu version differs
     required_providers {
       workaround = {
         source  = "aptexan/workaround"
         version = "0.0.1"
       }
     }
   }
   ```

   `example.tf`
   ```hcl
   resource "workaround_example" "example" {
   }
   ```
2. Create `~/.tofurc` (or merge into yours) with the **absolute** path of the build folder:

   ```hcl
   provider_installation {
     dev_overrides {
       "aptexan/workaround" = "/absolute/path/to/this/repo/build"
     }
     direct {}
   }
   ```

   Do not run `tofu init`; with `dev_overrides` it is not needed, and OpenTofu prints a warning about the override, which is normal.
3. `TOFU_DIR=/path/to/dir tools/check-tofu.sh`

To see what OpenTofu does by hand: `cd <dir>; TF_DISABLE_PLUGIN_TLS=1 TF_LOG=DEBUG tofu plan`.

## 5. How we know the tests themselves are right: negative tests

A check that cannot fail is worthless, so each check is tried against a provider that is **deliberately broken**, one feature at a time. Each break must turn its own check red and nothing else. This is automated:

```sh
tools/negative-tests/negative-check.sh        # about 7 minutes; not part of run-all.sh
```

It works on a temporary copy of the project, and its last check proves that your source and `build/` are exactly as they were (content, size and modification times). See `tools/negative-tests/README.md` for the list of breaks, how to add one, and why it is built the way it is. There are 26 breaks: the provider's startup rules, health, streams and shutdown, the mutual-TLS rules (client certificate not requested or not verified, plaintext despite a certificate, no certificate in the handshake line, a non-PEM client certificate accepted) seven properties of the provider's certificate, and three protections in `CMakeLists.txt`: Windows `PATH` entries not removed, packages from two installations accepted, and no standard-prefix fallback (the first two are skipped on a machine with no Windows `PATH` or no second gRPC install). Every break turned its own check red: `passed 62, failed 0` on 2026-11-05. The unmodified provider also passed every normal script many times in a row with no flaky result.

Writing the tests caught mistakes in the tests themselves. One more, from the TLS checks: a check on random output (the `=` padding in the certificate) passed a broken provider on one run in three, because one sample can be right by luck; it now looks at ten, and a later negative run caught the break. If a check depends on something random, look at many samples.

Earlier mistakes: A "port taken" check that used a second copy of the provider as the blocker passed by luck, because gRPC lets two servers share a port. And "the broker stream ended" could not be observed from `curl` (it keeps reading its input after the server cancels), so that check was replaced by the 0.5 s exit time. The negative-test script itself was checked the same way: fed a break that changes nothing and a break whose text does not exist, it reports `FAIL` for both.

If you change a check script, run `negative-check.sh` again.

## 6. What these tests do not cover

- **No comparison with a real Go provider.** The expected behaviour comes from reading the go-plugin and OpenTofu sources. The `curl` scripts act as Core according to that reading, so they test our implementation of it, not Go's. Only `check-tofu.sh` uses the genuine Core. (A Go reference plugin to compare against is a planned, deferred, stronger check.)
- **Encryption details.** Mutual TLS is tested against the real Core (`TLS=1 check-tofu.sh`) and against `curl`/`openssl` playing Core (`check-tls.sh`). Not tested: which cipher suites are chosen, certificate expiry handling, and Unix-socket TLS.
- **Provider protocol 6, Unix sockets, and broker multiplexing.** Not implemented, so not tested.
- **Real data.** The provider has one resource with no attributes, so the checks cover the transport and lifecycle, not resource handling.
- **Many streams at once.** Only two open streams were tried.
- **The wording of stderr messages.** The refusal checks require exit status 1 and no handshake line, not particular text.

## 7. Troubleshooting

- **The provider exits at once with "This binary is a plugin…".** It was started without the cookie. Use `--debug`, or start it through OpenTofu.
- **`Provider not found`.** Build it, or set `PROVIDER=`.
- **OpenTofu says `error reading server preface: EOF` / `Plugin did not respond` in TLS mode, and the provider logged `mutual TLS`.** The provider was built against a gRPC that uses BoringSSL, which cannot accept Core's P-521 client certificate. A current build refuses to compile that way; rebuild against OpenSSL (`BUILDING.md`).
- **`TLS=1` checks fail at once with "needs the openssl command".** Install the `openssl` command line tool.
- **Counting provider processes.** `pgrep -f name` also matches its own command line. The scripts use an anchored pattern, `pgrep -f '^/full/path/to/terraform-provider-workaround'`.
- **`curl` checks fail strangely.** Check `curl --version` lists `HTTP2`.
- **Core says the provider "failed to exit gracefully".** Core had to kill it because it did not exit in time; `check-streams.sh` shows whether a stream is holding it up.
- **Passes with `--debug` but not through OpenTofu.** `--debug` skips the cookie check only. Read the log in `$TMPDIR/tofu-logs/` for the handshake line and the first error.
