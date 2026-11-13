# Release structure audit — 13 November 2026

Scope: repository presentation and release packaging, not a fresh gameplay qualification. No remote is configured in this checkout. Existing gameplay and packaging edits were preserved.

## Comparable projects

| Project | Observed structure | Apply here |
|---|---|---|
| [CPS-2 Recomp](https://github.com/tmcleroy/cps2recomp) | Player setup and exact ROM requirements in README; `docs/`, `tools/`, `third_party/`, VERSION, CHANGELOG and third-party notices. [v1.5.0](https://github.com/tmcleroy/cps2recomp/releases/tag/v1.5.0) has platform archives, installation instructions, known issues and SHA256SUMS.txt. | Closest arcade-native-port comparison. Ship one complete Windows archive, explicit data requirements, concise release notes and checksums. |
| [DevilutionX](https://github.com/diasurgical/DevilutionX) | Short introduction and installation path, separate manual/build documentation, `Packaging/`, `docs/`, `test/`, VERSION. [1.5.5](https://github.com/diasurgical/DevilutionX/releases/tag/1.5.5) separates platform binaries and source archives. | Separate player and developer documentation; distinguish downloadable binaries from source. No need to adopt its larger build hierarchy. |
| [Sonic Mania decompilation](https://github.com/RSDKModding/Sonic-Mania-Decompilation) | Original assets required; explicit source/submodule build instructions; dependency and licence files. [v1.1.1](https://github.com/RSDKModding/Sonic-Mania-Decompilation/releases/tag/v1.1.1) has architecture-specific ZIPs and concrete fixes. | State supported data and architecture. Do not copy its submodule warning literally: this project has a different dependency setup. |

README contents and repository roots were inspected, together with each release's body and asset names. Binary interiors were not downloaded or audited. These are structural comparisons, not endorsements of licence compatibility.

## Decisions

- Keep the established `src/`, `tools/`, `LICENSES/` and launcher paths. Moving C files would add build risk without improving the download experience.
- Move the full options manual to PLAYER-GUIDE.md, build/source-map notes to DEVELOPMENT.md, and historical test claims to VERIFICATION.md. Retain all existing detailed sections.
- Keep generated releases under ignored `.local/release/`; attach archives to GitHub Releases rather than committing them. This is a native Windows port, not a MiSTer RBF release.
- Retain the current binary filename `InTheHunt-Windows-x64.zip`. The release tag identifies the version; record the exact commit and hashes alongside it.
- Add a release-note draft and a concrete publication checklist. Select a version only when the candidate is frozen; do not invent v1.0.0 qualification.

## Outstanding findings

1. No top-level project licence exists. CREDITS describes both reference-only material and possible donor use; reconcile actual incorporated code and its notices before selecting the project's licence. No licence was invented during this audit.
2. `package_source.py` enumerates tracked files but reads working-tree contents. It omits new untracked headers and can package modified files under an unrelated commit identity. Freeze and commit the intended source, confirm a clean tree, then package it.
3. `package_native.py` packages the existing executable; it does not build or prove its relationship to current source. Final packaging requires a source-matched build and recorded tests.
4. No Git remote or public release URL is configured. Add the real download/issue links after the destination is selected; publication requires a separate explicit instruction.
5. Existing CREDITS references ignored donor checkouts and older MAME presentation paths. Review these as historical references versus shipped native dependencies; a credited download is not evidence of artwork redistribution permission. Keep downloaded border art and ROM data outside the archive.
6. The current binary packager writes a useful START-HERE.txt, but does not include the complete README/player guide, a version/source manifest or archive checksum. Include these before release and verify the ZIP contents. Preserve every dependency and shader notice.

This preparation does not declare a full release ready. The unresolved source/licence/provenance checks are material, not cosmetic.
