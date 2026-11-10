"""Build recovered C routines into an in-memory overlay for the World V33."""
import argparse
from pathlib import Path
import re
import struct
import subprocess

from import_world import ROOT

# Source block, free ROM, maximum body length, C entry, ABI adapter after call.
# The sound adapter restores BX and the result flags of the original AND.
ROUTINES = [
    ("reset", 0x7f303, 0x75540, 160, "reset_board_outputs", bytes.fromhex("31 c0 e9 13 00")),
    ("input", 0x05e64, 0x0ff24, 64, "latch_cabinet_inputs", bytes.fromhex("31 c0 c3")),
    ("fire", 0x0821c, 0x0ff64, 64, "standard_weapon_ready", b""),
    ("sound", 0x0372d, 0x0ffa4, 92, "send_pending_sound", bytes.fromhex("8b 1e d2 a6 83 e3 0e fb cb")),
    ("object", 0x0179b, 0x755e0, 192, "insert_actor", b""),
    ("torpedo", 0x08253, 0x756a0, 96, "place_torpedo", b""),
    ("candidates", 0xd3d6, 0x5ff00, 128, "world_append_candidate", b""),
    ("terrain", 0x068df, 0x8eac0, 128, "tile_lookup_offset", b""),
    ("motion", 0x05e9b, 0x75700, 80, "advance_axis", b""),
]


def relative(opcode, start, target):
    return bytes([opcode]) + struct.pack("<H", (target - start - 3) & 0xffff)


def weapon_adapter(site, entry, cooldown, mask, ready, blocked):
    # Preserve AX, SI and ES around the C call. ES:SI addresses SS:[BP+field].
    code = bytearray.fromhex("50 56 06 16 07 8d 76")
    code += bytes([cooldown, 0xb8, mask, 0])
    code += relative(0xe8, site + len(code), entry)
    code += bytes.fromhex("85 c0 07 5e 58 75 03")
    code += relative(0xe9, site + len(code), blocked)
    code += relative(0xe9, site + len(code), ready)
    return code


def build_sound(compiler_dir, work):
    subprocess.run([compiler_dir / "wcc.exe", "-q", "-0", "-ms", "-os", "-s", "-zl",
                    "-fo=fm.obj", ROOT / "src/world_sound.c"], cwd=work, check=True)
    listing = subprocess.check_output([compiler_dir / "wdis.exe", "fm.obj"], cwd=work, text=True)
    # Source the exact World opcode substitution from the pinned MAME driver.
    source = (ROOT / ".local/mame-metadata/irem_cpu.cpp").read_text()
    table = source.split("inthunt_decryption_table[256] = {", 1)[1].split("};", 1)[0]
    entries = re.findall(r"0x[0-9a-fA-F]+|xxxx", table)
    if len(entries) != 256:
        raise ValueError("Invalid World V35 opcode table")
    encode = {int(value, 16): index for index, value in enumerate(entries) if value != "xxxx"}
    instructions = []
    position = 0
    for match in re.finditer(r"^([0-9A-F]+)  ((?:[0-9A-F]{2} )*[0-9A-F]{2})\t", listing, re.M):
        if int(match[1], 16) != position:
            raise ValueError("Non-contiguous V35 compiler listing")
        instruction = bytearray.fromhex(match[2])
        instructions.append(instruction)
        position += len(instruction)
    size = re.search(r"Routine Size: (\d+) bytes", listing)
    if not size or position != int(size[1]):
        raise ValueError("Incomplete V35 FM writer")
    # DX stays live through both waits; neither busy poll nor either MMIO
    # write touches it. Drop the compiler's redundant BX shadow and saves.
    prologue = [b"\x53", b"\x52", b"\x06", b"\x89\xd3"]
    epilogue = [b"\x07", b"\x5a", b"\x5b", b"\xc3"]
    if instructions[:4] != prologue or instructions[-4:] != epilogue:
        raise ValueError("Unexpected FM writer register allocation")
    body = bytearray([encode[0x9c], encode[0xfa], encode[0x50]])
    for instruction in instructions[4:-4]:
        if instruction == b"\x89\xda":
            continue
        if instruction == bytes.fromhex("a8 80"):
            # The World key lacks TEST AL,imm. AL is the temporary status byte;
            # AND sets the same branch flags and both paths next overwrite AL.
            instruction[0] = 0x24
        prefixes = 1 if instruction[0] == 0x36 else 0
        for index in range(prefixes + 1):
            instruction[index] = encode[instruction[index]]
        body.extend(instruction)
    body += bytes([encode[0x58], encode[0x9d], encode[0xc3]])
    original = (work / "soundcpu.bin").read_bytes()
    if body != original[0x713:0x733]:
        raise ValueError("Rebuilt encrypted YM2151 writer differs from World")
    print("V35 YM2151 writer matches all 32 World bytes")
    return [(0x713, body)]


