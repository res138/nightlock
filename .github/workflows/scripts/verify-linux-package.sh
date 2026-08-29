#!/bin/bash
# Verify the final Debian package locally and inside a clean Ubuntu baseline.
set -euo pipefail

DEB_PATH="$1"
EXPECTED_VERSION="$2"

[[ "$EXPECTED_VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || {
    echo "error: invalid expected version: $EXPECTED_VERSION" >&2
    exit 1
}

DEB_PATH="$(cd "$(dirname "$DEB_PATH")" && pwd)/$(basename "$DEB_PATH")"
[ -f "$DEB_PATH" ] || { echo "error: Debian package missing: $DEB_PATH" >&2; exit 1; }

PACKAGE_NAME="$(dpkg-deb -f "$DEB_PATH" Package)"
PACKAGE_VERSION="$(dpkg-deb -f "$DEB_PATH" Version)"
PACKAGE_ARCH="$(dpkg-deb -f "$DEB_PATH" Architecture)"
PACKAGE_DEPENDS="$(dpkg-deb -f "$DEB_PATH" Depends)"
[ "$PACKAGE_NAME" = 'nightlock' ] || {
    echo "error: unexpected Debian package name: $PACKAGE_NAME" >&2
    exit 1
}
[ "$PACKAGE_VERSION" = "$EXPECTED_VERSION" ] || {
    echo "error: Debian version $PACKAGE_VERSION != $EXPECTED_VERSION" >&2
    exit 1
}
[ "$PACKAGE_ARCH" = 'amd64' ] || {
    echo "error: Debian package architecture must be amd64, got $PACKAGE_ARCH" >&2
    exit 1
}
for required_dependency in libssl3 ca-certificates; do
    grep -Eq "(^|, )${required_dependency}([[:space:]]|,|$)" \
        <<<"$PACKAGE_DEPENDS" || {
        echo "error: Debian package is missing $required_dependency in Depends" >&2
        exit 1
    }
done

WORK_ROOT="$(mktemp -d "${RUNNER_TEMP:-/tmp}/nightlock-linux-package-smoke.XXXXXX")"
cleanup() {
    local status=$?
    rm -rf "$WORK_ROOT"
    trap - EXIT
    exit "$status"
}
trap cleanup EXIT
EXTRACT_ROOT="$WORK_ROOT/extracted"
mkdir -p "$EXTRACT_ROOT"
dpkg-deb -x "$DEB_PATH" "$EXTRACT_ROOT"

APP_ROOT="$EXTRACT_ROOT/usr/lib/nightlock"
GUI="$APP_ROOT/nightlock-desktop"
CLI="$EXTRACT_ROOT/usr/bin/nightlock"
required_paths=(
    "$GUI"
    "$CLI"
    "$APP_ROOT/qt.conf"
    "$APP_ROOT/lib/libQt6Core.so.6"
    "$APP_ROOT/lib/libQt6Gui.so.6"
    "$APP_ROOT/lib/libQt6Network.so.6"
    "$APP_ROOT/lib/libQt6Widgets.so.6"
    "$APP_ROOT/lib/libQt6Svg.so.6"
    "$APP_ROOT/plugins/platforms/libqxcb.so"
    "$APP_ROOT/plugins/platforms/libqoffscreen.so"
    "$APP_ROOT/plugins/tls/libqopensslbackend.so"
    "$EXTRACT_ROOT/usr/bin/nightlock-desktop"
)
for required_path in "${required_paths[@]}"; do
    [ -e "$required_path" ] || {
        echo "error: required packaged runtime path missing: $required_path" >&2
        exit 1
    }
done
for resource_directory in \
    "$EXTRACT_ROOT/usr/share/nightlock/icons" \
    "$EXTRACT_ROOT/usr/share/nightlock/fonts"; do
    [ -d "$resource_directory" ] || {
        echo "error: bundled resource directory missing: $resource_directory" >&2
        exit 1
    }
done

GUI_RPATH="$(patchelf --print-rpath "$GUI")"
[ "$GUI_RPATH" = '$ORIGIN/lib' ] || {
    echo "error: GUI RPATH must be \$ORIGIN/lib, got $GUI_RPATH" >&2
    exit 1
}
for plugin in \
    "$APP_ROOT/plugins/platforms/libqxcb.so" \
    "$APP_ROOT/plugins/platforms/libqoffscreen.so" \
    "$APP_ROOT/plugins/tls/libqopensslbackend.so"; do
    PLUGIN_RPATH="$(patchelf --print-rpath "$plugin")"
    [ "$PLUGIN_RPATH" = '$ORIGIN/../../lib' ] || {
        echo "error: plugin RPATH must be \$ORIGIN/../../lib, got $PLUGIN_RPATH ($plugin)" >&2
        exit 1
    }
done

if grep -Eiq '(QT_ROOT_DIR|/home/runner/|/_work/nightlock|/opt/Qt)' "$APP_ROOT/qt.conf"; then
    echo 'error: qt.conf contains a build-machine path' >&2
    exit 1
fi

# Check every ELF object in the payload. In particular, all Qt and ICU
# dependencies must resolve to the private package directory, never to the Qt
# SDK installed on the GitHub runner. ldd preserves the spelling of RUNPATH
# expansions (for example plugins/platforms/../../lib), so compare canonical
# paths instead of raw string prefixes.
PRIVATE_LIB_ROOT="$(readlink -f -- "$APP_ROOT/lib")"
while IFS= read -r -d '' object; do
    if ! file -b "$object" | grep -q '^ELF '; then
        continue
    fi
    LDD_OUTPUT="$(env -u LD_LIBRARY_PATH ldd "$object" 2>&1)" || {
        echo "$LDD_OUTPUT" >&2
        echo "error: ldd failed for $object" >&2
        exit 1
    }
    if grep -q 'not found' <<< "$LDD_OUTPUT"; then
        echo "$LDD_OUTPUT" >&2
        echo "error: unresolved shared library in $object" >&2
        exit 1
    fi
    while IFS=$'\t' read -r dependency resolved; do
        [ -n "$dependency" ] || continue
        resolved="$(readlink -f -- "$resolved")" || {
            echo "error: cannot canonicalize $dependency from $object" >&2
            exit 1
        }
        case "$resolved" in
            "$PRIVATE_LIB_ROOT"/*)
                ;;
            *)
                echo "error: $object resolves $dependency outside the private runtime: $resolved" >&2
                exit 1
                ;;
        esac
    done < <(
        awk '/lib(Qt6|icu)[^[:space:]]*[[:space:]]+=>/ { print $1 "\t" $3 }' \
            <<< "$LDD_OUTPUT"
    )
done < <(find "$EXTRACT_ROOT/usr" -type f -print0)

CLI_OUTPUT="$(env -i HOME="$WORK_ROOT/home" PATH='/usr/bin:/bin' "$CLI" --version 2>&1)"
[ "$CLI_OUTPUT" = "nightlock $EXPECTED_VERSION" ] || {
    echo "error: extracted CLI smoke failed: $CLI_OUTPUT" >&2
    exit 1
}

# apt installs only the dependencies declared by the .deb (plus the Xvfb test
# harness). This catches missing Depends entries and tests the GUI without any
# host Qt installation leaking into the process.
docker run --rm --platform linux/amd64 \
    --env "NIGHTLOCK_EXPECTED_VERSION=$EXPECTED_VERSION" \
    --mount "type=bind,src=$DEB_PATH,dst=/tmp/nightlock.deb,readonly" \
    ubuntu:22.04 \
    bash -euo pipefail -c '
        export DEBIAN_FRONTEND=noninteractive
        apt-get update
        apt-get install -y /tmp/nightlock.deb xvfb xauth file

        test "$(dpkg-query -W -f="\${Version}" nightlock)" = "$NIGHTLOCK_EXPECTED_VERSION"
        test "$(nightlock --version)" = "nightlock $NIGHTLOCK_EXPECTED_VERSION"

        private_lib_root="$(readlink -f -- /usr/lib/nightlock/lib)"
        while IFS= read -r -d "" object; do
            if ! file -b "$object" | grep -q "^ELF "; then
                continue
            fi
            output="$(env -u LD_LIBRARY_PATH ldd "$object" 2>&1)"
            if grep -q "not found" <<< "$output"; then
                echo "$output" >&2
                echo "unresolved dependency in installed object: $object" >&2
                exit 1
            fi
            while IFS="$(printf "\t")" read -r dependency resolved; do
                test -n "$dependency" || continue
                resolved="$(readlink -f -- "$resolved")" || {
                    echo "cannot canonicalize $dependency from $object" >&2
                    exit 1
                }
                case "$resolved" in
                    "$private_lib_root"/*)
                        ;;
                    *)
                        echo "$object resolves $dependency outside the private runtime: $resolved" >&2
                        exit 1
                        ;;
                esac
            done < <(
                awk '\''/lib(Qt6|icu)[^[:space:]]*[[:space:]]+=>/ {
                    print $1 "\t" $3
                }'\'' <<< "$output"
            )
        done < <(find /usr/lib/nightlock /usr/bin/nightlock -type f -print0)

        mkdir -p /tmp/nightlock-home
        HOME=/tmp/nightlock-home \
        NIGHTLOCK_TEST_TLS_RUNTIME=1 \
        xvfb-run -a -s "-screen 0 1280x800x24" /usr/bin/nightlock-desktop \
            2>&1 | tee /tmp/nightlock-tls.log
        grep -Fq "Nightlock TLS runtime check passed: openssl" \
            /tmp/nightlock-tls.log

        HOME=/tmp/nightlock-home \
        NIGHTLOCK_DEMO=1 \
        NIGHTLOCK_SCREENSHOT=/tmp/nightlock-gui.png \
        NIGHTLOCK_SCREENSHOT_DELAY=1000 \
        xvfb-run -a -s "-screen 0 1280x800x24" /usr/bin/nightlock-desktop
        test -s /tmp/nightlock-gui.png
    '

echo "Linux package smoke test passed for Nightlock $EXPECTED_VERSION on clean Ubuntu 22.04."
