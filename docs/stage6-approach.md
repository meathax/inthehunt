# Stage 6 approach timing

**Options → Game → Slowdown → Arcade** follows 19 measured combat bursts between 24:47.69 and 30:40.62 in the supplied PCB recording. The ROM spawns the battleship at camera coordinate 4064. The opening palette fade and the transition through that boss trigger retain their existing game timing.

| Encounter | PCB onset | PCB duration | Native duration | Native onset error |
|---|---:|---:|---:|---:|
| Surface warship and hull explosions | 27:08.36 | 30.697 s | 30.660 s | 0.052 s early |
| Gunship | 29:00.12 | 9.593 s | 9.581 s | 0.046 s late |
| Missile field | 30:08.07 | 28.161 s | 28.127 s | 0.039 s late |

Across 12 headless checkpoint replays, median absolute onset error was 0.061 s. The largest onset error was 0.17 s. The largest duration error was 0.12 s, in the short burst at 24:50.44. The reference and game use their respective refresh periods for seconds. The [per-burst replay results](stage6-approach-replay.csv) include the held fraction as a separate measure of slowdown intensity.

## Identifying the same encounter

The earlier stage-average comparison paired some approach frames with battleship frames showing the same background. The replacement alignment tracked features across native screenshots with known camera positions. Features moving at half camera speed identified the repeating background. Features moving at full speed provided foreground landmarks. Following both in recording order, with local motion between frames, resolved the repeated tiles. The ROM spawn table independently identifies the second surface warship at 2088, the gunship at 2832, the missile formations at 3520 and the battleship at 4064.

The reference is the [same 59.94 Hz recording and playfield-difference measurement](pcb-slowdown.md). A 30-refresh moving window enters a burst at 30% held refreshes and leaves at 12%. Gaps up to 18 refreshes join neighboring peaks. The minimum duration is 30 refreshes. The [calibration manifest](stage6-approach-bursts.csv) records each measured boundary, camera landmark and enemy condition. Video differencing estimates visible holds in compressed footage. It cannot recover the PCB controller inputs or CPU bus timing.

## Game behavior

Crossing a camera landmark arms the corresponding burst. Its delay counts board refreshes and its profile advances with game logic passes. A player moving through the encounter can reach the action sooner. Leaving the area ends eligibility, and waiting in it cannot repeat an exhausted burst. The major warship, gunship and missile windows also require the corresponding live enemy state. Warship eligibility includes the hull's destruction animation, which is still visibly stalled on the PCB.

An eligible pass uses its measured hold count, including a zero. This prevents the older CPU estimate from adding a second source of slowdown inside the burst. Outside the fitted windows the ordinary estimate applies. Minor bursts use camera position and encounter time alone. This is an empirical fit to one recording. Different routes, weapon choices and kill times can produce different results.

## Verification and regeneration

The diagnostic driver used ordinary directional and fire inputs to follow camera and submarine positions recovered from the footage. Those pad streams were replayed using the playable executable's public headless entry point, with the audio output device closed. The emitted refresh intervals matched the diagnostic run for every pass. Each trial's time origin was aligned once at the checkpoint. The individual burst starts were then measured from the resulting refresh stream. These are calibration-route checkpoints and reuse the recording used to construct the profiles.

A separate continuous replay ran from the beginning of stage 6 through the battleship trigger. A mid-burst save reproduced the subsequent 219 refreshes and scroll positions after restore. In a 240-refresh battleship control, Arcade produced two refreshes per update and Removed produced one.

To regenerate the profiles, run the following from the repository root with the reference recording available. The generator uses the checked-in landmark manifest. Private captures, checkpoint saves and pad journals from the test are under `build/stage6-bursts/`.

```powershell
ffmpeg -v error -i D:/Downloads/In-The-Hunt-SnapYT.App.mp4 -an -vf "scale=240:180,crop=232:142:4:20,format=yuv420p,tblend=all_mode=difference,signalstats,metadata=print:key=lavfi.signalstats.YAVG:file=build/stage6-diff.txt" -f null NUL
python tools/fit_stage6_bursts.py build/stage6-diff.txt
```
