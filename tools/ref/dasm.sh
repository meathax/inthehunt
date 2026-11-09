#!/bin/sh
# Usage: tools/ref/dasm.sh START LENGTH   (hex linear addresses, e.g. 7fb9 200)
# Disassembles World main-CPU code with MAME's NEC V33 disassembler.
root=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d); out="$tmp/out.asm"
cat > "$tmp/d.lua" <<EOF
local n=0
emu.register_frame_done(function() n=n+1
 if n==3 then manager.machine.debugger:command('dasm $out,0x$1,0x$2') end
 if n==5 then manager.machine:exit() end end)
EOF
cd "$tmp" && /usr/games/mame inthunt -rompath "$root/.local/roms" -video none -sound none -nothrottle \
  -skip_gameinfo -debug -debugger none -autoboot_script "$tmp/d.lua" >/dev/null 2>&1
cat "$out"; rm -rf "$tmp"
