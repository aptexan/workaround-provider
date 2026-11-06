#!/bin/bash
# Runs every check (the build itself first), in both modes where that makes sense: plaintext (Core told to skip
# encryption) and mutual TLS (how Core runs by default). Exit status 0 only if all passed.
#
# The NEGATIVE tests run by default, in two forms:
#   - inside each check: the cases where the provider must refuse something (a bad cookie, an unwanted client, a mistake
#     in the configuration);
#   - last: negative-tests/negative-check.sh, which breaks a copy of the provider on purpose and requires the checks to
#     notice. That one takes about 25 minutes, and needs VS Code closed and this project left alone while it runs.
#   --skip-negative (or SKIP_NEGATIVE=1)   leaves out both, which is about 30 seconds.
#
# The OpenTofu checks are skipped (and say so) when tofu is not installed. check-tls.sh and the TLS runs need the openssl
# command. See TESTING.md.
cd "$(dirname "$0")" || exit 2

SKIP_NEGATIVE=${SKIP_NEGATIVE:-0}
for option in "$@"; do
  case $option in
    --skip-negative) SKIP_NEGATIVE=1 ;;
    *) echo "unknown option: $option (the only option is --skip-negative)"; exit 2 ;;
  esac
done
SKIP=(); [ "$SKIP_NEGATIVE" = 1 ] && SKIP=(--skip-negative)

failed=0
run() {  # run <label> <script> [VAR=value ...]
  local label=$1 script=$2; shift 2
  echo; echo "=== $label"
  if [[ $script == check-tofu.sh || $script == check-data.sh ]] && ! command -v tofu >/dev/null; then
    echo "SKIPPED: tofu is not installed"; return
  fi
  env "$@" ./$script "${SKIP[@]}" || failed=1
}
run "check-build"                   check-build.sh
run "check-unit"                    check-unit.sh
run "check-startup"                 check-startup.sh
run "check-health   (plaintext)"    check-health.sh  TLS=0
run "check-streams  (plaintext)"    check-streams.sh TLS=0
run "check-tofu     (plaintext)"    check-tofu.sh    TLS=0
run "check-data     (plaintext)"    check-data.sh    TLS=0
run "check-tls"                     check-tls.sh
run "check-health   (mutual TLS)"   check-health.sh  TLS=1
run "check-streams  (mutual TLS)"   check-streams.sh TLS=1
run "check-tofu     (mutual TLS)"   check-tofu.sh    TLS=1
run "check-data     (mutual TLS)"   check-data.sh    TLS=1

echo; echo "=== negative-check (breaks the provider on purpose; about 25 minutes)"
if [ "$SKIP_NEGATIVE" = 1 ]; then
  echo "SKIPPED (--skip-negative)"
else
  ./negative-tests/negative-check.sh || failed=1
fi

echo; [ $failed = 0 ] && echo "ALL CHECKS PASSED" || { echo "SOME CHECKS FAILED"; exit 1; }
