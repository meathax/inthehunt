# RecompLauncher integration

The Windows package ships `recomplauncher.json` at its root. The profile asks for a World `inthunt.zip` or `inthunt.7z` through a named file input. `InTheHunt-Setup.exe` invokes the existing PowerShell importer without a window, validates chip sizes and SHA-1 hashes, and returns nonzero on failure. It neither compiles nor starts the game. The selected archive is passed through a child-process environment variable, not interpolated into shell code.

Play starts `InTheHunt.exe` directly with `{install}/.local/world`. Big Picture mode starts fullscreen; desktop launch retains the player's display preference. The existing keyboard/XInput controls are unchanged. Generic launcher controller labels do not imply native PlayStation-controller support.

The launcher edits the sectionless `native-settings.ini` through its INI adapter. Defaults mirror the game's defaults. Backup scope is that settings file only: imported ROM data is deliberately excluded. The release does not ship live settings. Built-in CRT shaders are exposed; external launcher Looks are disabled to avoid layering another filter over the native presenter.

## Local checks

The 13 November check passed the upstream JSON schema, 59 settings roundtrips through the actual C parser, executable icon-resource checks, fresh and repeated imports, rejection of an invalid archive without changing existing data, update preservation, and a 3,600-frame headless replay. Archive paths with spaces, an ampersand and a Greek character were tested. Windows tar's non-ASCII filename failure was repaired by staging a temporary ASCII archive name. Receipt: `build/recomplauncher-20261113/check-02/receipt.json` (local, ignored).

Build with `Build-WorldNative`, then run `python tools/package_native.py`. Use the tests and receipts under `build/recomplauncher-20261113/` for this integration. These checks do not replace the launcher's own Developer tools or interactive controller testing.

## Remaining launcher and catalogue steps

Desktop/browser automation was explicitly excluded by the user. In RecompLauncher, enable Settings → Developers → Developer tools, use Add a build on the extracted release, and check the profile. Test selecting valid/invalid archives, changing a setting, Play, Big Picture, and update preservation. The source archive and checksums are not player downloads.

Submit `https://github.com/meathax/inthehunt` through [Submit a Game](https://recomplauncher.com/submit?platform=pc), describing it as an unofficial native Windows port of the World arcade set. Tell the maintainers that the project maintains the shipped profile. A local manifest alone does not create a catalogue listing. Publish the tested Windows ZIP on a GitHub release before expecting catalogue installation to work.

References: [Developer guide](https://github.com/elripalda/RecompLauncher/blob/main/docs/DEVELOPERS.md), [named inputs](https://github.com/elripalda/RecompLauncher/blob/main/docs/SETUP-INPUTS.md), [schema](https://github.com/elripalda/RecompLauncher/blob/main/docs/recomplauncher.schema.json).
