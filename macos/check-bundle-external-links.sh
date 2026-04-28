#!/bin/sh

set -eu

APP_PATH="${1:?usage: check-bundle-external-links.sh /path/to/App.app [vendored-prefix] [homebrew-prefix]}"
VENDORED_PREFIX="${2:-}"
HOMEBREW_PREFIX="${3:-}"

if [ ! -d "${APP_PATH}" ]; then
    echo "error: app bundle not found: ${APP_PATH}" >&2
    exit 1
fi

if ! command -v otool >/dev/null 2>&1; then
    echo "error: otool is required to audit bundle links" >&2
    exit 1
fi

FAILED=0

is_forbidden_path() {
    case "$1" in
        /System/Library/*|/usr/lib/*)
            return 1
            ;;
    esac

    case "$1" in
        @rpath/*|@loader_path/*|@executable_path/*)
            return 1
            ;;
    esac

    case "$1" in
        "${APP_PATH}"/*)
            return 1
            ;;
    esac

    if [ -n "${VENDORED_PREFIX}" ]; then
        case "$1" in
            "${VENDORED_PREFIX}"/*)
                return 0
                ;;
        esac
    fi

    if [ -n "${HOMEBREW_PREFIX}" ]; then
        case "$1" in
            "${HOMEBREW_PREFIX}"/*)
                return 0
                ;;
        esac
    fi

    case "$1" in
        /*)
            return 0
            ;;
    esac

    return 1
}

while IFS= read -r file; do
    [ -z "${file}" ] && continue

    while IFS= read -r dep; do
        [ -z "${dep}" ] && continue
        if is_forbidden_path "${dep}"; then
            rel="${file#"${APP_PATH}/"}"
            echo "error: ${rel} links to external dependency ${dep}" >&2
            FAILED=1
        fi
    done <<EOF
$(otool -L "${file}" 2>/dev/null | awk 'NR > 1 {print $1}')
EOF
done <<EOF
$(find "${APP_PATH}/Contents" \( -type f -perm -111 -o -name '*.dylib' -o -name '*.so' \) -print | sort)
EOF

if [ "${FAILED}" -ne 0 ]; then
    echo "hint: vendored release builds must not link against live prefixes like /opt/homebrew or /usr/local." >&2
    exit 1
fi

echo "bundle link audit passed for ${APP_PATH}"
