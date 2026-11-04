#!/bin/bash
# Runs every check, in both modes where that makes sense: plaintext (Core told to skip encryption) and mutual TLS
# (how Core runs by default). Exit status 0 only if all passed. check-tofu.sh is skipped (and says so) when tofu is not
# installed or TOFU_DIR has no tofu.tf. check-tls.sh and the TLS runs need the openssl command. See TESTING.md.
cd "$(dirname "$0")" || exit 2
failed=0
run() {  # run <label> <script> [VAR=value ...]
  local label=$1 script=$2; shift 2
  echo; echo "=== $label"
  if [ $script = check-tofu.sh ] && { ! command -v tofu >/dev/null || [ ! -f "${TOFU_DIR:-$HOME/wpe}/tofu.tf" ]; }; then
    echo "SKIPPED: tofu is not installed, or no tofu.tf in TOFU_DIR (${TOFU_DIR:-$HOME/wpe})"; return
  fi
  env "$@" ./$script || failed=1
}
run "check-startup"                 check-startup.sh
run "check-health   (plaintext)"    check-health.sh  TLS=0
run "check-streams  (plaintext)"    check-streams.sh TLS=0
run "check-tofu     (plaintext)"    check-tofu.sh    TLS=0
run "check-tls"                     check-tls.sh
run "check-health   (mutual TLS)"   check-health.sh  TLS=1
run "check-streams  (mutual TLS)"   check-streams.sh TLS=1
run "check-tofu     (mutual TLS)"   check-tofu.sh    TLS=1
echo; [ $failed = 0 ] && echo "ALL CHECKS PASSED" || { echo "SOME CHECKS FAILED"; exit 1; }
