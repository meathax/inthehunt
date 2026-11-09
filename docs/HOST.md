# Windows host: what is connected to the running game

This describes the host around the recovered game core: `src/world_native.c`
(entry point, main loop), `src/world_window.c` (window, input, presentation)
and the modules they call. The host reads game state; it never writes work RAM
except through `world_scores_store_load` at boot.

The Windows build has been cross-compiled on Linux and has not been run on
Windows. The portable logic (input mapping, menu, service events,
achievement rules) is covered by `tests/`.

## Files in the data directory

All sit beside the imported ROM images (`.local/world` by default).

| File | Module | Written |
|---|---|---|
| `native-settings.ini` | `world_settings` | When a menu row or binding changes, on first run, and when `--timing=` is given. A file from a newer build is never overwritten |
| `native-highscores.dat` | `world_scores_store` | When name entry hands over to the ranking screen (master `C642` to `C66E`); one retry at shutdown if that write failed |
| `native-service-record.txt` | `world_achievements` | When an award unlocks and when a run begins or ends; retried once a second while another copy holds the lock, and at shutdown |

## Service record: events that are reported

`src/world_service.c` looks at the following state once after every completed
board frame and turns it into `world_achievements` events. Nothing else is
read.

| State | Meaning used |
|---|---|
| `RAM[0x80]` (word) | Master routine |
| `RAM[0x84]` (word) | Stage index, 0 for the first stage |
| `RAM[0x118]` (word) | Player 1 stock of submarines |
| `RAM[0x264]` bits 0-1 | Player 1 / Player 2 submarine in the water |
| `RAM[0x28C]` bit 0 | Attract demo is replaying ROM inputs (set only by the demo branch of the stage start in `world_frame.c`) |
| Player 1 score | `world_modes_observe_world` |

| Event | Reported when |
|---|---|
| Run begins | A player bit rises from none while the master routine is one of the title masters (`0A09`, `1D52`, `1DE6`, `1E6C`, `1EA5`, `1F52`, `1F85`, `1F19`) and the demo flag is clear |
| `STAGE_START` | At run begin for stage 0, and each time the stage index rises by one below 6 |
| `STAGE_CLEAR` | The stage index rises by one (master `0CED` does this after a cleared stage). **Only in runs Player 2 has not taken part in** |
| `DEATH`, player 1 | `RAM[0x118]` decreases. Every lost submarine takes one from the stock, the last one included |
| `CONTINUE`, player 1 | `RAM[0x118]` returns from zero after Player 1 has already been given a stock in this run |
| `JOIN` | A player bit rises for a player who was not yet part of the run |
| `SCORE` | Player 1's score changes |
| `FINISH`, campaign complete | The stage index reaches 6, where master `0CED` leaves for the ending |
| `FINISH`, not complete | The master routine returns to `0A09` (title) during a run |
| `ABORT` | The game is closed during a run |

Frames with the demo flag set are ignored entirely, so attract play never
counts. The pause menu stops the board, so nothing is observed while paused.

Every run is reported as Arcade, eligible, with `fresh_start` set only for a
Player 1 start. The difficulty, default-lives and shot-reporting flags of the
run are left clear because the host does not read them back from the board.

## Events the core does not provide yet

These are never sent. No award is granted from a guess.

| Missing | Why | Awards that therefore stay locked |
|---|---|---|
| `KILL` with weapon and torpedo type | The core does not report which projectile destroyed an enemy | Cast Off, Depth Charge, Mixed Salvo, Contact Report, Salvo Drill, Sea Trials, Torpedo School |
| `BOSS_START`, `BOSS_KILL` | No documented state marks a boss encounter or its defeat | First Contact, Under Pressure, Broadside, Salvage Crew, and every Boss Rush award |
| `UPGRADE` | Pickups are not reported | Dockyard Refit |
| `SHOT` | Secondary-weapon fire is not reported | Torpedo Discipline (`world_achievements` now requires `reports_shots` for it) |
| Player 2 losses and continues | Only Player 1's stock is in the documented state | Wolf Pack, Double Hull, Convoy Duty; and Steady Helm, Battle Stations and Iron Watch in any run Player 2 joined |
| Difficulty and starting lives | Not read back from the board | Hard to Port, Silent Running, Abyssal Command, Perfect Charts |
| Ranked modes | See below | Every Score Attack and Boss Rush award |

What can unlock today, once the core plays that far: Second Watch, Steady
Helm, Battle Stations, Iron Watch, Back to Port and One Credit Under the Sea.

## Unlock plate

Unlocks are queued in earned order and drawn by `world_achievement_view` for
4.5 seconds each, blended over the bottom 60 lines of the 320x240 frame
before presentation, so the plate is always inside the 4:3 picture. A plate
earned while a stage is in play (master `0B2E`) is held until the board
leaves that master. The Achievement popups option turns the plate off; the
award is still recorded. The plate is not placed in the border margin, and
there is no sound or reduced-motion setting.

## Modes

`world_modes_available` is asked with a `WorldModeCore` that has no launch
function, so Boss Rush and Score Attack are unavailable. The pause menu lists
both as locked and shows `world_modes_unavailable_reason` for the selected
one. They become selectable only when the core supplies a launch function
that starts a boss encounter or a fixed-rule campaign; the host then still
needs to act on the menu's start commands. Mode records are not loaded or
saved.

## Menu

Esc pauses the board clock and the audio queue and opens `world_menu`.
Rows are offered only when the host can act on them.

| Row | Applies | Offered when |
|---|---|---|
| Sound volume | Next mixed audio frame | An audio device opened |
| Slowdown | Next logic pass (`world_clock_request_mode`) | Always |
| Fullscreen, Window size | Immediately; the size is clamped to the desktop | Always (size only while windowed) |
| Display profile, Vsync | Immediately | Direct3D is presenting |
| Cabinet border, Border brightness | Immediately; Enter on the border row picks an image file | Always |
| Controls | Rebinds through `world_input` capture; saved when bound | Always |
| Achievement popups | Next unlock | The service-record view was created |

Not present: a launch menu (the game boots to the title), the built-in
Pressure hull and Deep water borders (only black or an image file), coin and
start entries, save states, cheats and online play.

## Module changes made while connecting

- `world_present`: loads `d3d11.dll` and `d3dcompiler_47.dll` at run time, so
  the GDI fallback also covers a machine without them.
- `world_menu`: Display page, locked mode rows with a reason,
  `world_menu_buttons`, `world_menu_pressed` and the Controls page. The `/`
  glyph was drawn mirrored.
- `world_achievements`: `reports_shots` in the run configuration. Without it
  Torpedo Discipline was granted for any clean solo stage when the host sent
  no shot events.
- `world_audio_output`: `world_audio_output_pause`.
- `world_crt`: `world_crt_source` (`world_crt_shader.c` embeds the HLSL).
