#!/bin/bash
# The unit tests of the provider's core: the Terraform value model and its msgpack/JSON encodings, and the rules of
# the one resource (what is valid, how a change is planned and applied). No OpenTofu and no gRPC involved: it runs
# build/unit-tests (built together with the provider; the source is in tests/). Details: TESTING.md.
. "$(dirname "$0")/lib.sh"
UNIT_TESTS=${UNIT_TESTS:-$(dirname "$PROVIDER")/unit-tests}
[ -x "$UNIT_TESTS" ] || { echo "no $UNIT_TESTS: build the project first"; exit 2; }
if negatives; then exec "$UNIT_TESTS"; else exec "$UNIT_TESTS" --skip-negative; fi
