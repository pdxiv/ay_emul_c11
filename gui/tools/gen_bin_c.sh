#!/bin/sh
# Turns a binary asset into a C array definition, the same shape
# gui/src/default_skin.c and gui/src/about_bitmap.c used to carry
# checked directly into source control (see migration_debt.yaml for
# why that changed: binary blobs re-encoded as 6000+/39000+ line C
# files made every asset swap an unreviewable diff). Assets now live
# as real binary files under gui/assets/; this script regenerates the
# equivalent C source at build time (gui/Makefile's $(GEN_DIR)/%.c
# rules), landing in gui/build/generated/ - gitignored, never checked
# in.
#
# Usage: gen_bin_c.sh <input-asset> <output.c>
#
# Symbol names: xxd -i derives a C identifier from the input's
# basename (dots/non-alnum become underscores) - e.g. default_skin.ays
# -> `default_skin_ays`/`default_skin_ays_len`. This script prefixes
# both with `gui_` and adds `const` (xxd doesn't emit either), so
# callers (gui/src/skin.c, gui/src/dialogs/about.c) see the exact same
# `gui_default_skin_ays`/`gui_about_bmp` symbols as before - no
# consuming code needed to change.
set -eu

if [ "$#" -ne 2 ]; then
  echo "usage: $0 <input-asset> <output.c>" >&2
  exit 2
fi

asset="$1"
out="$2"
asset_dir=$(dirname "$asset")
asset_base=$(basename "$asset")

{
  echo "/* Auto-generated from $asset by gui/tools/gen_bin_c.sh - DO NOT EDIT."
  echo " * Regenerated on every build (gui/Makefile); never checked into git."
  echo " */"
  (cd "$asset_dir" && xxd -i "$asset_base") | \
    sed -E 's/^(unsigned char|unsigned int) /const \1 gui_/'
} > "$out"
