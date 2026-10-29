from pathlib import Path
import yaml

ROOT = Path(__file__).resolve().parents[1]


def test_every_manifest_gates_online_actions():
    def plain(node):
        if isinstance(node, yaml.ScalarNode): return node.value
        if isinstance(node, yaml.SequenceNode): return [plain(x) for x in node.value]
        return {plain(k): plain(v) for k, v in node.value}
    gated = 'id(device_network_settings).internet_allowed()'
    checked = []
    def walk(node, guarded=False):
        if isinstance(node, list):
            for x in node: walk(x, guarded)
        elif isinstance(node, dict):
            for key, value in node.items():
                if key == 'if':
                    cond = str(value.get('condition', ''))
                    walk(value.get('then', []), guarded or gated in cond)
                    walk(value.get('else', []), guarded)
                elif key in ('update.check', 'component.update') and value == 'esphome_fw_updater':
                    assert guarded, key
                elif key in ('cc2652_flasher.check_update', 'efr32_flasher.check_update',
                             'cc2652_flasher.update_firmware', 'efr32_flasher.update_firmware'):
                    assert guarded, key
                else: walk(value, guarded)
    for path in (ROOT / 'manifests').glob('*.yaml'):
        data = plain(yaml.compose(path.read_text()))
        if 'script' not in data: continue  # Personal wrappers include a hardware manifest.
        walk(data)
        for name in ('check_connectivity', 'run_update_checks'):
            script = next(x for x in data['script'] if x['id'] == name)
            assert '!'+gated in str(script['then'][0])
            assert 'script.stop' in str(script['then'][0])
        if 'cc2652_flasher' in data:
            assert data['cc2652_flasher']['check_on_boot'] == 'false'
            assert data['cc2652_flasher']['check_interval_days'] == '0'
        assert data['update'][0]['update_interval'] == 'never'
        checked.append(path.name)
    assert len(checked) >= 22
    # Shared CC flash script gates before pausing or queuing a manifest request.
    assert 'internet_allowed() && id(cc2652_flash_session).begin' in (ROOT/'packages/cc2652-flash.yaml').read_text()


def test_cc2652_startup_identity_is_local_and_unconditional():
    def plain(node):
        if isinstance(node, yaml.ScalarNode): return node.value
        if isinstance(node, yaml.SequenceNode): return [plain(x) for x in node.value]
        return {plain(k): plain(v) for k, v in node.value}
    package = plain(yaml.compose((ROOT/'packages/cc2652-flash.yaml').read_text()))
    assert package['esphome']['on_boot'] == [{'priority': '-100', 'then': [{'script.execute': 'probe_cc2652_on_boot'}]}]
    probe = next(x for x in package['script'] if x['id'] == 'probe_cc2652_on_boot')
    assert probe['mode'] == 'single'
    assert 'internet_allowed' not in str(probe)
    assert 'is_trial_active' not in str(probe)
    assert 'http' not in str(probe).lower()
    actions = probe['then']
    pause = next(i for i, x in enumerate(actions) if 'stream_server.pause' in x)
    detect = next(i for i, x in enumerate(actions) if 'cc2652_flasher.detect_variant' in x)
    restore = next(i for i, x in enumerate(actions) if 'if' in x)
    assert pause < detect < restore
    assert actions[restore]['if']['then'][0] == {'stream_server.resume': {'id': '${cc2652_stream_server}'}}
    assert 'cc2652_boot_probe_was_paused' in str(actions[restore]['if']['condition'])
    checked = []
    for path in (ROOT/'manifests').glob('*.yaml'):
        data = plain(yaml.compose(path.read_text()))
        if 'cc2652_flasher' not in data: continue
        assert 'cc2652_flash' in data['packages']
        updates = next(x for x in data['script'] if x['id'] == 'run_update_checks')
        assert {'script.wait': 'probe_cc2652_on_boot'} in updates['then']
        assert 'cc2652_flasher.detect_variant' not in str(updates)
        assert 'request_cc2652_probe' not in str(updates)
        checked.append(path.name)
    assert len(checked) == 9


def test_online_reenable_and_no_duplicate_weekly_checks():
    from test_frontend_rollout import load
    common=load(ROOT/'packages/network-settings.yaml')
    assert common['network_settings']['on_internet_enabled']==[{'script.execute':'check_connectivity'}]
    checked=0
    for path in (ROOT/'manifests').glob('*.yaml'):
        data=load(path)
        if 'esp32' not in data: continue
        assert data['esphome']['min_version']=='2026.10.1'
        assert data['interval'][0]['then']==[{'script.execute':'check_connectivity'}]
        if '"conn_check"' in path.read_text():
            source=path.read_text()
            assert 'begin_connectivity_check()' in source
            assert source.count('end_connectivity_check()')==2
            assert 'if (created != pdPASS)' in source
        checked+=1
    assert checked==22
