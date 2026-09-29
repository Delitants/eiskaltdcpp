#!/bin/bash
set -euo pipefail
if [[ $# -lt 1 || $# -gt 2 || "$1" != /* || -e "$1" ]]; then
    echo 'Usage: metadata-rebuild.sh /absolute/new-qa-directory [generator]' >&2
    exit 64
fi
QA="$1"
GENERATOR="${2:-Xcode}"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
mkdir -p "$QA/source"
cp "$REPO/eiskaltdcpp-qt/Info.plist.in" "$QA/source/Info.plist.in"
printf 'int main() { return 0; }\n' > "$QA/source/main.cpp"
cat > "$QA/source/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.10...3.28)
project(MetadataSmoke LANGUAGES CXX)
option(USE_TORRENT "Advertise torrent files" ON)
add_executable(${PROJECT_NAME} MACOSX_BUNDLE main.cpp)
set_target_properties(${PROJECT_NAME} PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/Info.plist.in"
    XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO)
set(MACOSX_BUNDLE_GUI_IDENTIFIER "org.eiskaltdcpp.test.metadata")
set(MACOSX_BUNDLE_BUNDLE_NAME "MetadataSmoke")
set(MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0")
set(MACOSX_BUNDLE_BUNDLE_VERSION "1.0")
include("${REPO}/cmake/MacOpenHandlers.cmake")
include("${REPO}/cmake/MacBundleMetadata.cmake")
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND /usr/bin/codesign --force --sign - "$<TARGET_BUNDLE_DIR:${PROJECT_NAME}>"
    COMMAND ${CMAKE_COMMAND} -E touch "${PROJECT_BINARY_DIR}/signed.stamp"
    VERBATIM)
CMAKE
cmake -S "$QA/source" -B "$QA/build" -G "$GENERATOR" -DREPO="$REPO" > "$QA/configure.log" 2>&1
cmake --build "$QA/build" --config Release > "$QA/initial-build.log" 2>&1
APP="$(find "$QA/build" -type d -name MetadataSmoke.app -print -quit)"
test -n "$APP"
codesign --verify --deep --strict "$APP"
cp "$QA/build/MacBundleMetadata.cpp" "$QA/initial-metadata.cpp"
STAMP="$(stat -f %m "$QA/build/signed.stamp")"
sleep 2
# A template-only edit must cause reconfiguration, compilation, linking and signing.
sed -i '' 's/<string>English<\//<string>French<\//' "$QA/source/Info.plist.in"
cmake --build "$QA/build" --config Release > "$QA/changed-build.log" 2>&1
test "$(stat -f %m "$QA/build/signed.stamp")" -gt "$STAMP"
! cmp -s "$QA/initial-metadata.cpp" "$QA/build/MacBundleMetadata.cpp"
test "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleDevelopmentRegion' "$APP/Contents/Info.plist")" = French
codesign --verify --deep --strict "$APP"
STAMP="$(stat -f %m "$QA/build/signed.stamp")"
sleep 2
# A feature-only change must remove the document declaration and re-sign too.
cmake -S "$QA/source" -B "$QA/build" -DUSE_TORRENT=OFF > "$QA/off-configure.log" 2>&1
cmake --build "$QA/build" --config Release > "$QA/off-build.log" 2>&1
test "$(stat -f %m "$QA/build/signed.stamp")" -gt "$STAMP"
! /usr/libexec/PlistBuddy -c 'Print :CFBundleDocumentTypes' "$APP/Contents/Info.plist" >/dev/null 2>&1
codesign --verify --deep --strict "$APP"
printf 'PASS: %s metadata-only and torrent-toggle changes rebuild and re-sign.\n' "$GENERATOR" | tee "$QA/result.txt"
