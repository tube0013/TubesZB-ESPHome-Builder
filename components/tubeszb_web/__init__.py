"""Local TubesZB frontend for single and dual radio gateways."""

import gzip

import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.components import binary_sensor, button, ethernet, network_settings, select, switch, text, text_sensor, uart, web_server, web_server_base
from esphome.components.http_request.update import HttpRequestUpdate
from esphome.components.web_server_base import CONF_WEB_SERVER_BASE_ID
from esphome.const import CONF_ID
from esphome.core import CORE
from .assets import embedded_page

DEPENDENCIES = ["esp32", "web_server", "network_settings", "api"]
def AUTO_LOAD(config):
    # The serial backend supports an optional selector; fixed-baud profiles
    # still compile that backend without creating a select entity.
    return ["json", "select"] if "serial_radio" in config else ["json"]

ns = cg.esphome_ns.namespace("tubeszb_web")
TubesZBWeb = ns.class_("TubesZBWeb", cg.Component)

def zigbee_schema(value):
    from esphome.components import stream_server, script
    fields = {cv.Required("stream_server_id"): cv.use_id(stream_server.StreamServerComponent),
              cv.Required("bootloader_script"): cv.use_id(script.Script)}
    for key in ["check_button", "flash_button", "probe_button", "reset_button", "pause_button", "resume_button", "bootloader_button"]:
        fields[cv.Required(key)] = cv.use_id(button.Button)
    for key in ["firmware_sensor", "latest_sensor", "board_sensor", "manufacturer_sensor", "ieee_sensor", "status_sensor"]:
        fields[cv.Required(key)] = cv.use_id(text_sensor.TextSensor)
    fields[cv.Required("busy_sensor")] = cv.use_id(binary_sensor.BinarySensor)
    fields[cv.Required("gateway_beta_switch")] = cv.use_id(switch.Switch)
    fields[cv.Optional("zigbee_beta_switch")] = cv.use_id(switch.Switch)
    fields[cv.Required("baud_select")] = cv.use_id(select.Select)
    fields[cv.Required("custom_url_text")] = cv.use_id(text.Text)
    return cv.Schema(fields)(value)


def cc2652_schema(value):
    from esphome.components import stream_server, script
    fields = {cv.Required("stream_server_id"): cv.use_id(stream_server.StreamServerComponent),
              cv.Required("flash_script"): cv.use_id(script.Script),
              cv.Required("boot_probe_script"): cv.use_id(script.Script)}
    for key in ["check_button", "flash_button", "probe_button", "reset_button", "pause_button", "resume_button", "bootloader_button"]:
        fields[cv.Required(key)] = cv.use_id(button.Button)
    for key in ["firmware_sensor", "latest_sensor", "board_sensor", "ieee_sensor"]:
        fields[cv.Required(key)] = cv.use_id(text_sensor.TextSensor)
    for key in ["gateway_beta_switch", "zigbee_beta_switch"]:
        fields[cv.Required(key)] = cv.use_id(switch.Switch)
    return cv.Schema(fields)(value)


def serial_radio_schema(value):
    from esphome.components import stream_server
    fields = {cv.Required("stream_server_id"): cv.use_id(stream_server.StreamServerComponent),
              cv.Optional("baud_select"): cv.use_id(select.Select),
              cv.Optional("radio_model", default="MGM21"): cv.string}
    for key in ["reset_button", "pause_button", "resume_button"]:
        fields[cv.Required(key)] = cv.use_id(button.Button)
    return cv.Schema(fields)(value)


CONFIG_SCHEMA = cv.All(cv.Schema({
    cv.GenerateID(): cv.declare_id(TubesZBWeb),
    cv.GenerateID(CONF_WEB_SERVER_BASE_ID): cv.use_id(web_server_base.WebServerBase),
    cv.GenerateID("web_server_id"): cv.use_id(web_server.WebServer),
    cv.Required("network_settings_id"): cv.use_id(network_settings.NetworkSettings),
    cv.Required("ethernet_id"): cv.use_id(ethernet.EthernetComponent),
    cv.Required("uart_id"): cv.use_id(uart.UARTComponent),
    cv.Required("firmware_update_id"): cv.use_id(HttpRequestUpdate),
    cv.Optional("model"): cv.string,
    cv.Optional("zwave_uart_id"): cv.use_id(uart.UARTComponent),
    cv.Optional("serial_radio"): serial_radio_schema,
    cv.Optional("zigbee"): zigbee_schema,
    cv.Optional("cc2652"): cc2652_schema,
}).extend(cv.COMPONENT_SCHEMA), cv.only_with_framework("esp-idf"), cv.has_at_most_one_key("zigbee", "cc2652", "serial_radio"))


