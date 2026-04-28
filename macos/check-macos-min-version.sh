#!/bin/sh

set -eu

APP_PATH="${1:?usage: check-macos-min-version.sh /path/to/App.app [max-min-os-version]}"
MAX_MIN_OS="${2:-14.0}"

if [ ! -d "${APP_PATH}" ]; then
    echo "error: app bundle not found: ${APP_PATH}" >&2
    exit 1
fi

if ! command -v otool >/dev/null 2>&1; then
    echo "error: otool is required to audit macOS binaries" >&2
    exit 1
fi

version_le() {
    awk -v left="$1" -v right="$2" '
        function norm(v,    a, n, i, out) {
            n = split(v, a, ".")
            for (i = 1; i <= 3; ++i) {
                out = out sprintf("%04d", (i <= n ? a[i] : 0))
            }
            return out
        }
        BEGIN { exit !(norm(left) <= norm(right)) }
    '
}

extract_minos() {
    otool -l "$1" 2>/dev/null | awk '
        /LC_BUILD_VERSION/ { build = 1; next }
        build && $1 == "minos" { print $2; exit }
        /LC_VERSION_MIN_MACOSX/ { legacy = 1; next }
        legacy && $1 == "version" { print $2; exit }
    '
}

FAILED=0

while IFS= read -r file; do
    minos="$(extract_minos "${file}")"
    [ -z "${minos}" ] && continue

    if ! version_le "${minos}" "${MAX_MIN_OS}"; then
        rel="${file#"${APP_PATH}/"}"
        echo "error: ${rel} requires macOS ${minos}, which is newer than the requested ${MAX_MIN_OS}" >&2
        FAILED=1
    fi
done <<EOF
$(find "${APP_PATH}/Contents" \( -type f -perm -111 -o -name '*.dylib' \) -print | sort)
EOF

if [ "${FAILED}" -ne 0 ]; then
    echo "hint: rebuild the offending Homebrew dependencies from source with OSX_DEPLOYMENT_TARGET=${MAX_MIN_OS} before packaging." >&2
    exit 1
fi

echo "macOS minimum-version audit passed for ${APP_PATH} (<= ${MAX_MIN_OS})"
