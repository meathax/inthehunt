"""Build the portable native Windows ZIP from the compiled host and launcher."""
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile

ROOT = Path(__file__).resolve().parents[1]
FILES = {
    "LICENSES/MAME-Timing-BSD-3-Clause.txt": "LICENSES/MAME-Timing-BSD-3-Clause.txt",
    "LICENSES/ymfm-BSD-3-Clause.txt": "LICENSES/ymfm-BSD-3-Clause.txt",
    "LICENSES/GA20-BSD-3-Clause.txt": "LICENSES/GA20-BSD-3-Clause.txt",
    "InTheHunt.exe": ".local/native/InTheHunt.exe",
    "play.cmd": "play.cmd",
    "play.ps1": "play.ps1",
    "tools/world_regions.json": "tools/world_regions.json",
    "tools/world_native.ps1": "tools/world_native.ps1",
    "CREDITS.md": "CREDITS.md",
    "tools/get_border_art.ps1": "tools/get_border_art.ps1",
}
INSTRUCTIONS = """In The Hunt World - native Windows build

Extract the complete ZIP to a writable folder and open play.cmd.
Select your own World ROM archive when prompted. The launcher also checks your
Downloads folder and the folder beside play.cmd for inthunt.7z. The imported
images stay under .local/world. Later launches can run without the archive.

5 inserts a coin, 1 starts Player 1, the arrow keys move and Z and X fire.
On an Xbox-compatible controller, use Back, Start, the D-pad and A/B.
F1 (or L3 + R3 together on a controller) opens the pause menu: resume,
video, sound, game timing, border, controls and quit. Esc goes back in the menu.
Options also has an ENHANCE tab: optional caustics, light shafts, dynamic
lights, bloom and more, all off by default (pure arcade picture).
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
        archive.writestr("START-HERE.txt", INSTRUCTIONS)
    temporary.replace(output)
    print(output)
