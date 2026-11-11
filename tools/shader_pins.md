# CRT shader runtime and data pins

The CRT shaders run through **librashader** (the RetroArch shader runtime as a
library) on Direct3D 12. The DLLs are not kept in git (`*.dll` is ignored);
place them in `third_party/librashader/` and the build copies them beside the
executable.

| File | Origin | SHA-256 |
|---|---|---|
| `librashader.dll` | librashader `librashader-v0.12.0` (commit `87e8a97b50516d997defeaa168173dcd185d4022`), built from the unmodified source with only the Direct3D 12 runtime and a static C runtime | `8e5912fc7432ff39e935c61674823f37b96a824bb6c727f1319cc31fc561a74a` |
| `dxcompiler.dll` | DirectX Shader Compiler release `v1.9.2609` (`dxc_2026_09_29.zip`, `bin/x64`) | `0689e2fee0a1c9df8662a59680469f24e2a3ad7ce6bbbcc9284f7a83172bfc59` |

Both come from the same pinned builds used by the Gauntlet Dark Legacy port
(`GDL DECOMP/third_party/PINS_crt.md` holds the full build recipe).
Licences: `LICENSES/librashader-MPL-2.0.md` (MPL-2.0; source: the tag above,
unmodified) and `LICENSES/dxcompiler-*.txt`.

`src/post/post_chain.{h,cpp}` is the post chain from that port (its own code),
with one change: `Settings::keep_background`, so border art shows outside the
picture. `src/post/librashader*.h` are librashader's MIT-licensed C headers.
The beam simulator in it is a port of the Blur Busters CRT Beam Simulator
(MIT, `src/post/blurbusters/`, `LICENSES/BlurBusters-CRT-Beam-Simulator-MIT.txt`).

## Shader presets

`python -I tools/make_shaders.py --slang PATH` copies the presets listed in
that script, and every file they pull in, unmodified from a libretro
slang-shaders checkout at commit `2345ee91602a7404d3a1a6a5df6108d7305fdad4`
into `.local/shaders` (released as `shaders/`), with `SOURCES.md` listing each
file, its SHA-256 and the licence named in its header. Most CRT shaders are
GPL; they are data files loaded at run time. Sony Megatron is not included
(no licence statement upstream).
