#!/bin/bash
# Builds a self-contained .deb from an installed tree: Qt (from the
# aqt/official binaries — distro Qt is too old for this app) is copied
# into /usr/lib/nightlock next to the GUI binary, so the package only
# depends on base system libraries.
#
#   packaging/linux/make_deb.sh <stage-dir> <qt-dir> <version> <out.deb>
#
# <stage-dir> is the root that holds usr/ after
#   cmake --install build --prefix <stage-dir>/usr
# <qt-dir> is the Qt prefix (the directory holding lib/ and plugins/).
set -euo pipefail

STAGE="$1"
QT_DIR="$2"
VERSION="$3"
OUT_DEB="$4"

APP_DIR="$STAGE/usr/lib/nightlock"
BIN="$APP_DIR/nightlock-desktop"
[ -f "$BIN" ] || { echo "error: $BIN missing (cmake --install first)" >&2; exit 1; }
[ -d "$QT_DIR/lib" ] || { echo "error: $QT_DIR has no lib/" >&2; exit 1; }

# --- bundle Qt plugins the app actually loads at runtime ------------
# No networking in the app, so the tls plugins stay out. imageformats
# matter: the icon packs are .ico/.png/.jpg and the menu icons are SVG.
PLUGIN_CATEGORIES=(
    platforms
    platforminputcontexts
    xcbglintegrations
    wayland-decoration-client
    wayland-graphics-integration-client
    wayland-shell-integration
    imageformats
    iconengines
    platformthemes
)
mkdir -p "$APP_DIR/plugins"
for category in "${PLUGIN_CATEGORIES[@]}"; do
    if [ -d "$QT_DIR/plugins/$category" ]; then
        cp -a "$QT_DIR/plugins/$category" "$APP_DIR/plugins/"
    fi
done

# --- copy the shared-library closure out of the Qt prefix -----------
# Iterate ldd over the binary and every bundled plugin, copying any
# dependency that lives inside the Qt prefix (libQt6*, libicu*, ...)
# until nothing new appears. System libraries stay system — they are
# the package's Depends.
mkdir -p "$APP_DIR/lib"
copied=1
while [ "$copied" -eq 1 ]; do
    copied=0
    while IFS= read -r -d '' object; do
        while IFS= read -r dep; do
            base="$(basename "$dep")"
            if [ -f "$QT_DIR/lib/$base" ] && [ ! -e "$APP_DIR/lib/$base" ]; then
                cp -a "$QT_DIR/lib/$base"* "$APP_DIR/lib/" 2>/dev/null || \
                    cp -L "$QT_DIR/lib/$base" "$APP_DIR/lib/"
                copied=1
            fi
        done < <(ldd "$object" 2>/dev/null | awk '/=>/ { print $1 }')
    done < <(find "$APP_DIR" -name '*.so*' -print0; printf '%s\0' "$BIN")
done

# Binaries look up Qt relative to themselves from here on.
patchelf --set-rpath '$ORIGIN/lib' "$BIN"
find "$APP_DIR/plugins" -name '*.so' -exec patchelf --set-rpath '$ORIGIN/../../lib' {} \;
cat > "$APP_DIR/qt.conf" <<'EOF'
[Paths]
Prefix = .
Plugins = plugins
Libraries = lib
EOF

# Convenience launcher name in PATH alongside the CLI.
mkdir -p "$STAGE/usr/bin"
ln -sf ../lib/nightlock/nightlock-desktop "$STAGE/usr/bin/nightlock-desktop"

strip --strip-unneeded "$BIN" "$STAGE/usr/bin/nightlock" 2>/dev/null || true

# --- debian metadata -------------------------------------------------
mkdir -p "$STAGE/DEBIAN"
INSTALLED_SIZE=$(du -sk "$STAGE/usr" | cut -f1)
cat > "$STAGE/DEBIAN/control" <<EOF
Package: nightlock
Version: $VERSION
Architecture: amd64
Maintainer: Nightlock <nightlock@users.noreply.github.com>
Installed-Size: $INSTALLED_SIZE
Depends: libc6, libstdc++6, libgcc-s1, libgl1, libegl1, libfontconfig1, libfreetype6, libdbus-1-3, libx11-6, libx11-xcb1, libxcb1, libxcb-cursor0, libxcb-icccm4, libxcb-image0, libxcb-keysyms1, libxcb-randr0, libxcb-render-util0, libxcb-render0, libxcb-shape0, libxcb-shm0, libxcb-sync1, libxcb-xfixes0, libxcb-xkb1, libxkbcommon0, libxkbcommon-x11-0, libwayland-client0, libwayland-cursor0, libwayland-egl1
Section: utils
Priority: optional
Homepage: https://github.com/rodukov/nightlock
Description: Encrypted password vault
 Nightlock is a desktop password manager with an encrypted vault
 (XChaCha20-Poly1305, Argon2id) and a matching command-line tool.
EOF

dpkg-deb --build --root-owner-group -Zxz "$STAGE" "$OUT_DEB"
echo "built: $OUT_DEB"
