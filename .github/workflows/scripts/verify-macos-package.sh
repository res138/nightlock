#!/bin/bash
# Verify the final DMG from an isolated copy, with no Qt SDK paths available.
set -euo pipefail

DMG_PATH="$1"
EXPECTED_VERSION="$2"
EXPECTED_ARCHITECTURES="${3:-$(uname -m)}"

case "$EXPECTED_VERSION" in
    ''|*[!0-9.]*)
        echo "error: invalid expected version: $EXPECTED_VERSION" >&2
        exit 1
        ;;
esac

DMG_PATH="$(cd "$(dirname "$DMG_PATH")" && pwd)/$(basename "$DMG_PATH")"
[ -f "$DMG_PATH" ] || { echo "error: DMG missing: $DMG_PATH" >&2; exit 1; }

WORK_ROOT="$(mktemp -d "${RUNNER_TEMP:-/tmp}/nightlock-macos-package-smoke.XXXXXX")"
MOUNT_POINT="$WORK_ROOT/mount"
APP_COPY="$WORK_ROOT/Nightlock.app"
MOUNTED=0
mkdir -p "$MOUNT_POINT"

cleanup() {
    local status=$?
    if [ "$MOUNTED" -eq 1 ]; then
        hdiutil detach "$MOUNT_POINT" -quiet >/dev/null 2>&1 ||
            hdiutil detach "$MOUNT_POINT" -force -quiet >/dev/null 2>&1 || true
    fi
    rm -rf "$WORK_ROOT"
    trap - EXIT
    exit "$status"
}
trap cleanup EXIT

if ! hdiutil attach "$DMG_PATH" -nobrowse -readonly \
        -mountpoint "$MOUNT_POINT" -quiet; then
    echo "error: could not mount DMG: $DMG_PATH" >&2
    exit 1
fi
MOUNTED=1
[ -d "$MOUNT_POINT/Nightlock.app" ] || {
    echo "error: Nightlock.app is missing from the mounted DMG" >&2
    exit 1
}
ditto "$MOUNT_POINT/Nightlock.app" "$APP_COPY"
hdiutil detach "$MOUNT_POINT" -quiet
MOUNTED=0

GUI="$APP_COPY/Contents/MacOS/Nightlock"
CLI="$APP_COPY/Contents/Helpers/nightlock"
INFO_PLIST="$APP_COPY/Contents/Info.plist"

required_files=(
    "$GUI"
    "$CLI"
    "$INFO_PLIST"
    "$APP_COPY/Contents/Frameworks/QtCore.framework/Versions/A/QtCore"
    "$APP_COPY/Contents/Frameworks/QtGui.framework/Versions/A/QtGui"
    "$APP_COPY/Contents/Frameworks/QtWidgets.framework/Versions/A/QtWidgets"
    "$APP_COPY/Contents/Frameworks/QtSvg.framework/Versions/A/QtSvg"
    "$APP_COPY/Contents/PlugIns/platforms/libqcocoa.dylib"
)
for required_file in "${required_files[@]}"; do
    [ -f "$required_file" ] || {
        echo "error: required bundled runtime file missing: $required_file" >&2
        exit 1
    }
done

for resource_directory in icons fonts; do
    [ -d "$APP_COPY/Contents/Resources/$resource_directory" ] || {
        echo "error: bundled resource directory missing: $resource_directory" >&2
        exit 1
    }
done

PDF_PLUGIN="$APP_COPY/Contents/PlugIns/imageformats/libqpdf.dylib"
if [ -e "$PDF_PLUGIN" ]; then
    echo 'error: unused qpdf plugin was bundled; its QtPdf framework is not deployed' >&2
    exit 1
fi

BUNDLE_VERSION="$(plutil -extract CFBundleShortVersionString raw -o - "$INFO_PLIST")"
[ "$BUNDLE_VERSION" = "$EXPECTED_VERSION" ] || {
    echo "error: bundle version $BUNDLE_VERSION != $EXPECTED_VERSION" >&2
    exit 1
}
MINIMUM_SYSTEM_VERSION="$(plutil -extract LSMinimumSystemVersion raw -o - "$INFO_PLIST")"
[ "$MINIMUM_SYSTEM_VERSION" = '13.0' ] || {
    echo "error: LSMinimumSystemVersion must be 13.0, got $MINIMUM_SYSTEM_VERSION" >&2
    exit 1
}

assert_architectures() {
    local binary="$1"
    local actual_architectures
    actual_architectures="$(lipo -archs "$binary")"
    for expected_architecture in $EXPECTED_ARCHITECTURES; do
        case " $actual_architectures " in
            *" $expected_architecture "*) ;;
            *)
                echo "error: $binary lacks $expected_architecture slice ($actual_architectures)" >&2
                exit 1
                ;;
        esac
    done
}

assert_minimum_system_version() {
    local binary="$1"
    local versions
    versions="$(otool -l "$binary" | awk '$1 == "minos" { print $2 }')"
    [ -n "$versions" ] || {
        echo "error: no LC_BUILD_VERSION minos found in $binary" >&2
        exit 1
    }
    while IFS= read -r version; do
        [ "$version" = '13.0' ] || {
            echo "error: $binary has minos $version instead of 13.0" >&2
            exit 1
        }
    done <<< "$versions"
}

