"""Build and replay the World encounter captured by tools/play_reference.ps1 -CaptureEncounter."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
WORLD = ROOT / ".local" / "world"
SOURCES = (
    "dispatch", "actors_1", "actors_2", "actors_3", "actors_4", "actors_5", "actors_6", "stage1", "stage2", "stage3", "stage4", "stage5", "stage6", "common", "gaps", "credits", "coop", "explore1", "explore2",
    "title", "title_master", "gameover", "enemy_target", "stage", "fire", "camera", "torpedo", "contact", "terrain",
    "death", "score", "candidates", "player", "frame", "frame_video", "palette",
)
REGIONS = ("work", "video", "oam", "palette")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "gcc"),
                        help="path or name of a GCC-compatible host C compiler")
    args = parser.parse_args()
    cc = shutil.which(args.cc)
    if cc is None:
        parser.error(f"compiler not found: {args.cc}")
    if not (WORLD / "encounter-inputs.txt").is_file():
        parser.error("run tools/setup_reference.ps1 and tools/play_reference.ps1 -CaptureEncounter first")
    env = os.environ.copy()
    env["PATH"] = str(Path(cc).parent) + os.pathsep + env["PATH"]
    binary = WORLD / ("replay_frame.exe" if os.name == "nt" else "replay_frame")
    subprocess.run([cc, "-std=c99", "-O2", "-Wall", "-Wextra", "tools/replay_frame.c",
                    *(f"src/world_{name}.c" for name in SOURCES),
                    "-o", binary.relative_to(ROOT).as_posix()], check=True, cwd=ROOT, env=env)
    irqs = ["--irqs", "encounter-irqs.txt"] if (WORLD / "encounter-irqs.txt").exists() else []
    board_events = WORLD / "encounter-hardware.txt"
    input_name = board_events.name if board_events.exists() else "encounter-inputs.txt"
    replay = subprocess.run([str(binary), "--encounter", "maincpu.bin",
                    *(f"encounter-initial-{r}" for r in REGIONS), input_name,
                    *(f"encounter-native-{r}" for r in REGIONS), *irqs], check=True, cwd=WORLD, env=env,
                   stdout=subprocess.PIPE, text=True)
    print(replay.stdout, end="")
    actual_commands = [line for line in replay.stdout.splitlines() if line.startswith("transition ")]
    expected_commands = (WORLD / "encounter-sound.txt").read_text().splitlines()
    if actual_commands != expected_commands:
        raise SystemExit("Sound commands or their frame positions differ from the World reference")
    print(f"sound: {len(actual_commands)} commands matched")
    for region in REGIONS:
        actual = (WORLD / f"encounter-native-{region}").read_bytes()
        expected = (WORLD / f"encounter-final-{region}").read_bytes()
        if region == "work":
            actual, expected = actual[:0xc000], expected[:0xc000]
        if actual != expected:
            raise SystemExit(f"{region} differs from the World reference")
        print(f"{region}: {len(actual)} bytes matched")


if __name__ == "__main__":
    main()
