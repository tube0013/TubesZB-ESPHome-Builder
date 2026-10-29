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
