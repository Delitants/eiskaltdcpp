#!/bin/sh
# Only disposable local test state is accepted. No endpoint/config argument.
set -eu
umask 077
case "${1-}" in start|stop) ;; *) echo 'usage: gost-fixture.sh start|stop PRIVATE_TEMP_DIR' >&2; exit 2;; esac
test "$#" = 2
exec python3 "$(dirname "$0")/gost_fixture.py" "$1" "$2"
