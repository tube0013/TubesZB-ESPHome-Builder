"""Older board routing, software-flow UART, pause ownership and local probes."""

from pathlib import Path
from test_frontend_rollout import load, frontend
ROOT = Path(__file__).resolve().parents[1]


def test_model_specific_flat_manifests_and_uart_wiring():
    package = load(ROOT / 'packages/older-efr32-radio.yaml')
    flasher = package['efr32_flasher']
    assert flasher['variant'] == 'flat'
    assert flasher['hardware_flow_control'] == 'false'
    assert flasher['bootloader_baud_rate'] == '115200'
    assert flasher['bootloader_entry'] == '${older_efr32_bootloader_entry}'
    assert flasher['rst_switch'] == 'zRST_gpio'
    assert flasher['runtime_baud_select'] == 'change_baud_rate'
    for model,feed,tx,rx,boot,reset in [
        ('mgm12-pro1','mgm12','GPIO5','GPIO17','32','33'),
        ('mgm210-series-2','mgm210','GPIO5','GPIO17','32','33'),
        ('mgm210-poe-2022','mgm210_poe','GPIO16','GPIO5','4','13'),
        ('mgm210-poe-2023','mgm210_poe','GPIO4','GPIO36','16','5')]:
        path = ROOT / 'manifests' / ('tubeszb-efr32-' + model + '.yaml')
        data = load(path)
        assert data['efr32_flasher']['boot_switch']=='zBSL'
        assert data['substitutions']['older_efr32_manifest'].endswith('/efr32manifest_' + feed + '.json')
        assert (data['uart']['tx_pin'], data['uart']['rx_pin']) == (tx,rx)
        pins = {s['id']: s['pin'] for s in data['switch'] if s['platform']=='gpio'}
        assert pins['zBSL'] == boot and pins['zRST_gpio'] == reset
        ui = frontend(path)['zigbee']
        assert 'zigbee_beta_switch' not in ui
        assert ui['flash_button']=='flash_efr32' and ui['bootloader_button']=='trigger_efr32_bootloader'
        assert 'check_efr32_update' in str(data['script'])


def test_probe_and_flash_bridge_ownership_and_offline_guards():
    package = load(ROOT / 'packages/older-efr32-radio.yaml')
    # All bridge ownership belongs to a single busy callback, including the probe.
    assert 'stream_server_id' not in package['efr32_info']
    busy = package['binary_sensor'][0]
    assert '.is_paused()' in busy['on_press'][0]['lambda']
    assert busy['on_press'][1].get('stream_server.pause')
    assert '!id(radio_was_paused)' in busy['on_release'][0]['if']['condition']['lambda']
    scripts = {s['id']: s for s in package['script']}
    probe = str(scripts['boot_probe_sequence'])
    assert 'efr32_info.probe' in probe and 'internet_allowed' not in probe
    assert package['esphome']['on_boot'][0]['then']==[{'script.execute':'boot_probe_sequence'}]
    buttons = {b['id']:b for b in package['button']}
    for key in ('flash_efr32','check_efr32_update'):
        assert 'internet_allowed()' in buttons[key]['on_press'][0]['if']['condition']['lambda']
    assert 'is_applying_manifest_baud()' in str(scripts['apply_uart_baud']['then'][0])
