# World timing evidence

Research notes behind the Arcade and Remove Slowdown timing modes. Arcade slowdown in the native build is approximate; see README.

The original slowdown is a busy-VBlank protocol. A native pass currently runs in one host tick, so the current default does not reproduce this behavior.

## Clock and event domain

Pinned MAME m92.cpp configures a 9,000,000 Hz V33, a 6,666,666 Hz pixel clock, 424 pixel clocks per line and 262 lines per frame. VBlank IRQ rises at line 248, and the raster IRQ is independently positioned by the playfield register. Nominal frame rate is 3333333/55544 = 60.01247659513179 Hz. One nominal frame contains 149968.8149968815 V33 cycles. Use rational accumulated phase, since truncating the cycle budget per frame drifts.

The screen/audio clocks must advance during missed actor passes. Audio is independent at 14,318,181/57,344 Hz timer A, with GA20 clocks at oscillator/16 and OPM output clocks at oscillator/256.

## Program protocol

- IRQ 0550 tests RAM[25E] bit 15. Busy jumps to 0696, skipping display transfer, scroll/control updates and normal player input servicing.
- IRQ 0696 increments physical board tick RAM[276], samples coin/system edges and services the sound handshake.
- IRQ 07F8 sets busy bit 15 before returning.
- Actor/master work may cross more physical VBlanks while this bit remains set.
- Actor-list tail 0946 waits until system port 2 bit 7 is high, meaning sprite/palette DMA has completed.
- The tail then builds the next sprite list at 0E74, advances palette work at 125D, clears busy at 0961 and enters the idle spin at 084D.
- Idle spin increments RAM[B892], tests busy bit 15 and loops until the next IRQ. The count must include instruction phase around IRQ entry.
- RNG at 0608A uses the 32-bit state B74E/B750. B892 is the spin diagnostic counter. Briefing 0A0E seeds the RNG using actor/master pass counter 274 and its negation.

A correct native model requires cycle/event checkpoints inside long transfers. Deferring every IRQ to a returning handler boundary changes when input and sound events occur.

## Board waits and limits of current references

MAME nec.cpp explicitly calls the V33 prefetch size/timing a complete guess (six bytes, one clock). Manufacturer V33 bus documentation describes two-clock bus cycles with TW extension through READY. Instruction timing tables omit prefetch, predecode and bus waits.

MiSTer m92.sv connects V33 READY to GA23 busy. GA23 grants CPU VRAM access in memory slot 6 and releases it in slot 7. Per-line row-scroll loading adds a 16-cycle burst before normal eight-slot allocation resumes. Consequently VRAM bus costs depend on pixel phase.

MAME m92_v.cpp models DMA duration as (1024 + 4*N sprite entries) / (2*pixelHz), explicitly an assumption. MiSTer clocks GA21 at 9 MHz and additionally implements sprite clear and multi-cycle transfer states. These references disagree, so matching one alone cannot prove perfect PCB DMA timing.

## Captured validation targets

The private slowdown-reference-events.json stores existing original-ROM development traces. It contains full actor-pass physical-frame positions, board events and actor busy IRQ positions. It is a validation oracle and must stay out of runtime and releases.

- encounter 580..3699: 3,084 passes, 32 busy I events, frame deltas 3049x1 / 32x2 / 2x3.
- deep-clean 580..6000: 5,215 passes, 202 busy I events, frame deltas 5010x1 / 202x2 / 2x3.
- push-clean 580..13504: 12,678 passes, 238 busy I events, frame deltas 12436x1 / 236x2 / 4x3 / 1x4.

The additional 170+ missed frames after the opening rule out a fixed intro-delay implementation.

## Remove-slowdown semantics

Run one complete game pass per physical board frame without CPU-overrun busy VBlanks. Retain authored animation timers, stage pauses and the original board/audio rate. Changing a global speed multiplier would change music and authored timing. Input timestamps continue to follow physical frames.

## Sources

- Local pinned donors/mame/src/mame/irem/m92.cpp lines 917, 932, scanline_interrupt.
- Local pinned donors/mame/src/mame/irem/m92_v.cpp dmacontrol_w, dma_done.
- Local pinned donors/mame/src/devices/cpu/nec/nec.cpp v33_base_device and do_prefetch.
- Local pinned donors/Arcade-IremM92_MiSTer/rtl/m92.sv, ga23.sv and ga21.sv.
- Manufacturer V33 datasheet scan https://datasheet.datasheetarchive.com/originals/scans/Scans-055/DSAIH000102789.pdf
- Later manufacturer V-series instruction manual https://www.bitsavers.org/components/nec/V-Series/uPD701xx-um-v51_200009.pdf

A further timing-engineering reference is https://github.com/Reyanon/reirem. Its runtime CPU execution is outside the requested native architecture. Its claimed timing baseline is FBNeo interpreter parity. No code was imported.
