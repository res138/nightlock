#!/bin/bash
# Builds a relocatable .deb from an installed tree. Qt and every
# non-system library (notably a dynamically linked libsodium) are
# copied below /usr/lib/nightlock; only normal distro runtime
# libraries remain as package dependencies.
#
#   packaging/linux/make_deb.sh <stage-dir> <qt-dir> <version> <out.deb>
#
# <stage-dir> is the root that holds usr/ after
#   cmake --install build --prefix <stage-dir>/usr
# <qt-dir> is the Qt prefix (the directory holding lib/ and plugins/).
set -euo pipefail

usage() {
    echo "usage: $0 <stage-dir> <qt-dir> <version> <out.deb>" >&2
}

die() {
    echo "error: $*" >&2
    exit 1
}

require_command() {
    command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"
}

if [ "$#" -ne 4 ]; then
    usage
    exit 2
fi

STAGE="${1%/}"
QT_DIR="${2%/}"
VERSION="$3"
OUT_DEB="$4"

# The script intentionally replaces package-only directories below
# STAGE. Never allow an accidentally empty/root prefix to turn that
# cleanup into a host-system operation.
[ -n "$STAGE" ] && [ "$STAGE" != "/" ] || die "stage-dir must not be /"
[ -d "$STAGE/usr" ] || die "$STAGE/usr missing (run cmake --install first)"
[ -n "$QT_DIR" ] && [ -d "$QT_DIR/lib" ] || die "$QT_DIR has no lib/"
[ -d "$QT_DIR/plugins" ] || die "$QT_DIR has no plugins/"

for tool in awk cmp dpkg dpkg-deb dpkg-shlibdeps env find ldd patchelf readelf readlink strip; do
    require_command "$tool"
done
dpkg --validate-version "$VERSION" >/dev/null 2>&1 || die "invalid Debian version: $VERSION"

APP_DIR="$STAGE/usr/lib/nightlock"
BIN="$APP_DIR/nightlock-desktop"
CLI="$STAGE/usr/bin/nightlock"

[ -x "$BIN" ] || die "$BIN missing or not executable"
[ -x "$CLI" ] || die "$CLI missing or not executable"
[ -d "$STAGE/usr/share/nightlock/icons" ] || die "installed icon resources are missing"
[ -f "$STAGE/usr/share/applications/nightlock.desktop" ] || die "desktop entry is missing"
[ -f "$STAGE/usr/share/icons/hicolor/512x512/apps/nightlock.png" ] || \
    die "512px desktop icon is missing"

# Start from a clean private runtime on repeated packaging attempts.
rm -rf -- "$APP_DIR/plugins" "$APP_DIR/lib"
mkdir -p "$APP_DIR/plugins" "$APP_DIR/lib"

# The xcb plugin is the baseline Linux desktop backend. The OpenSSL backend is
# the supported HTTPS implementation for update checks. The image and icon
# plugins below are required by Nightlock's on-disk ICO/JPEG/SVG icon packs.
# Refuse to produce an installer that starts but cannot use its required
# network or rendering features.
REQUIRED_PLUGINS=(
    platforms/libqxcb.so
    platforms/libqoffscreen.so
    tls/libqopensslbackend.so
    imageformats/libqico.so
    imageformats/libqjpeg.so
    imageformats/libqsvg.so
    iconengines/libqsvgicon.so
)
for plugin in "${REQUIRED_PLUGINS[@]}"; do
    [ -f "$QT_DIR/plugins/$plugin" ] || die "required Qt plugin missing: $plugin"
done

# Copy an explicit runtime set instead of every plugin installed on the
# CI builder. In particular, qgtk3/IBus/PDF/virtual-keyboard plugins can
# pull large optional stacks or refer to addon Qt modules that are not
# installed. Nightlock's own styling and Qt file dialogs do not require
# them. Native Wayland and XCB GL integrations remain enabled whenever
# this Qt distribution provides them.
copy_plugin() {
    local relative="$1"
    local destination="$APP_DIR/plugins/$relative"
    mkdir -p "$(dirname "$destination")"
    cp -a -- "$QT_DIR/plugins/$relative" "$destination"
}

for plugin in "${REQUIRED_PLUGINS[@]}"; do
    copy_plugin "$plugin"
done

OPTIONAL_PLUGINS=(
    platforms/libqwayland-egl.so
    platforms/libqwayland-generic.so
    platforminputcontexts/libcomposeplatforminputcontextplugin.so
    platformthemes/libqxdgdesktopportal.so
)
for plugin in "${OPTIONAL_PLUGINS[@]}"; do
    if [ -f "$QT_DIR/plugins/$plugin" ]; then
        copy_plugin "$plugin"
    fi
done

