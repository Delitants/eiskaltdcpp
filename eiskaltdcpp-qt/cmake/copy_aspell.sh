#!/bin/sh
set -eu

SRC="$1"
DST="$2"

[ -d "$SRC" ] || exit 0
mkdir -p "$DST"

for f in "$SRC"/*.rws "$SRC"/*.multi "$SRC"/*.alias; do
    [ -e "$f" ] || continue
    cp -f "$f" "$DST"/
done
