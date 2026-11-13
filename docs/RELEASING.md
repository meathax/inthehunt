# Preparing a native Windows release

## Freeze the candidate

- Resolve the project licence and reconcile incorporated donor code with CREDITS and LICENSES. Retain upstream notices and any required corresponding source.
- Review and commit the intended changes, including new headers and documentation. Confirm `git status --short` is empty. The existing source packager only includes tracked files.
- Choose the release tag and record `git rev-parse HEAD`. Do not label an untested working-tree build as that commit.
- Follow [DEVELOPMENT.md](DEVELOPMENT.md) to build the native executable and pinned shader bundle. Record tool/dependency versions and the executable SHA-256.
- Run relevant gameplay regressions and a fresh-extraction launch with user-supplied World data. Test keyboard/controller, two-player join, pause/settings persistence, shader fallback and exit. Keep original data and captures local. Record what was actually tested and remaining limitations.

## Archive contract

Use `python tools/package_native.py` and `python tools/package_source.py` only after candidate qualification. Outputs live in `.local/release/`.

The player ZIP must contain `InTheHunt.exe`, `play.cmd`, `play.ps1`, the importer helper and ROM metadata under `tools/`, both CRT runtime DLLs, `shaders/` with SOURCES.md and notices, LICENSES, CREDITS and START-HERE.txt. Add the player guide and applicable project licence before publishing. Include the dependencies of every linked document, or make package links point to the exact public source tag.

Do not include ROM archives, imported `.local/world`, settings, snapshots, screenshots from test runs, donor checkouts, compiler caches, credentials or private paths. Do not copy the entire `.local/` tree into a release.

Attach these assets to the selected GitHub release:

| Asset | Purpose |
|---|---|
| `InTheHunt-Windows-x64.zip` | Complete portable player distribution |
| `inthehunt-source.zip` | Reviewed source corresponding to the binary |
| `SHA256SUMS.txt` | SHA-256 and filename of each attached ZIP |
| `release-manifest.json` | Tag, full source commit, executable and ZIP hashes, dependency pins, build command and verification summary |

Verify ZIP integrity and enumerate the archive contents. Extract into a new writable directory and launch through the packaged `play.cmd`, not the checkout. Hash the final archives only after their contents are fixed. Preserve the manifest and test evidence; a successful ZIP operation is not a gameplay pass.

## Publication

Complete [RELEASE-NOTES.md](../RELEASE-NOTES.md), replacing the draft status with the chosen version and actual verification results. Link the real release and issue tracker from README. Keep known limitations visible. A screenshot of the actual port would improve the landing page, but no fabricated or unrelated image should stand in for one.

Publishing, pushing and creating remote releases require the user's explicit instruction. This checklist does not grant it.
