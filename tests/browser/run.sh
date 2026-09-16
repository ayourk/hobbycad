#!/bin/sh
# =====================================================================
#  tests/browser/run.sh — objects browser node model tests
#  SPDX-License-Identifier: GPL-3.0-only
# =====================================================================
#  Dependency-free, same approach as tests/solver/run.sh: link the built
#  static library directly, no test framework. Build the project first.
#
#  Usage:  tests/project/run.sh [path/to/build]
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=${1:-$ROOT/build}
LIB=$BUILD/src/libhobbycad/libhobbycad.a
[ -f "$LIB" ] || { echo "error: $LIB not found; build the project first" >&2; exit 2; }

# ---- Qt flags follow the BUILD, not this machine ---------------------
# A build configured without Qt compiles libhobbycad with HOBBYCAD_HAS_QT
# 0. Handing the test Qt's cflags anyway would define QT_CORE_LIB, so the
# test would see a different Project and SketchData than the library it
# links against, and the link would fail. Ask the build what it is.
HC_QT=0
if [ -f "$BUILD/CMakeCache.txt" ] && grep -q '^Qt6_DIR:PATH=/' "$BUILD/CMakeCache.txt"; then
    HC_QT=1
fi
hc_qt_cflags() { [ "$HC_QT" = 1 ] || return 0; pkg-config --cflags "$@" 2>/dev/null || true; }
hc_qt_libs()   { [ "$HC_QT" = 1 ] || return 0; pkg-config --libs "$@" 2>/dev/null || true; }
# Without Qt the library decodes images through libwebp directly.
HC_IMG_LIBS=
[ "$HC_QT" = 1 ] || HC_IMG_LIBS=$(pkg-config --libs libwebp 2>/dev/null || echo -lwebp)

QT_CFLAGS=$(hc_qt_cflags Qt6Gui Qt6Core)
QT_LIBS=$(hc_qt_libs Qt6Gui Qt6Core)
OCC_INC=/usr/include/opencascade
OCC_LIBS=$(ls /usr/lib/*/libTK*.so 2>/dev/null | sed 's|.*/lib\(TK[^.]*\)\.so|-l\1|' | tr '\n' ' ')
# libhobbycad.a pulls in the solver; the app links it via CMake, so a test
# that links the static library directly has to name it too.
# pkg-config first: a libslvs built from source lands in /usr/local (the
# Debian container job), where the multiarch path below finds nothing.
SLVS_LIB=$(pkg-config --libs slvs 2>/dev/null || ls /usr/lib/*/libslvs.so 2>/dev/null | head -1)

OUT=$(mktemp -d); trap 'rm -rf "$OUT"' EXIT
STATUS=0

QT_W_CFLAGS=$(hc_qt_cflags Qt6Widgets Qt6Gui Qt6Core)
QT_W_LIBS=$(hc_qt_libs Qt6Widgets Qt6Gui Qt6Core Qt6Test || hc_qt_libs Qt6Widgets Qt6Gui Qt6Core)
# The widget test needs the moc output CMake already generated.
# Find the autogen directory that actually holds the moc output; CMake
# creates several and only one of them has these files.
MOC=$(find "$BUILD" -name moc_objectsbrowserwidget.cpp 2>/dev/null | head -1)
AUTOGEN=$(dirname "$MOC" 2>/dev/null)

for src in "$(dirname "$0")"/*.cpp; do
    name=$(basename "$src" .cpp)
    EXTRA_SRC=""
    case "$name" in
        paramsdialog_smoke)
            EXTRA_SRC="$ROOT/src/hobbycad/gui/parametersdialog.cpp $AUTOGEN/moc_parametersdialog.cpp" ;;
        bgimagedialog_smoke)
            EXTRA_SRC="$ROOT/src/hobbycad/gui/backgroundimagedialog.cpp $AUTOGEN/moc_backgroundimagedialog.cpp" ;;
        formulaedit_smoke)
            EXTRA_SRC="$ROOT/src/hobbycad/gui/formulaedit.cpp $AUTOGEN/moc_formulaedit.cpp" ;;
        prefsdialog_smoke)
            EXTRA_SRC="$ROOT/src/hobbycad/gui/preferencesdialog.cpp $AUTOGEN/moc_preferencesdialog.cpp \
                       $ROOT/src/hobbycad/gui/bindingsdialog.cpp $AUTOGEN/moc_bindingsdialog.cpp \
                       $ROOT/src/hobbycad/gui/bindingeditrow.cpp $AUTOGEN/moc_bindingeditrow.cpp" ;;
        clipager_smoke)
            # cli_translator.cpp comes along because the command layer is
            # Qt-free now: the panel installs it so CLI messages are still
            # translated, and without it the link fails on that symbol.
            EXTRA_SRC="$ROOT/src/hobbycad/gui/clipanel.cpp $AUTOGEN/moc_clipanel.cpp \
                       $ROOT/src/hobbycad/cli/cliengine.cpp \
                       $ROOT/src/hobbycad/cli/clihistory.cpp \
                       $ROOT/src/hobbycad/cli_translator.cpp \
                       $BUILD/src/libhobbycad/libhobbycad.a" ;;
    esac
    case "$name" in *_smoke)
        if [ -z "$MOC" ]; then
            # A test that does not run cannot fail, so it must not pass.
            echo "  [FAIL] $name: cannot run, moc output not found; build the GUI first"
            STATUS=1
            continue
        fi
        # shellcheck disable=SC2086
        g++ -std=c++17 -fPIC -I"$ROOT/src/hobbycad/gui" -I"$ROOT/src/hobbycad/cli" -I"$ROOT/src/hobbycad" -I"$ROOT/src/libhobbycad" \
            -I"$OCC_INC" $QT_W_CFLAGS "$src" \
            "$ROOT/src/hobbycad/gui/objectsbrowserwidget.cpp" "$MOC" \
            $EXTRA_SRC \
            "$LIB" $QT_W_LIBS $OCC_LIBS $SLVS_LIB -o "$OUT/$name" || { STATUS=1; continue; }
        QT_QPA_PLATFORM=offscreen "$OUT/$name" || STATUS=1
        continue ;;
    esac
    # shellcheck disable=SC2086
    g++ -std=c++17 -I"$ROOT/src/libhobbycad" -I"$OCC_INC" $QT_CFLAGS \
        "$src" "$LIB" $QT_LIBS $HC_IMG_LIBS $OCC_LIBS -o "$OUT/$name" || { STATUS=1; continue; }
    "$OUT/$name" "$OUT/$name.data" || STATUS=1
done

if [ "$STATUS" -eq 0 ]; then echo "browser tests: ALL PASS"; else echo "browser tests: FAILURES"; fi
exit "$STATUS"
