#!/bin/sh
set -eu

app=${1:?usage: normalize-bundle-dylib-ids.sh /path/to/App.app}
test -d "$app/Contents"
frameworks="$app/Contents/Frameworks"
test -d "$frameworks" || exit 0

# macdeployqt can leave a copied library or framework ID pointing to Homebrew.
# Only normalize IDs; dependency rewriting and signing remain separate steps.
find "$frameworks" -type f -print | while IFS= read -r library; do
    # Framework binaries can be extensionless and non-executable.
    case "$(file -b "$library")" in *Mach-O*) ;; *) continue ;; esac
    ids=$(otool -D "$library")
    id=$(printf '%s\n' "$ids" | sed -n '2p')
    case "$id" in
        /*)
            relative=${library#"$frameworks/"}
            chmod u+w "$library"
            install_name_tool -id "@executable_path/../Frameworks/$relative" "$library"
            ;;
    esac
done
