"""Build the portable native Windows ZIP from the compiled host and launcher."""
from pathlib import Path
import argparse
from zipfile import ZIP_DEFLATED, ZipFile

ROOT = Path(__file__).resolve().parents[1]
FILES = {
    "recomplauncher.json": "recomplauncher.json",
    "LICENSES/MAME-Timing-BSD-3-Clause.txt": "LICENSES/MAME-Timing-BSD-3-Clause.txt",
    "LICENSES/ymfm-BSD-3-Clause.txt": "LICENSES/ymfm-BSD-3-Clause.txt",
    "LICENSES/GA20-BSD-3-Clause.txt": "LICENSES/GA20-BSD-3-Clause.txt",
    "InTheHunt.exe": ".local/native/InTheHunt.exe",
    # CRT shader runtime: librashader (MPL-2.0, unmodified v0.12.0 build) and the DirectX Shader Compiler.
    "librashader.dll": ".local/native/librashader.dll",
    "dxcompiler.dll": ".local/native/dxcompiler.dll",
    "LICENSES/librashader-MPL-2.0.md": "LICENSES/librashader-MPL-2.0.md",
    "LICENSES/dxcompiler-LLVM.txt": "LICENSES/dxcompiler-LLVM.txt",
    "LICENSES/dxcompiler-MS.txt": "LICENSES/dxcompiler-MS.txt",
    "LICENSES/dxcompiler-MIT.txt": "LICENSES/dxcompiler-MIT.txt",
    "LICENSES/BlurBusters-CRT-Beam-Simulator-MIT.txt": "LICENSES/BlurBusters-CRT-Beam-Simulator-MIT.txt",
}
INSTRUCTIONS = """IN THE HUNT - Windows edition

Open InTheHunt.exe. On first launch, select your own World inthunt.zip or
inthunt.7z archive. The game checks and copies the required data into
.local/world. You can then move or remove the original archive.

5 = coin, 1 = start, arrows = move, Z/X = fire, F1/Esc = options.
Xbox-style pad: Back = coin, A = start, D-pad/stick = move, A/B = fire.
Optional enhancements are off by default; enable them in Options > Enhance.

Updates: copy .local/world into the new game folder to keep data/settings.
Requires Windows 10/11 x64; CRT shaders require Direct3D 12.
World ROM set only. No game data included.
Help and enhancement guide: https://github.com/meathax/inthehunt
"""

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('--native-dir', type=Path, default=ROOT / '.local/native')
    parser.add_argument('--output', type=Path, default=ROOT / '.local/release/InTheHunt-Windows-x64.zip')
    args = parser.parse_args()
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(".tmp")
    with ZipFile(temporary, "w", ZIP_DEFLATED) as archive:
        for target, source in FILES.items():
            path = args.native_dir / Path(source).name if source.startswith('.local/native/') else ROOT / source
            archive.write(path, target)
        # The CRT presets (libretro slang-shaders data, mostly GPL; see shaders/SOURCES.md).
        shaders = ROOT / ".local/shaders"
        if not (shaders / "SOURCES.md").is_file():
            raise SystemExit("run tools/make_shaders.py first")
        for f in sorted(shaders.rglob("*")):
            if f.is_file():
                archive.write(f, "shaders/" + f.relative_to(shaders).as_posix())
        archive.writestr("START-HERE.txt", INSTRUCTIONS)
    temporary.replace(output)
    print(output)
