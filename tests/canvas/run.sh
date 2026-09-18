#!/bin/sh
# =====================================================================
#  tests/canvas/run.sh — sketch canvas tests against the built application
#  SPDX-License-Identifier: GPL-3.0-only
# =====================================================================
#  The canvas is too entangled with the rest of the GUI (tool handlers,
#  renderers, dialogs) to compile on its own, so these tests link the
#  application's own object files, all but main(), with the flags CMake
#  compiled them with. Build the application first. Qt only: a
#  library-only build has no canvas, and these tests fail there rather
#  than pass.
#
#  Usage:  tests/canvas/run.sh [path/to/build]
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
# Absolute, because the link below runs from inside the build directory.
BUILD=$(cd "${1:-$ROOT/build}" && pwd)
OBJDIR=$BUILD/src/hobbycad/CMakeFiles/hobbycad.dir
if [ ! -f "$OBJDIR/gui/sketchcanvas.cpp.o" ]; then
    echo "  [FAIL] canvas: application objects not found; build the application first"
    echo "canvas tests: FAILURES"
    exit 1
fi

# The flags the canvas was compiled with, and the libraries the application
# links, both as CMake recorded them.
FLAGS=$(python3 - "$BUILD" <<'EOF'
import json, shlex, sys
cc = json.load(open(sys.argv[1] + "/compile_commands.json"))
cmd = next(e for e in cc if e["file"].endswith("/gui/sketchcanvas.cpp"))["command"]
keep, args = [], shlex.split(cmd)
i = 0
while i < len(args):
    a = args[i]
    if a in ("-isystem", "-I", "-D"):
        keep += [a, args[i + 1]]
        i += 2
        continue
    if a.startswith(("-I", "-D", "-std=")):
        keep.append(a)
    i += 1
print(" ".join(shlex.quote(k) for k in keep))
EOF
)
LIBS=$(awk '/^build src\/hobbycad\/hobbycad:/ { f = 1 }
            f && /LINK_LIBRARIES/ { sub(/^ *LINK_LIBRARIES = /, ""); print; exit }' \
       "$BUILD/build.ninja")
OBJS=$(find "$OBJDIR" -name '*.o' ! -name 'main.cpp.o' | sort)

OUT=$(mktemp -d); trap 'rm -rf "$OUT"' EXIT
STATUS=0
cd "$BUILD"
for src in "$HERE"/*.cpp; do
    name=$(basename "$src" .cpp)
    # shellcheck disable=SC2086
    eval "c++ $FLAGS -fPIC -I\"$ROOT/src/hobbycad/gui\" -c \"$src\" -o \"$OUT/$name.o\"" \
        || { STATUS=1; continue; }
    # shellcheck disable=SC2086
    c++ "$OUT/$name.o" $OBJS $LIBS -o "$OUT/$name" || { STATUS=1; continue; }
    QT_QPA_PLATFORM=offscreen "$OUT/$name" || STATUS=1
done

if [ "$STATUS" -eq 0 ]; then echo "canvas tests: ALL PASS"; else echo "canvas tests: FAILURES"; fi
exit "$STATUS"
