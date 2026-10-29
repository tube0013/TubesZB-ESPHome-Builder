"""Exercise the release guard without any third-party Python modules installed."""
from pathlib import Path
import os
import subprocess
import sys

import pytest

from test_frontend_rollout import load

ROOT = Path(__file__).resolve().parents[1]


@pytest.mark.parametrize('version,supported', [
    ('2026.10.1', True), ('2026.10.2', True), ('2026.11.0', True),
    ('2026.10.1+local', True), ('2026.10.1.post1', True),
    ('2026.10.0', False), ('2026.6.5', False),
    ('2026.10.1b1', False), ('2026.10.1rc1', False), ('2026.10.1.dev0', False),
    ('2026.11.0b1', True), ('unknown', False),
])
def test_release_guard_uses_cli_without_site_packages(tmp_path: Path, version: str, supported: bool) -> None:
    job = load(ROOT / '.github/workflows/build.yml')['jobs']['build-firmwares']
    step = next(s for s in job['steps'] if s.get('name') == 'Require the supported ESPHome API version')
    script = step['run']
    assert script.startswith("python - <<'PYVERSION'\n")
    guard = script.split('\n', 1)[1].rsplit('\nPYVERSION', 1)[0]
    # The actual guard runs under -S, so it cannot accidentally rely on pytest's
    # or ESPHome's site-packages as the previous packaging import did.
    cli = tmp_path / 'esphome'
    cli.write_text('#!' + sys.executable + '\nimport sys\n'
                   'assert sys.argv[1:] == ["version"]\n'
                   'print(' + repr('Version: ' + version) + ')\n')
    cli.chmod(0o755)
    env = {**os.environ, 'PATH': str(tmp_path) + os.pathsep + os.environ['PATH']}
    result = subprocess.run([sys.executable, '-S', '-c', guard], env=env, capture_output=True, text=True)
    assert (result.returncode == 0) == supported, result.stderr
    if not supported:
        assert 'required' in result.stderr or 'Could not determine' in result.stderr
