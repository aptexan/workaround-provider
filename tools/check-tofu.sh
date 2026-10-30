#!/bin/bash
# The real thing: run the real OpenTofu (plan, apply, destroy) against the provider with debug logging, and check that
# it succeeds and logs no errors or warnings.
#   Needs: the provider built, and ~/.tofurc with a dev_overrides entry for it (see TESTING.md).
#   TOFU_DIR = the directory holding your tofu.tf (default ~/wpe). Its terraform.tfstate* files are DELETED before and
#   after, so do not point it at a directory with real state. Logs: $TMPDIR/tofu-logs/ (or /tmp/tofu-logs/).
#   Plaintext only: TF_DISABLE_PLUGIN_TLS=1 until mutual TLS is implemented.
. "$(dirname "$0")/lib.sh"
TOFU_DIR=${TOFU_DIR:-$HOME/wpe}
LOGS=${TMPDIR:-/tmp}/tofu-logs; mkdir -p "$LOGS"
cd "$TOFU_DIR" || exit 2
export TF_DISABLE_PLUGIN_TLS=1 TF_LOG=DEBUG
rm -f terraform.tfstate*

for command in "plan" "apply -auto-approve" "destroy -auto-approve"; do
  name=${command%% *}; log=$LOGS/$name.log
  tofu $command -no-color >"$log" 2>&1; status=$?
  expect "$name: tofu succeeds (exit status)"       "$status" 0
  expect "$name: no [ERROR] lines in the log"       "$(grep -c '\[ERROR\]' "$log")" 0
  expect "$name: no [WARN] lines in the log"        "$(grep -c '\[WARN\]' "$log")" 0
  expect "$name: no 'Error:' messages"              "$(grep -c '^Error:' "$log")" 0
  # Core logs one 'starting plugin' per provider process it launches and one 'plugin process exited' when it ends.
  # A provider that had to be killed logs [WARN]/[ERROR] instead, so these two numbers must be equal.
  expect "$name: every provider process exited"     "$(grep -c 'plugin process exited' "$log")" "$(grep -c 'starting plugin' "$log")"
done
rm -f terraform.tfstate*
expect "no provider process left running"           "$(pgrep -fc "^$PROVIDER")" 0     # ^ : do not match this very command line
echo "logs are in $LOGS"
summary
