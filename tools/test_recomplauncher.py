"""Integration checks against the built package; never opens a gameplay window.

Requires jsonschema and pefile. Keep ROM fixtures and output in ignored build/.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--rom', type=Path, required=True)
    parser.add_argument('--schema', type=Path, required=True)
    parser.add_argument('--settings-test', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    import jsonschema
    import pefile
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    profile = json.loads((ROOT / 'recomplauncher.json').read_text())
    jsonschema.validate(profile, json.loads(args.schema.read_text()))
    env = {k: v for k, v in os.environ.items() if not k.startswith('WORLD_')}
    env['RECOMPLAUNCHER'] = '1'
    def run(command, label, expected=0, cwd=ROOT):
        result = subprocess.run([str(x) for x in command], cwd=cwd, env=env,
                                capture_output=True, timeout=300,
                                creationflags=subprocess.CREATE_NO_WINDOW)
        (out / (label + '.log')).write_bytes(result.stdout + result.stderr)
        assert (result.returncode == 0) == (expected == 0), (label, result.returncode)
        return result
    config = out / 'settings.ini'
    harness = args.settings_test.resolve()
    run([harness, 'defaults', config], 'defaults')
    def read_config():
        return dict(line.split('=', 1) for line in config.read_text().splitlines()
                    if '=' in line and not line.startswith('#'))
    defaults = read_config()
    checks = 0
    for setting in profile['settings']:
        for key, value in setting['defaults'].items():
            assert defaults[key] == value, (key, defaults[key], value)
        cases = [choice['set'] for choice in setting.get('choices', [])]
        if 'slider' in setting:
            slider = setting['slider']
            cases += [{slider['key']: str(slider[n])} for n in ('min', 'max')]
        for values in cases:
            config.write_text('version=1\n' + ''.join(f'{k}={v}\n' for k, v in values.items()))
            run([harness, 'roundtrip', config], f'settings-{checks}')
            saved = read_config()
            assert all(saved[k] == v for k, v in values.items()), values
            checks += 1
    install = out / 'install with spaces & symbols'
    package = ROOT / '.local/release/InTheHunt-Windows-x64.zip'
    with zipfile.ZipFile(package) as archive:
        assert archive.testzip() is None
        assert 'recomplauncher.json' in archive.namelist()
        assert not any(n.startswith('.local/') for n in archive.namelist())
        archive.extractall(install)
    exe = install / 'InTheHunt.exe'
    pe = pefile.PE(str(exe))
    resources = {entry.id for entry in pe.DIRECTORY_ENTRY_RESOURCE.entries}
    assert 3 in resources and 14 in resources, 'Executable icon/group missing'
    pe.close()
    # Path punctuation and non-ASCII are data, never shell syntax.
    rom = out / 'World & copy \u03a9.zip'
    shutil.copyfile(args.rom, rom)
    setup = install / 'InTheHunt-Setup.exe'
    run([setup, rom], 'fresh-import', cwd=install)
    world = install / '.local/world'
    regions = ['maincpu.bin', 'soundcpu.bin', 'gfx1.bin', 'gfx2.bin', 'irem.bin']
    before = {n: sha(world / n) for n in regions}
    settings = world / 'native-settings.ini'
    settings.write_text('version=1\nvolume=35\nfullscreen=0\n')
    original = settings.read_bytes()
    run([setup, rom], 'repeat-import', cwd=install)
    assert settings.read_bytes() == original
    bad = out / 'wrong.zip'
    with zipfile.ZipFile(bad, 'w') as archive:
        archive.writestr('wrong.txt', 'not the World ROM set')
    run([setup, bad], 'reject-wrong-rom', expected=1, cwd=install)
    assert before == {n: sha(world / n) for n in regions}
    assert settings.read_bytes() == original
    # Updates must not include or overwrite live settings/data.
    with zipfile.ZipFile(package) as archive:
        archive.extractall(install)
    assert settings.read_bytes() == original
    assert before == {n: sha(world / n) for n in regions}
    script = out / 'smoke.txt'
    script.write_text('0 0 0\n300 0 4\n306 0 0\n420 0 1\n426 0 0\n2040 ac1 0\n3600 end\n')
    run([exe, world, '--headless=' + str(script)], 'gameplay', cwd=install)
    actors = (world / 'headless-actors.txt').read_text()
    assert actors.startswith('frame 3600'), actors[:80]
    assert not (world / 'native-stops.tsv').exists()
    receipt = {'schema': 'PASS', 'settings_roundtrips': checks,
               'fresh_import': 'PASS', 'repeat_import_preserves_settings': 'PASS',
               'invalid_rom_preserves_existing_data': 'PASS',
               'update_preserves_data_and_settings': 'PASS', 'headless_frames': 3600,
               'embedded_icon': 'PASS', 'executable_sha256': sha(exe),
               'zip_sha256': sha(package),
               'limits': 'No RecompLauncher UI, Big Picture display or catalogue submission test.'}
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(receipt, indent=2))

if __name__ == '__main__':
    main()
