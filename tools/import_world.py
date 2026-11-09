"""Assemble verified World chip dumps into bus-order images for native recovery."""
import argparse
import hashlib
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / ".local/mame-metadata/m92.cpp"


def assemble(archive):
    source = DRIVER.read_text(encoding="utf-8").split("ROM_START( inthunt )", 1)[1].split("ROM_END", 1)[0]
    regions = {}
    for line in source.splitlines():
        region = re.search(r'ROM_REGION\( (0x\w+), "(\w+)"', line)
        if region:
            name = region[2]
            regions[name] = bytearray(int(region[1], 16))
        load = re.search(r'ROM_LOAD(16_BYTE)?\( "([^"]+)", (0x\w+), (0x\w+), CRC\(\w+\) SHA1\((\w+)\)', line)
        if not load:
            continue
        interleave, member, offset, size, digest = load.groups()
        data = subprocess.check_output(["tar", "-xOf", str(archive), member])
        if len(data) != int(size, 16) or hashlib.sha1(data).hexdigest() != digest:
            raise ValueError(f"{member}: World chip size or SHA-1 mismatch")
        start = int(offset, 16)
        step = 2 if interleave else 1
        regions[name][start:start + step * len(data):step] = data
    return regions


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--output", type=Path, default=ROOT / ".local/world")
    args = parser.parse_args()
    images = assemble(args.archive.resolve())
    args.output.mkdir(parents=True, exist_ok=True)
    for name, image in images.items():
        target = args.output / f"{name}.bin"
        target.write_bytes(image)
        print(f"{name:10} {len(image):8} bytes")
