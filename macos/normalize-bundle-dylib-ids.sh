#!/bin/sh
set -eu

app=${1:?usage: normalize-bundle-dylib-ids.sh /path/to/App.app}
test -d "$app/Contents"
app=$(cd "$app" && pwd -P)
frameworks="$app/Contents/Frameworks"
test -d "$frameworks" || exit 0

# macdeployqt can leave copied IDs and unused build-machine search paths behind.
# Dependency rewriting and signing remain separate steps.
find "$app/Contents" -type f -print | while IFS= read -r library; do
    # Framework binaries can be extensionless and non-executable.
    case "$(file -b "$library")" in *Mach-O*) ;; *) continue ;; esac
    ids=$(otool -D "$library")
    id=$(printf '%s\n' "$ids" | sed -n '2p')
    case "$id" in
        /*)
            case "$library" in
                "$frameworks/"*)
                    relative=${library#"$frameworks/"}
                    chmod u+w "$library"
                    install_name_tool -id "@executable_path/../Frameworks/$relative" "$library"
                    ;;
            esac
            ;;
    esac
    otool -l "$library" | awk '/cmd LC_RPATH/{p=1;next} p && /^[[:space:]]*path /{sub(/^[[:space:]]*path /,"");sub(/ \(offset.*$/,"");print;p=0}' | sort -u | while IFS= read -r runpath; do
        keep=no
        case "$runpath" in
            @loader_path|@loader_path/*)
                expanded="$(dirname "$library")${runpath#@loader_path}"
                ;;
            @executable_path|@executable_path/*)
                expanded="$app/Contents/MacOS${runpath#@executable_path}"
                ;;
            /System/Library|/System/Library/*|/usr/lib|/usr/lib/*)
                expanded="$runpath"
                ;;
            *) expanded= ;;
        esac
        if [ -n "$expanded" ]; then
            resolved=$(cd "$expanded" 2>/dev/null && pwd -P) || resolved=
            case "$resolved" in
                "$app"|"$app/"*|/System/Library|/System/Library/*|/usr/lib|/usr/lib/*) keep=yes ;;
            esac
        fi
        if [ "$keep" = no ]; then
            chmod u+w "$library"
            install_name_tool -delete_rpath "$runpath" "$library"
        fi
    done
done
