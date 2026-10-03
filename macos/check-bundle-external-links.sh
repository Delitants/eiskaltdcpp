#!/bin/sh
# Retain legacy prefix arguments for callers; no external prefix is permitted.
set -eu
APP_PATH="${1:?usage: check-bundle-external-links.sh /path/to/App.app [vendored-prefix] [homebrew-prefix]}"
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec python3 "$SCRIPT_DIR/audit-bundle-closure.py" "$APP_PATH"
