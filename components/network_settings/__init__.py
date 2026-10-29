"""Persistent Ethernet configuration with a confirmed/trial boot transaction."""

from pathlib import Path

import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.components import ethernet, web_server_base, http_request
from esphome.components.http_request.update import HttpRequestUpdate
from esphome.components.web_server_base import CONF_WEB_SERVER_BASE_ID
from esphome.const import CONF_ID

DEPENDENCIES = ["ethernet", "web_server", "esp32"]
AUTO_LOAD = ["json"]

ns = cg.esphome_ns.namespace("network_settings")
NetworkSettings = ns.class_("NetworkSettings", cg.Component)

CONFIG_SCHEMA = cv.All(
    cv.Schema({
        cv.GenerateID(): cv.declare_id(NetworkSettings),
        cv.GenerateID("http_request_id"): cv.use_id(http_request.HttpRequestComponent),
        cv.Required("firmware_update_id"): cv.use_id(HttpRequestUpdate),
        cv.Required("ethernet_id"): cv.use_id(ethernet.EthernetComponent),
        cv.GenerateID(CONF_WEB_SERVER_BASE_ID): cv.use_id(web_server_base.WebServerBase),
    }).extend(cv.COMPONENT_SCHEMA),
    cv.only_with_framework("esp-idf"),
)


def final_validate(config):
    full = fv.full_config.get()
    if "manual_ip" in full["ethernet"]:
        raise cv.Invalid("network_settings owns Ethernet addressing; remove ethernet.manual_ip")
    if full["ethernet"].get("enable_on_boot", True) is False:
        raise cv.Invalid("network_settings requires Ethernet enabled on boot")
    if "wifi" in full:
        raise cv.Invalid("network_settings currently supports Ethernet-only devices")


FINAL_VALIDATE_SCHEMA = final_validate


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_ethernet(await cg.get_variable(config["ethernet_id"])))
    cg.add(var.set_web_server_base(await cg.get_variable(config[CONF_WEB_SERVER_BASE_ID])))
    cg.add(var.set_http_request(await cg.get_variable(config["http_request_id"])))
    cg.add(var.set_update(await cg.get_variable(config["firmware_update_id"])))
    # Keep ESPHome's own static-IP path compiled in, even though DHCP is the
    # factory default. The saved static settings are assigned before Ethernet setup.
    cg.add_define("USE_ETHERNET_MANUAL_IP")
    page = (Path(__file__).parent / "network_settings.html").read_text()
    validation = (Path(__file__).parent / "network_validation.js").read_text()
    page = page.replace("/* NETWORK_VALIDATION */", validation)
    cg.add_global(cg.RawStatement('namespace esphome::network_settings {\n'
                                 'extern const char NETWORK_PAGE[] = R"TZBNET(' + page + ')TZBNET";\n}'))
