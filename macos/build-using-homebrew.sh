#!/bin/sh

# Authors: Boris Pek
# License: Public Domain
# Created: 2018-08-21
# Updated: 2022-04-05
# Version: N/A
#
# Description: script for building of app bundles for macOS
# Currently it is used for testing builds on Travis CI and for producing
# official builds of program which are hosted on SourceForge.
#
# Build dependencies and useful tools:
# export HOMEBREW_NO_BOTTLE_SOURCE_FALLBACK=1
# brew install ccache coreutils cmake
# brew install aspell jsoncpp libidn2 lua miniupnpc qt@5
#
# Additional tools:
# brew install wget htop

set -e

resolve_path() {
    if command -v grealpath >/dev/null 2>&1; then
        grealpath "$1"
    elif command -v realpath >/dev/null 2>&1; then
        realpath "$1"
    else
        case "$1" in
            /*) printf '%s\n' "$1" ;;
            *) printf '%s/%s\n' "$(pwd)" "$1" ;;
        esac
    fi
}

if [ -z "${HOMEBREW}" ]; then
    if command -v brew >/dev/null 2>&1; then
        HOMEBREW="$(brew --prefix)"
    elif [ -d /opt/homebrew ]; then
        HOMEBREW="/opt/homebrew"
    else
        HOMEBREW="/usr/local"
    fi
fi
[ -z "${DEPS_PREFIX}" ] && DEPS_PREFIX="${HOMEBREW}"
[ -z "${OSX_ARCHITECTURES}" ] && OSX_ARCHITECTURES="$(uname -m)"
if [ -z "${OSX_DEPLOYMENT_TARGET}" ]; then
    if [ "${OSX_ARCHITECTURES}" = "arm64" ]; then
        OSX_DEPLOYMENT_TARGET="14.0"
    else
        OSX_DEPLOYMENT_TARGET="10.15"
    fi
fi
export DEPS_PREFIX
export OSX_ARCHITECTURES
export OSX_DEPLOYMENT_TARGET

PATH="${DEPS_PREFIX}/bin:${DEPS_PREFIX}/opt/qtbase/bin:${HOMEBREW}/bin:${PATH}"
PATH="${HOMEBREW}/opt/ccache/libexec:${PATH}"
SCRIPT_PATH="$(resolve_path "$0")"
CUR_DIR="$(cd "$(dirname "${SCRIPT_PATH}")" && pwd -P)"
MAIN_DIR="$(cd "${CUR_DIR}/.." && pwd -P)"
TOOLCHAIN_FILE="${CUR_DIR}/homebrew-toolchain.cmake"
MIN_OS_CHECK="${CUR_DIR}/check-macos-min-version.sh"
BUNDLE_PATH_AUDIT="${CUR_DIR}/check-bundle-external-links.sh"
SELF_CONTAIN_FIX="${CUR_DIR}/fix-bundle-self-containment.sh"

BUILD_OPTIONS="-DCMAKE_BUILD_TYPE=Release \
               -DEISKALT_MACOS_DEPS_PREFIX=${DEPS_PREFIX} \
               -DUSE_QT6=ON \
               -DUSE_QT_SQLITE=ON \
               -DUSE_MINIUPNP=ON \
               -DUSE_ASPELL=ON \
               -DUSE_PROGRESS_BARS=OFF \
               -DNO_UI_DAEMON=OFF \
               -DJSONRPC_DAEMON=OFF \
               -DPERL_REGEX=ON \
               -DLUA_SCRIPT=ON \
               -DWITH_SOUNDS=ON \
               -DWITH_LUASCRIPTS=ON \
               -DLOCAL_ASPELL_DATA=OFF \
               -DLOCAL_JSONCPP=OFF"

mkdir -p "${MAIN_DIR}/builddir"
cd "${MAIN_DIR}/builddir"

which nproc > /dev/null && JOBS=$(nproc) || JOBS=4

cmake .. -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" ${BUILD_OPTIONS} ${@}
cmake --build . --target all -- -j ${JOBS}

BUILD_APP="${MAIN_DIR}/builddir/eiskaltdcpp-qt/EiskaltDC++.app"
if [ -d "${BUILD_APP}" ]; then
    "${SELF_CONTAIN_FIX}" "${BUILD_APP}"
fi

cpack -G DragNDrop
STAGED_APP="$(find "${MAIN_DIR}/builddir/_CPack_Packages/Darwin/DragNDrop" -maxdepth 3 -name '*.app' | head -n 1)"
"${SELF_CONTAIN_FIX}" "${STAGED_APP}"
"${MIN_OS_CHECK}" "${STAGED_APP}" "${OSX_DEPLOYMENT_TARGET}"
"${BUNDLE_PATH_AUDIT}" "${STAGED_APP}" "${DEPS_PREFIX}" "${HOMEBREW}"
cp -a EiskaltDC++*.dmg "${MAIN_DIR}/../"

echo
echo "App bundle is built successfully! See:"
echo "$(cd "${MAIN_DIR}/.." && pwd -P)/$(ls EiskaltDC++*.dmg | sort -V | tail -n1)"
echo
