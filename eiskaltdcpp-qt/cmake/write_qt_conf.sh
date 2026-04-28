#!/bin/sh
set -eu
cat > "$1" <<'EOT'
[Paths]
Plugins = PlugIns
EOT