def validate_dependencies(config):
    full = fv.full_config.get()
    if "zigbee" in config:
        if "efr32_flasher" not in full or "efr32_info" not in full:
            raise cv.Invalid("Zigbee frontend requires the EFR32 flasher and information components")
    elif "cc2652" in config:
        if "cc2652_flasher" not in full:
            raise cv.Invalid("CC2652 frontend requires the CC2652 flasher")
    elif "serial_radio" in config:
        if "stream_server" not in full:
            raise cv.Invalid("Serial radio frontend requires the existing Zigbee serial bridge")
    elif "zwave_proxy" not in full:
        raise cv.Invalid("Configure a Z-Wave proxy or the Zigbee frontend bindings")

    radio_key = next((k for k in ("zigbee", "cc2652", "serial_radio") if k in config), None)
    if radio_key is not None:
        streams = full["stream_server"]
        if isinstance(streams, dict):
            streams = [streams]
        stream = next(s for s in streams if str(s[CONF_ID]) == str(config[radio_key]["stream_server_id"]))
        if str(stream["uart_id"]) != str(config["uart_id"]):
            raise cv.Invalid("The Zigbee frontend UART must match its serial bridge")
        flasher_key = {"zigbee": "efr32_flasher", "cc2652": "cc2652_flasher"}.get(radio_key)
        if flasher_key:
            flashers = full[flasher_key]
            if isinstance(flashers, dict):
                flashers = [flashers]
            if not any(str(f["uart_id"]) == str(config["uart_id"]) for f in flashers):
                raise cv.Invalid("The Zigbee frontend UART must match its native flasher")

    if radio_key is not None and "zwave_proxy" in full and "zwave_uart_id" not in config:
        raise cv.Invalid("Combined Zigbee/Z-Wave frontends require the separate zwave_uart_id binding")
    if "zwave_uart_id" in config:
        if "zwave_proxy" not in full or not any(k in config for k in ("zigbee", "cc2652", "serial_radio")):
            raise cv.Invalid("Dual-radio frontend requires Zigbee and a Z-Wave proxy")
        if str(full["zwave_proxy"]["uart_id"]) != str(config["zwave_uart_id"]):
            raise cv.Invalid("zwave_uart_id must match the Z-Wave proxy UART")
        if str(config["uart_id"]) == str(config["zwave_uart_id"]):
            raise cv.Invalid("Zigbee and Z-Wave must use separate UARTs")


FINAL_VALIDATE_SCHEMA = validate_dependencies


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add_define("USE_OTA_STATE_LISTENER")
    for key, method in [(CONF_WEB_SERVER_BASE_ID, "set_base"), ("web_server_id", "set_web_server"),
                        ("network_settings_id", "set_network_settings"), ("ethernet_id", "set_ethernet"),
                        ("uart_id", "set_uart"), ("firmware_update_id", "set_update")]:
        cg.add(getattr(var, method)(await cg.get_variable(config[key])))
    model = "TubesZB ZW2026" if "2026" in CORE.name else "TubesZB Z-Wave"
    if "experimental" in CORE.name:
        model += " (experimental)"
    if any(k in config for k in ("zigbee", "cc2652", "serial_radio")):
        cg.add_define("USE_TUBESZB_ZIGBEE")
    if "cc2652" in config:
        cg.add_define("USE_TUBESZB_CC2652")
        model = "TubesZB " + CORE.name.removeprefix("tubeszb-").upper().replace("-", " ")
        for key, value in config["cc2652"].items():
            cg.add(getattr(var, "set_" + key)(await cg.get_variable(value)))
    if "zigbee" in config:
        cg.add_define("USE_TUBESZB_EFR32")
        model = "TubesZB MGM24"
        for key, value in config["zigbee"].items():
            cg.add(getattr(var, "set_" + key)(await cg.get_variable(value)))
    if "serial_radio" in config:
        cg.add_define("USE_TUBESZB_SERIAL_RADIO")
        model = "TubesZB Dual Radio MGM21 + Z-Wave" if "zwave_uart_id" in config else "TubesZB MGM210"
        for key, value in config["serial_radio"].items():
            cg.add(getattr(var, "set_" + key)(value if key == "radio_model" else await cg.get_variable(value)))
    if "zwave_uart_id" in config:
        cg.add_define("USE_TUBESZB_DUAL_RADIO")
        cg.add(var.set_zwave_uart(await cg.get_variable(config["zwave_uart_id"])))
    cg.add(var.set_model(config.get("model", model)))
    cg.add(var.set_board(CORE.config["esp32"].get("board", "ESP32")))
    page = gzip.compress(embedded_page().encode(), mtime=0)
    cg.add_global(cg.RawStatement("namespace esphome::tubeszb_web {\nextern const uint8_t FRONTEND_GZ[] = {" +
                                 ",".join(str(byte) for byte in page) + "};\nextern const size_t FRONTEND_GZ_SIZE = sizeof(FRONTEND_GZ);\n}"))
