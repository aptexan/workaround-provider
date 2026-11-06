#!/bin/bash
# Does the DATA come through? The real OpenTofu runs the configuration in tools/tofu-test against the provider and
# follows its three resources through their life: create, no change, update in place, replacement, destroy. Each step
# reads the values back from OpenTofu's own plan/state JSON and compares them with what the provider's rules say.
# It also checks that mistakes in the configuration come back as the provider's own error and warning messages.
#   Needs: the provider built, ~/.tofurc with a dev_overrides entry for it, python3. See TESTING.md.
#   Plaintext by default; TLS=1 leaves encryption on. TOFU_DIR=<folder> is NOT meant for this script.
. "$(dirname "$0")/lib.sh"
unset TOFU_DIR
setup_tofu
command -v python3 >/dev/null || { echo "this check needs python3"; exit 2; }

# run <step> <tofu subcommand> [options...]: runs OpenTofu (debug log in $LOGS/data-<step>.log). Sets STATUS and LOG.
run() {
  local step=$1 subcommand=$2; shift 2
  LOG=$LOGS/data-$step.log
  tofu "$subcommand" -no-color "$@" >"$LOG" 2>&1; STATUS=$?
}
# A step that should be clean: exit 0, and nothing logged at [ERROR] or [WARN].
clean() {  # clean <step description>
  expect "$1: exit status" "$STATUS" 0
  expect "$1: no [ERROR] or [WARN] in the log" "$(grep -c '\[ERROR\]\|\[WARN\]' "$LOG")" 0
}
has() { grep -qF -- "$1" "$LOG" && echo yes || echo no; }       # does the log of the last run contain this text?
has_line() { grep -qE -- "$1" "$LOG" && echo yes || echo no; }  # ... a line matching this pattern?
# Where Core points. With the attribute's path in the diagnostic it shows that attribute's line, e.g.
#   39:   size        = var.full_size
# Without a path it shows only the header of the whole resource block. (The debug log also mentions var.full_size
# elsewhere, so the text alone proves nothing: the line has to be shown the way Core shows a source line.)
points_at() { has_line "^ +[0-9]+: +$1 +\= var\.$2\$"; }

# Read OpenTofu's JSON. A Python expression is evaluated over:
#   state: r = the resource, v = its values, s = which values are sensitive;   plan: c = the resource's planned change
# and printed (text as is, anything else as JSON).
show() { tofu show -json "$@" 2>/dev/null; }
PY='import json,sys; d=json.load(open(sys.argv[1])); a=sys.argv[2]
if "values" in d:
    r=next(x for x in d["values"]["root_module"]["resources"] if x["address"]==a); v=r["values"]; s=r["sensitive_values"]
else:
    c=next(x for x in d["resource_changes"] if x["address"]==a)["change"]
x=eval(sys.argv[3]); print(x if isinstance(x,str) else json.dumps(x,sort_keys=True))'
st() { show >"$WORK/state.json"; python3 -I -c "$PY" "$WORK/state.json" "$1" "$2"; }                  # st <address> <expression>
pl() { show "$1" >"$WORK/plan.json"; python3 -I -c "$PY" "$WORK/plan.json" "$2" "$3"; }                # pl <planfile> <address> <expression>

BASIC=workaround_example.basic FULL=workaround_example.full CHAINED=workaround_example.chained

echo "== validate"
run validate validate
expect "tofu validate accepts the configuration (the provider validated it)" "$STATUS" 0

