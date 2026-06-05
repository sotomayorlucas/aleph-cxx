#!/usr/bin/env bash
# tools/visual_review.sh — render the editor's scripted scene headlessly and
# assemble a single labeled contact sheet so the engine's visual output can be
# eyeballed (by a human or by Claude via an image-capable Read) as it improves.
#
# It drives `aleph_edit --headless`, which walks a fixed Op script (init, +object,
# +light, recolor, delete) and, per step, writes a RASTER frame (the fast nav
# preview) and a PATH-TRACE frame (the idle ground truth). This script converts the
# PPMs to PNG and tiles them: top row = raster, bottom row = path-trace, columns =
# steps. The two rows side by side are the whole point — the raster preview should
# converge toward the path-trace reference.
#
# Usage:
#   tools/visual_review.sh [out_dir] [build_dir]
#     out_dir    where frames + contact sheet land   (default: /tmp/aleph_review)
#     build_dir  CMake build dir with aleph_edit      (default: build-release)
#
# Env:
#   ALEPH_DUMP_DEPTH=1   also emit per-step *_depth frames (1/w, near=bright) and a
#                        second depth contact sheet — handy for debugging occlusion.
#
# Output: $out_dir/_contact.png   (and _depth.png when ALEPH_DUMP_DEPTH=1)
# Requires: ImageMagick (`magick`).
set -euo pipefail

