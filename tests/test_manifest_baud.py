"""Manifest parsing and numeric UART persistence, including legacy selector migration."""
from component_paths import COMPONENTS_ROOT

import os
from pathlib import Path
import subprocess
import pytest
ROOT = Path(__file__).resolve().parents[1]


def test_runtime_baud_persistence(tmp_path):
    exe = tmp_path / 'runtime-baud'
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(COMPONENTS_ROOT.parent),
                    '-I', str(ROOT / 'tests/baud_stubs'), str(ROOT / 'tests/runtime_baud_test.cpp'),
                    '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)


def test_manifest_baud_parser(tmp_path):
    candidates = list((ROOT / 'manifests/.esphome/build').glob('*/managed_components/bblanchon__arduinojson/src'))
    include = os.environ.get('ARDUINOJSON_INCLUDE') or (str(candidates[0]) if candidates else None)
    if not include:
        pytest.skip('Set ARDUINOJSON_INCLUDE or compile an ESPHome image to supply ArduinoJson')
    exe = tmp_path / 'manifest-baud'
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(COMPONENTS_ROOT.parent), '-I', include,
                    str(ROOT / 'tests/manifest_baud_test.cpp'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)


def test_all_native_flashers_bind_shared_baud_support():
    # Catch a forgotten model or a duplicate remote source silently replacing the patch.
    from test_frontend_rollout import load
    counts = {'cc2652_flasher': 0, 'efr32_flasher': 0}
    for path in (ROOT / 'manifests').glob('*.yaml'):
        data = load(path)
        if 'older_efr32_profile' in data.get('substitutions', {}):
            common = load(ROOT / 'packages/older-efr32-radio.yaml')
            data['efr32_flasher'] = {**common['efr32_flasher'], **data.get('efr32_flasher', {})}
            for key in ('external_components', 'select', 'script'):
                data[key] = common.get(key, []) + data.get(key, [])
        for family in counts:
            if family not in data:
                continue
            counts[family] += 1
            sources = [s for s in data['external_components'] if family in s['components']]
            assert len(sources) == 1, path.name
            assert sources[0]['source'] == 'github://tube0013/esphome-components@${tubeszb_components_ref}', path.name
            assert 'zigbee_firmware_baud' in sources[0]['components'], path.name
            selector = next((s for s in data.get('select', []) if s['id'] == 'change_baud_rate'), None)
            if selector:
                assert data[family]['runtime_baud_select'] == selector['id'], path.name
                script = next(s for s in data['script'] if s['id'] == 'apply_uart_baud')
                guard = script['then'][0]['if']
                assert '.is_applying_manifest_baud()' in guard['condition']['lambda'], path.name
                assert guard['then'] == [{'script.stop': 'apply_uart_baud'}], path.name
    assert counts == {'cc2652_flasher': 9, 'efr32_flasher': 9}


def test_cc2652_native_member_layout_matches():
    # Upstream declares this class in both translation units. Layout drift corrupts memory.
    import re
    layouts = []
    for suffix in ('h', 'cpp'):
        text = (COMPONENTS_ROOT / 'cc2652_flasher' / ('cc2652_flasher.' + suffix)).read_text()
        fields = text.split('esphome::uart::UARTComponent *uart_', 1)[1].split('\n};', 1)[0]
        layouts.append(re.sub(r'\s+', '', re.sub(r'//[^\n]*', '', fields)))
    assert layouts[0] == layouts[1]


def test_all_external_component_sources_are_github():
    from test_frontend_rollout import load
    source='github://tube0013/esphome-components@${tubeszb_components_ref}'
    for folder in ('manifests','packages'):
        for path in (ROOT/folder).glob('*.yaml'):
            data=load(path)
            for item in data.get('external_components',[]):
                assert item['source']==source,path.name
    common=load(ROOT/'packages/network-settings.yaml')
    ref=common['substitutions']['tubeszb_components_ref']
    assert ref and '${' not in ref