def build(compiler_dir):
    work = ROOT / ".local/world"
    original = (work / "maincpu.bin").read_bytes()
    patches = []
    for name, site, free, limit, symbol, adapter in ROUTINES:
        unit = {"terrain": "world_tile_v33.c", "motion": "world_motion.c", "candidates": "world_candidates.c"}.get(name, "world_io.c")
        subprocess.run([compiler_dir / "wcc.exe", "-q", "-0", "-ms", "-os", "-s", "-zl",
                        "-dBUILD_" + name.upper(), f"-fo={name}.obj", ROOT / "src" / unit],
                       cwd=work, check=True)
        link = work / f"{name}.lnk"
        link.write_text(f"format raw bin\noption quiet\nname {name}.bin\nfile {name}.obj\noption map={name}.map\n")
        subprocess.run([compiler_dir / "wlink.exe", "@" + link.name], cwd=work, check=True)
        body = (work / f"{name}.bin").read_bytes()
        if len(body) > limit or original[free:free + len(body)] != b"\xff" * len(body):
            raise ValueError(f"{name} exceeds its World ROM padding")
        address = re.search(rf"^([0-9A-Fa-f]+)\*?\s+{symbol}_\s*$",
                            (work / f"{name}.map").read_text(), re.M)
        if not address:
            raise ValueError(f"Linker omitted {symbol}")
        entry = free + int(address[1], 16)
        patches.append((free, body))
        if name == "terrain":
            shim = b"\x9a" + struct.pack("<HH", entry - 0x80000, 0x8000)
            patches.append((site, shim + relative(0xe9, site + len(shim), 0x6909)))
        elif name == "candidates":
            for index, (site, table) in enumerate([(0xd3d6, 0xa6e2), (0xd3ea, 0xaee6)]):
                # Far jump after the shape test. Preserve SI; return old cursor
                # in BX and the flags from the original cursor increment.
                thunk = 0x5ff80 + 32 * index
                shim = bytes.fromhex("56 89 ee bb") + struct.pack("<H", table)
                shim += relative(0xe8, thunk + len(shim), entry)
                shim += bytes.fromhex("5e 53 83 c3 02 5b cb")
                if original[thunk:thunk + len(shim)] != b"\xff" * len(shim):
                    raise ValueError("Candidate adapter overlaps World data")
                patches.append((thunk, shim))
                patches.append((site, b"\xea" + struct.pack("<HH", thunk - 0x50000, 0x5000)))
        elif name == "motion":
            # The four original entry points each have 13 bytes before RETF.
            for index, (position, velocity) in enumerate([(0x20, 0x38), (0x23, 0x3a),
                                                          (0x30, 0x38), (0x33, 0x3a)]):
                thunk = 0x75750 + index * 20
                # Save ES/DI; pass SS:BP+position and the signed velocity.
                shim = bytes.fromhex("06 57 16 07 8d 7e") + bytes([position, 0x8b, 0x46, velocity])
                shim += relative(0xe8, thunk + len(shim), entry) + bytes.fromhex("5f 07 cb")
                if original[thunk:thunk + len(shim)] != b"\xff" * len(shim):
                    raise ValueError("Motion adapter overlaps World data")
                patches.append((thunk, shim))
                patches.append((site + index * 13, b"\xea" + struct.pack("<HH", thunk - 0x70000, 0x7000)))
        elif name == "torpedo":
            far_call = b"\x9a" + struct.pack("<HH", entry - 0x70000, 0x7000)
            # Parent in SS:BP, child in DS:DI; caller retains the pose in BX.
            shim = bytes.fromhex("06 56 16 07 89 ee") + far_call + bytes.fromhex("5e 07 8b 5e 56")
            patches.append((site, shim + relative(0xe9, site + len(shim), 0x8278)))
        elif name == "object":
            far_call = b"\x9a" + struct.pack("<HH", entry - 0x70000, 0x7000)
            for site, parent in [(0x179b, "16 07 89 ee"), (0x17f9, "1e 07 89 de"),
                                 (0x1848, "1e 07 89 de")]:
                # Save ES, pass parent in ES:SI, then reproduce AX, flags and RETF.
                patches.append((site, b"\x06" + bytes.fromhex(parent) + far_call
                                + bytes.fromhex("07 8b 45 08 48 40 f8 cb")))
        elif name == "fire":
            patches.append((site, weapon_adapter(site, entry, 0x4a, 0x80, 0x823e, 0x82c8)))
            site = 0x82d4
            patches.append((site, weapon_adapter(site, entry, 0x4c, 0x40, 0x82f6, 0x83ae)))
        else:
            patches.append((site, relative(0xe8, site, entry) + adapter))
    # MAME loads and audits the user's original set. Patch the running copy only.
    lua = ['local rom = manager.machine.memory.regions[":maincpu"]',
           'local installed = false',
           'local function patch(offset, before, after)',
           '    local same = true',
           '    for i = 1, #after do same = same and rom:read_u8(offset+i-1) == after:byte(i) end',
           '    if same then return end',
           '    installed = true',
           '    for i = 1, #before do assert(rom:read_u8(offset+i-1) == before:byte(i), "World ROM mismatch") end',
           '    for i = 1, #after do rom:write_u8(offset+i-1, after:byte(i)) end',
           'end']
    def literal(data):
        return '"' + ''.join(f'\\x{byte:02x}' for byte in data) + '"'
    for offset, after in patches:
        before = original[offset:offset + len(after)]
        lua.append(f"patch(0x{offset:x}, {literal(before)}, {literal(after)})")
    audio = (work / "soundcpu.bin").read_bytes()
    lua.append('rom = manager.machine.memory.regions[":soundcpu"]')
    for offset, after in build_sound(compiler_dir, work):
        before = audio[offset:offset + len(after)]
        lua.append(f"patch(0x{offset:x}, {literal(before)}, {literal(after)})")
    lua += ['print("World C overlay: reset, cabinet input, standard weapons, actors, motion, sound queue and FM writes")',
            'if installed then manager.machine:soft_reset(); return end']
    (work / "hybrid.lua").write_text("\n".join(lua) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler-dir", type=Path, default=ROOT / ".local/watcom/binnt64")
    build(parser.parse_args().compiler_dir.resolve())
