#!/bin/bash
# Core opens two calls that never finish by themselves: the stdio and broker streams. The provider must keep them open,
# keep answering other calls meanwhile, and cancel them when asked to shut down, so it exits quickly (Core allows ~2 s
# before it kills the process). No OpenTofu needed.
. "$(dirname "$0")/lib.sh"
start_provider
NO_FIELDS='\x00\x00\x00\x00\x00'

# Open both streams in the background. (-m 8 only stops the script hanging if something goes wrong. The broker stream
# is two-way, so its client keeps the request side open by sleeping.)
printf "$NO_FIELDS" | grpc_call /plugin.GRPCStdio/StreamStdio -m 8 >"$WORK/stdio_status" & STDIO=$!
{ printf "$NO_FIELDS"; sleep 7; } | grpc_call /plugin.GRPCBroker/StartStream -m 8 >/dev/null & BROKER=$!
sleep 1
still_running() { kill -0 "$1" 2>/dev/null && echo yes || echo no; }

expect "stdio stream still open after 1 s"   "$(still_running $STDIO)" yes
expect "broker stream still open after 1 s"  "$(still_running $BROKER)" yes
expect "GetSchema works meanwhile"           "$(printf "$NO_FIELDS" | grpc_call /tfplugin5.Provider/GetSchema)" 0

# Shutdown is what Core calls when it is done. With both streams open, the provider must still exit almost at once.
# (If it did not cancel the streams itself, the server would wait about 1 s for them.)
expect "Shutdown succeeds"                   "$(printf "$NO_FIELDS" | grpc_call /plugin.GRPCController/Shutdown)" 0
for _ in $(seq 25); do kill -0 $PID 2>/dev/null || break; sleep 0.02; done          # up to 0.5 s
expect "provider exited within 0.5 s"        "$(still_running $PID)" no
sleep 0.3
expect "stdio stream was cancelled (status 1)" "$(cat "$WORK/stdio_status")" 1
summary
