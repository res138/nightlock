#!/bin/bash
# Builds a self-contained release DMG from an installed tree.
#
#   packaging/macos/make_dmg.sh <install-dir> <out.dmg>
#
# <install-dir> is the `cmake --install` prefix holding Nightlock.app.
# Qt's install deploy step supplies the frameworks/plugins; this script
# closes helper-library dependencies, validates the result and signs it.
set -euo pipefail

usage() {
    echo "usage: $0 <install-dir> <out.dmg>" >&2
}

die() {
    echo "error: $*" >&2
    exit 1
}

require_command() {
    command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"
}

if [ "$#" -ne 2 ]; then
    usage
    exit 2
fi

INSTALL_DIR="${1%/}"
OUT_DMG="$2"
APP="$INSTALL_DIR/Nightlock.app"

[ -n "$INSTALL_DIR" ] || die "install-dir must not be empty"
[ -d "$APP" ] || die "$APP not found (run cmake --install first)"
for tool in codesign ditto file hdiutil install_name_tool lipo otool plutil xattr; do
    require_command "$tool"
done

STAGE="$(mktemp -d)"
cleanup() { rm -rf -- "$STAGE"; }
trap cleanup EXIT

STAGED_APP="$STAGE/Nightlock.app"
ditto "$APP" "$STAGED_APP"

CONTENTS="$STAGED_APP/Contents"
FRAMEWORKS="$CONTENTS/Frameworks"
PLUGINS="$CONTENTS/PlugIns"
RESOURCES="$CONTENTS/Resources"
MAIN="$CONTENTS/MacOS/Nightlock"
CLI="$CONTENTS/Helpers/nightlock"
INFO_PLIST="$CONTENTS/Info.plist"

[ -x "$MAIN" ] || die "GUI executable is missing: $MAIN"
[ -x "$CLI" ] || die "CLI executable is missing: $CLI"
[ -f "$INFO_PLIST" ] || die "Info.plist is missing"
[ -d "$RESOURCES/icons" ] || die "installed icon resources are missing"
[ -f "$RESOURCES/qt.conf" ] || die "Qt deployment did not install qt.conf"

REQUIRED_QT_FILES=(
    Frameworks/QtCore.framework/Versions/A/QtCore
    Frameworks/QtGui.framework/Versions/A/QtGui
    Frameworks/QtNetwork.framework/Versions/A/QtNetwork
    Frameworks/QtWidgets.framework/Versions/A/QtWidgets
    Frameworks/QtSvg.framework/Versions/A/QtSvg
    PlugIns/platforms/libqcocoa.dylib
    PlugIns/styles/libqmacstyle.dylib
    PlugIns/tls/libqsecuretransportbackend.dylib
    PlugIns/imageformats/libqico.dylib
    PlugIns/imageformats/libqjpeg.dylib
    PlugIns/imageformats/libqsvg.dylib
    PlugIns/iconengines/libqsvgicon.dylib
)
for relative in "${REQUIRED_QT_FILES[@]}"; do
    [ -f "$CONTENTS/$relative" ] || die "required deployed Qt file missing: $relative"
done

# Some Qt installations expose optional plugins even when their addon
# frameworks were not installed (qpdf/QtPdf and virtual keyboard are
# common examples). Nightlock needs only Cocoa, the native style,
# SecureTransport for HTTPS update checks and its ICO/JPEG/SVG renderers.
# Keeping an explicit runtime allowlist prevents an unrelated builder plugin
# from making a clean Mac fail or silently changing the TLS implementation.
while IFS= read -r -d '' plugin; do
    relative="${plugin#"$PLUGINS/"}"
    case "$relative" in
        platforms/libqcocoa.dylib|\
        styles/libqmacstyle.dylib|\
        tls/libqsecuretransportbackend.dylib|\
        imageformats/libqico.dylib|\
        imageformats/libqjpeg.dylib|\
        imageformats/libqsvg.dylib|\
        iconengines/libqsvgicon.dylib)
            ;;
        *)
            rm -f -- "$plugin"
            ;;
    esac
done < <(find "$PLUGINS" -type f -name '*.dylib' -print0)

# Enumerate Mach-O payloads only. Resources include thousands of icon
# files and should not be sent through otool/lipo.
macho_files() {
    find "$CONTENTS/MacOS" "$CONTENTS/Helpers" "$FRAMEWORKS" "$PLUGINS" \
        -type f -print0 | while IFS= read -r -d '' candidate; do
        if file -b "$candidate" | grep -q 'Mach-O'; then
            printf '%s\0' "$candidate"
        fi
    done
}

