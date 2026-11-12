# Source credits




The following pinned sources are available under `donors/`. The native Windows build uses the World chip layout and checksums documented by MAME. The optional reference and hybrid tools use MAME's runtime and decryption definitions. The CPS2 checkout supplies the vendored ymfm synthesizer. Its menu, FBNeo and MiSTer checkouts also serve as design and engineering references.



| Source and revision | Material consulted and intended use |

|---|---|

| [CPS-2 Recomp — Tommy McLeroy and contributors](https://github.com/tmcleroy/cps2recomp/tree/d3bf22f277d7c610a8acbc8bc164620401f79a38) | Full checkout in `cps2recomp/`. Shared menu, settings, launcher, HUD, features and Giga Wing shooter modes. Menu and HUD files carry GPL-3.0-only notices. Retain the upstream LICENSE, individual file notices and THIRD_PARTY_NOTICES.md when incorporating components. |

| [MAME — MAME contributors](https://github.com/mamedev/mame/tree/73346bee98cd79d605514d14691f40ee5e368915) | Selected files in `mame/`. Bryan McPhail's M92 board and Irem opcode-table sources, NEC CPU/disassembler sources, and the GA20 model credited to Acho A. Tang, R. Belmont and Valley Bell. ROM layout, inputs, timing, video and sound references. COPYING and the individual source headers accompany the snapshot. |

| [FinalBurn Neo — FBNeo team and contributors](https://github.com/finalburnneo/FBNeo/tree/af5e474749be60eab5adbaddb505a2e742800ce9) | Selected driver, opcode and GA20 files in `FBNeo/`. Independent implementation to consult when M92 driver behavior is unclear. Reference only in the planned build. The original terms are in `src/license.txt`. |

| [Irem M92 MiSTer — core authors and contributors](https://github.com/MiSTer-devel/Arcade-IremM92_MiSTer/tree/68a4683e237eafca02e3df56dd84bacc255fba55) | Full checkout in `Arcade-IremM92_MiSTer/`. The World MRA supplies game-specific stage, timer, lives and weapon leads. RTL provides a hardware timing reference. LICENSE and component notices are included upstream. |

| Irem, *In the Hunt* / *Kaitei Daisensou* (1993) | Original game, program, art and audio. World chip images from the user's archive are the input to the loader and the basis of the routines recovered in `src/`. |



## World build and recovery tools



- Zig 0.16.0 provides Clang and the Windows target libraries for the standalone Windows build. [Zig release](https://ziglang.org/download/0.16.0/release-notes.html)

- The region sizes, chip names, interleave offsets and SHA-1 identifiers in tools/world_regions.json come from the World ROM definition in the pinned MAME driver. [M92 World ROM metadata](https://github.com/mamedev/mame/blob/73346bee98cd79d605514d14691f40ee5e368915/src/mame/irem/m92.cpp)



- MAME 0.289 provides V33/V35 execution, M92 video and audio, Lua integration and the NEC disassembler. `tools/import_world.py` reads ROM metadata from Bryan McPhail's `m92.cpp`. `tools/build_world.py` uses the In the Hunt V35 substitution table from his `irem_cpu.cpp`. The corresponding MAME source headers include their BSD-3-Clause notices. [MAME 0.289 release](https://github.com/mamedev/mame/releases/tag/mame0289)

- The Open Watcom contributors provide the 16-bit C compiler, linker and disassembler used to rebuild the game routines. The pinned Windows tools are build dependencies. [Open Watcom, November 1, 2026 build](https://github.com/open-watcom/open-watcom-v2/releases/tag/2026-11-01-Build)

- GNU Binutils objdump and Capstone 5.0.6 assisted local instruction analysis. MAME's NEC disassembler supplied the V33-specific bit instructions. [GNU Binutils](https://sourceware.org/binutils/) · [Capstone](https://github.com/capstone-engine/capstone)



When importing code, extend its entry with the upstream file, local destination, revision and license, and retain its original notices. Record newly used sources here in the same change.



## Playable presentation



- cgwg's CRT-Geom and CRT-Geom Deluxe BGFX chains from MAME 0.289 supply the two playable flat CRT profiles. The launcher derives local chain settings from the installed files. MAME's unfiltered chain supplies nearest-neighbour output. [MAME BGFX chains](https://github.com/mamedev/mame/tree/mame0289/bgfx/chains)

- Borgar's MAME hiscore plugin and its community-maintained game definitions supply arcade high-score persistence. [MAME hiscore plugin](https://github.com/mamedev/mame/tree/mame0289/plugins/hiscore)

- The Libretro shader authors and documentation informed the standalone CRT shortlist. [Libretro shader collection](https://github.com/libretro/slang-shaders)



The host World graphics decoder and tilemap sampler in src/world_video.c use the pinned MAME M92 board layout and playfield behaviour as hardware references. [M92 video implementation](https://github.com/mamedev/mame/blob/73346bee98cd79d605514d14691f40ee5e368915/src/mame/irem/m92_v.cpp)



## Optional cabinet artwork



- The World arcade marquee (3840 × 1185) reproduces Irem's original game artwork. The World fan background (1920 × 1080, database image 581211) is catalogued separately. Individual artist attribution was unavailable on the gallery listing. [LaunchBox In the Hunt gallery](https://gamesdb.launchbox-app.com/games/images/29540-in-the-hunt)

- ArsInvictus published the 2160 × 3840 portrait cabinet composition. ShaunDBurch supplied the original bezel for scanning. Briball located the control-panel reference and PapaShine rendered the panel. [Original bezel scan](https://forums.libretro.com/t/4k-vertical-overlay-community-contributions/25247/279) and [control-panel artwork](https://forums.libretro.com/t/4k-vertical-overlay-community-contributions/25247/844)

- The optional download script retrieves the credited public images into the user's local artwork folder. Redistribution permission for these images has not been established.



## Native PCM synthesis



The GA20 register and sample-clock behavior in src/world_ga20.c is adapted from Acho A. Tang, R. Belmont and Valley Bell’s MAME implementation at revision 73346bee98cd79d605514d14691f40ee5e368915, src/devices/sound/iremga20.cpp. The native C implementation includes a rational 48 kHz sample integrator. Its BSD-3-Clause terms accompany the source and Windows packages in LICENSES/GA20-BSD-3-Clause.txt. [GA20 source](https://github.com/mamedev/mame/blob/73346bee98cd79d605514d14691f40ee5e368915/src/devices/sound/iremga20.cpp)



## Native FM synthesis



Aaron Giles’s ymfm supplies YM2151 waveform generation in src/third_party/ymfm. The five files are copied unchanged from the CPS2 donor revision d3bf22f277d7c610a8acbc8bc164620401f79a38. The C wrapper src/world_ym.cpp clocks the chip and resamples its mono board mix at 48 kHz. Copyright notices remain in each file, and LICENSES/ymfm-BSD-3-Clause.txt accompanies both packages. [ymfm project](https://github.com/aaronsgiles/ymfm) · [Pinned donor files](https://github.com/tmcleroy/cps2recomp/tree/d3bf22f277d7c610a8acbc8bc164620401f79a38/third_party)


## Native timing reference

The source-annotated V33 instruction timing and prefetch accumulator in src/world_cycle_costs.c use Bryan McPhail’s NEC core as their reference, pinned to MAME revision 73346bee98cd79d605514d14691f40ee5e368915. Its BSD-3-Clause notice accompanies the source and Windows archives in LICENSES/MAME-Timing-BSD-3-Clause.txt. These annotations account for timing without fetching or executing ROM instructions. [Pinned NEC source](https://github.com/mamedev/mame/blob/73346bee98cd79d605514d14691f40ee5e368915/src/devices/cpu/nec/nec.cpp)

## CRT shaders

- [librashader](https://github.com/SnowflakePowered/librashader) v0.12.0 (SnowflakePowered and contributors, MPL-2.0) runs the RetroArch presets; loaded at run time from `librashader.dll`, unmodified. `src/post/librashader*.h` are its MIT-licensed C headers.
- [libretro slang-shaders](https://github.com/libretro/slang-shaders) at `2345ee9`: CRT-Royale (TroggleMonkey; Kurozumi tuning), CRT-Guest-Advanced (guest(r), Dr. Venom), CRT-Geom (cgwg et al.), CRT-Lottes (Timothy Lottes), CRT-Hyllian (Hyllian), CRT-Easymode (EasyMode), CRT-Aperture (EasyMode), crt-consumer, crt-caligari, zfast-crt (Greg Hogan). Shipped unmodified as data in `shaders/`, each with its own notice; `shaders/SOURCES.md` lists them.
- [Blur Busters CRT Beam Simulator](https://github.com/blurbusters/crt-beam-simulator) by Mark Rejhon and Timothy Lottes (MIT), ported into the post chain's beam pass.
- [DirectX Shader Compiler](https://github.com/microsoft/DirectXShaderCompiler) v1.9.2609 (LLVM / MIT), `dxcompiler.dll`.
- `src/post/post_chain.*` comes from the same author's Gauntlet Dark Legacy port; its sharpening follows AMD FidelityFX CAS (MIT).
