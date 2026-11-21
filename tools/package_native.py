"""Build the portable native Windows ZIP: the game, its launcher profile and licence notices."""
import argparse
import hashlib
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile

ROOT = Path(__file__).resolve().parents[1]
FILES = {
    "InTheHunt.exe": ".local/native/InTheHunt.exe",
    "recomplauncher.json": "recomplauncher.json",
    # Notices for third-party code compiled into the executable.
    "LICENSES/MAME-Timing-BSD-3-Clause.txt": "LICENSES/MAME-Timing-BSD-3-Clause.txt",
    "LICENSES/ymfm-BSD-3-Clause.txt": "LICENSES/ymfm-BSD-3-Clause.txt",
    "LICENSES/GA20-BSD-3-Clause.txt": "LICENSES/GA20-BSD-3-Clause.txt",
    "LICENSES/BlurBusters-CRT-Beam-Simulator-MIT.txt": "LICENSES/BlurBusters-CRT-Beam-Simulator-MIT.txt",
    "LICENSES/librashader-headers-MIT.txt": "LICENSES/librashader-headers-MIT.txt",
    "LICENSES/librashader-MPL-2.0.md": "LICENSES/librashader-MPL-2.0.md",
    "LICENSES/dxcompiler-LLVM.txt": "LICENSES/dxcompiler-LLVM.txt",
    "LICENSES/dxcompiler-MIT.txt": "LICENSES/dxcompiler-MIT.txt",
    "LICENSES/dxcompiler-MS.txt": "LICENSES/dxcompiler-MS.txt",
    "librashader.dll": ".local/native/librashader.dll",
    "dxcompiler.dll": ".local/native/dxcompiler.dll",
}
RUNTIME_HASHES = {
    "librashader.dll": "8e5912fc7432ff39e935c61674823f37b96a824bb6c727f1319cc31fc561a74a",
    "dxcompiler.dll": "0689e2fee0a1c9df8662a59680469f24e2a3ad7ce6bbbcc9284f7a83172bfc59",
}

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('--native-dir', type=Path, default=ROOT / '.local/native')
    parser.add_argument('--output', type=Path, default=ROOT / '.local/release/InTheHunt-Windows-x64.zip')
    args = parser.parse_args()
    for name, expected in RUNTIME_HASHES.items():
        dll = args.native_dir / name
        if not dll.is_file() or hashlib.sha256(dll.read_bytes()).hexdigest() != expected:
            parser.error(f"missing or incorrect {dll}; see the pinned shader runtime in README.md")
    shaders = ROOT / "third_party/slang-shaders"
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(".tmp")
    with ZipFile(temporary, "w", ZIP_DEFLATED) as archive:
        for target, source in FILES.items():
            path = args.native_dir / Path(source).name if source.startswith('.local/native/') else ROOT / source
            archive.write(path, target)
        for path in sorted(shaders.rglob("*")):
            if path.is_file():
                archive.write(path, "shaders/" + path.relative_to(shaders).as_posix())
    temporary.replace(output)
    print(output)
