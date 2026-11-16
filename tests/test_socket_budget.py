"""Generate real firmware settings to keep socket declarations and auto-sizing together."""

import os
from pathlib import Path
import subprocess
import sys

import pytest

ROOT = Path(__file__).resolve().parents[1]

PROBE = r'''
import sys
from pathlib import Path
from esphome import config
from esphome.core import CORE
from esphome.components import socket
from esphome.__main__ import generate_cpp_contents
import esphome.components.external_components as external

CORE.config_path = Path(sys.argv[1]).resolve()
# Use the exact component checkout supplied by CI instead of fetching another copy.
external._process_git_config = lambda source, refresh: Path(sys.argv[2]).resolve()
conf = config.read_config({})
assert conf is not None
assert 'CONFIG_LWIP_MAX_SOCKETS' not in conf['esp32']['framework']['sdkconfig_options']
CORE.config = conf
generate_cpp_contents(conf)
counts = socket.get_socket_counts()
actual = CORE.data['esp32']['sdkconfig_options']['CONFIG_LWIP_MAX_SOCKETS']
assert actual == max(10, counts.tcp + counts.udp + counts.tcp_listen)
tcp = CORE.data[socket.KEY_SOCKET_CONSUMERS_TCP]
udp = CORE.data[socket.KEY_SOCKET_CONSUMERS_UDP]
assert tcp['network_settings.connectivity_probe'] == 1
assert tcp.get('network_settings.http_request', 0) + sum(
    count for name, count in tcp.items() if name == 'http_request' or name.startswith('http_request.')
) >= 1
assert udp.get('network_settings.httpd_control', 0) + udp.get('web_server_idf', 0) == 2
for flasher in ('cc2652_flasher', 'efr32_flasher'):
    assert tcp.get(flasher, 0) == int(flasher in conf)
'''


@pytest.mark.parametrize('model', [
    'tubeszb-cc2652p7-poe-2023',
    'tubeszb-efr32-mgm24-2023',
    'tubeszb-2026-zw',
])
def test_component_socket_requirements_drive_generated_sdkconfig(model):
    components = os.environ.get('TZB_COMPONENTS_ROOT')
    if not components:
        pytest.skip('Set TZB_COMPONENTS_ROOT to the matching external component checkout')
    result = subprocess.run(
        [sys.executable, '-c', PROBE, str(ROOT / 'manifests' / (model + '.yaml')), components],
        capture_output=True, text=True,
    )
    assert result.returncode == 0, result.stdout + result.stderr
