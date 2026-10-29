"""The new prerelease and legacy issue path must not overwrite each other's inputs."""
from pathlib import Path
import re

import pytest
import yaml

from check_legacy_static import devices, render, workflow
from test_frontend_rollout import load

ROOT = Path(__file__).resolve().parents[1]


def template(tmp_path: Path) -> Path:
    target = tmp_path / 'esphome-config/tubeszb-cc2652p2-poe-2021.yaml'
    target.parent.mkdir(parents=True)
    target.write_text('''esphome:
  name: tubeszb-cc2652p2-poe-2021
ethernet:
  manual_ip:
    static_ip: STATIC_IP
    gateway: GATEWAY
    subnet: SUBNET
external_components:
  - source: github://tube0013/esphome-components
    components: [stream_server]
''')
    return target


def test_legacy_renderer_sets_manual_ip_and_frozen_components(tmp_path: Path) -> None:
    path = template(tmp_path)
    result = render(tmp_path, 'tubeszb-cc2652p2-poe-2021')
    assert result.returncode == 0, result.stdout
    data = yaml.safe_load(path.read_text())
    assert data['ethernet']['manual_ip'] == {
        'static_ip': '192.168.40.50', 'gateway': '192.168.40.1', 'subnet': '255.255.255.0'}
    assert data['external_components'][0]['source'] == (
        'github://tube0013/esphome-components@' + workflow()['env']['LEGACY_COMPONENTS_REF'])
    assert 'network_settings:' not in path.read_text()


@pytest.mark.parametrize('overrides', [
    {'Device_Name': "bad'; echo injected"}, {'Device_Name': '../../outside'},
    {'Static_IP': "1.2.3.4'; echo injected"}, {'Static_IP': '256.1.1.1'},
    {'Gateway': '::1'}, {'Subnet': '255.0.255.0'}, {'Subnet': ''},
])
def test_invalid_issue_values_cannot_modify_templates(tmp_path: Path, overrides: dict) -> None:
    path = template(tmp_path)
    before = path.read_text()
    result = render(tmp_path, 'tubeszb-cc2652p2-poe-2021', overrides)
    assert result.returncode != 0
    assert path.read_text() == before


def test_legacy_pins_do_not_depend_on_current_release_variables() -> None:
    data = workflow()
    pins = data['env']
    assert pins['LEGACY_RELEASE_TAG'] == '2026.10.25.0'
    assert pins['LEGACY_SOURCE_REF'] == '7f968ca5aa95db46bd67e7be209946c45cb11bf5'
    assert pins['LEGACY_COMPONENTS_REF'] == '0e35b4afe1ad12b17418626d3bd75b7bcad52de6'
    assert pins['ESPHOME_LEGACY_VERSION'] == '2026.6.5'
    steps = data['jobs']['build']['steps']
    checkout = next(x for x in steps if x.get('uses', '').startswith('actions/checkout@'))
    assert checkout['with']['ref'] == '${{ env.LEGACY_SOURCE_REF }}'
    assert 'vars.ESPHOME_VERSION' not in (ROOT / '.github/workflows/main.yml').read_text()
    assert len(devices()) == 19
    assert 'startsWith(github.event.issue.title' in data['jobs']['build']['if']
    assert any(x.get('uses', '').startswith('actions/upload-artifact@v4') for x in steps)


def test_prerelease_version_and_shared_component_ref() -> None:
    hardware = [p for p in (ROOT / 'manifests').glob('*.yaml') if 'esp32' in load(p)]
    assert len(hardware) == 22
    for path in hardware:
        assert load(path)['esphome']['project']['version'] == '2026.10.29.0', path.name
    assert load(ROOT / 'packages/network-settings.yaml')['substitutions']['tubeszb_components_ref'] == 'refs/pull/7/head'
    build = load(ROOT / '.github/workflows/build.yml')
    assert build['on']['release']['types'] == ['published']
    release = build['jobs']['release-assets']['steps']
    for step in release:
        text = str(step.get('run', '')) + str(step.get('with', ''))
        if 'Latest_Release' in text or 'ota_manifests' in text and 'beta_ota_manifests' not in text:
            assert step.get('if') == '${{ !github.event.release.prerelease }}', step['name']
        if 'Beta_Release' in text or 'beta_ota_manifests' in text:
            assert step.get('if') == '${{ github.event.release.prerelease }}', step['name']


def test_existing_mgm21_form_alias_targets_the_released_filename(tmp_path: Path) -> None:
    path = template(tmp_path)
    canonical = path.with_name('tubeszb-dual-radio-mgm21-2022.yaml')
    path.rename(canonical)
    result = render(tmp_path, 'tubeszb-dual-radio-kit-mgm21-2022')
    assert result.returncode == 0, result.stdout
    assert 'STATIC_IP' not in canonical.read_text()
    assert not canonical.with_name('tubeszb-dual-radio-kit-mgm21-2022.yaml').exists()
