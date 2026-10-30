#!/bin/bash
# What does the provider do with what Core tells it BEFORE any gRPC happens? Core passes instructions in environment
# variables (cookie, protocol versions, port range) and then reads one handshake line from the provider's stdout.
# Core only ever sends good values, so the refusals can only be tested by starting the provider by hand, here.
# No OpenTofu needed.
. "$(dirname "$0")/lib.sh"

# run <VAR=value ...>   Start the provider with ONLY these environment variables and give it 1 second.
# Afterwards: STATUS = its exit status, where 124 means "still running after 1 s" (so it started), and
#             LINE   = what it printed on stdout (the handshake line, if any).
run() { LINE=$(timeout 1 env -i PATH="$PATH" "$@" "$PROVIDER" $ARGS 2>/dev/null); STATUS=$?; }
RUNNING=124
port_of() { echo "$LINE" | cut -d'|' -f4 | cut -d: -f2; }      # the handshake line is  1|version|tcp|127.0.0.1:PORT|grpc|
version_of() { echo "$LINE" | cut -d'|' -f2; }
GOOD="TF_PLUGIN_MAGIC_COOKIE=$COOKIE"                         # what Core always sends
refused() { echo "$STATUS|$LINE"; }                           # a refusal is: exit status 1 and no handshake line

echo "== magic cookie"
run PLUGIN_PROTOCOL_VERSIONS=5;                         expect "no cookie: refused"      "$(refused)" "1|"
run TF_PLUGIN_MAGIC_COOKIE=nope PLUGIN_PROTOCOL_VERSIONS=5; expect "wrong cookie: refused" "$(refused)" "1|"
run $GOOD PLUGIN_PROTOCOL_VERSIONS=5;                   expect "right cookie: starts"    "$STATUS" $RUNNING
expect "handshake line looks like 1|5|tcp|127.0.0.1:PORT|grpc|" "$(echo "$LINE" | sed 's/:[0-9]*|grpc|$/:PORT|grpc|/')" "1|5|tcp|127.0.0.1:PORT|grpc|"
ARGS=--debug run PLUGIN_PROTOCOL_VERSIONS=5;            expect "--debug, no cookie: starts" "$STATUS" $RUNNING

echo "== protocol version (we serve only 5)"
for offer in 5,6 6,5 abc,5; do
  run $GOOD PLUGIN_PROTOCOL_VERSIONS=$offer;            expect "Core offers $offer: we answer 5" "$(version_of)" 5
done
run $GOOD PLUGIN_PROTOCOL_VERSIONS=6;                   expect "Core offers only 6: refused"   "$(refused)" "1|"
run $GOOD PLUGIN_PROTOCOL_VERSIONS=4;                   expect "Core offers only 4: refused"   "$(refused)" "1|"
run $GOOD PLUGIN_PROTOCOL_VERSIONS=abc;                 expect "Core offers junk: refused"     "$(refused)" "1|"
run $GOOD;                                              expect "Core offers nothing: refused"  "$(refused)" "1|"

echo "== port range (PLUGIN_MIN_PORT .. PLUGIN_MAX_PORT)"
OK="$GOOD PLUGIN_PROTOCOL_VERSIONS=5"
run $OK PLUGIN_MIN_PORT=34567 PLUGIN_MAX_PORT=34567;    expect "range of one port: uses it"    "$(port_of)" 34567
run $OK PLUGIN_MIN_PORT=34600 PLUGIN_MAX_PORT=34500;    expect "MIN greater than MAX: refused" "$(refused)" "1|"
run $OK PLUGIN_MIN_PORT=abc   PLUGIN_MAX_PORT=34500;    expect "MIN not a number: refused"     "$(refused)" "1|"
run $OK;                                                expect "no range: starts anywhere"     "$STATUS" $RUNNING
ARGS=--port=34580 run $OK;                              expect "--port=34580: uses it"         "$(port_of)" 34580

# To test "port taken" we need a taken port. (Not another copy of the provider: gRPC lets servers share a port.)
# This one-liner listens on 34570 for 10 seconds.
python3 -c 'import socket,time; s=socket.socket(); s.bind(("127.0.0.1",34570)); s.listen(); time.sleep(10)' &
sleep 0.5
run $OK PLUGIN_MIN_PORT=34570 PLUGIN_MAX_PORT=34571;    expect "34570 taken, range 34570-34571: uses 34571" "$(port_of)" 34571
run $OK PLUGIN_MIN_PORT=34570 PLUGIN_MAX_PORT=34570;    expect "34570 taken, range 34570 only: refused"     "$(refused)" "1|"
summary
