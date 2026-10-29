"""Execute the real issue renderer against a frozen checkout; validate/build without posting an issue."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

import yaml

ROOT = Path(__file__).resolve().parents[1]


def workflow() -> dict:
    return yaml.safe_load((ROOT / '.github/workflows/main.yml').read_text())


def devices() -> list[str]:
    form = yaml.safe_load((ROOT / '.github/ISSUE_TEMPLATE/esphome-static-ip-configuration.yml').read_text())
    return next(x['attributes']['options'] for x in form['body'] if x.get('id') == 'device_name')


def model_name(model: str) -> str:
    return json.loads(workflow()['env']['LEGACY_DEVICE_ALIASES']).get(model, model)


def renderer() -> str:
    step = next(x for x in workflow()['jobs']['build']['steps']
                if x['name'] == 'Validate input and render frozen static configuration')
    lines = step['run'].strip().splitlines()
    assert lines[0] == "python - <<'PYLEGACY'" and lines[-1] == 'PYLEGACY'
    return '\n'.join(lines[1:-1])


def render(checkout: Path, model: str, overrides: dict | None = None) -> subprocess.CompletedProcess:
    payload = {'Device_Name': model, 'Static_IP': '192.168.40.50',
               'Gateway': '192.168.40.1', 'Subnet': '255.255.255.0', **(overrides or {})}
    with tempfile.NamedTemporaryFile() as env_file:
        env = {**os.environ, **workflow()['env'], 'ISSUE_PAYLOAD': json.dumps(payload), 'GITHUB_ENV': env_file.name}
        return subprocess.run([sys.executable, '-c', renderer()], cwd=checkout, env=env,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True, help='Checkout of the pinned legacy release')
    parser.add_argument('--compile', action='append', default=[], dest='builds')
    args = parser.parse_args()
    for model in devices():
        result = render(args.root, model)
        if result.returncode:
            print(result.stdout)
            return result.returncode
        config = args.root / 'esphome-config' / (model_name(model) + '.yaml')
        check = subprocess.run([sys.executable, '-m', 'esphome', 'config', str(config)],
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        print(('PASS ' if check.returncode == 0 else 'FAIL ') + model, flush=True)
        if check.returncode:
            print(check.stdout)
            return check.returncode
    print(str(len(devices())) + ' frozen legacy configurations validated.', flush=True)
    for model in args.builds:
        if model not in devices():
            raise SystemExit('Compile target is absent from the preserved form: ' + model)
        result = subprocess.run([sys.executable, '-m', 'esphome', 'compile',
                                 str(args.root / 'esphome-config' / (model_name(model) + '.yaml'))])
        if result.returncode:
            return result.returncode
        build_root = args.root / 'esphome-config/.esphome/build' / model_name(model)
        for kind in ('factory', 'ota'):
            if not next(build_root.rglob('firmware.' + kind + '.bin'), None):
                raise SystemExit('Missing legacy artifact: firmware.' + kind + '.bin for ' + model)
        print('BUILD PASS ' + model + ' (factory + OTA artifacts found)', flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
