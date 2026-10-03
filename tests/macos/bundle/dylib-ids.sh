#!/bin/sh
set -eu
root=${1:?repository path required}
work=$(mktemp -d "${TMPDIR:-/tmp}/eiskalt-bundle-id.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
app="$work/Test App.app"
libs="$app/Contents/Frameworks"
mkdir -p "$libs" "$app/Contents/PlugIns/platforms"
# Closure auditing now needs a real executable context, not a library fragment.
make_main() {
    mkdir -p "$1/Contents/MacOS"
    printf 'int main(void) { return 0; }\n' > "$work/main.c"
    xcrun clang "$work/main.c" -mmacosx-version-min=11.0 -Wl,-rpath,@executable_path/../Frameworks -o "$1/Contents/MacOS/Main"
    printf '<plist version="1.0"><dict><key>CFBundleExecutable</key><string>Main</string></dict></plist>\n' > "$1/Contents/Info.plist"
}
make_main "$app"
printf 'int bundle_test_value(void) { return 42; }\n' > "$work/library.c"
xcrun clang -dynamiclib "$work/library.c" -Wl,-install_name,/opt/homebrew/lib/libfixture.dylib -o "$libs/libfixture.dylib"
xcrun clang -dynamiclib "$work/library.c" -Wl,-install_name,@rpath/librelative.dylib -o "$libs/librelative.dylib"
cp "$libs/librelative.dylib" "$app/Contents/PlugIns/platforms/libqcocoa.dylib"
plugin_before=$(shasum -a 256 "$app/Contents/PlugIns/platforms/libqcocoa.dylib")
relative_before=$(shasum -a 256 "$libs/librelative.dylib")
/bin/sh "$root/macos/normalize-bundle-dylib-ids.sh" "$app"
test "$(otool -D "$libs/libfixture.dylib" | tail -1)" = '@executable_path/../Frameworks/libfixture.dylib'
test "$(shasum -a 256 "$libs/librelative.dylib")" = "$relative_before"
test "$(shasum -a 256 "$app/Contents/PlugIns/platforms/libqcocoa.dylib")" = "$plugin_before"
first=$(shasum -a 256 "$libs/libfixture.dylib")
/bin/sh "$root/macos/normalize-bundle-dylib-ids.sh" "$app"
test "$(shasum -a 256 "$libs/libfixture.dylib")" = "$first"
/bin/sh "$root/macos/check-bundle-external-links.sh" "$app"
printf 'PASS absolute ID normalized, relative library/plugin untouched, idempotent\n'

xcrun clang -dynamiclib "$work/library.c" -Wl,-install_name,@rpath/librunpaths.dylib \
    '-Wl,-rpath,/opt/homebrew/a path folder/lib' -Wl,-rpath,@loader_path \
    -Wl,-rpath,@loader_path/../../../../lib -o "$libs/librunpaths.dylib"
cp "$libs/librunpaths.dylib" "$app/Contents/PlugIns/platforms/librunpaths.dylib"
/bin/sh "$root/macos/normalize-bundle-dylib-ids.sh" "$app"
for binary in "$libs/librunpaths.dylib" "$app/Contents/PlugIns/platforms/librunpaths.dylib"; do
    paths=$(otool -l "$binary" | awk '/cmd LC_RPATH/{p=1;next} p && /^[[:space:]]*path /{sub(/^[[:space:]]*path /,"");sub(/ \(offset.*$/,"");print;p=0}')
    test "$paths" = '@loader_path'
done
first=$(shasum -a 256 "$libs/librunpaths.dylib")
/bin/sh "$root/macos/normalize-bundle-dylib-ids.sh" "$app"
test "$(shasum -a 256 "$libs/librunpaths.dylib")" = "$first"
test "$(shasum -a 256 "$app/Contents/PlugIns/platforms/libqcocoa.dylib")" = "$plugin_before"
printf 'PASS external library/plugin runpaths removed, relative paths/plugin preserved, idempotent\n'

framework_app="$work/Framework App.app"
framework="$framework_app/Contents/Frameworks/Fixture.framework/Versions/A/Fixture"
make_main "$framework_app"
mkdir -p "$(dirname "$framework")/Resources"
printf 'not a Mach-O binary\n' > "$(dirname "$framework")/Resources/Info.plist"
xcrun clang -dynamiclib "$work/library.c" -mmacosx-version-min=11.0 \
    -Wl,-install_name,/opt/homebrew/Fixture.framework/Versions/A/Fixture -o "$framework"
chmod 644 "$framework"
failed=0
if /bin/sh "$root/macos/check-bundle-external-links.sh" "$framework_app" > "$work/links.log" 2>&1; then
    printf 'FAIL non-executable framework external ID was not detected\n'
    failed=1
fi
if /bin/sh "$root/macos/check-macos-min-version.sh" "$framework_app" 10.15 > "$work/minos.log" 2>&1; then
    printf 'FAIL non-executable framework minimum OS was not detected\n'
    failed=1
fi
/bin/sh "$root/macos/normalize-bundle-dylib-ids.sh" "$framework_app"
if [ "$(otool -D "$framework" | tail -1)" != '@executable_path/../Frameworks/Fixture.framework/Versions/A/Fixture' ]; then
    printf 'FAIL non-executable framework ID was not normalized\n'
    failed=1
fi
test "$failed" = 0 || exit 1
test ! -x "$framework"
first=$(shasum -a 256 "$framework")
/bin/sh "$root/macos/normalize-bundle-dylib-ids.sh" "$framework_app"
test "$(shasum -a 256 "$framework")" = "$first"
/bin/sh "$root/macos/check-bundle-external-links.sh" "$framework_app"
/bin/sh "$root/macos/check-macos-min-version.sh" "$framework_app" 11.0
printf 'PASS non-executable framework IDs and OS floor audited; resources ignored\n'