assert_architectures "$GUI"
assert_architectures "$CLI"
assert_minimum_system_version "$GUI"
assert_minimum_system_version "$CLI"

codesign --verify --deep --strict --verbose=2 "$APP_COPY"

# Every non-system absolute load command is a build-machine leak. Resolve the
# relative load commands as well, so a plugin that names an absent framework
# cannot pass merely because its dependency starts with @rpath.
while IFS= read -r -d '' candidate; do
    if ! file -b "$candidate" | grep -q 'Mach-O'; then
        continue
    fi

    while IFS= read -r version; do
        if ! awk -v version="$version" 'BEGIN {
            split(version, fields, ".")
            major = fields[1] + 0
            minor = fields[2] + 0
            exit (major < 13 || (major == 13 && minor <= 0)) ? 0 : 1
        }'; then
            echo "error: $candidate requires macOS $version (target baseline is 13.0)" >&2
            exit 1
        fi
    done < <(
        otool -l "$candidate" |
            awk '
                $2 == "LC_VERSION_MIN_MACOSX" { legacy_min = 1; next }
                legacy_min && $1 == "version" { print $2; legacy_min = 0; next }
                $1 == "minos" { print $2 }
            '
    )

    INSTALL_ID="$(
        otool -D "$candidate" 2>/dev/null | awk '
            /:$/ { next }
            NF { sub(/^[[:space:]]+/, ""); print; exit }
        ' || true
    )"
    while IFS= read -r dependency; do
        [ "$dependency" = "$INSTALL_ID" ] && continue
        case "$dependency" in
            @rpath/*)
                resolved="$APP_COPY/Contents/Frameworks/${dependency#@rpath/}"
                [ -e "$resolved" ] || {
                    echo "error: unresolved @rpath dependency in $candidate: $dependency" >&2
                    exit 1
                }
                ;;
            @loader_path/*)
                resolved="$(dirname "$candidate")/${dependency#@loader_path/}"
                [ -e "$resolved" ] || {
                    echo "error: unresolved @loader_path dependency in $candidate: $dependency" >&2
                    exit 1
                }
                ;;
            @executable_path/*)
                resolved="$APP_COPY/Contents/MacOS/${dependency#@executable_path/}"
                [ -e "$resolved" ] || {
                    echo "error: unresolved @executable_path dependency in $candidate: $dependency" >&2
                    exit 1
                }
                ;;
            /System/Library/*|/usr/lib/*|/Library/Apple/System/Library/*)
                ;;
            *)
                echo "error: non-portable Mach-O dependency in $candidate: $dependency" >&2
                exit 1
                ;;
        esac
    done < <(
        otool -L "$candidate" |
            sed -n -E \
                's/^[[:space:]]*(.*)[[:space:]]+\(compatibility version.*$/\1/p'
    )
done < <(find "$APP_COPY" -type f -print0)

SMOKE_HOME="$WORK_ROOT/home"
SMOKE_TMP="$WORK_ROOT/tmp"
SCREENSHOT="$WORK_ROOT/nightlock-gui.png"
GUI_LOG="$WORK_ROOT/gui.log"
mkdir -p "$SMOKE_HOME" "$SMOKE_TMP"

CLI_OUTPUT="$({
    env -i \
        HOME="$SMOKE_HOME" \
        TMPDIR="$SMOKE_TMP" \
        PATH='/usr/bin:/bin:/usr/sbin:/sbin' \
        LANG='en_US.UTF-8' \
        "$CLI" --version
} 2>&1)"
[ "$CLI_OUTPUT" = "nightlock $EXPECTED_VERSION" ] || {
    echo "error: installed CLI smoke failed: $CLI_OUTPUT" >&2
    exit 1
}

env -i \
    HOME="$SMOKE_HOME" \
    TMPDIR="$SMOKE_TMP" \
    PATH='/usr/bin:/bin:/usr/sbin:/sbin' \
    LANG='en_US.UTF-8' \
    NIGHTLOCK_DEMO=1 \
    NIGHTLOCK_SCREENSHOT="$SCREENSHOT" \
    NIGHTLOCK_SCREENSHOT_DELAY=1000 \
    "$GUI" >"$GUI_LOG" 2>&1 &
GUI_PID=$!

for _ in $(seq 1 60); do
    if ! kill -0 "$GUI_PID" 2>/dev/null; then
        break
    fi
    sleep 0.5
done

if kill -0 "$GUI_PID" 2>/dev/null; then
    kill "$GUI_PID" 2>/dev/null || true
    wait "$GUI_PID" 2>/dev/null || true
    sed -n '1,240p' "$GUI_LOG" >&2
    echo 'error: packaged GUI did not finish its smoke run within 30 seconds' >&2
    exit 1
fi

set +e
wait "$GUI_PID"
GUI_STATUS=$?
set -e
if [ "$GUI_STATUS" -ne 0 ]; then
    sed -n '1,240p' "$GUI_LOG" >&2
    echo "error: packaged GUI exited with status $GUI_STATUS" >&2
    exit 1
fi
[ -s "$SCREENSHOT" ] || {
    sed -n '1,240p' "$GUI_LOG" >&2
    echo 'error: packaged GUI did not produce a smoke screenshot' >&2
    exit 1
}

echo "macOS package smoke test passed for Nightlock $EXPECTED_VERSION ($EXPECTED_ARCHITECTURES)."
