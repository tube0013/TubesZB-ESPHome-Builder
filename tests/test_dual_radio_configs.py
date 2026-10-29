"""Prevent UART, GPIO and release-channel bindings crossing between dual radios."""
from pathlib import Path
from test_frontend_rollout import load, frontend, resolve_values
ROOT=Path(__file__).resolve().parents[1]


def test_all_dual_radio_bindings_and_preserved_hardware():
    checked=[]
    for path in (ROOT/'manifests').glob('*.yaml'):
        data=load(path)
        if 'zwave_proxy' not in data or 'stream_server' not in data:continue
        mgm21='mgm21' in path.name
        common=load(ROOT/'packages/older-efr32-radio.yaml') if mgm21 else {}
        variables={**common.get('substitutions',{}),**data.get('substitutions',{})}
        ui=frontend(path)
        family='cc2652' if 'cc2652_flasher' in data else 'zigbee'
        radio=ui[family]
        uarts={u['id']:u for u in data['uart']}
        streams=data['stream_server'];streams=streams if isinstance(streams,list) else [streams]
        stream=next(x for x in streams if x['id']==radio['stream_server_id'])
        assert stream['port']=='6638'
        assert ui['uart_id']==stream['uart_id']=='uart_bus_zb'
        assert ui['zwave_uart_id']==data['zwave_proxy']['uart_id']=='uart_bus_zw'
        assert (uarts['uart_bus_zw']['rx_pin'],uarts['uart_bus_zw']['tx_pin'],uarts['uart_bus_zw']['baud_rate'])==('GPIO15','GPIO14','115200')
        assert (uarts['uart_bus_zb']['rx_pin'],uarts['uart_bus_zb']['tx_pin'])==(('GPIO5','GPIO16') if mgm21 else ('GPIO36','GPIO4'))
        reset=next(x for x in data['switch'] if x.get('id')=='zRST_gpio')
        assert reset['pin']==('2' if mgm21 else '5')
        buttons={x.get('id'):x for x in common.get('button',[])+data['button']}
        reset_button=buttons[radio['reset_button']]
        assert {'switch.turn_on':'zRST_gpio'} in reset_button['on_press']
        assert {'switch.turn_off':'zRST_gpio'} in reset_button['on_press']
        for key,value in radio.items():
            if key.endswith('_button'):assert value in buttons,(path.name,key,value)
        key='cc2652_flasher' if family=='cc2652' else 'efr32_flasher'
        flasher=resolve_values(common[key],variables) if mgm21 else data[key]
        assert flasher['uart_id']==ui['uart_id']
        switches={x['id']:resolve_values(x,variables) for x in common.get('switch',[])+data['switch'] if x['platform']=='template'}
        gateway=switches['beta_channel_switch'];assert gateway['restore_mode']=='RESTORE_DEFAULT_OFF'
        ota=data['update'][0]['source']
        assert ota in str(gateway['turn_off_action'])
        assert ota.rsplit('/',1)[-1] in str(gateway['turn_on_action'])
        assert '/beta_ota_manifests/' in str(gateway['turn_on_action'])
        if mgm21:
            assert 'boot_switch' not in flasher and flasher['bootloader_entry']=='ezsp'
            assert flasher['hardware_flow_control']=='false' and flasher['variant']=='flat'
            assert flasher['url'].endswith('/efr32manifest_mgm210_poe.json')
            assert all(x.get('id')!='zBSL' for x in data['switch'])
            boot=next(x for x in data['binary_sensor'] if x.get('id')=='zBSL')
            assert boot['pin']=={'number':'GPIO36','inverted':'true'}
            assert 'zigbee_beta_switch' not in radio
        else:
            assert switches['zigbee_fw_beta_switch']['restore_mode']=='RESTORE_DEFAULT_OFF'
        if family=='cc2652':
            assert data['packages']['cc2652_flash']['vars']['cc2652_stream_server']=='zb'
            assert radio['boot_probe_script']=='probe_cc2652_on_boot'
        else:
            boot=common.get('esphome',{}).get('on_boot',[])+data['esphome']['on_boot']
            assert any({'script.execute':'boot_probe_sequence'} in item['then'] for item in boot)
            scripts={x['id']:x for x in common.get('script',[])+data['script']}
            assert 'internet_allowed' not in str(scripts['boot_probe_sequence'])
            assert 'efr32_info.probe' in str(scripts['boot_probe_sequence'])
            if mgm21:
                baud=resolve_values(scripts['apply_uart_baud'],variables)
                assert 'id(uart_bus_zb)' in str(baud) and 'uart_bus_zw' not in str(baud)
                assert 'efr32_flasher.enter_bootloader' in str(scripts['fw_update_mode'])
        checked.append(path.name)
    assert len(checked)==6
