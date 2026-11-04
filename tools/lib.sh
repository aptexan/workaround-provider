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

# TLS=1 runs the provider, and talks to it, with mutual TLS: the way Core does by default. Otherwise plaintext.
TLS=${TLS:-0}
MODE=$([ "$TLS" = 1 ] && echo "mutual TLS" || echo "plaintext")

# make_cert <name>: a certificate and private key in $WORK/<name>.pem and <name>.key, like the one Core makes for a
# provider (go-plugin's generateCert): self-signed, P-521, CA, name "localhost". Needs the openssl command.
make_cert() {
  openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:secp521r1 -nodes -days 30 \
    -keyout "$WORK/$1.key" -out "$WORK/$1.pem" -subj "/CN=localhost/O=HashiCorp" \
    -addext "subjectAltName=DNS:localhost" -addext "basicConstraints=critical,CA:TRUE" \
    -addext "keyUsage=digitalSignature,keyEncipherment,keyAgreement,keyCertSign" \
    -addext "extendedKeyUsage=clientAuth,serverAuth" 2>/dev/null
}

# handshake_cert_to_pem <handshake line> <file>: field 6 of the handshake line is the provider's certificate as
# base64 DER without '=' padding. Core trusts exactly this certificate. Here it is turned back into a PEM file.
handshake_cert_to_pem() {
  local field; field=$(echo "$1" | cut -d'|' -f6)
  case $(( ${#field} % 4 )) in 2) field="$field==";; 3) field="$field=";; esac
  { echo '-----BEGIN CERTIFICATE-----'; echo "$field" | fold -w64; echo '-----END CERTIFICATE-----'; } > "$2"
}

# Start the provider in the background as Core would. Afterwards: PID = its process id, ADDR = host:port from its
# handshake line, CURL_ARGS = how to reach it. Plaintext: started with --debug (no cookie needed). TLS: started with
# the real cookie and with a client certificate in PLUGIN_CLIENT_CERT, and curl is given that certificate and key and
# told to trust only the certificate from the handshake line.
start_provider() {
  if [ "$TLS" = 1 ]; then
    command -v openssl >/dev/null || { echo "TLS=1 needs the openssl command"; exit 2; }
    make_cert client
    TF_PLUGIN_MAGIC_COOKIE=$COOKIE PLUGIN_PROTOCOL_VERSIONS=5 PLUGIN_CLIENT_CERT="$(cat "$WORK/client.pem")" \
      "$PROVIDER" >"$WORK/handshake" 2>/dev/null & PID=$!
  else
    "$PROVIDER" --debug >"$WORK/handshake" 2>/dev/null & PID=$!
  fi
  for _ in $(seq 100); do [ -s "$WORK/handshake" ] && break; sleep 0.05; done     # wait up to 5 s for the line
  ADDR=$(cut -d'|' -f4 "$WORK/handshake")
  if [ "$TLS" = 1 ]; then
    handshake_cert_to_pem "$(cat "$WORK/handshake")" "$WORK/server.pem"
    # Core connects to the name "localhost" (that is what the certificate is for), not to the number.
    CURL_ARGS=(--http2 --cacert "$WORK/server.pem" --cert "$WORK/client.pem" --key "$WORK/client.key"
               --resolve "localhost:${ADDR##*:}:127.0.0.1")
    BASE_URL=https://localhost:${ADDR##*:}
  else
    CURL_ARGS=(--http2-prior-knowledge)
    BASE_URL=http://$ADDR
  fi
}

# A gRPC call is an HTTP/2 POST. Its body is: 0x00, the message length as 4 bytes, then the protobuf message.
# The request is read from stdin; this prints the grpc-status of the reply (0 = OK); the reply body is kept in $WORK/reply.
grpc_call() {  # grpc_call <path> [extra curl options]
  local path=$1; shift
  curl -s "${CURL_ARGS[@]}" -D - -o "$WORK/reply" -H 'content-type: application/grpc' -H 'te: trailers' \
       --data-binary @- "$@" "$BASE_URL$path" | tr -d '\r' | grep -i '^grpc-status:' | cut -d' ' -f2
}
reply_hex() { od -An -tx1 "$WORK/reply" | tr -s ' \n' ' ' | sed 's/^ //; s/ $//'; }   # the reply body as hex bytes
