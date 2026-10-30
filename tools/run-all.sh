#!/bin/bash
# Runs all four checks. Exit status 0 only if all passed. check-tofu.sh is skipped (and says so) when tofu is not
# installed or TOFU_DIR has no tofu.tf. See TESTING.md.
cd "$(dirname "$0")" || exit 2
failed=0
for script in check-startup check-health check-streams check-tofu; do
  echo; echo "=== $script"
  if [ $script = check-tofu ] && { ! command -v tofu >/dev/null || [ ! -f "${TOFU_DIR:-$HOME/wpe}/tofu.tf" ]; }; then
    echo "SKIPPED: tofu is not installed, or no tofu.tf in TOFU_DIR (${TOFU_DIR:-$HOME/wpe})"; continue
  fi
  ./$script.sh || failed=1
done
echo; [ $failed = 0 ] && echo "ALL CHECKS PASSED" || { echo "SOME CHECKS FAILED"; exit 1; }
