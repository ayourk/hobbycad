#!/bin/sh
# =====================================================================
#  tests/i18n/run.sh — the catalogs, and the generator that reads them
#  SPDX-License-Identifier: GPL-3.0-only
# =====================================================================
#  Text and process checks, no build of the project needed, so this runs
#  against a library-only Qt-free build as well as the full one.
#
#  What it guards:
#    - the lookup key is (context, disambiguation, source); 24 messages
#      differ only by the disambiguation, so a key that drops it merges
#      them and a menu entry would read correctly while pointing at the
#      wrong command;
#    - the 15 catalogs stay in step;
#    - no %n reaches hobbycad::translate(), which carries no count;
#    - scripts/ts2cpp.py refuses hostile catalogs, and its output (which
#      is COMPILED) carries a translation holding C++ as escaped bytes
#      rather than as code.
#
#  Usage:  tests/i18n/run.sh [build dir, ignored]
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)

if ! command -v python3 > /dev/null 2>&1; then
    echo "  [FAIL] i18n: python3 is needed for these checks"
    echo "i18n tests: FAILURES"
    exit 1
fi

if python3 "$ROOT/tests/i18n/checks.py" "$ROOT"; then
    echo "i18n tests: ALL PASS"
else
    echo "i18n tests: FAILURES"
    exit 1
fi