dependencies() {
    # Fat binaries add unindented per-architecture headers such as
    #   /path/QtGui (architecture arm64):
    # between their dependency groups.  Only actual load-command rows carry
    # the "(compatibility version ...)" suffix, so select those explicitly
    # instead of treating every line after the first as a dependency.
    otool -L "$1" | sed -n -E \
        's/^[[:space:]]*(.*)[[:space:]]+\(compatibility version.*$/\1/p' | \
        awk '!seen[$0]++'
}

dylib_id() {
    # Thin output starts with "file:", while fat output repeats
    # "file (architecture ...):" before every slice.  Install names are the
    # only non-empty rows that do not end in a header colon.
    otool -D "$1" 2>/dev/null | awk '
        /:$/ { next }
        NF { sub(/^[[:space:]]+/, ""); print; exit }
    '
}

# macdeployqt processes the GUI but not Contents/Helpers/nightlock.
# If a build used system libsodium, the helper therefore retained an
# absolute Homebrew/MacPorts path and failed on a clean Mac. Close all
# remaining non-system dylib references and rewrite them to the app's
# Frameworks directory. Missing Qt frameworks are never copied as one
# loose binary; that would create an invalid framework bundle.
copied=1
while [ "$copied" -eq 1 ]; do
    copied=0
    while IFS= read -r -d '' object; do
        object_id="$(dylib_id "$object")"
        while IFS= read -r dependency; do
            [ -n "$dependency" ] || continue
            [ "$dependency" = "$object_id" ] && continue
            case "$dependency" in
                /System/Library/*|/Library/Apple/System/Library/*|/usr/lib/*)
                    continue
                    ;;
                @*)
                    continue
                    ;;
                /*.framework/*)
                    framework_relative="$(sed -E 's|^.*/([^/]+\.framework/.*)$|\1|' \
                        <<<"$dependency")"
                    bundled_framework="$FRAMEWORKS/$framework_relative"
                    [ -f "$bundled_framework" ] || \
                        die "unbundled framework dependency in $object: $dependency"
                    install_name_tool -change "$dependency" \
                        "@executable_path/../Frameworks/$framework_relative" "$object"
                    ;;
                /*)
                    [ -f "$dependency" ] || \
                        die "external dependency does not exist: $dependency"
                    base="$(basename "$dependency")"
                    destination="$FRAMEWORKS/$base"
                    if [ ! -e "$destination" ]; then
                        cp -pL "$dependency" "$destination"
                        install_name_tool -id \
                            "@executable_path/../Frameworks/$base" "$destination"
                        copied=1
                    fi
                    install_name_tool -change "$dependency" \
                        "@executable_path/../Frameworks/$base" "$object"
                    ;;
                *)
                    die "unsupported Mach-O dependency in $object: $dependency"
                    ;;
            esac
        done < <(dependencies "$object")
    done < <(macho_files)
done

# Verify that every relocatable install name has a real target in the
# bundle and that no build-machine absolute path survived. The two
# executables both live one directory below Contents, so their
# @executable_path/../Frameworks references resolve identically.
dependency_errors=0
while IFS= read -r -d '' object; do
    object_id="$(dylib_id "$object")"
    while IFS= read -r dependency; do
        [ -n "$dependency" ] || continue
        [ "$dependency" = "$object_id" ] && continue
        case "$dependency" in
            /System/Library/*|/Library/Apple/System/Library/*|/usr/lib/*)
                ;;
            @executable_path/*)
                relative="${dependency#@executable_path/}"
                if [ ! -e "$CONTENTS/MacOS/$relative" ] && \
                   [ ! -e "$CONTENTS/Helpers/$relative" ]; then
                    echo "error: unresolved install name in $object: $dependency" >&2
                    dependency_errors=1
                fi
                ;;
            @loader_path/*)
                relative="${dependency#@loader_path/}"
                if [ ! -e "$(dirname "$object")/$relative" ]; then
                    echo "error: unresolved install name in $object: $dependency" >&2
                    dependency_errors=1
                fi
                ;;
            @rpath/*)
                relative="${dependency#@rpath/}"
                if [ ! -e "$FRAMEWORKS/$relative" ]; then
                    echo "error: unresolved install name in $object: $dependency" >&2
                    dependency_errors=1
                fi
                ;;
            *)
                echo "error: non-system absolute install name in $object: $dependency" >&2
                dependency_errors=1
                ;;
        esac
    done < <(dependencies "$object")
done < <(macho_files)
[ "$dependency_errors" -eq 0 ] || die "Mach-O dependency verification failed"

# Release artifacts are universal by default. This catches a thin
# helper/plugin/framework which would make only one CPU architecture
# crash at launch. Developers can explicitly opt into a thin local DMG.
MAIN_ARCHS="$(lipo -archs "$MAIN")"
if [ "${NIGHTLOCK_ALLOW_SINGLE_ARCH:-0}" != "1" ]; then
    for required_arch in arm64 x86_64; do
        case " $MAIN_ARCHS " in
            *" $required_arch "*) ;;
            *) die "GUI is not universal (missing $required_arch): $MAIN_ARCHS" ;;
        esac
    done
fi
while IFS= read -r -d '' object; do
    object_archs="$(lipo -archs "$object")"
    for required_arch in $MAIN_ARCHS; do
        case " $object_archs " in
            *" $required_arch "*) ;;
            *) die "$object is missing architecture $required_arch" ;;
        esac
    done
done < <(macho_files)

# Make Info.plist advertise the highest deployment target actually
# encoded in any shipped Mach-O slice. An empty/stale value otherwise
# turns an ordinary "requires newer macOS" case into a launch failure.
DEPLOYMENT_TARGET="$({
    while IFS= read -r -d '' object; do
        otool -l "$object" | awk '
            $1 == "cmd" { legacy = ($2 == "LC_VERSION_MIN_MACOSX") }
            $1 == "minos" { print $2 }
            legacy && $1 == "version" { print $2; legacy = 0 }
        '
    done < <(macho_files)
} | awk '
    function newer(candidate, current, a, b, count_a, count_b, i) {
        count_a = split(candidate, a, ".");
        count_b = split(current, b, ".");
        for (i = 1; i <= 3; ++i) {
            if ((a[i] + 0) > (b[i] + 0)) return 1;
            if ((a[i] + 0) < (b[i] + 0)) return 0;
        }
        return 0;
    }
    max == "" || newer($1, max) { max = $1 }
    END { print max }
')"
[[ "$DEPLOYMENT_TARGET" =~ ^[0-9]+\.[0-9]+([.][0-9]+)?$ ]] || \
    die "could not determine the macOS deployment target"
if ! /usr/libexec/PlistBuddy -c \
    "Set :LSMinimumSystemVersion $DEPLOYMENT_TARGET" "$INFO_PLIST" >/dev/null 2>&1; then
    /usr/libexec/PlistBuddy -c \
        "Add :LSMinimumSystemVersion string $DEPLOYMENT_TARGET" "$INFO_PLIST"
fi
plutil -lint "$INFO_PLIST" >/dev/null

# macdeployqt/install_name_tool edits invalidate existing signatures,
# and arm64 macOS refuses malformed signatures. Use Developer ID when
# the release environment provides it; ad-hoc signing keeps local and
# public unsigned builds executable without an extra certificate.
xattr -cr "$STAGED_APP"
if [ -n "${MACOS_SIGN_IDENTITY:-}" ]; then
    codesign --force --deep --options runtime --timestamp \
        --sign "$MACOS_SIGN_IDENTITY" "$STAGED_APP"
else
    codesign --force --deep --sign - "$STAGED_APP"
fi
codesign --verify --deep --strict --verbose=2 "$STAGED_APP"

ln -s /Applications "$STAGE/Applications"

# The CLI lives inside the self-contained app; no second binary copy
# (and therefore no second dependency tree) is installed on the host.
cat > "$STAGE/Install command-line tool.txt" <<'EOF'
The nightlock command-line tool ships inside the app. To use it from
the terminal, link it into your PATH once:

  sudo mkdir -p /usr/local/bin
  sudo ln -sf "/Applications/Nightlock.app/Contents/Helpers/nightlock" /usr/local/bin/nightlock

Then `nightlock --help` works from any shell.
EOF

rm -f -- "$OUT_DMG"
hdiutil create -volname Nightlock -srcfolder "$STAGE" -ov -format UDZO "$OUT_DMG"
hdiutil verify "$OUT_DMG" >/dev/null
echo "built: $OUT_DMG"
