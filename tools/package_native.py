"""Build the portable native Windows ZIP: the game, its launcher profile and licence notices."""
from pathlib import Path
import argparse
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
}

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
    temporary.replace(output)
    print(output)
