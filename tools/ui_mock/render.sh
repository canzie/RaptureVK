#!/usr/bin/env bash
# Renders a mock at the editor's 1080p window size: render.sh <file.html> [variant]
# Writes renders/<file>-<variant>.png and renders/compare-<variant>.png, the render beside reference/current.png.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
PAGE="$1"
VARIANT="${2:-current}"
NAME="$(echo "$VARIANT" | tr '&=,' '---')"
OUT="$HERE/renders/$(basename "$PAGE" .html)-$NAME.png"
PROFILE="$(mktemp -d)"
mkdir -p "$HERE/renders"
firefox --headless --no-remote --profile "$PROFILE" --window-size=1908,1048 --screenshot "$OUT" "file://$HERE/$PAGE?variant=$VARIANT" >/dev/null 2>&1
rm -rf "$PROFILE"
python3 - "$OUT" "$HERE/reference/current.png" "$HERE/renders/compare-$NAME.png" <<'PY'
import sys
from PIL import Image
render, reference = Image.open(sys.argv[1]).convert("RGB"), Image.open(sys.argv[2]).convert("RGB")
pair = Image.new("RGB", (render.width, render.height + reference.height + 8), (255, 0, 255))
pair.paste(reference, (0, 0))
pair.paste(render, (0, reference.height + 8))
pair.save(sys.argv[3])
PY
echo "$OUT"
