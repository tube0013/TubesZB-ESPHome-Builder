"""Every hardware/release entry point must inherit the branded frontend."""
from pathlib import Path
import yaml

ROOT=Path(__file__).resolve().parents[1]

def load(path):
    def plain(node):
        if isinstance(node,yaml.ScalarNode):return node.value
        if isinstance(node,yaml.SequenceNode):return [plain(v) for v in node.value]
        return {plain(k):plain(v) for k,v in node.value}
    return plain(yaml.compose(path.read_text()))

def merge(left,right):
    result=dict(left)
    for key,value in right.items():
        result[key]=merge(result.get(key,{}),value) if isinstance(value,dict) and isinstance(result.get(key,{}),dict) else value
    return result

def resolve_values(node, variables):
    if isinstance(node,str):
        for key,value in variables.items():node=node.replace('${'+key+'}',value)
        return node
    if isinstance(node,list):return [resolve_values(v,variables) for v in node]
    if isinstance(node,dict):return {k:resolve_values(v,variables) for k,v in node.items()}
    return node


def frontend(path, inherited=None):
    data=load(path);result={}
    variables={**data.get('substitutions',{}),**(inherited or {})}
    for value in data.get('packages',{}).values():
        include=value.get('file') if isinstance(value,dict) else value
        if isinstance(include,str):result=merge(result,frontend((path.parent/include).resolve(),variables))
    return resolve_values(merge(result,data.get('tubeszb_web',{})),variables)


def test_every_hardware_and_compatibility_entry_has_ui():
    checked=[]
    for path in (ROOT/'manifests').glob('*.yaml'):
        data=load(path)
        if 'esp32' not in data:continue
        ui=frontend(path)
        assert ui['network_settings_id']=='device_network_settings',path.name
        assert ui['ethernet_id']==data['ethernet']['id'],path.name
        assert ui['firmware_update_id']=='esphome_fw_updater',path.name
        assert ui['uart_id'] in [x['id'] for x in (data['uart'] if isinstance(data['uart'],list) else [data['uart']])]
        wrapper=ROOT/'esphome-config'/path.name
        assert wrapper.exists()
        assert frontend(wrapper)==ui
        checked.append(path.name)
    assert len(checked)>=22


def test_mgm210_poe_existing_controls_and_distinct_wiring():
    for year,rx,tx,reset,boot in [('2022','GPIO5','GPIO16','13','4'),('2023','GPIO36','GPIO4','5','16')]:
        path=ROOT/'manifests'/('tubeszb-efr32-mgm210-poe-'+year+'.yaml')
        data=load(path);ui=frontend(path);radio=ui['zigbee']
        assert ui['model']=='TubesZB MGM210 PoE '+year
        assert data['substitutions']['older_efr32_stream']=='stream_srv'
        assert (data['uart']['rx_pin'],data['uart']['tx_pin'])==(rx,tx)
        pins={x['id']:x['pin'] for x in data['switch'] if x['platform']=='gpio'}
        assert pins['zRST_gpio']==reset and pins['zBSL']==boot
        buttons={x.get('id') for x in data['button']}
        for name in ('reset_button','pause_button','resume_button'):assert radio[name] in buttons
        assert radio['baud_select']=='change_baud_rate'
        selected=next(x for x in load(ROOT/'packages/older-efr32-radio.yaml')['select'] if x['id']==radio['baud_select'])
        assert selected['restore_value'].lower()=='true'


def test_retired_silabs_match_archived_hardware_and_preserve_default_baud():
    # Independently transcribed from tube_gateways/models/retired:
    # tubeszb-efr32-MGM112-pro1/.../tubeszb-efr32-mgm12-pro1.yaml and
    # tubeszb-efr32-MGM210_Series_2/.../tubeszb-efr32-mgm210-series-2.yaml.
    for name,model in [('tubeszb-efr32-mgm12-pro1','MGM12'),('tubeszb-efr32-mgm210-series-2','MGM210')]:
        path=ROOT/'manifests'/(name+'.yaml');data=load(path);ui=frontend(path)
        eth=data['ethernet']
        assert (eth['type'],eth['mdc_pin'],eth['mdio_pin'])==('LAN8720','GPIO23','GPIO18')
        assert eth['clk']=={'pin':'GPIO0','mode':'CLK_EXT_IN'}
        assert (eth['phy_addr'],eth['power_pin'])==('1','GPIO16')
        assert (data['uart']['rx_pin'],data['uart']['tx_pin'],data['uart']['baud_rate'])==('GPIO17','GPIO5','115200')
        pins={x['id']:x['pin'] for x in data['switch'] if x['platform']=='gpio'}
        assert pins=={'zRST_gpio':'33','zBSL':'32'}
        assert all(x['inverted']=='yes' and x['restore_mode']=='ALWAYS_OFF' for x in data['switch'] if x['platform']=='gpio')
        assert data['esphome']['name']==name
        assert data['esp32']['framework']['type']=='esp-idf'
        assert model in ui['model']
        assert ui['zigbee']['baud_select']=='change_baud_rate'
        assert data['stream_server']['id']==data['substitutions']['older_efr32_stream']=='ss'
        assert data['stream_server']['port']=='6638'
        assert any(x['service']=='_tubeszb' for x in data['mdns']['services'])


def test_retired_cc2652_hardware_matches_existing_2021_profiles():
    # Archived CC2652 Ethernet/PoE sources use these same UART/Ethernet/GPIO maps.
    for name,rx,tx,clock,mode,phy,power in [
        ('tubeszb-cc2652p2-ethusb-2021','GPIO5','GPIO17','GPIO0','CLK_EXT_IN','1','GPIO16'),
        ('tubeszb-cc2652p2-poe-2021','GPIO5','GPIO16','GPIO17','CLK_OUT','0','GPIO12')]:
        data=load(ROOT/'manifests'/(name+'.yaml'));eth=data['ethernet']
        assert (data['uart']['rx_pin'],data['uart']['tx_pin'])==(rx,tx)
        assert eth['clk']=={'pin':clock,'mode':mode}
        assert (eth['phy_addr'],eth['power_pin'])==(phy,power)
        assert (eth['mdc_pin'],eth['mdio_pin'])==('GPIO23','GPIO18')
        pins={x['id']:x['pin'] for x in data['switch'] if x['platform']=='gpio'}
        assert pins=={'zRST_gpio':'33','zBSL':'32'}
        assert 'cc2652' in frontend(ROOT/'manifests'/(name+'.yaml'))


def test_every_hardware_profile_has_an_explicit_model():
    profiles=[]
    for path in (ROOT/'manifests').glob('*.yaml'):
        if 'esp32' not in load(path):continue
        model=frontend(path).get('model')
        assert model and model.startswith('TubesZB '),path.name
        profiles.append(path.name)
    assert len(profiles)==22
