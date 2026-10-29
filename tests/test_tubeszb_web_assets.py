"""Verify the actual page assembly, branding, and offline asset packaging."""
from component_paths import COMPONENTS_ROOT


import gzip
import importlib.util
from pathlib import Path
from html.parser import HTMLParser

ROOT = Path(__file__).resolve().parents[1]


def test_embedded_frontend_assets():
    spec = importlib.util.spec_from_file_location("tubeszb_assets", COMPONENTS_ROOT / "tubeszb_web/assets.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    page = module.embedded_page()
    assert "<!-- BRAND_SYMBOL -->" not in page
    assert "/* NETWORK_VALIDATION */" not in page
    assert "/* FRONTEND_SCRIPT */" not in page
    assert "Design preview" not in page
    assert "window.openai" not in page
    assert 'viewBox="43.4 0 17.1 20.6"' in page
    assert "Download logs" in page
    assert len(gzip.compress(page.encode(), mtime=0)) < 30000

    class Assets(HTMLParser):
        def handle_starttag(self, tag, attrs):
            attrs = dict(attrs)
            if tag in ("script", "img", "link"):
                assert not attrs.get("src", "").startswith(("https://", "http://"))
                assert not attrs.get("href", "").startswith(("https://", "http://"))

    Assets().feed(page)


def test_http_statuses_supported_by_esphome_idf():
    """Catch accepted actions becoming HTTP 500 in the real IDF response adapter."""
    import re
    import esphome

    adapter = Path(esphome.__file__).parent / "components/web_server_idf/web_server_idf.cpp"
    source = adapter.read_text().split("void AsyncWebServerRequest::init_response_(", 1)[1]
    source = source.split("httpd_resp_set_status(*this, status);", 1)[0]
    supported = {int(code) for code in re.findall(r"case (\d+):", source)} | {500}
    for relative in ("tubeszb_web/tubeszb_web.cpp", "network_settings/network_settings.cpp"):
        handlers = (COMPONENTS_ROOT / relative).read_text()
        emitted = {int(code) for code in re.findall(
            r"(?:send_json_|send_error_|error_|reject)\((?:request,\s*)?(\d+),", handlers
        )}
        assert emitted <= supported, f"{relative}: IDF maps {emitted - supported} to HTTP 500"


def test_custom_radio_url_validation(tmp_path):
    """Exercise the same URL policy the device uses before saving or flashing."""
    import subprocess
    target = tmp_path / "gbl-url-test"
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(COMPONENTS_ROOT.parent),
                    str(ROOT / "tests/gbl_url_test.cpp"), "-o", str(target)], check=True)
    subprocess.run([str(target)], check=True)


def test_cc2652_native_log_events(tmp_path):
    """Percentages and transfer messages cannot stand in for a native flash result."""
    import subprocess
    target = tmp_path / "cc2652-log-test"
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(COMPONENTS_ROOT.parent),
                    str(ROOT / "tests/cc2652_log_test.cpp"), "-o", str(target)], check=True)
    subprocess.run([str(target)], check=True)


def test_cc2652_flash_session(tmp_path):
    """Restore the original bridge state after success, failure and retry."""
    import subprocess
    target = tmp_path / "cc2652-flash-session-test"
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(COMPONENTS_ROOT.parent),
                    str(ROOT / "tests/cc2652_flash_session_test.cpp"), "-o", str(target)], check=True)
    subprocess.run([str(target)], check=True)


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
