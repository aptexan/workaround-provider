#!/bin/bash
# Mutual TLS, the part Core does by default. Core gives the provider ITS certificate; the provider makes its own, puts it
# in the handshake line, and from then on answers only clients that present Core's certificate. Core in turn trusts only
# the certificate from the handshake line. These checks play Core's side with curl and openssl (needs both).
# Core's certificate is made like go-plugin makes it: P-521, self-signed, CA, name "localhost".
# The real Core is tested by TLS=1 check-tofu.sh. Details: tools/TESTING.md.
TLS=1
. "$(dirname "$0")/lib.sh"
command -v openssl >/dev/null || { echo "this check needs the openssl command"; exit 2; }
start_provider
PORT=${ADDR##*:}
FIELD6=$(cut -d'|' -f6 "$WORK/handshake")

# Calls Health/Check with the given extra curl options. Prints the grpc-status (0 = it worked), or "no reply" when the
# connection itself failed.
ASK='\x00\x00\x00\x00\x08\x0a\x06plugin'            # HealthCheckRequest{service="plugin"}, see check-health.sh
try_curl() {
  local status
  status=$(printf "$ASK" | curl -s --http2 -D - -o "$WORK/reply" -H 'content-type: application/grpc' -H 'te: trailers' \
           --data-binary @- --resolve "localhost:$PORT:127.0.0.1" "$@" 2>/dev/null | tr -d '\r' | grep -i '^grpc-status:' | cut -d' ' -f2)
  echo "${status:-no reply}"
}
URL=https://localhost:$PORT/grpc.health.v1.Health/Check
make_cert stranger                                   # a certificate that is NOT Core's

echo "== who may talk to the provider"
expect "Core's certificate: the call works"                  "$(try_curl --cacert "$WORK/server.pem" --cert "$WORK/client.pem" --key "$WORK/client.key" "$URL")" 0
if negatives; then
  expect "no client certificate: refused"                      "$(try_curl --cacert "$WORK/server.pem" "$URL")" "no reply"
  expect "someone else's client certificate: refused"          "$(try_curl --cacert "$WORK/server.pem" --cert "$WORK/stranger.pem" --key "$WORK/stranger.key" "$URL")" "no reply"
  expect "plaintext to the TLS port: refused"                  "$(try_curl --http2-prior-knowledge "http://127.0.0.1:$PORT/grpc.health.v1.Health/Check")" "no reply"
fi

echo "== what the client (Core) checks about the provider"
if negatives; then
  expect "provider's certificate is the one in the handshake line (a different trusted certificate is refused)" \
         "$(try_curl --cacert "$WORK/stranger.pem" --cert "$WORK/client.pem" --key "$WORK/client.key" "$URL")" "no reply"
  expect "the name must be localhost (127.0.0.1 is refused)"   "$(try_curl --cacert "$WORK/server.pem" --cert "$WORK/client.pem" --key "$WORK/client.key" "https://127.0.0.1:$PORT/grpc.health.v1.Health/Check")" "no reply"
fi

echo "== TLS versions (minimum 1.2)"
expect "TLS 1.2 works"                                       "$(try_curl --tls-max 1.2 --cacert "$WORK/server.pem" --cert "$WORK/client.pem" --key "$WORK/client.key" "$URL")" 0
if negatives; then
  expect "TLS 1.1 is refused"                                  "$(try_curl --tlsv1.0 --tls-max 1.1 --cacert "$WORK/server.pem" --cert "$WORK/client.pem" --key "$WORK/client.key" "$URL")" "no reply"
fi

echo "== the certificate in the handshake line, compared with go-plugin's"
TEXT=$(openssl x509 -in "$WORK/server.pem" -noout -text 2>&1)
has() { echo "$TEXT" | grep -q -- "$1" && echo yes || echo no; }       # does the certificate text contain this?
expect "key is ECDSA P-521"                                  "$(has 'ASN1 OID: secp521r1')" yes
expect "signed with ECDSA and SHA-512"                       "$(has 'Signature Algorithm: ecdsa-with-SHA512')" yes
expect "subject name is localhost"                           "$(has 'Subject: .*CN *= *localhost')" yes
expect "alternative name is DNS:localhost"                   "$(has 'DNS:localhost')" yes
expect "CA flag is set"                                      "$(has 'CA:TRUE')" yes
expect "key usage has Certificate Sign"                      "$(has 'Certificate Sign')" yes
expect "key usage has Digital Signature"                     "$(has 'Digital Signature')" yes
expect "key usage has Key Encipherment"                      "$(has 'Key Encipherment')" yes
expect "key usage has Key Agreement"                         "$(has 'Key Agreement')" yes
expect "extended key usage has server authentication"        "$(has 'TLS Web Server Authentication')" yes
expect "extended key usage has client authentication"        "$(has 'TLS Web Client Authentication')" yes
expect "valid now"                                           "$(openssl x509 -in "$WORK/server.pem" -noout -checkend 0 >/dev/null && echo yes || echo no)" yes
expect "valid for at least 25 more years"                    "$(openssl x509 -in "$WORK/server.pem" -noout -checkend $((25*365*24*3600)) >/dev/null && echo yes || echo no)" yes

echo "== fresh key every time, and bad input"
# Base64 pads with '=' only when the certificate's length is not a multiple of 3 bytes, and that length varies from key
# to key. So ONE certificate can look unpadded by luck even if the provider forgot to strip the '='. Look at 10: a
# provider that forgot would show a '=' in about two thirds of them, and misses all 10 less than once in 50000 times.
padded=0
for _ in $(seq 10); do
  start_provider; kill $PID; wait $PID 2>/dev/null
  [[ $(cut -d'|' -f6 "$WORK/handshake") == *=* ]] && padded=$((padded+1))
done
expect "field 6 never has '=' padding (looked at 10 certificates)" "$padded" 0
start_provider
expect "every start makes a new certificate"                 "$([ "$(cut -d'|' -f6 "$WORK/handshake")" != "$FIELD6" ] && echo yes || echo no)" yes
if negatives; then
  OUT=$(timeout 1 env -i PATH="$PATH" TF_PLUGIN_MAGIC_COOKIE=$COOKIE PLUGIN_PROTOCOL_VERSIONS=6 PLUGIN_CLIENT_CERT=not-a-certificate "$PROVIDER" 2>"$WORK/err")
  expect "a client certificate that is not PEM: refused (exit 1, no handshake)" "$?|$OUT" "1|"
  # The provider would refuse anyway (gRPC cannot use such a certificate), but only our own check explains why.
  expect "  ... and the reason names PLUGIN_CLIENT_CERT" "$(grep -q PLUGIN_CLIENT_CERT "$WORK/err" && echo yes || echo no)" yes
fi
summary