echo "== plan: what the provider promises before anything exists"
run plan1 plan -out=plan1.bin
clean "plan"
expect "basic: will be created"                         "$(pl plan1.bin $BASIC 'c["actions"]')" '["create"]'
expect "basic: the id is not known yet"                 "$(pl plan1.bin $BASIC 'c["after_unknown"]["id"]')" true
expect "basic: enabled gets the provider's default"     "$(pl plan1.bin $BASIC 'c["after"]["enabled"]')" true
expect "basic: the summary is already known"            "$(pl plan1.bin $BASIC 'c["after_unknown"].get("summary", False)')" false
expect "basic: ... and says what it will say"           "$(pl plan1.bin $BASIC 'c["after"]["summary"]')" "basic: size=none, enabled=true"
expect "full: a set drops its duplicate"                "$(pl plan1.bin $FULL 'sorted(c["after"]["labels"])')" '["x", "y"]'
expect "chained: its name is not known yet"             "$(pl plan1.bin $CHAINED 'c["after_unknown"]["name"]')" true
expect "chained: so its summary is not known either"    "$(pl plan1.bin $CHAINED 'c["after_unknown"]["summary"]')" true

echo "== apply that plan"
run apply1 apply plan1.bin
clean "apply"
BASIC_ID=$(st $BASIC 'v["id"]')
expect "basic: the id is the prefix, a dash and 8 hex digits" "$(echo "$BASIC_ID" | grep -cE '^wp-[0-9a-f]{8}$')" 1
expect "basic: summary"                                 "$(st $BASIC 'v["summary"]')" "basic: size=none, enabled=true"
expect "full: description"                              "$(st $FULL 'v["description"]')" "everything set"
expect "full: size"                                     "$(st $FULL 'v["size"]')" 7
expect "full: ratio (a fraction)"                       "$(st $FULL 'v["ratio"]')" 0.5
expect "full: tags (a map)"                             "$(st $FULL 'v["tags"]')" '{"env": "test", "team": "core"}'
expect "full: items (a list keeps its order)"           "$(st $FULL 'v["items"]')" '["a", "b", "c"]'
expect "full: labels (a set)"                           "$(st $FULL 'sorted(v["labels"])')" '["x", "y"]'
expect "full: summary"                                  "$(st $FULL 'v["summary"]')" "full: size=7, enabled=true"
expect "full: the secret is marked sensitive"           "$(st $FULL 's["secret"]')" true
expect "full: ... and its value was stored"             "$(st $FULL 'v["secret"]')" "s3cret"
expect "full: tags are not sensitive"                   "$(st $FULL '"tags" in s and s["tags"] != {}')" false
expect "chained: its name became basic's id"            "$(st $CHAINED 'v["name"]')" "$BASIC_ID"
expect "chained: summary uses it"                       "$(st $CHAINED 'v["summary"]')" "$BASIC_ID: size=none, enabled=true"
expect "output: basic_id"                               "$(tofu output -raw basic_id 2>/dev/null)" "$BASIC_ID"
expect "output: full_summary"                           "$(tofu output -raw full_summary 2>/dev/null)" "full: size=7, enabled=true"

echo "== plan again: nothing changed, so nothing to do"
run plan2 plan -detailed-exitcode
clean "a second plan finds no changes (exit 0, not 2)"

echo "== update in place"
FULL_ID=$(st $FULL 'v["id"]')
run plan3 plan -out=plan3.bin -var full_size=9
clean "plan"
expect "full: an update, not a replacement"             "$(pl plan3.bin $FULL 'c["actions"]')" '["update"]'
expect "basic: untouched"                               "$(pl plan3.bin $BASIC 'c["actions"]')" '["no-op"]'
expect "full: the id stays known during the plan"       "$(pl plan3.bin $FULL 'c["after_unknown"].get("id", False)')" false
run apply3 apply plan3.bin
clean "apply"
expect "full: the id did not change"                    "$(st $FULL 'v["id"]')" "$FULL_ID"
expect "full: the size changed"                         "$(st $FULL 'v["size"]')" 9
expect "full: the summary followed"                     "$(st $FULL 'v["summary"]')" "full: size=9, enabled=true"

echo "== a fraction survives the round trip through OpenTofu"
run apply4 apply -auto-approve -var full_size=9 -var full_ratio=0.1
clean "apply with ratio 0.1"
expect "full: ratio 0.1 (stored as float64)"            "$(st $FULL 'v["ratio"]')" 0.1
run plan4 plan -detailed-exitcode -var full_size=9 -var full_ratio=0.1
clean "plan afterwards finds no drift"

