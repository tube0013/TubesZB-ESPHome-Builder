"""Native software bootloader protocol, independent of the web frontend."""
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]


def test_native_ezsp_bootloader_sequence(tmp_path):
    exe=tmp_path/'ezsp-bootloader'
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-I',str(ROOT),
                    str(ROOT/'tests/ezsp_bootloader_test.cpp'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)


def test_native_uart_ash_transport(tmp_path):
    import os
    import pytest
    candidates=list((ROOT/'manifests/.esphome/build').glob('*/src/esphome/components/efr32_info/ash_util.h'))
    include=os.environ.get('ESPHOME_EFR32_BUILD_SRC') or (str(candidates[0].parents[3]) if candidates else None)
    if not include:
        pytest.skip('Compile an EFR32 image or set ESPHOME_EFR32_BUILD_SRC for the actual ASH dependency')
    exe=tmp_path/'ezsp-uart'
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Wno-unused-variable','-Wno-unused-parameter',
                    '-I',str(ROOT/'tests/ezsp_stubs'),'-I',str(ROOT),'-I',include,
                    str(ROOT/'tests/ezsp_bootloader_uart_test.cpp'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)


def test_native_entry_modes_and_transfer_gate():
    source=(ROOT/'components/efr32_flasher/efr32_flasher.cpp').read_text()
    update=source.split('void EFR32Flasher::run_update_(){',1)[1].split('void EFR32Flasher::run_check_update_',1)[0]
    gate=update.split('if (!enter_bootloader_()) {',1)[1].split('ESP_LOGD(TAG, "Bootloader entered',1)[0]
    assert 'esp_http_client_cleanup(client)' in gate and 'set_busy_(false)' in gate and 'return;' in gate
    assert update.index('if (!enter_bootloader_())') < update.index('xmodem_send_(')
    entry=source.split('bool EFR32Flasher::enter_bootloader_(){',1)[1].split('void EFR32Flasher::leave_bootloader_',1)[0]
    software=entry.split('if (!bl_sw_ || !rst_sw_)',1)[0]
    assert software.index('apply_runtime_baud_()') < software.index('ezsp_bootloader::launch(')
    assert 'probe_bootloader_menu_(4000)' in software
    assert 'bl_sw_->' not in software
