# Arcade slowdown calibration

Choose **Options → Game → Slowdown → Arcade**. The port makes its decision each logic pass and services extra board VBlanks through the game's existing interrupt path. The setting persists when leaving the menu. **Removed** bypasses the calibration.

## Where the recording slows down

The supplied *In-The-Hunt-SnapYT.App.mp4* runs for 41:04.829 at 60000/1001 frames per second. These are ten-second examples of visible gameplay. The rate is the estimated number of distinct playfield updates per second during that window.

| Stage | Recording time | Scene | Held refreshes | Visible updates/s |
|---|---|---|---:|---:|
| 1 | 04:34–04:44 | Ice boss and player fire | 14% | 52 |
| 2 | 07:14–07:24 | Rising missiles among submerged buildings | 50% | 30 |
| 2 | 09:38–09:48 | Two machines firing across the submarine | 38% | 37 |
| 3 | 12:42–12:52 | Explosions along the channel wall | 41% | 35 |
| 4 | 17:50–18:00 | Creature and explosions in the cavern | 26% | 44 |
| 5 | 21:52–22:02 | Stone passage with the advancing giant | 44% | 34 |
| 6 | 33:18–33:28 and 34:10–34:20 | Battleship red-bomb barrages | 50% | 30 |

The [two-second timeline](pcb-slowdown-timeline.csv) covers the full recording. Stage zero marks unclassified intervals including title cards and credits. Static scenes, fades, camera stops, source conversion and compression can all affect the detector. A zero in the timeline means it detected no clear isolated holds.

## Measurement and implementation

Reference SHA-256 `AEFEBE2B91E9CDE2C3B87086ECD7F493F86F38A158DA924CC0FB913671735B0E`. FFmpeg scaled each frame to 240 × 180, cropped a 232 × 142 playfield at (4, 20), and measured the mean absolute luma change from its predecessor. A likely hold has less than 25% of the stronger adjacent change, with that reference above one luma level. The strict and loose CSV columns repeat the calculation at 15% and 35%. Alternating holds in the heavy barrages survive all three thresholds. These are estimates from compressed video.

For calibration, headless snapshot replays supplied the native stage, camera progress, average modeled CPU cost and original held refresh count. Native screenshots were compared with frames sampled every 2.002 seconds from the video. Matches used SIFT features agreeing on a screen translation, with at least 16 inliers for the comparison below. The original cost model incorporates dispatched actors, sprites, tile updates and palette work. The calibration uses that load above a quiet-scene floor, with higher sensitivity in the channel at progress 2048–3071 and the stone passage at 512–1023. A deterministic pass phase distributes partial rates over refreshes. Existing multi-VBlank overruns take precedence.

The final battleship needs separate treatment. During its two cannon attacks, two-second samples with 8–11 detected red bombs averaged 48% held refreshes. Samples with 16–19 averaged 49%. The model ramps to one extra VBlank per pass at eight active muzzle bombs. During the subsequent core attack, samples with 0–1 visible torpedoes averaged 5% holds, 6–7 averaged 32%, and 8–9 averaged 39%. The core attack uses the number of live volley torpedoes on screen. Both effects follow the attack as the player moves or destroys enemies.

## Headless comparison

Across 441 landmark-matched two-second windows, the PCB detector averaged 10.9% held refreshes. The native sample average moved from 2.1% to 11.4%. The root-mean-square difference across windows fell from 17.7 to 14.3 percentage points. These windows reuse some native snapshots and helped calibrate the model. The PCB player and headless player took different routes and fired different weapons.

| Matched windows | PCB held | Original port | Calibrated port |
|---|---:|---:|---:|
| Stage 1 · 78 | 6.8% | 0.1% | 6.4% |
| Stage 2 · 103 | 17.0% | 0.0% | 17.4% |
| Stage 3 · 92 | 10.0% | 2.2% | 10.1% |
| Stage 4 · 48 | 6.6% | 0.5% | 5.3% |
| Stage 5 · 59 | 12.2% | 2.5% | 13.7% |
| Stage 6 approach · 61 (initial comparison) | 9.2% | 9.0% | 11.9% |

Stage 6's repeating background sometimes matched different combat states. The initial approach percentage included native battleship snapshots. The [stage 6 approach measurements](stage6-approach.md) supersede that row with ordered camera landmarks and burst-by-burst headless replays. Seven late battleship snapshots separately reproduced the PCB's 50% held refreshes during the bomb barrage.
