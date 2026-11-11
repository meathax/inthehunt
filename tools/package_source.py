"""Package only reviewed project text. Generated assets and donor trees stay local."""
import subprocess
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED

ROOT = Path(__file__).resolve().parents[1]
def tracked_files():
    """Every file Git tracks; the source release is the reviewed repository."""
    output = subprocess.run(["git", "-C", str(ROOT), "ls-files", "-z"], check=True,
                            capture_output=True).stdout
    return [name for name in output.decode("utf-8").split(chr(0)) if name]


if __name__ == "__main__":
    dest = ROOT / ".local/release/inthehunt-source.zip"
    dest.parent.mkdir(parents=True, exist_ok=True)
    temporary = dest.with_suffix(".tmp")
    with ZipFile(temporary, "w", ZIP_DEFLATED) as archive:
        for name in tracked_files():
            path = ROOT / name
            if path.resolve() != path or not path.is_file():
                raise ValueError(name)
            data = path.read_bytes()
            data.decode("utf-8-sig")
            if b"\0" in data:
                raise ValueError(f"Binary content in {name}")
            archive.writestr(name, data)
    temporary.replace(dest)
    print(dest)



