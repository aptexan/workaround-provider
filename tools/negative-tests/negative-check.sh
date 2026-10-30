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
# Needs: cmake, make, python3, and the same libraries as BUILDING.md (CMAKE_PREFIX_PATH, default ~/.local/grpc).
# The check-tofu.sh cases are skipped if tofu or TOFU_DIR/tofu.tf is missing. See negative-tests/README.md.
set -u
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
PREFIX=${CMAKE_PREFIX_PATH:-$HOME/.local/grpc}
SRC=$COPY/src/tofu-plugin.cpp

echo "== copy the project to $COPY and build it (first build is the slow one)"
tar -C "$REPO" --exclude=./build --exclude=./.git --exclude=./session_logs -cf - . | tar -C "$COPY" -xf -
cp "$SRC" "$COPY/pristine.cpp"                                   # the unmodified source, to restore from
cmake -S "$COPY" -B "$COPY/build" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="$PREFIX" >"$COPY/cmake.log" 2>&1 \
  && make -C "$COPY/build" -j4 >"$COPY/make.log" 2>&1 \
  || { echo "The copy did not build. See $COPY/cmake.log and make.log"; exit 2; }

export PROVIDER=$COPY/build/terraform-provider-workaround       # every check script now tests the COPY
. "$TOOLS/lib.sh"
trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$WORK" "$COPY"' EXIT   # lib.sh set a trap too; this one also removes the copy

# check-tofu.sh finds the provider through ~/.tofurc. Give it a private config that points at the COPY instead.
printf 'provider_installation {\n  dev_overrides { "aptexan/workaround" = "%s/build" }\n  direct {}\n}\n' "$COPY" >"$COPY/tofurc"
export TF_CLI_CONFIG_FILE=$COPY/tofurc
# check-tofu.sh deletes terraform.tfstate* in its TOFU_DIR, so give it a private copy of your .tf files, not the real folder.
REAL_TOFU_DIR=${TOFU_DIR:-$HOME/wpe}
have_tofu() { command -v tofu >/dev/null && [ -f "$REAL_TOFU_DIR/tofu.tf" ]; }
if have_tofu; then mkdir "$COPY/tofu-dir"; cp "$REAL_TOFU_DIR"/*.tf "$COPY/tofu-dir/"; export TOFU_DIR=$COPY/tofu-dir; fi

# Runs a check script quietly; says "green" if it passed, "red" if it failed.
colour_of() { "$TOOLS/$1" >/dev/null 2>&1 && echo green || echo red; }

echo "== first, with NO break, every check must be green (otherwise 'red' below would mean nothing)"
for script in check-startup.sh check-health.sh check-streams.sh; do
  expect "unbroken copy: $script is green" "$(colour_of $script)" green
done
have_tofu && expect "unbroken copy: check-tofu.sh is green" "$(colour_of check-tofu.sh)" green

# broken_provider <what we break> <check that must notice> <exact text in tofu-plugin.cpp> <what to replace it with>
broken_provider() {
  local what=$1 script=$2 old=$3 new=$4
  if [ "$script" = check-tofu.sh ] && ! have_tofu; then echo "SKIP  $what ($script needs tofu and a tofu.tf)"; return; fi
  cp "$COPY/pristine.cpp" "$SRC"                                  # always start from the unmodified source
  # The old text must occur exactly once. If someone edits the provider so it does not, this fails loudly.
  python3 -c 'import sys; f,old,new=sys.argv[1:]; s=open(f).read(); assert s.count(old)==1; open(f,"w").write(s.replace(old,new))' \
    "$SRC" "$old" "$new" 2>/dev/null || { expect "$what: break applied to the source" "no" yes; return; }
  # The broken copy must still build; otherwise "red" could just mean "no binary".
  expect "$what: broken copy builds" "$(make -C "$COPY/build" >/dev/null 2>&1 && echo yes || echo no)" yes
  expect "$what: $script goes red" "$(colour_of $script)" red
}

echo "== now break the provider, one feature at a time: each check below MUST go red"
broken_provider "health name 'plugin' not registered"   check-health.sh  'health->SetServingStatus("plugin", true);' 'health->SetServingStatus("other", true);'
broken_provider "streams not cancelled on shutdown"     check-streams.sh 'streams.CancelAll();'                      '/* broken: streams not cancelled */'
broken_provider "stdio service not registered"          check-streams.sh 'builder.RegisterService(&stdio_service);'  '/* broken */'
broken_provider "Shutdown service not registered"       check-streams.sh 'builder.RegisterService(&controller_service);' '/* broken */'
broken_provider "Shutdown service not registered"       check-tofu.sh    'builder.RegisterService(&controller_service);' '/* broken */'
broken_provider "cookie check disabled"                 check-startup.sh 'if (!debug && (cookie == nullptr'         'if (false && (cookie == nullptr'
broken_provider "protocol version always 5"             check-startup.sh 'if (config.protocol_version == 0) {'       'config.protocol_version = 5; if (config.protocol_version == 0) {'
broken_provider "port range ignored"                    check-startup.sh 'if (port != 0 || config.max_port == 0) {'  'if (true) {'
broken_provider "handshake line missing its last '|'"   check-startup.sh '"1|%d|tcp|127.0.0.1:%d|grpc|\n"'          '"1|%d|tcp|127.0.0.1:%d|grpc\n"'
broken_provider "log line printed on stdout"            check-startup.sh 'std::cerr << "Server listening'            'std::cout << "Server listening'

echo "== finally: the real project must be exactly as it was"
expect "your source and build/ are untouched (content, size, modification times)" "$(fingerprint)" "$BEFORE"
summary
