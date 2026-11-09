#!/bin/sh
# Usage: tools/ref/run_capture.sh OUTDIR 'lua table body'   e.g.  'frames=20000, refill=true, stage=2'
# Runs MAME headless (dev reference only) on .local/roms/inthunt.zip.
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
out=$1; shift
mkdir -p "$out"
out=$(cd "$out" && pwd)
script="$out/capture.lua"
printf 'REF = { out=%s, %s }\n' "\"$out\"" "$1" > "$script"
cat "$root/tools/ref/ref_capture.lua" >> "$script"
home="$root/.local/mamehome"; mkdir -p "$home"
cd "$home"
exec /usr/games/mame inthunt -rompath "$root/.local/roms" -video none -sound none -nothrottle \
  -skip_gameinfo -nonvram_save -cfg_directory "$home/cfg" -nvram_directory "$home/nvram" \
  -state_directory "$home/sta" -snapshot_directory "$out" -autoboot_script "$script" 2>&1 | grep -v -E "WRONG CHECKSUMS|EXPECTED|FOUND:|might not run"
