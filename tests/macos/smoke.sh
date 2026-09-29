#!/bin/bash
set -euo pipefail
if [[ $# -lt 2 || $# -gt 3 || ( $# -eq 3 && "$3" != --no-torrent ) ]]; then
    echo "Usage: $0 /absolute/generated-Info.plist /absolute/new-qa-directory [--no-torrent]" >&2
    exit 64
fi
SOURCE_PLIST="$1"
QA="$2"
TORRENT=ON
if [[ "${3:-}" = --no-torrent ]]; then TORRENT=OFF; fi
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="${REPO:-$(cd "$HERE/../.." && pwd)}"
[[ "$SOURCE_PLIST" = /* && -f "$SOURCE_PLIST" && "$QA" = /* && ! -e "$QA" ]] || {
    echo 'Require an existing absolute plist path and a fresh absolute QA directory' >&2
    exit 64
}
mkdir -p "$QA"
cp "$SOURCE_PLIST" "$QA/source.Info.plist"
shasum -a 256 "$QA/source.Info.plist" "$REPO/eiskaltdcpp-qt/src/PendingOpenEvents.h" > "$QA/inputs.sha256"
cmake -S "$HERE" -B "$QA/build" -DREPO="$REPO" -DUSE_TORRENT="$TORRENT" \
    -DCMAKE_PREFIX_PATH="${CMAKE_PREFIX_PATH:-/opt/homebrew/opt/qt}" > "$QA/configure.log" 2>&1
cmake --build "$QA/build" -j 2 > "$QA/build.log" 2>&1
xcrun swiftc -module-cache-path "$QA/swift-module-cache" "$HERE/smoke.swift" -o "$QA/smoke-driver" > "$QA/swift-build.log" 2>&1
"$QA/smoke-driver" "$QA/build/native_open_receiver" "$QA/source.Info.plist" "$QA" "${@:3}" 2>&1 | tee "$QA/driver.log"
