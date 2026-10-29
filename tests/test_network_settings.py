"""Run the actual firmware state machine and compare browser/firmware validation."""

import json
from pathlib import Path
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[1]


@pytest.fixture(scope="session")
def executable(tmp_path_factory):
    target = tmp_path_factory.mktemp("cpp") / "network-test"
    subprocess.run([
        "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT),
        str(ROOT / "tests/network_state_test.cpp"), "-o", str(target),
    ], check=True)
    return target


def browser_results(kind, cases):
    return json.loads(subprocess.check_output(
        ["node", "tests/validation_probe.cjs"], input=json.dumps({"kind": kind, "cases": cases}).encode(), cwd=ROOT,
    ))


def test_boot_transaction(executable):
    subprocess.run([str(executable)], check=True)


def test_ipv4_parser_parity(executable):
    valid = ["0.0.0.0", "255.255.255.255", "192.168.1.50", "10.0.0.1", "1.2.3.4"]
    invalid = ["", "192.168.1", "1.2.3.4.5", "256.1.1.1", "1.2.3.-1", "1.2.3.4x",
               " 1.2.3.4", "1.2.3.4 ", "01.2.3.4", "1..2.3", "1.2.3.", "0x10.0.0.1",
               "192.168.1.1/24", "localhost", "::1", "１２７.0.0.1", "1.2.3.\n", "1.2.3.4\n"]
    cases = valid + invalid
    expected = [True] * len(valid) + [False] * len(invalid)
    assert browser_results("parse", cases) == expected
    assert [subprocess.check_output([str(executable), "parse", case]) == b"1" for case in cases] == expected


def test_configuration_validation_parity(executable):
    base = dict(mode="static", ip="192.168.1.50", subnet="255.255.255.0", gateway="192.168.1.1", dns1="1.1.1.1", dns2="")
    cases = [(base, True), ({**base, "mode": "dhcp", "ip": "garbage"}, True)]
    for field, values in {
        "mode": ["other", ""],
        "ip": ["192.168.1.0", "192.168.1.255", "0.1.2.3", "127.0.0.1", "169.254.1.1", "224.0.0.1", "255.255.255.255", "garbage"],
        "subnet": ["255.0.255.0", "0.0.0.0", "255.255.255.255", "255.255.255.254", "255.255.255.1"],
        "gateway": ["192.168.2.1", "192.168.1.50", "192.168.1.0", "192.168.1.255", "0.0.0.0", "127.0.0.1"],
        "dns1": ["", "0.0.0.0", "127.0.0.1", "224.0.0.1"],
        "dns2": ["garbage", "224.0.0.1"],
    }.items():
        cases.extend(({**base, field: value}, False) for value in values)
    cases.extend([
        ({**base, "dns2": "8.8.8.8"}, True),
        ({**base, "dns2": "0.0.0.0"}, True),
        ({**base, "ip": "10.0.0.2", "gateway": "10.0.0.1", "subnet": "255.255.255.252"}, True),
        ({**base, "ip": "10.0.0.2", "gateway": "10.0.0.1", "subnet": "128.0.0.0"}, True),
    ])
    expected = [valid for _, valid in cases]
    assert browser_results("validate", [value for value, _ in cases]) == expected
    assert [subprocess.check_output([str(executable), "validate", *[value[key] for key in
            ("mode", "ip", "subnet", "gateway", "dns1", "dns2")]]) == b"1" for value, _ in cases] == expected
