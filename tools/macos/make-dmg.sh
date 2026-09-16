#!/bin/bash
# =====================================================================
#  HobbyCAD   tools/macos/make-dmg.sh   Wrap HobbyCAD.app in a DMG
# =====================================================================
#  SPDX-License-Identifier: GPL-3.0-only
#
#  The macOS installer: a compressed disk image holding HobbyCAD.app and
#  an Applications shortcut, so installing is one drag. What a finished
#  image carries, and where each part comes from here:
#
#    - the application, code-signed. Apple Silicon refuses to run an
#      unsigned executable at all, and lipo strips whatever signature
#      the linker applied, so the staged copy is signed before it goes
#      into the image: with CODESIGN_IDENTITY (a Developer ID) when the
#      environment provides one, ad hoc ("-") otherwise. Ad hoc lets the
#      binary run; only a Developer ID plus notarization satisfies
#      Gatekeeper on another Mac.
#    - the Finder presentation: window size, icon size and the two icon
#      positions (app left, Applications right), stored in the volume's
#      .DS_Store. Finder writes that file, so it needs a logged-in GUI
#      session (a developer's Mac, or the GitHub macOS runners, which
#      have one); over a bare SSH login the step is skipped and the
#      image is functional but unarranged.
#    - the volume icon: the application's own icon, with the custom
#      icon flag set on the volume root.
#    - notarization and stapling, when a Developer ID is in use, are a
#      separate step after this script (xcrun notarytool submit --wait,
#      xcrun stapler staple); nothing here needs an Apple account.
#
#  Format: HFS+, UDZO (zlib) compressed, readable by every macOS the
#  build targets (deployment target 11.0). Everything used ships with
#  macOS or the Command Line Tools (hdiutil, PlistBuddy, SetFile,
#  codesign, osascript). bash 3.2 compatible: this is /bin/bash on macOS.
#
#    tools/macos/make-dmg.sh <HobbyCAD.app> <arch> [output-dir]
#
#  <arch> names the slice for the file name (arm64, x86_64, universal).
#  The version comes from the bundle's Info.plist.
#  Output: <output-dir>/HobbyCAD-<version>-<arch>.dmg   (default: .)
# =====================================================================
set -eu

die() { echo "make-dmg: $*" >&2; exit 1; }

APP=${1:?usage: make-dmg.sh <HobbyCAD.app> <arch> [output-dir]}
ARCH=${2:?usage: make-dmg.sh <HobbyCAD.app> <arch> [output-dir]}
OUTDIR=${3:-.}

[ -d "$APP/Contents/MacOS" ] || die "$APP is not an application bundle"
APP=$(cd "$APP" && pwd)
PLIST=$APP/Contents/Info.plist
VERSION=$(/usr/libexec/PlistBuddy -c 'Print CFBundleShortVersionString' "$PLIST" 2>/dev/null) \
    || die "no CFBundleShortVersionString in $PLIST"
NAME=HobbyCAD-$VERSION-$ARCH
VOLNAME="HobbyCAD $VERSION"
mkdir -p "$OUTDIR"
OUTDIR=$(cd "$OUTDIR" && pwd)
DMG=$OUTDIR/$NAME.dmg

WORK=$(mktemp -d "${TMPDIR:-/tmp}/hobbycad-dmg.XXXXXX")
trap 'rm -rf "$WORK"' EXIT
STAGE=$WORK/stage
mkdir -p "$STAGE"

echo "== staging $(basename "$APP") ($VERSION, $ARCH)"
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"
APPNAME=$(basename "$APP")
# No hidden-extension flag on the bundle: Finder information on a signed
# bundle is "detritus" to codesign, the signature fails strict verification
# and Gatekeeper rejects the app. Finder hides ".app" by default anyway.

# The hardened runtime (--options runtime) only with a real identity: it
# enables library validation, which refuses any framework not signed by
# the same team, and an ad hoc signature has no team. A dynamically
# linked development build (the Qt frameworks under ~/Qt) then dies in
# dyld at launch; the static CI builds carry no frameworks at all.
echo "== signing (${CODESIGN_IDENTITY:-ad hoc})"
if [ -n "${CODESIGN_IDENTITY:-}" ]; then
    codesign --force --deep --options runtime --timestamp \
        -s "$CODESIGN_IDENTITY" "$STAGE/$APPNAME"
else
    codesign --force --deep -s - "$STAGE/$APPNAME"
fi
codesign --verify --deep --strict "$STAGE/$APPNAME"
ICNS=$APP/Contents/Resources/hobbycad.icns

# Writable image first so the volume's custom-icon flag can be set, then
# a compressed read-only conversion for distribution.
RW=$WORK/$NAME-rw.dmg
echo "== creating image"
hdiutil create -quiet -volname "$VOLNAME" -srcfolder "$STAGE" -fs HFS+ \
    -format UDRW -ov "$RW"
MNT=$(hdiutil attach -readwrite -noverify -nobrowse "$RW" \
      | awk -F'\t' '/\/Volumes\//{print $NF}')
[ -n "$MNT" ] || die "could not mount $RW"

# Finder layout, written into the volume's .DS_Store by Finder itself.
# Needs a GUI session; skipped without one.
# Finder addresses the disk by its mount name, which macOS suffixes with
# a number when a volume of the same name is already mounted, so the
# name is taken from the actual mount point rather than from VOLNAME.
echo "== Finder layout"
DISKNAME=$(basename "$MNT")
if osascript -e 'tell application "Finder" to name' >/dev/null 2>&1; then
    osascript <<APPLESCRIPT >/dev/null 2>&1 || echo "   (Finder declined; layout skipped)"
tell application "Finder"
    tell disk "$DISKNAME"
        open
        set current view of container window to icon view
        set toolbar visible of container window to false
        set statusbar visible of container window to false
        set the bounds of container window to {200, 120, 760, 480}
        set theViewOptions to the icon view options of container window
        set arrangement of theViewOptions to not arranged
        set icon size of theViewOptions to 128
        set position of item "$APPNAME" of container window to {150, 180}
        set position of item "Applications" of container window to {410, 180}
        close
        open
        update without registering applications
        delay 2
        close
    end tell
end tell
APPLESCRIPT
    sync
    if [ -f "$MNT/.DS_Store" ]; then
        echo "   layout written"
    else
        echo "   (no .DS_Store written; layout skipped)"
    fi
else
    echo "   (no GUI session for Finder; layout skipped)"
fi
# Volume icon last: Finder rewrites the volume root when it saves the
# layout and discards an icon set before it.
echo "== volume icon"
if [ -f "$ICNS" ]; then
    SETFILE=$(xcrun -f SetFile 2>/dev/null || true)
    if [ -n "$SETFILE" ]; then
        cp "$ICNS" "$MNT/.VolumeIcon.icns"
        "$SETFILE" -c icnC "$MNT/.VolumeIcon.icns"
        "$SETFILE" -a C "$MNT"
        echo "   set from the bundle"
    else
        echo "   (SetFile not available; volume keeps the stock icon)"
    fi
else
    echo "   (no hobbycad.icns in the bundle; volume keeps the stock icon)"
fi
sync
hdiutil detach -quiet "$MNT"
rm -f "$DMG"
hdiutil convert -quiet "$RW" -format UDZO -imagekey zlib-level=9 -o "$DMG"
hdiutil verify -quiet "$DMG"

echo "== $DMG"
ls -l "$DMG" | awk '{print "   " $5 " bytes"}'
