#!/bin/bash
# Builds the release DMG from an installed tree.
#
#   packaging/macos/make_dmg.sh <install-dir> <out.dmg>
#
# <install-dir> is the `cmake --install` prefix holding Nightlock.app
# (Qt frameworks already deployed by the install step). The DMG is the
# classic drag-to-Applications layout. No code signing — first-launch
# needs right-click → Open on unidentified-developer warnings.
set -euo pipefail

INSTALL_DIR="$1"
OUT_DMG="$2"
APP="$INSTALL_DIR/Nightlock.app"

[ -d "$APP" ] || { echo "error: $APP not found (run cmake --install first)" >&2; exit 1; }

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

cp -R "$APP" "$STAGE/"

# macdeployqt's install_name_tool edits invalidate the executable's
# ad-hoc signature, and arm64 macOS kills unsigned binaries outright.
# A fresh ad-hoc signature (no Developer ID involved) fixes that.
codesign --force --deep --sign - "$STAGE/Nightlock.app"

ln -s /Applications "$STAGE/Applications"

# The CLI hides in Contents/Helpers; tell people how to reach it.
cat > "$STAGE/Install command-line tool.txt" <<'EOF'
The nightlock command-line tool ships inside the app. To use it from
the terminal, link it into your PATH once:

  sudo ln -sf "/Applications/Nightlock.app/Contents/Helpers/nightlock" /usr/local/bin/nightlock

Then `nightlock --help` works from any shell.
EOF

rm -f "$OUT_DMG"
hdiutil create -volname Nightlock -srcfolder "$STAGE" -ov -format UDZO "$OUT_DMG"
echo "built: $OUT_DMG"
