#!/bin/bash
# The real thing: run the real OpenTofu (plan, apply, destroy) against the provider with debug logging, and check that
# it succeeds and logs no errors or warnings. It uses the configuration in tools/tofu-test, which has resources that
# set every kind of attribute (see check-data.sh for what the values are checked against).
#   Needs: the provider built, and ~/.tofurc with a dev_overrides entry for it (see TESTING.md).
#   TOFU_DIR=<folder with a tofu.tf> uses your own configuration instead; its terraform.tfstate* files are DELETED
#   before and after, so do not point it at a folder with real state. Logs: $TMPDIR/tofu-logs/ (or /tmp/tofu-logs/).
#   Plaintext by default (TF_DISABLE_PLUGIN_TLS=1). TLS=1 leaves encryption on, which is how Core normally runs.
. "$(dirname "$0")/lib.sh"
setup_tofu

for command in "plan" "apply -auto-approve" "destroy -auto-approve"; do
  name=${command%% *}; log=$LOGS/$name.log
  tofu $command -no-color >"$log" 2>&1; status=$?
  expect "$name: tofu succeeds (exit status)"       "$status" 0
  # Core logs this line only when it uses mutual TLS, so this proves the mode we asked for is the mode that ran.
  expect "$name: Core used mutual TLS (only in TLS mode)" "$(grep -q 'configuring client automatic mTLS' "$log" && echo yes || echo no)" "$([ "$TLS" = 1 ] && echo yes || echo no)"
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
