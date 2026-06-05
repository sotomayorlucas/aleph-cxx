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
