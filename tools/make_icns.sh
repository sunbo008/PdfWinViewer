#!/usr/bin/env bash
set -euo pipefail

# make_icns.sh <source_png> <dest_icns>
SRC=${1:-}
DST=${2:-}
if [[ -z "$SRC" || -z "$DST" ]]; then
  echo "Usage: $0 <source_png> <dest_icns>" >&2
  exit 2
fi
if [[ ! -f "$SRC" ]]; then
  echo "Source png not found: $SRC" >&2
  exit 3
fi
ICONSET=$(mktemp -d /tmp/appicon.XXXXXX.iconset)
sizes=(16 32 128 256 512 1024)
for s in "${sizes[@]}"; do
  s2=$((s*2))
  sips -z "$s" "$s"   "$SRC" --out "$ICONSET/icon_${s}x${s}.png" >/dev/null
  sips -z "$s2" "$s2" "$SRC" --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null
done
mkdir -p "$(dirname "$DST")"
iconutil -c icns "$ICONSET" -o "$DST"
rm -rf "$ICONSET"
echo "Created $DST"


