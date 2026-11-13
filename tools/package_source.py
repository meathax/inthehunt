"""Package tracked project text and documentation PNGs; game assets stay local."""
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
            if (name.startswith("docs/screenshots/") or name == "artwork/icon/inthehunt.png") and name.endswith(".png"):
                if not data.startswith(b"\x89PNG\r\n\x1a\n"):
                    raise ValueError(f"Invalid documentation PNG in {name}")
            elif name == "artwork/icon/inthehunt.ico":
                if not data.startswith(b"\x00\x00\x01\x00"):
                    raise ValueError(f"Invalid application icon in {name}")
            else:
                data.decode("utf-8-sig")
                if b"\0" in data:
                    raise ValueError(f"Binary content in {name}")
            archive.writestr(name, data)
    temporary.replace(dest)
    print(dest)