echo "== replacement"
run plan5 plan -out=plan5.bin -var full_name=full2 -var full_size=9 -var full_ratio=0.1
clean "plan"
expect "full: replaced (deleted and created)"           "$(pl plan5.bin $FULL 'sorted(c["actions"])')" '["create", "delete"]'
expect "full: because of the name"                      "$(pl plan5.bin $FULL 'c["replace_paths"]')" '[["name"]]'
run apply5 apply plan5.bin
clean "apply"
expect "full: it has a new id"                          "$([ "$(st $FULL 'v["id"]')" != "$FULL_ID" ] && echo yes || echo no)" yes
expect "full: the new name"                             "$(st $FULL 'v["name"]')" "full2"
expect "basic: its id did not change"                   "$(st $BASIC 'v["id"]')" "$BASIC_ID"

echo "== a boolean set to false, and the warning about it"
VARS=(-var full_name=full2 -var full_size=9 -var full_ratio=0.1 -var full_enabled=false)
run plan6 plan "${VARS[@]}"
expect "plan succeeds"                                  "$STATUS" 0
expect "the provider's warning is shown"                "$(has 'description has no effect')" yes
expect "... as a warning"                               "$(has 'Warning:')" yes
run apply6 apply -auto-approve "${VARS[@]}"
expect "apply succeeds"                                 "$STATUS" 0
expect "full: enabled is false"                         "$(st $FULL 'v["enabled"]')" false
expect "full: and so is the summary"                    "$(st $FULL 'v["summary"]')" "full2: size=9, enabled=false"
run plan7 plan -detailed-exitcode "${VARS[@]}"
expect "no drift after a false boolean (exit 0)"        "$STATUS" 0

if negatives; then
  echo "== mistakes in the configuration come back as the provider's own messages, and nothing changes"
  BEFORE=$(st $FULL 'v')
  run bad1 plan -var full_size=500
  expect "size 500: plan fails"                           "$STATUS" 1
  expect "... with the provider's summary"                "$(has 'Invalid size')" yes
  expect "... and its explanation"                        "$(has 'between 1 and 100')" yes
  expect "... pointing at the size attribute's own line"  "$(points_at size full_size)" yes
  run bad2 plan -var 'full_name=bad name'
  expect "a space in the name: plan fails"                "$STATUS" 1
  expect "... with the provider's summary"                "$(has 'Invalid name')" yes
  expect "... pointing at the name attribute's own line"  "$(points_at name full_name)" yes
  run bad3 plan -var prefix=BAD
  expect "a bad provider prefix: plan fails"              "$STATUS" 1
  expect "... with the provider's summary"                "$(has 'Invalid id_prefix')" yes
  expect "... pointing at the id_prefix line in the provider block" "$(points_at id_prefix prefix)" yes
  run bad4 plan -var full_ratio=2
  expect "ratio 2: plan fails"                            "$STATUS" 1
  expect "... with the provider's summary"                "$(has 'Invalid ratio')" yes
  run bad5 plan -var full_size=500 -var full_ratio=2
  expect "two mistakes: both are reported"                "$(has 'Invalid size')$(has 'Invalid ratio')" yesyes
  expect "the state was not touched by any of that"       "$(st $FULL 'v')" "$BEFORE"
fi

echo "== destroy"
run destroy destroy -auto-approve "${VARS[@]}"
clean "destroy"
expect "no resources are left in the state"             "$(show | python3 -I -c 'import json,sys; d=json.load(sys.stdin); print(len(d.get("values",{}).get("root_module",{}).get("resources",[])))')" 0
rm -f terraform.tfstate*
expect "no provider process left running"               "$(pgrep -fc "^$PROVIDER")" 0
echo "logs are in $LOGS"
summary
