"""Compile the real polling function with a deterministic XInput device stub."""
import argparse
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path, default=ROOT / 'src/world_window.c')
parser.add_argument('--out', type=Path, required=True)
args = parser.parse_args()
args.out.mkdir(parents=True, exist_ok=True)
source = args.source.read_text()
start = source.index('static void poll_controller(')
end = source.index('\nint world_window_poll(', start)
fixture = r'''
#ifdef NDEBUG
#error Controller regression requires assertions.
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xinput.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
static DWORD retry_at[2];
static WORD pad_buttons[2];
static SHORT pad_lx[2], pad_ly[2];
static int last_device_pad, connected;
static DWORD WINAPI fake_state(DWORD player, XINPUT_STATE *state) {
    (void)player;
    if (!connected) return ERROR_DEVICE_NOT_CONNECTED;
    ZeroMemory(state, sizeof *state);
    state->Gamepad.wButtons = XINPUT_GAMEPAD_START | XINPUT_GAMEPAD_DPAD_RIGHT;
    return ERROR_SUCCESS;
}
static DWORD (WINAPI *controller_state)(DWORD, XINPUT_STATE *) = fake_state;
'''
fixture += source[start:end]
fixture += r'''
int main(void) {
    uint16_t held = 0, buttons = 0;
    pad_buttons[0] = XINPUT_GAMEPAD_START;
    pad_lx[0] = 32767; pad_ly[0] = -32768;
    poll_controller(0, &held, &buttons);
    assert(!pad_buttons[0] && !pad_lx[0] && !pad_ly[0] && !held && !buttons);
    poll_controller(0, &held, &buttons);
    assert(!pad_buttons[0] && !pad_lx[0] && !pad_ly[0]);
    connected = 1; retry_at[0] = 0;
    poll_controller(0, &held, &buttons);
    assert((pad_buttons[0] & XINPUT_GAMEPAD_START) && (held & 1));
    puts("PASS: disconnect clears menu state; retry stays neutral; reconnect works");
    return 0;
}
'''
c = args.out.resolve() / 'controller.c'
exe = args.out.resolve() / 'controller.exe'
c.write_text(fixture)
env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(ROOT / '.local/zig-cache'),
           ZIG_LOCAL_CACHE_DIR=str(ROOT / '.local/zig-local'))
compiler = ROOT / '.local/zig-x86_64-windows-0.16.0/zig.exe'
subprocess.run([str(compiler), 'cc', '-target', 'x86_64-windows-gnu', '-std=c99',
                '-O2', '-UNDEBUG', str(c), '-o', str(exe)], env=env, check=True)
result = subprocess.run([str(exe)], capture_output=True, creationflags=subprocess.CREATE_NO_WINDOW)
(args.out / 'result.log').write_bytes(result.stdout + result.stderr)
print((result.stdout + result.stderr).decode(errors='replace'))
raise SystemExit(result.returncode)