copy_plugin_category() {
    local category="$1"
    local source relative
    [ -d "$QT_DIR/plugins/$category" ] || return 0
    while IFS= read -r -d '' source; do
        relative="${source#"$QT_DIR/plugins/"}"
        copy_plugin "$relative"
    done < <(find "$QT_DIR/plugins/$category" -maxdepth 1 -type f -name '*.so' -print0)
}

copy_plugin_category xcbglintegrations
if [ -f "$APP_DIR/plugins/platforms/libqwayland-egl.so" ] || \
   [ -f "$APP_DIR/plugins/platforms/libqwayland-generic.so" ]; then
    copy_plugin_category wayland-decoration-client
    copy_plugin_category wayland-graphics-integration-client
    copy_plugin_category wayland-shell-integration
fi

is_elf() {
    readelf -h "$1" >/dev/null 2>&1
}

# Emit every ELF object that must work on the target host. Libraries
# are regular files; their SONAME symlinks do not need a second pass.
elf_objects() {
    printf '%s\0%s\0' "$BIN" "$CLI"
    find "$APP_DIR/plugins" "$APP_DIR/lib" -type f -print0
}

# Print SONAME<TAB>resolved-path for dependencies reported by ldd.
# The build Qt directory is included only during discovery; final
# verification below deliberately runs without it.
dependency_lines() {
    LD_LIBRARY_PATH="$APP_DIR/lib:$QT_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        ldd "$1" 2>/dev/null | awk '
            $2 == "=>" {
                if ($3 == "not") print $1 "\tnot found";
                else print $1 "\t" $3;
            }
        '
}

# Copy a library as one real file plus its requested SONAME. This
# avoids absolute/out-of-tree symlinks and is sufficient for the ELF
# loader. Return success only when a new SONAME was added.
copy_runtime_library() {
    local soname="$1"
    local source="$2"
    local real_source real_name real_destination existing

    real_source="$(readlink -f -- "$source")"
    [ -f "$real_source" ] || die "cannot resolve runtime library: $source"
    real_name="$(basename "$real_source")"
    real_destination="$APP_DIR/lib/$real_name"

    if [ -e "$APP_DIR/lib/$soname" ]; then
        existing="$(readlink -f -- "$APP_DIR/lib/$soname")"
        cmp -s -- "$existing" "$real_source" || \
            die "different libraries provide the same SONAME: $soname"
        return 1
    fi

    if [ -e "$real_destination" ]; then
        cmp -s -- "$real_destination" "$real_source" || \
            die "runtime library filename collision: $real_name"
    else
        cp -pL -- "$real_source" "$real_destination"
    fi
    if [ "$soname" != "$real_name" ]; then
        ln -s "$real_name" "$APP_DIR/lib/$soname"
    fi
    return 0
}

