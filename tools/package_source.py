"""Package only reviewed project text. Generated assets and donor trees stay local."""
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED

ROOT = Path(__file__).resolve().parents[1]
FILES = (
    "src/world_clock.c", "src/world_clock.h", "src/world_cycle_costs.c", "src/world_cycle_costs.h",
    "src/world_timing_options.c", "src/world_timing_options.h", "src/world_settings.c", "src/world_settings.h",
    "src/world_input.c", "src/world_input.h", "src/world_border.c", "src/world_border.h",
    "LICENSES/MAME-Timing-BSD-3-Clause.txt",
    "play.cmd", "tools/package_native.py", "tools/world_native.ps1", "tools/world_regions.json", "tools/setup_reference.ps1", "tools/play_reference.ps1",
    "src/world_audio_internal.h", "src/world_audio_notes.c", "src/world_audio_driver.c", "src/world_audio_driver.h", "src/world_native.c", "src/world_window.c", "src/world_window.h", "src/world_boot.c", "src/world_boot.h",
    "src/world_audio_output.c", "src/world_audio_output.h", "src/world_ga20.c", "src/world_ga20.h", "LICENSES/GA20-BSD-3-Clause.txt",
    "src/world_ym.cpp", "src/world_ym.h", "LICENSES/ymfm-BSD-3-Clause.txt",
    "src/third_party/ymfm/ymfm.h", "src/third_party/ymfm/ymfm_fm.h", "src/third_party/ymfm/ymfm_fm.ipp",
    "src/third_party/ymfm/ymfm_opm.h", "src/third_party/ymfm/ymfm_opm.cpp",
    "README.md", "CREDITS.md", "PLAN.md", "achievements.json", "achievement-preview.html",
    "tools/render_world.py", "tools/render_world.c", "tools/capture_video.lua",
    "tools/capture_encounter.lua", "tools/run_encounter.py", "src/world_frame.c", "src/world_frame.h",
    "src/world_frame_video.c", "src/world_frame_video.h", "src/world_palette.c", "src/world_palette.h",
    "tools/get_border_art.ps1", "tools/world_timing.ps1", "tools/world_display.ps1", "tools/world_menu.ps1", "tools/world_controls.lua", "tools/world_borders.ps1",
    "artwork/worldborders/default.lay", "artwork/worldborders/hull.svg", "artwork/worldborders/ocean.svg", "tools/world_menu_input.ps1",
    "src/world_video.c", "src/world_video.h", "src/world_gameover.c", "src/world_title_master.c", "src/world_title_master.h", "src/world_title.c", "src/world_title.h", "src/world_enemy_target.c", "src/world_enemy_target.h", "play.ps1", "setup.ps1", "tools/import_world.py", "tools/build_world.py",
    "tools/stage_one.lua", "tools/package_source.py", "src/world_io.c",
    # Presentation, modes and service-record modules awaiting integration.
    "src/world_achievements.c", "src/world_achievements.h", "src/world_achievement_view.c", "src/world_achievement_view.h",
    "src/world_crt.c", "src/world_crt.h", "src/world_crt_shader.c", "src/shaders/world_crt.hlsl",
    "LICENSES/CRT-Hyllian-MIT.txt", "LICENSES/CRT-Lottes-Public-Domain.txt",
    "src/world_menu.c", "src/world_menu.h", "src/world_modes.c", "src/world_modes.h",
    "src/world_present.c", "src/world_present.h", "src/world_scores_store.c", "src/world_scores_store.h",
    "src/world_service.c", "src/world_service.h", "docs/HOST.md",
    "src/world_dispatch_internal.h", "src/world_actors_1.c", "src/world_actors_2.c", "src/world_actors_3.c", "src/world_actors_4.c", "src/world_actors_5.c", "src/world_actors_6.c", "src/world_stage1.c", "src/world_stage2.c", "src/world_stage3.c", "src/world_stage4.c", "src/world_stage5.c", "src/world_stage6.c", "src/world_common.c", "src/world_gaps.c", "src/world_credits.c", "src/world_coop.c", "src/world_explore1.c", "src/world_explore2.c", "tools/ref/native_run.c", "tools/ref/verify_all.sh", "tools/ref/ledger.py", "tools/ref/setup_worktree.sh", "docs/RECOVERY.md", "docs/COVERAGE.md", "docs/HOST.md", "docs/TIMING.md",
    "tools/clean_local.ps1", "tools/ref/build.mk", "tools/ref/check_replay.c", "tools/ref/ref_capture.lua", "tools/ref/run_capture.sh", "tools/ref/dasm.sh",
    "src/InTheHunt.manifest", ".gitignore", ".gitattributes",
    "tests/README.md", "tests/run_tests.ps1", "tests/achievements-test.c", "tests/clock-test.c",
    "tests/controls-remap-test.c", "tests/cycle-cost-test.c", "tests/menu-test.c", "tests/service-test.c", "tests/prefetch-test.c", "tests/timing-option-test.c",
    "src/world_player.c", "src/world_camera.c", "src/world_dispatch.c", "src/world_dispatch.h", "src/world_terrain.c", "src/world_tile_address.h", "src/world_tile_v33.c", "src/world_stage.c", "src/world_stage.h", "src/world_candidates.c", "src/world_death.c", "src/world_fire.c", "src/world_torpedo.h", "src/world_torpedo.c", "src/world_contact.c", "src/world_score.c", "tools/replay_score.c", "tools/replay_frame.c", "src/world_motion.c", "src/world_sound.c",
)

if __name__ == "__main__":
    dest = ROOT / ".local/release/inthehunt-source.zip"
    dest.parent.mkdir(parents=True, exist_ok=True)
    temporary = dest.with_suffix(".tmp")
    with ZipFile(temporary, "w", ZIP_DEFLATED) as archive:
        for name in FILES:
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



