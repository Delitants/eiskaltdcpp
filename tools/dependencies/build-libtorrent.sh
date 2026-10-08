#!/usr/bin/env bash
set -euo pipefail

repo=$(cd "$(dirname "$0")/../.." && pwd -P)
root="$repo/Work/dependencies/selected-recheck"
prefix=
archive=
openssl_root=
boost_root=
arch=$(uname -m)
floor=
jobs=2
while (($#)); do
    case "$1" in
        --help) cat <<'HELP'
Build Eiskalt's checksum-pinned libtorrent into an isolated project prefix.
Usage: build-libtorrent.sh --prefix PATH [--source-archive PATH]
       [--staging-root PATH] [--architecture arm64|x86_64]
       [--deployment-target VERSION] [--jobs 1|2]
       [--openssl-root PATH] [--boost-root PATH]
The staging root and prefix must be inside this checkout's Work directory.
Requires CMake, jq, curl, patch, tar and a compatible Boost/OpenSSL toolchain.
--boost-root is the include directory containing boost/, not a package prefix.
HELP
            exit 0 ;;
        --prefix|--source-archive|--staging-root|--architecture|--deployment-target|--jobs|--openssl-root|--boost-root)
            (($# >= 2)) || { echo "Missing value for $1" >&2; exit 2; }
            case "$1" in
                --prefix) prefix=$2 ;; --source-archive) archive=$2 ;;
                --staging-root) root=$2 ;; --architecture) arch=$2 ;;
                --deployment-target) floor=$2 ;; --jobs) jobs=$2 ;;
                --openssl-root) openssl_root=$2 ;; --boost-root) boost_root=$2 ;;
            esac
            shift 2 ;;
        *) echo "Unknown argument: $1" >&2; exit 2 ;;
    esac
done
[[ -n "$prefix" && "$jobs" =~ ^[12]$ ]] || { echo "Specify --prefix and --jobs 1 or 2" >&2; exit 2; }
[[ "$arch" == arm64 || "$arch" == x86_64 ]] || { echo "Unsupported architecture" >&2; exit 2; }
[[ -z "$floor" || "$floor" =~ ^[0-9]+(\.[0-9]+)*$ ]] || { echo "Invalid deployment target" >&2; exit 2; }
for path in "$root" "$prefix"; do
    case "/$path/" in
        *'/../'*|*'/./'*) echo "Staging paths must not contain dot components" >&2; exit 2 ;;
    esac
done

resolve() {
    if [[ -d "$1" ]]; then (cd "$1" && pwd -P)
    else local parent leaf; parent=$(dirname "$1"); leaf=$(basename "$1");
        [[ "$parent" != "$1" ]] || return 1
        printf '%s/%s\n' "$(resolve "$parent")" "$leaf"
    fi
}
root=$(resolve "$root")
prefix=$(resolve "$prefix")
work=$(resolve "$repo/Work")
[[ "$root" == "$work/"* && "$prefix" == "$root/"* ]] || {
    echo "Staging root/prefix must be inside this checkout's Work directory" >&2; exit 2;
}
manifest="$repo/dependencies/libtorrent/source.json"
patchfile="$repo/dependencies/libtorrent/patches/0001-selected-recheck.patch"
url=$(jq -er '.url' "$manifest")
expected=$(jq -er '.sha256' "$manifest")
patchexpected=$(jq -er '.patch_sha256' "$manifest")
[[ "$url" == https://github.com/arvidn/libtorrent/releases/download/v2.1.1/libtorrent-rasterbar-2.1.1.tar.gz
    && "$expected" =~ ^[0-9a-f]{64}$ && "$patchexpected" =~ ^[0-9a-f]{64}$ ]] || exit 2
sha256() { if command -v sha256sum >/dev/null; then sha256sum "$1" | cut -d ' ' -f1; else shasum -a 256 "$1" | cut -d ' ' -f1; fi; }
[[ "$(sha256 "$patchfile")" == "$patchexpected" ]] || { echo "Patch checksum mismatch" >&2; exit 1; }
mkdir -p "$root"
if [[ -z "$archive" ]]; then
    archive="$root/libtorrent-rasterbar-2.1.1.tar.gz"
    if [[ ! -f "$archive" ]]; then
        curl --fail --location --proto '=https' --proto-redir '=https' \
            --output "$archive.partial" "$url"
        [[ "$(sha256 "$archive.partial")" == "$expected" ]] || { echo "Source checksum mismatch" >&2; exit 1; }
        mv "$archive.partial" "$archive"
    fi
fi
[[ "$(sha256 "$archive")" == "$expected" ]] || { echo "Source checksum mismatch" >&2; exit 1; }
source_root="$root/source-$patchexpected"
source_dir="$source_root/libtorrent-rasterbar-2.1.1"
if [[ ! -f "$source_root/patched" ]]; then
    [[ ! -e "$source_root" ]] || { echo "Incomplete source staging; choose a fresh staging root" >&2; exit 1; }
    mkdir "$source_root"
    tar -xzf "$archive" -C "$source_root"
    patch --batch --fuzz=0 -d "$source_dir" -p1 < "$patchfile"
    printf '%s\n' "$patchexpected" > "$source_root/patched"
fi
[[ $(cat "$source_root/patched") == "$patchexpected" ]] || { echo "Source stamp mismatch" >&2; exit 1; }
args=(-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix"
    -Dbuild_tests=OFF -Dbuild_examples=OFF -Dpython-bindings=OFF)
[[ -z "$openssl_root" ]] || args+=(-DOPENSSL_ROOT_DIR="$openssl_root" -DOPENSSL_INCLUDE_DIR="$openssl_root/include")
[[ -z "$boost_root" ]] || args+=(-DBoost_NO_BOOST_CMAKE=ON -DBoost_NO_SYSTEM_PATHS=ON -DBoost_INCLUDE_DIR="$boost_root")
if [[ $(uname -s) == Darwin ]]; then
    if [[ -n "$openssl_root" ]]; then
        for library in libssl.dylib libcrypto.dylib; do
            [[ -f "$openssl_root/lib/$library" ]] || { echo "Missing selected OpenSSL library: $library" >&2; exit 2; }
        done
        # Override successful cached find_library results when the selected
        # provider changes; never pair the new headers with old libraries.
        args+=(-DOPENSSL_SSL_LIBRARY:FILEPATH="$openssl_root/lib/libssl.dylib"
            -DOPENSSL_CRYPTO_LIBRARY:FILEPATH="$openssl_root/lib/libcrypto.dylib")
    fi
    args+=(-DCMAKE_OSX_ARCHITECTURES="$arch" "-DCMAKE_PROJECT_libtorrent_INCLUDE=$repo/cmake/LibtorrentOpenSSLHeaders.cmake")
    [[ -z "$floor" ]] || args+=(-DCMAKE_OSX_DEPLOYMENT_TARGET="$floor")
fi
build="$root/build-$patchexpected-$arch"
cmake -S "$source_dir" -B "$build" "${args[@]}"
cmake --build "$build" -j "$jobs"
cmake --install "$build"
printf 'Private libtorrent installed at %s\n' "$prefix"