# Close dependencies over the GUI, CLI, plugins and every library
# copied on a previous pass. Qt-prefix libraries and libsodium are
# always private. Other libraries outside standard distro locations
# are private too, preventing references to /opt, /usr/local or a CI
# workspace from leaking into the package.
copied=1
while [ "$copied" -eq 1 ]; do
    copied=0
    while IFS= read -r -d '' object; do
        is_elf "$object" || continue
        while IFS=$'\t' read -r soname resolved; do
            [ -n "$soname" ] || continue
            source=""
            if [ -e "$QT_DIR/lib/$soname" ]; then
                source="$QT_DIR/lib/$soname"
            elif [[ "$soname" == libsodium.so* && "$resolved" == /* ]]; then
                source="$resolved"
            elif [ "$resolved" != "not found" ]; then
                case "$resolved" in
                    /lib/*|/lib64/*|/usr/lib/*|/usr/lib64/*)
                        ;;
                    /*)
                        source="$resolved"
                        ;;
                esac
            fi

            if [ -n "$source" ] && copy_runtime_library "$soname" "$source"; then
                copied=1
            fi
        done < <(dependency_lines "$object")
    done < <(elf_objects)
done

[ -e "$APP_DIR/lib/libQt6Network.so.6" ] || \
    die "Qt Network runtime was not collected from the desktop executable"

# Every object gets an origin-relative RUNPATH. Setting it on the Qt
# libraries themselves matters: DT_RUNPATH on the GUI is not
# transitive when one bundled Qt module needs another.
patchelf --set-rpath '$ORIGIN/lib' "$BIN"
patchelf --set-rpath '$ORIGIN/../lib/nightlock/lib' "$CLI"
while IFS= read -r -d '' object; do
    is_elf "$object" || continue
    patchelf --set-rpath '$ORIGIN/../../lib' "$object"
done < <(find "$APP_DIR/plugins" -type f -print0)
while IFS= read -r -d '' object; do
    is_elf "$object" || continue
    patchelf --set-rpath '$ORIGIN' "$object"
done < <(find "$APP_DIR/lib" -type f -print0)

cat > "$APP_DIR/qt.conf" <<'EOF'
[Paths]
Prefix = .
Plugins = plugins
Libraries = lib
EOF

# Convenience launcher name in PATH alongside the CLI.
ln -sfn ../lib/nightlock/nightlock-desktop "$STAGE/usr/bin/nightlock-desktop"

# The installed artifacts must resolve using only their embedded
# RUNPATHs and normal system locations. A package referencing the Qt
# SDK or containing even one "not found" dependency is rejected.
link_errors=0
while IFS= read -r -d '' object; do
    is_elf "$object" || continue
    output="$(env -u LD_LIBRARY_PATH ldd "$object" 2>&1 || true)"
    if grep -q '=> not found' <<<"$output"; then
        echo "error: unresolved dependency in $object" >&2
        grep '=> not found' <<<"$output" >&2
        link_errors=1
    fi
    if grep -Fq "$QT_DIR" <<<"$output"; then
        echo "error: build-time Qt path leaked into $object" >&2
        grep -F "$QT_DIR" <<<"$output" >&2
        link_errors=1
    fi
done < <(elf_objects)
[ "$link_errors" -eq 0 ] || die "runtime dependency verification failed"

# Generate versioned Depends from every shipped ELF object, including
# plugins. --ignore-missing-info applies only to our bundled private
# Qt/libsodium files; public distro libraries still contribute their
# package dependencies.
DEPS_WORK="$(mktemp -d)"
cleanup() { rm -rf -- "$DEPS_WORK"; }
trap cleanup EXIT
mkdir -p "$DEPS_WORK/debian"
cat > "$DEPS_WORK/debian/control" <<'EOF'
Source: nightlock
Section: utils
Priority: optional
Maintainer: Nightlock <nightlock@users.noreply.github.com>

Package: nightlock
Architecture: any
Description: Encrypted password vault
EOF

SHLIB_ARGS=()
while IFS= read -r -d '' object; do
    is_elf "$object" || continue
    SHLIB_ARGS+=("-e$object")
done < <(elf_objects)
SHLIB_OUTPUT="$({
    cd "$DEPS_WORK"
    dpkg-shlibdeps --ignore-missing-info --warnings=0 \
        "-l$APP_DIR/lib" -O "${SHLIB_ARGS[@]}"
})"
RUNTIME_DEPENDS="$(sed -n 's/^shlibs:Depends=//p' <<<"$SHLIB_OUTPUT")"
[ -n "$RUNTIME_DEPENDS" ] || die "dpkg-shlibdeps produced no runtime dependencies"
if grep -Eqi '(^|, )(libqt6|libsodium)' <<<"$RUNTIME_DEPENDS"; then
    die "bundled library unexpectedly remained in Depends: $RUNTIME_DEPENDS"
fi

# Qt's OpenSSL TLS plugin resolves OpenSSL at runtime instead of declaring a
# normal ELF dependency, and certificate roots are data. dpkg-shlibdeps cannot
# infer either requirement, so make both explicit for the Ubuntu 22.04 target.
for required_dependency in libssl3 ca-certificates; do
    if ! grep -Eq "(^|, )${required_dependency}([[:space:]]|,|$)" \
            <<<"$RUNTIME_DEPENDS"; then
        RUNTIME_DEPENDS="$RUNTIME_DEPENDS, $required_dependency"
    fi
done

strip --strip-unneeded "$BIN" "$CLI" 2>/dev/null || true

# Debian metadata. The native architecture is used instead of a
# hard-coded amd64 value, so the same packager is valid for arm64
# builders too (the caller still supplies a matching Qt/build tree).
ARCHITECTURE="${NIGHTLOCK_DEB_ARCH:-$(dpkg --print-architecture)}"
[[ "$ARCHITECTURE" =~ ^[a-z0-9][a-z0-9-]*$ ]] || die "invalid Debian architecture: $ARCHITECTURE"

rm -rf -- "$STAGE/DEBIAN"
mkdir -p "$STAGE/DEBIAN"
INSTALLED_SIZE="$(du -sk "$STAGE/usr" | cut -f1)"
cat > "$STAGE/DEBIAN/control" <<EOF
Package: nightlock
Version: $VERSION
Architecture: $ARCHITECTURE
Maintainer: Nightlock <nightlock@users.noreply.github.com>
Installed-Size: $INSTALLED_SIZE
Depends: $RUNTIME_DEPENDS
Section: utils
Priority: optional
Homepage: https://github.com/res138/nightlock
Description: Encrypted password vault
 Nightlock is a desktop password manager with an encrypted vault
 (XChaCha20-Poly1305, Argon2id) and a matching command-line tool.
EOF

dpkg-deb --build --root-owner-group -Zxz "$STAGE" "$OUT_DEB"
dpkg-deb --info "$OUT_DEB" >/dev/null
echo "built: $OUT_DEB"
