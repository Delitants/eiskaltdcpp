#!/bin/sh

set -eu

APP_PATH="${1:?usage: fix-bundle-self-containment.sh /path/to/App.app}"
ROOT_DIR="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
FRAMEWORKS_DIR="${APP_PATH}/Contents/Frameworks"
PLUGINS_DIR="${APP_PATH}/Contents/PlugIns"
PLATFORMS_DIR="${PLUGINS_DIR}/platforms"
STYLES_DIR="${PLUGINS_DIR}/styles"
RESOURCES_DIR="${APP_PATH}/Contents/Resources"

if [ ! -d "${APP_PATH}" ]; then
    echo "error: app bundle not found: ${APP_PATH}" >&2
    exit 1
fi

mkdir -p "${FRAMEWORKS_DIR}"
mkdir -p "${PLATFORMS_DIR}"
mkdir -p "${STYLES_DIR}"
mkdir -p "${RESOURCES_DIR}"

rm -rf "${RESOURCES_DIR}/emoticons"

copy_and_rewrite() {
    src="${1:?missing source dylib}"
    dest_name="${2:-$(basename "${src}")}"
    dest="${FRAMEWORKS_DIR}/${dest_name}"

    if [ ! -f "${src}" ]; then
        echo "error: dependency not found: ${src}" >&2
        exit 1
    fi

    cp -f "${src}" "${dest}"
    chmod u+w "${dest}"
    install_name_tool -id "@executable_path/../Frameworks/${dest_name}" "${dest}"
}

copy_plugin() {
    src="${1:?missing source plugin}"
    dest_dir="${2:?missing destination directory}"
    dest="${dest_dir}/$(basename "${src}")"

    if [ ! -f "${src}" ]; then
        echo "error: plugin not found: ${src}" >&2
        exit 1
    fi

    cp -f "${src}" "${dest}"
    chmod u+w "${dest}"
}

copy_and_rewrite "/opt/homebrew/opt/brotli/lib/libbrotlicommon.1.dylib"
copy_and_rewrite "/opt/homebrew/opt/little-cms2/lib/liblcms2.2.dylib"
copy_plugin "/opt/homebrew/Cellar/qtbase/6.11.0/share/qt/plugins/platforms/libqcocoa.dylib" "${PLATFORMS_DIR}"

if [ -f "/opt/homebrew/Cellar/qtbase/6.11.0/share/qt/plugins/styles/libqmacstyle.dylib" ]; then
    copy_plugin "/opt/homebrew/Cellar/qtbase/6.11.0/share/qt/plugins/styles/libqmacstyle.dylib" "${STYLES_DIR}"
fi

chmod u+w "${FRAMEWORKS_DIR}/libbrotlicommon.1.dylib" "${FRAMEWORKS_DIR}/libmng.2.dylib"
install_name_tool -change "/opt/homebrew/opt/brotli/lib/libbrotlicommon.1.dylib" "@executable_path/../Frameworks/libbrotlicommon.1.dylib" "${FRAMEWORKS_DIR}/libbrotlicommon.1.dylib"
install_name_tool -change "/opt/homebrew/opt/little-cms2/lib/liblcms2.2.dylib" "@executable_path/../Frameworks/liblcms2.2.dylib" "${FRAMEWORKS_DIR}/libmng.2.dylib"

codesign --force --deep --sign - "${APP_PATH}"

echo "bundle self-containment fix applied to ${APP_PATH}"
