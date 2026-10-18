#!/usr/bin/env bash
# Called only inside the workflow's loopback-only network namespace.
set -euo pipefail
evidence="$PWD/Work/qa/linux-torrent"
build="$PWD/build-torrent-linux"
mkdir -p "$evidence/home" "$evidence/runtime"
chmod 700 "$evidence/runtime"
export HOME="$evidence/home" XDG_CONFIG_HOME="$evidence/home/config"
export XDG_DATA_HOME="$evidence/home/data" XDG_CACHE_HOME="$evidence/home/cache"
export XDG_RUNTIME_DIR="$evidence/runtime" QT_QPA_PLATFORM=offscreen

ip -json address show > "$evidence/network.json"
python3 - "$evidence/network.json" <<'PY'
import json, sys
interfaces = json.load(open(sys.argv[1]))
if len(interfaces) != 1 or interfaces[0]['ifname'] != 'lo':
    raise SystemExit('Refusing tests outside a loopback-only network namespace')
PY
ctest --test-dir "$build" --show-only=json-v1 > "$evidence/inventory.json"
python3 tools/ci/verify-linux-torrent-qa.py "$build" "$evidence"

# The explicit dependency target is not registered with CTest.
timeout --signal=TERM --kill-after=10s 600s \
    "$build/tests/torrent/eiskalt-selected-recheck-dependency-tests" \
    '[selected-recheck-dependency]' --reporter junit --out "$evidence/dependency.xml" \
    2>&1 | tee "$evidence/dependency.log"
ctest --test-dir "$build" --parallel 1 --timeout 600 --output-on-failure \
    --output-junit "$evidence/ctest.xml" 2>&1 | tee "$evidence/ctest.log"
python3 tools/ci/verify-linux-torrent-qa.py "$build" "$evidence" --results
