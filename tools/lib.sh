# Shared by the check-*.sh scripts (sourced, not run). Every check is one line:
#     expect "what we check" "what we got" "what we want"        -> prints PASS or FAIL
# A script ends with `summary`; its exit status is 0 only if every check passed.

PROVIDER=${PROVIDER:-$HOME/tpc/build/terraform-provider-workaround}
COOKIE=d602bf8f470bc67ca7faa0386276bbdd4330efaf76d1a219cb4d6991ca9872b2    # go-plugin's fixed "magic cookie"
PASSED=0; FAILED=0
WORK=$(mktemp -d)
trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$WORK"' EXIT                    # stop anything we started in the background

[ -x "$PROVIDER" ] || { echo "Provider not found: $PROVIDER (build it, or set PROVIDER=...)"; exit 2; }

expect() {
  if [ "$2" = "$3" ]; then PASSED=$((PASSED+1)); echo "PASS  $1"
  else FAILED=$((FAILED+1)); echo "FAIL  $1"; echo "        wanted: $3"; echo "        got:    $2"; fi
}
summary() { echo; echo "passed $PASSED, failed $FAILED"; [ "$FAILED" = 0 ]; }

# Start the provider in the background as Core would (but --debug: no cookie needed).
# Afterwards: PID = its process id, ADDR = the host:port it printed in its handshake line.
start_provider() {
  "$PROVIDER" --debug >"$WORK/handshake" 2>/dev/null & PID=$!
  for _ in $(seq 100); do [ -s "$WORK/handshake" ] && break; sleep 0.05; done     # wait up to 5 s for the line
  ADDR=$(cut -d'|' -f4 "$WORK/handshake")
}

# A gRPC call is an HTTP/2 POST. Its body is: 0x00, the message length as 4 bytes, then the protobuf message.
# The request is read from stdin; this prints the grpc-status of the reply (0 = OK); the reply body is kept in $WORK/reply.
grpc_call() {  # grpc_call <path> [extra curl options]
  local path=$1; shift
  curl -s --http2-prior-knowledge -D - -o "$WORK/reply" -H 'content-type: application/grpc' -H 'te: trailers' \
       --data-binary @- "$@" "http://$ADDR$path" | tr -d '\r' | grep -i '^grpc-status:' | cut -d' ' -f2
}
reply_hex() { od -An -tx1 "$WORK/reply" | tr -s ' \n' ' ' | sed 's/^ //; s/ $//'; }   # the reply body as hex bytes
