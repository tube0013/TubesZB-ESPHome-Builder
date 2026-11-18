"""Hardware binding contract for the shared CC2652 flash automation."""
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]

def test_cc2652_flash_scripts_bind_the_radio_uart():
    """Every CC2652 config pauses its Zigbee UART, including dual-radio models."""
    import yaml

    def plain(node):
        if isinstance(node, yaml.ScalarNode):
            return node.value
        if isinstance(node, yaml.SequenceNode):
            return [plain(value) for value in node.value]
        return {plain(key): plain(value) for key, value in node.value}

    checked = []
    for path in (ROOT / "manifests").glob("*.yaml"):
        source = path.read_text()
        config = plain(yaml.compose(source))
        if "cc2652_flasher" not in config:
            continue
        package = config["packages"]["cc2652_flash"]
        assert Path(package["file"]).name == "cc2652-flash.yaml"
        stream_id = package["vars"]["cc2652_stream_server"]
        streams = config["stream_server"]
        if isinstance(streams, dict):
            streams = [streams]
        stream = next(item for item in streams if item["id"] == stream_id)
        assert stream["uart_id"] == config["cc2652_flasher"]["uart_id"], path.name
        button = next(item for item in config["button"] if item.get("id") == "flash_cc2652")
        assert button["on_press"]["then"] == [{"script.execute": "flash_cc2652_safely"}], path.name
        assert "cc2652_flasher.update_firmware" not in source, path.name
        checked.append(path.name)
    assert len(checked) >= 9


def test_long_cc2652_flash_observes_result_after_wait_timeout():
    """Expiry skips the wait predicate; cleanup must still read the native result."""
    from test_frontend_rollout import load
    package=load(ROOT/'packages/cc2652-flash.yaml')
    flash=next(x for x in package['script'] if x['id']=='flash_cc2652_safely')
    actions=flash['then'][0]['if']['then']
    wait=next(i for i,x in enumerate(actions) if 'wait_until' in x)
    assert actions[wait]['wait_until']['timeout']=='35s'
    assert actions[wait+1]=={'lambda':'id(cc2652_flash_session).observe(id(zigbee_flasher).get_flash_status());'}
    failure=actions[wait+2]['if']
    assert '!id(cc2652_flash_session).completed' in failure['condition']['lambda']
    success=actions[wait+3]['if']
    assert success['condition']['lambda']=='return id(cc2652_flash_session).success;'
    assert success['then']==[{'delay':'5s'}]
    assert 'resume_after' in actions[wait+4]['if']['condition']['lambda']


def test_saved_cc2652_variant_override_is_shared_by_every_model():
    from test_frontend_rollout import load
    package = load(ROOT / 'packages/cc2652-flash.yaml')
    control = next(s for s in package['select'] if s['id'] == 'cc2652_variant_override')
    assert control['initial_option'] == 'Auto'
    assert control['options'] == ['Auto', 'P2', 'P7']
    assert control['restore_value'].lower() == 'true'
    assert control['optimistic'].lower() == 'true'
    assert float(control['setup_priority']) > 800
    assert 'set_variant(i == 1 ? 2 : i == 2 ? 7 : 0)' in control['on_value']['then'][0]['lambda']
    for name in ('cc2652-web.yaml', 'dual-cc2652-web.yaml'):
        frontend = load(ROOT / 'packages' / name)
        assert frontend['tubeszb_web']['cc2652']['variant_select'] == control['id']
    paths = list((ROOT / 'manifests').glob('*cc2652*.yaml'))
    assert len(paths) == 9
    for path in paths:
        config = load(path)
        assert config['cc2652_flasher']['variant'] == 'auto'
        entry = load(ROOT / 'esphome-config' / path.name)
        assert entry['packages']['firmware'].endswith('/' + path.name)