# wave sub-mode: render the 48-frame deterministic wave animation (--wave) and
# montage it into one 8x6 contact sheet (ripple grows, node deleted @24, re-route).
#   tools/visual_review.sh wave [out] [build]
if [ "${1:-}" = "wave" ]; then
  OUT="${2:-/tmp/aleph_wave}"; BUILD="${3:-build-release}"
  ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
  APP="$ROOT/$BUILD/apps/aleph_edit/aleph_edit"
  command -v magick >/dev/null 2>&1 || { echo "error: ImageMagick (magick) not found" >&2; exit 1; }
  echo "==> building aleph_edit_app ($BUILD)"
  cmake --build "$ROOT/$BUILD" --target aleph_edit_app >/dev/null
  rm -rf "$OUT"; mkdir -p "$OUT"
  echo "==> rendering wave frames -> $OUT"
  "$APP" --wave "$OUT"
  echo "==> converting PPM -> PNG"
  for f in "$OUT"/*.ppm; do magick "$f" "${f%.ppm}.png"; done
  # numeric-sorted frame list (step0..step47) so the montage is in time order
  mapfile -t FR < <(ls "$OUT"/step*_wave_raster.png | sort -V)
  echo "==> montage -> $OUT/_wave_contact.png"
  magick montage "${FR[@]}" -tile 8x6 -geometry +2+2 -background '#222' \
    -title 'aleph.sim: wave on the shared Laplacian — ripple (0-23), node deleted, re-route (24-47)' \
    "$OUT/_wave_contact.png"
  echo "done: $OUT/_wave_contact.png"
  exit 0
fi

# diff sub-mode: render headless, compute per-step |raster - path_trace| heatmaps,
# print the mean abs diff per step, and tile [raster | path-trace | diff] per step.
#   tools/visual_review.sh diff [out] [build]
if [ "${1:-}" = "diff" ]; then
  OUT="${2:-/tmp/aleph_diff}"; BUILD="${3:-build-release}"
  ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
  APP="$ROOT/$BUILD/apps/aleph_edit/aleph_edit"
  command -v magick >/dev/null 2>&1 || { echo "error: ImageMagick (magick) not found" >&2; exit 1; }
  echo "==> building aleph_edit_app ($BUILD)"
  cmake --build "$ROOT/$BUILD" --target aleph_edit_app >/dev/null
  rm -rf "$OUT"; mkdir -p "$OUT"
  echo "==> rendering headless -> $OUT"
  "$APP" --headless "$OUT"
  for f in "$OUT"/*.ppm; do magick "$f" "${f%.ppm}.png"; done
  echo "==> computing |raster - path_trace| heatmaps (mean abs diff per step)"
  python3 - "$OUT" <<'PY'
import sys, os
from PIL import Image
OUT = sys.argv[1]
steps = ["step0_init","step1_add_object","step2_add_light","step3_set_material","step4_delete_object"]
def hot(d):                      # mean-abs-diff 0..255 -> black->blue->red->white
    t = min(1.0, d / 96.0)       # saturate at diff=96
    if t < 0.33: return (0, 0, int(255 * t / 0.33))
    if t < 0.66:
        f = (t - 0.33) / 0.33;  return (int(255 * f), 0, 255 - int(255 * f))
    f = (t - 0.66) / 0.34;      return (255, int(255 * f), int(255 * f))
for s in steps:
    rp = os.path.join(OUT, s + "_raster.png"); pp = os.path.join(OUT, s + "_pt.png")
    if not (os.path.exists(rp) and os.path.exists(pp)): continue
    r = Image.open(rp).convert("RGB"); p = Image.open(pp).convert("RGB")
    W, H = r.size; ra = r.load(); pa = p.load()
    out = Image.new("RGB", (W, H)); oa = out.load(); tot = 0.0
    for y in range(H):
        for x in range(W):
            a = ra[x, y]; b = pa[x, y]
            d = (abs(a[0]-b[0]) + abs(a[1]-b[1]) + abs(a[2]-b[2])) / 3.0
            tot += d; oa[x, y] = hot(d)
    out.save(os.path.join(OUT, s + "_diff.png"))
    print(f"  {s:22} mean|diff| = {tot/(W*H):5.1f} / 255")
PY
  echo "==> montage [raster | path-trace | diff] -> $OUT/_diff_contact.png"
  ROWS=()
  for s in step0_init step1_add_object step2_add_light step3_set_material step4_delete_object; do
    ROWS+=("$OUT/${s}_raster.png" "$OUT/${s}_pt.png" "$OUT/${s}_diff.png")
  done
  magick montage "${ROWS[@]}" -tile 3x5 -geometry +3+3 -background '#222' \
    -title 'raster | path-trace | |diff| heatmap (black=match, hot=differ)' \
    "$OUT/_diff_contact.png"
  echo "done: $OUT/_diff_contact.png"
  exit 0
fi

OUT="${1:-/tmp/aleph_review}"
BUILD="${2:-build-release}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="$ROOT/$BUILD/apps/aleph_edit/aleph_edit"

command -v magick >/dev/null 2>&1 || { echo "error: ImageMagick (magick) not found" >&2; exit 1; }

# Build the app target (named aleph_edit_app; the binary is aleph_edit).
echo "==> building aleph_edit_app ($BUILD)"
cmake --build "$ROOT/$BUILD" --target aleph_edit_app >/dev/null

rm -rf "$OUT"; mkdir -p "$OUT"
echo "==> rendering headless script -> $OUT"
"$APP" --headless "$OUT"

echo "==> converting PPM -> PNG"
shopt -s nullglob
for f in "$OUT"/*.ppm; do magick "$f" "${f%.ppm}.png"; done

# Steps in script order; keep in sync with apps/aleph_edit/main.cpp run_headless.
STEPS=(step0_init step1_add_object step2_add_light step3_set_material step4_delete_object)

build_row() {  # $1 = suffix (raster|pt|depth) ; echoes the file list if all exist
    local suffix="$1" list=() s
    for s in "${STEPS[@]}"; do
        local p="$OUT/${s}_${suffix}.png"
        [[ -f "$p" ]] && list+=("$p")
    done
    echo "${list[@]}"
}

RASTER=($(build_row raster)); PT=($(build_row pt))
echo "==> montage -> $OUT/_contact.png"
magick montage "${RASTER[@]}" "${PT[@]}" \
    -tile "${#STEPS[@]}x2" -geometry +4+4 -background '#222' \
    -title 'TOP: raster (nav preview)   BOTTOM: path-trace (idle truth)   |   steps: init, +obj, +light, recolor, delete' \
    "$OUT/_contact.png"

if [[ -n "${ALEPH_DUMP_DEPTH:-}" ]]; then
    DEPTH=($(build_row depth))
    if [[ ${#DEPTH[@]} -gt 0 ]]; then
        echo "==> depth montage -> $OUT/_depth.png"
        magick montage "${DEPTH[@]}" -tile "${#STEPS[@]}x1" -geometry +4+4 \
            -background '#222' -title 'raster depth = 1/w (near = bright)' \
            "$OUT/_depth.png"
    fi
fi

echo "done. open: $OUT/_contact.png"
