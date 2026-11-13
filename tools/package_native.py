"""Build the portable native Windows ZIP from the compiled host and launcher."""
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile

ROOT = Path(__file__).resolve().parents[1]
FILES = {
    "recomplauncher.json": "recomplauncher.json",
    "InTheHunt-Setup.exe": ".local/native/InTheHunt-Setup.exe",
    "tools/recomplauncher_setup.ps1": "tools/recomplauncher_setup.ps1",
    "LICENSES/MAME-Timing-BSD-3-Clause.txt": "LICENSES/MAME-Timing-BSD-3-Clause.txt",
    "LICENSES/ymfm-BSD-3-Clause.txt": "LICENSES/ymfm-BSD-3-Clause.txt",
    "LICENSES/GA20-BSD-3-Clause.txt": "LICENSES/GA20-BSD-3-Clause.txt",
    "InTheHunt.exe": ".local/native/InTheHunt.exe",
    "play.cmd": "play.cmd",
    "play.ps1": "play.ps1",
    "tools/world_regions.json": "tools/world_regions.json",
    "tools/world_native.ps1": "tools/world_native.ps1",
    "README.md": "README.md",
    "RELEASE-NOTES.md": "RELEASE-NOTES.md",
    "tools/get_border_art.ps1": "tools/get_border_art.ps1",
    # CRT shader runtime: librashader (MPL-2.0, unmodified v0.12.0 build) and the DirectX Shader Compiler.
    "librashader.dll": ".local/native/librashader.dll",
    "dxcompiler.dll": ".local/native/dxcompiler.dll",
    "LICENSES/librashader-MPL-2.0.md": "LICENSES/librashader-MPL-2.0.md",
    "LICENSES/dxcompiler-LLVM.txt": "LICENSES/dxcompiler-LLVM.txt",
    "LICENSES/dxcompiler-MS.txt": "LICENSES/dxcompiler-MS.txt",
    "LICENSES/dxcompiler-MIT.txt": "LICENSES/dxcompiler-MIT.txt",
    "LICENSES/BlurBusters-CRT-Beam-Simulator-MIT.txt": "LICENSES/BlurBusters-CRT-Beam-Simulator-MIT.txt",
}
INSTRUCTIONS = """In The Hunt World - native Windows build

Extract the complete ZIP to a writable folder and open play.cmd.
Select your own World ROM archive when prompted. The launcher also checks your
Downloads folder and the folder beside play.cmd for inthunt.7z. The imported
images stay under .local/world. Later launches can run without the archive.

5 inserts a coin, 1 starts Player 1, the arrow keys move and Z and X fire.
On an Xbox-compatible controller, Back inserts a coin; A starts, joins or
continues play. Use the D-pad to move and A/B to fire during play.
F1 (or Start on a controller) opens the pause menu: resume,
video, sound, game timing, border, controls and quit. Esc goes back in the menu.
OPTIONS > VIDEO > CRT SHADER picks a CRT look (CRT-Royale, Guest Advanced,
CRT-Geom, Lottes and more), always flat with no vignette. Needs Direct3D 12.
Options also has an ENHANCE tab: optional caustics, light shafts, dynamic
lights, shadows and more. The master switch defaults to off (pure arcade
picture). Wakes, debris, silt, surface splashes, underwater sparks and impact
particles each have their own switch.
The enhancement suite adds sprite materials, pressure waves,
illuminated sediment, volumetric water, ocean depth and battle aftermath.
Each is independently selectable. See README.md.
On first launch the game downloads an optional undersea border image;
turn it on under OPTIONS > VIDEO > BORDER ART.

Timing (Arcade or RemoveSlowdown) can be changed in the menu or with
.\\play.ps1 -TimingMode Arcade. The choice is saved.

All six stages, both endings and two-player play run on natively translated
game code with native music and sample playback.
"""

if __name__ == "__main__":
    output = ROOT / ".local/release/InTheHunt-Windows-x64.zip"
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(".tmp")
    with ZipFile(temporary, "w", ZIP_DEFLATED) as archive:
        for target, source in FILES.items():
            archive.write(ROOT / source, target)
        for f in sorted((ROOT / "docs").rglob("*")):
            if f.is_file() and f.suffix in {".md", ".png", ".json"}:
                archive.write(f, f.relative_to(ROOT).as_posix())
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
