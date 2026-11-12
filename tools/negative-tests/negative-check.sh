#!/bin/bash
# Negative tests: do the normal checks (tools/check-*.sh) notice when the provider is broken?
#
# A check that cannot fail proves nothing. This script breaks a COPY of the provider on purpose, one feature at a time,
# builds the broken copy, and runs the check that should notice. That check must FAIL ("go red").
# It never touches your source or your build/ folder: everything happens in a temporary copy, deleted at the end.
# Even OpenTofu's working folder is a private copy, and the script PROVES the claim: it fingerprints your whole project
# folder (every file's content, size and modification time, except .git) before and after, and fails if anything differs.
# (So do not edit or rebuild the project while it runs; that would be reported as a difference.)
#
# Slow (one full build, then one rebuild per break: a few minutes) and separate from run-all.sh on purpose.
# Needs: cmake, make, python3, and the same libraries as BUILDING.md (CMAKE_PREFIX_PATH, default ~/.local/grpc-openssl).
# The check-tofu.sh and check-data.sh cases are skipped if tofu is missing. See negative-tests/README.md.
set -u
[ $# -eq 0 ] || { echo "no options (this IS the negative test; ONLY='regex' picks which breaks to run)"; exit 2; }
REPO=$(cd "$(dirname "$0")/../.." && pwd)
TOOLS=$REPO/tools
COPY=$(mktemp -d)
mkdir "$COPY/tmp"; export TMPDIR=$COPY/tmp      # every temporary file and log from the checks lands inside the copy

# A fingerprint of the real project: file contents, sizes and modification times. .git is left out because plain
# `git status` rewrites its index.
fingerprint() {
  ( cd "$REPO" && find . -path ./.git -prune -o -type f -printf '%p %s %T@\n' | sort | cksum
    cd "$REPO" && find . -path ./.git -prune -o -type f -exec cksum {} + | sort | cksum )
}
BEFORE=$(fingerprint)
# The copy must use the same gRPC as your real build: read it from there (read-only), unless CMAKE_PREFIX_PATH is set.
PREFIX=${CMAKE_PREFIX_PATH:-$(sed -n 's/^CMAKE_PREFIX_PATH:[A-Z]*=//p' "$REPO/build/CMakeCache.txt" 2>/dev/null)}
PREFIX=${PREFIX:-$HOME/.local/grpc-openssl}
command -v openssl >/dev/null || { echo "this script needs the openssl command (check-tls.sh does)"; exit 2; }

echo "== copy the project to $COPY and build it (first build is the slow one)"
tar -C "$REPO" --exclude=./build --exclude=./.git --exclude=./session_logs -cf - . | tar -C "$COPY" -xf -
cp -r "$COPY/src" "$COPY/pristine-src"                            # the unmodified sources, to restore from
cp "$COPY/CMakeLists.txt" "$COPY/pristine-CMakeLists.txt"
cmake -S "$COPY" -B "$COPY/build" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="$PREFIX" >"$COPY/cmake.log" 2>&1 \
  && make -C "$COPY/build" -j4 >"$COPY/make.log" 2>&1 \
  || { echo "The copy did not build. See $COPY/cmake.log and make.log"; exit 2; }

export PROVIDER=$COPY/build/terraform-provider-workaround       # every check script now tests the COPY
export SKIP_NEGATIVE=0   # the checks under test must run their negative cases, whatever the caller's environment says
. "$TOOLS/lib.sh"
trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$WORK" "$COPY"' EXIT   # lib.sh set a trap too; this one also removes the copy

# check-tofu.sh finds the provider through ~/.tofurc. Give it a private config that points at the COPY instead.
printf 'provider_installation {\n  dev_overrides { "aptexan/workaround" = "%s/build" }\n  direct {}\n}\n' "$COPY" >"$COPY/tofurc"
export TF_CLI_CONFIG_FILE=$COPY/tofurc
# The OpenTofu checks run tools/tofu-test in a private copy (lib.sh does that), so nothing of yours is touched. A TOFU_DIR
# of your own would be used instead, so make sure there is none.
unset TOFU_DIR
have_tofu() { command -v tofu >/dev/null; }

# Runs a check quietly; says "green" if it passed, "red" if it failed. The check is "[VAR=value ...] script.sh".
colour_of() {
  local script=${1##* } vars=${1% *}; [ "$vars" = "$1" ] && vars=
  env $vars "$TOOLS/$script" >/dev/null 2>&1 && echo green || echo red
}

echo "== first, with NO break, every check must be green (otherwise 'red' below would mean nothing)"
CHECK_BUILD="BUILD_DIR=$COPY/build SOURCE_DIR=$COPY check-build.sh"     # the build checks look at the COPY
for check in "$CHECK_BUILD" "check-unit.sh" "check-startup.sh" "TLS=0 check-health.sh" "TLS=0 check-streams.sh" "check-tls.sh" "TLS=1 check-health.sh" "TLS=1 check-streams.sh"; do
  expect "unbroken copy: $check is green" "$(colour_of "$check")" green
done
if have_tofu; then
  for check in "TLS=0 check-tofu.sh" "TLS=1 check-tofu.sh" "TLS=0 check-data.sh" "TLS=1 check-data.sh"; do
    expect "unbroken copy: $check is green" "$(colour_of "$check")" green
  done
fi

# broken_provider <what we break> <check that must notice> <file, relative to the project> <exact text in it> <replacement>
broken_provider() {
  local what=$1 check=$2 file=$3 old=$4 new=$5
  # ONLY='regex' runs just the breaks whose description matches (the unbroken baseline above still runs). For quick
  # re-checks while working on this script; a full run, without ONLY, is the proof.
  if [ -n "${ONLY:-}" ] && ! [[ $what =~ $ONLY ]]; then return; fi
  if [[ $check == *check-tofu.sh || $check == *check-data.sh ]] && ! have_tofu; then echo "SKIP  $what ($check needs tofu)"; return; fi
  cp "$COPY/pristine-src/"* "$COPY/src/"                          # always start from the unmodified sources
  cp "$COPY/pristine-CMakeLists.txt" "$COPY/CMakeLists.txt"
  # The old text must occur exactly once. If someone edits the provider so it does not, this fails loudly.
  python3 -c 'import sys; f,old,new=sys.argv[1:]; s=open(f).read(); assert s.count(old)==1; open(f,"w").write(s.replace(old,new))' \
    "$COPY/$file" "$old" "$new" 2>/dev/null || { expect "$what: break applied to the source" "no" yes; return; }
  # The broken copy must still build; otherwise "red" could just mean "no binary".
  expect "$what: broken copy builds" "$(make -C "$COPY/build" >/dev/null 2>&1 && echo yes || echo no)" yes
  expect "$what: $check goes red" "$(colour_of "$check")" red
}

echo "== now break the provider, one feature at a time: each check below MUST go red"
broken_provider "health name 'plugin' not registered"   check-health.sh  src/tofu-plugin.cpp 'health->SetServingStatus("plugin", true);' 'health->SetServingStatus("other", true);'
broken_provider "streams not cancelled on shutdown"     check-streams.sh src/tofu-plugin.cpp 'streams.CancelAll();'                      '/* broken: streams not cancelled */'
broken_provider "stdio service not registered"          check-streams.sh src/tofu-plugin.cpp 'builder.RegisterService(&stdio_service);'  '/* broken */'
broken_provider "Shutdown service not registered"       check-streams.sh src/tofu-plugin.cpp 'builder.RegisterService(&controller_service);' '/* broken */'
broken_provider "Shutdown service not registered"       check-tofu.sh    src/tofu-plugin.cpp 'builder.RegisterService(&controller_service);' '/* broken */'
broken_provider "cookie check disabled"                 check-startup.sh src/tofu-plugin.cpp 'if (!debug && (cookie == nullptr'         'if (false && (cookie == nullptr'
broken_provider "protocol version always 6"             check-startup.sh src/tofu-plugin.cpp 'if (config.protocol_version == 0) {'       'config.protocol_version = 6; if (config.protocol_version == 0) {'
broken_provider "port range ignored"                    check-startup.sh src/tofu-plugin.cpp 'if (port != 0 || config.max_port == 0) {'  'if (true) {'
broken_provider "handshake line missing its last '|'"   check-startup.sh src/tofu-plugin.cpp '"1|%d|tcp|127.0.0.1:%d|grpc|%s\n"'       '"1|%d|tcp|127.0.0.1:%d|grpc%s\n"'
broken_provider "log line printed on stdout"            check-startup.sh src/tofu-plugin.cpp 'std::cerr << "Server listening'            'std::cout << "Server listening'

echo "== break the TLS rules: check-tls.sh (and the real Core) must notice"
SERVER_CREDENTIALS='credentials = grpc::SslServerCredentials(options);'
broken_provider "client certificate not requested"           check-tls.sh          src/tofu-plugin.cpp 'GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY' 'GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE'
broken_provider "client certificate not verified"            check-tls.sh          src/tofu-plugin.cpp 'GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY' 'GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_BUT_DONT_VERIFY'
broken_provider "plaintext although Core sent a certificate" check-tls.sh          src/tofu-plugin.cpp "$SERVER_CREDENTIALS" 'credentials = grpc::InsecureServerCredentials();'
broken_provider "plaintext although Core sent a certificate" "TLS=1 check-tofu.sh" src/tofu-plugin.cpp "$SERVER_CREDENTIALS" 'credentials = grpc::InsecureServerCredentials();'
broken_provider "handshake line without the certificate"    check-tls.sh          src/tofu-plugin.cpp 'handshake_certificate = certificate->handshake_field;' 'handshake_certificate = "";'
broken_provider "handshake line without the certificate"    "TLS=1 check-tofu.sh" src/tofu-plugin.cpp 'handshake_certificate = certificate->handshake_field;' 'handshake_certificate = "";'
broken_provider "a client certificate that is not PEM is accepted" check-tls.sh    src/tofu-plugin.cpp 'if (!IsPemCertificate(config.client_cert_pem)) {' 'if (false) {'
broken_provider "certificate: key is P-256, not P-521"       check-tls.sh          src/server-cert.cpp 'EVP_EC_gen("P-521")' 'EVP_EC_gen("P-256")'
broken_provider "certificate: CA flag off"                   check-tls.sh          src/server-cert.cpp 'NID_basic_constraints, "critical,CA:TRUE"' 'NID_basic_constraints, "critical,CA:FALSE"'
broken_provider "certificate: wrong name"                    check-tls.sh          src/server-cert.cpp '"DNS:localhost"' '"DNS:example.invalid"'
broken_provider "certificate: no client-auth usage"          check-tls.sh          src/server-cert.cpp '"serverAuth,clientAuth"' '"serverAuth"'
broken_provider "certificate: no Certificate Sign usage"     check-tls.sh          src/server-cert.cpp '"digitalSignature,keyEncipherment,keyAgreement,keyCertSign"' '"digitalSignature,keyEncipherment,keyAgreement"'
broken_provider "certificate: '=' padding left in field 6"   check-tls.sh          src/server-cert.cpp "while (!result.handshake_field.empty() && result.handshake_field.back() == '=') {" "while (false) {"

echo "== break the data layer: the unit tests, and the real OpenTofu in check-data.sh, must notice"
DATA="TLS=0 check-data.sh"
EX=src/workaround-example.cpp
broken_provider "enabled gets no default"                    check-unit.sh  $EX 'if (planned["enabled"].IsNull()) planned["enabled"] = Value::Bool(true);' 'if (false) planned["enabled"] = Value::Bool(true);'
broken_provider "enabled gets no default"                    "$DATA"        $EX 'if (planned["enabled"].IsNull()) planned["enabled"] = Value::Bool(true);' 'if (false) planned["enabled"] = Value::Bool(true);'
broken_provider "a new name does not replace the resource"   check-unit.sh  $EX 'result.requires_replace.push_back(AttributeAt("name"));' '(void)0;'
broken_provider "a new name does not replace the resource"   "$DATA"        $EX 'result.requires_replace.push_back(AttributeAt("name"));' '(void)0;'
broken_provider "the summary is worded differently"          check-unit.sh  $EX '", enabled=" +' '", on=" +'
broken_provider "the summary is worded differently"          "$DATA"        $EX '", enabled=" +' '", on=" +'
broken_provider "size is not validated"                      check-unit.sh  $EX 'bool valid = size.IsInteger() && size.AsInt64() >= 1 && size.AsInt64() <= 100;' 'bool valid = true;'
broken_provider "size is not validated"                      "$DATA"        $EX 'bool valid = size.IsInteger() && size.AsInt64() >= 1 && size.AsInt64() <= 100;' 'bool valid = true;'
broken_provider "the id is not what the settings say"        check-unit.sh  $EX 'Value::String(settings.id_prefix + "-" + id_suffix)' 'Value::String("fixed")'
broken_provider "the id is not what the settings say"        "$DATA"        $EX 'Value::String(settings.id_prefix + "-" + id_suffix)' 'Value::String("fixed")'
broken_provider "an unknown is written as null"               check-unit.sh  src/cty.cpp '*out = Json::binary(std::vector<std::uint8_t>{0x00}, 0);  // msgpack fixext1, type 0: d4 00 00' '*out = nullptr;'
broken_provider "an unknown is written as null"               "$DATA"        src/cty.cpp '*out = Json::binary(std::vector<std::uint8_t>{0x00}, 0);  // msgpack fixext1, type 0: d4 00 00' '*out = nullptr;'
broken_provider "a set keeps its duplicates"                 check-unit.sh  src/cty.cpp 'return a.IsKnown() && b.IsKnown() && Compare(a, b) == 0;' 'return false;'
# (No check-data.sh case for duplicates in a set: OpenTofu removes them itself when it turns a list into a set, so a real
# Core never sends any. Only the unit test, which does, can see this break.)
broken_provider "an object may lack attributes"              check-unit.sh  src/cty.cpp 'if (!missing_attributes_are_null && tree.size() != type.attributes().size()) {' 'if (false) {'
broken_provider "the sensitive flag is not sent"             "$DATA"        src/tofu-plugin.cpp 'proto_attribute->set_sensitive(attribute.sensitive);' '(void)0;'
broken_provider "requires_replace is not sent"               "$DATA"        src/tofu-plugin.cpp 'SetPath(path, response->add_requires_replace());' '(void)path;'
broken_provider "a diagnostic loses its attribute path"      "$DATA"        src/tofu-plugin.cpp 'if (!diagnostic.attribute.empty()) SetPath(diagnostic.attribute, proto->mutable_attribute());' '(void)0;'
broken_provider "a warning is sent as an error"              "$DATA"        src/tofu-plugin.cpp ': tfplugin6::Diagnostic::WARNING);' ': tfplugin6::Diagnostic::ERROR);'
broken_provider "stored state is thrown away on upgrade"     "$DATA"        src/tofu-plugin.cpp 'Encode(*state, *type, "the upgraded state"' 'Encode(cty::Value::Null(), *type, "the upgraded state"'
broken_provider "the schema loses a required flag"           "$DATA"        src/tofu-plugin.cpp 'proto_attribute->set_required(attribute.required);' '(void)0;'

echo "== break the build protections in CMakeLists.txt: check-build.sh must notice"
# These two need something to be wrong with: a Windows PATH, and a second gRPC install. SKIP when the machine has neither.
if [[ $PATH == */mnt/* ]]; then
  broken_provider "Windows PATH entries are not removed"   "$CHECK_BUILD" CMakeLists.txt 'set(ENV{PATH} "${_clean_path}")' '# broken'
else echo "SKIP  Windows PATH entries are not removed (this machine's PATH has no /mnt entries)"; fi
broken_provider "no standard-prefix fallback (VS Code's configure loses the prefix)" "$CHECK_BUILD" CMakeLists.txt 'if(NOT CMAKE_PREFIX_PATH AND EXISTS "$ENV{HOME}/.local/grpc-openssl")' 'if(FALSE)'
if ls -d "$HOME"/.local/grpc*/lib/cmake/protobuf >/dev/null 2>&1 && [ "$(ls -d "$HOME"/.local/grpc*/lib/cmake/protobuf | wc -l)" -gt 1 ]; then
  broken_provider "packages from two installations are accepted" "$CHECK_BUILD" CMakeLists.txt 'if(NOT ALLOW_MIXED_DEPENDENCY_PREFIXES)' 'if(FALSE)'
else echo "SKIP  packages from two installations are accepted (no second install with Protobuf)"; fi

echo "== finally: the real project must be exactly as it was"
expect "your source and build/ are untouched (content, size, modification times)" "$(fingerprint)" "$BEFORE"
summary
