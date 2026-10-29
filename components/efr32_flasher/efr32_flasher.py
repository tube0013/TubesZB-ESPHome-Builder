import zlib
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select, uart, switch as switch_, text_sensor, binary_sensor
from esphome.const import CONF_ID, CONF_URL
from esphome.automation import register_action

DEPENDENCIES = ["uart", "switch"]
AUTO_LOAD = ["md5", "json", "zigbee_firmware_baud"]

CONF_BL_SWITCH = "boot_switch"
CONF_RST_SWITCH = "rst_switch"
CONF_DEBUG = "debug"
CONF_SHOW_PROGRESS = "show_progress"
CONF_PROGRESS_STEP = "progress_step"
CONF_FW_VERSION_TEXT = "fw_version_text"
CONF_LATEST_FW_TEXT = "latest_fw_version_text"
CONF_EZSP_PROTOCOL_TEXT = "ezsp_protocol_text"
CONF_INSTALLED_STACK_TEXT = "installed_stack_text"
CONF_XNCP_VERSION_TEXT = "xncp_version_text"
CONF_IEEE_TEXT = "ieee_text"
CONF_MANUF_ID_TEXT = "manuf_id_text"
CONF_BOARD_NAME_TEXT = "board_name_text"
CONF_MFG_STRING_TEXT = "mfg_string_text"
CONF_CHIP_TEXT = "chip_text"
CONF_BAUD_RATE = "baud_rate"
CONF_BOOT_BAUD_RATE = "bootloader_baud_rate"
CONF_VARIANT = "variant"
CONF_BUSY_BINARY_SENSOR = "busy_binary_sensor"
efr32_ns = cg.esphome_ns.namespace("efr32_flasher")
EFR32Flasher = efr32_ns.class_("EFR32Flasher", cg.Component)
UpdateFirmwareAction = efr32_ns.class_("UpdateFirmwareAction")
CheckUpdateAction = efr32_ns.class_("CheckUpdateAction")
EnterBootloaderAction = efr32_ns.class_("EnterBootloaderAction")

CONF_PAUSE_SWITCH = "pause_switch"

def validate_bootloader(config):
    if config["bootloader_entry"] == "gpio" and CONF_BL_SWITCH not in config:
        raise cv.Invalid("boot_switch is required for GPIO bootloader entry")
    if config["bootloader_entry"] == "ezsp" and CONF_BL_SWITCH in config:
        raise cv.Invalid("EZSP bootloader entry must not configure a boot output")
    return config


CONFIG_SCHEMA = cv.All(cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(EFR32Flasher),
        cv.Required(CONF_URL): cv.string_strict,
        cv.Required("uart_id"): cv.use_id(uart.UARTComponent),
        cv.Optional("runtime_baud_select"): cv.use_id(select.Select),
        cv.Optional(CONF_BL_SWITCH): cv.use_id(switch_.Switch),
        cv.Optional("bootloader_entry", default="gpio"): cv.one_of("gpio", "ezsp", lower=True),
        cv.Required(CONF_RST_SWITCH): cv.use_id(switch_.Switch),
        cv.Optional(CONF_DEBUG, default=False): cv.boolean,
        cv.Optional("hardware_flow_control", default=True): cv.boolean,
        cv.Optional(CONF_SHOW_PROGRESS, default=True): cv.boolean,
        cv.Optional(CONF_PROGRESS_STEP, default=5): cv.int_range(min=1, max=50),
        cv.Optional(CONF_FW_VERSION_TEXT): cv.use_id(text_sensor.TextSensor),
        cv.Optional(CONF_LATEST_FW_TEXT): cv.use_id(text_sensor.TextSensor),
        cv.Optional(CONF_EZSP_PROTOCOL_TEXT): cv.use_id(text_sensor.TextSensor),
        cv.Optional(CONF_INSTALLED_STACK_TEXT): cv.use_id(text_sensor.TextSensor),
        cv.Optional(CONF_XNCP_VERSION_TEXT): cv.use_id(text_sensor.TextSensor),
        cv.Optional(CONF_IEEE_TEXT): cv.use_id(text_sensor.TextSensor),
        cv.Optional(CONF_MANUF_ID_TEXT): cv.use_id(text_sensor.TextSensor),
        cv.Optional(CONF_BOARD_NAME_TEXT): cv.use_id(text_sensor.TextSensor),
        cv.Optional(CONF_MFG_STRING_TEXT): cv.use_id(text_sensor.TextSensor),
        cv.Optional(CONF_CHIP_TEXT): cv.use_id(text_sensor.TextSensor),
        cv.Optional(CONF_BAUD_RATE, default=0): cv.int_range(min=0, max=921600),
        cv.Optional(CONF_BOOT_BAUD_RATE, default=115200): cv.int_range(min=9600, max=921600),
        cv.Optional(CONF_PAUSE_SWITCH): cv.use_id(switch_.Switch),
        cv.Optional(CONF_BUSY_BINARY_SENSOR): cv.use_id(binary_sensor.BinarySensor),
        # variant: auto | MGM24 | BM24 (case-insensitive)
        cv.Optional(CONF_VARIANT, default="auto"): cv.one_of("auto", "mgm24", "bm24", "flat", lower=True),
    }
).extend(cv.COMPONENT_SCHEMA), validate_bootloader)


@register_action("efr32_flasher.update_firmware", UpdateFirmwareAction, cv.Schema({cv.Required(CONF_ID): cv.use_id(EFR32Flasher)}), synchronous=False)
async def update_firmware_action_to_code(config, action_id, template_arg, args):
    act = cg.new_Pvariable(action_id)
    parent = await cg.get_variable(config[CONF_ID])
    cg.add(act.set_parent(parent))
    return act


@register_action("efr32_flasher.check_update", CheckUpdateAction, cv.Schema({cv.Required(CONF_ID): cv.use_id(EFR32Flasher)}), synchronous=False)
async def check_update_action_to_code(config, action_id, template_arg, args):
    act = cg.new_Pvariable(action_id)
    parent = await cg.get_variable(config[CONF_ID])
    cg.add(act.set_parent(parent))
    return act


@register_action("efr32_flasher.enter_bootloader", EnterBootloaderAction, cv.Schema({cv.Required(CONF_ID): cv.use_id(EFR32Flasher)}), synchronous=False)
async def enter_bootloader_to_code(config, action_id, template_arg, args):
    act = cg.new_Pvariable(action_id)
    cg.add(act.set_parent(await cg.get_variable(config[CONF_ID])))
    return act


async def to_code(config):
    # Ensure our C++ header is visible to the generated build (local and external_components).
    cg.add_global(cg.RawStatement('#include "esphome/components/efr32_flasher/efr32_flasher.h"'), prepend=True)

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    preference_hash = zlib.crc32(("efr32_flasher:" + str(config[CONF_ID]) + ":runtime_baud").encode())
    cg.add(var.set_baud_preference_hash(preference_hash))
    if "runtime_baud_select" in config:
        cg.add(var.set_runtime_baud_select(await cg.get_variable(config["runtime_baud_select"])))

    cg.add(var.set_update_url(config[CONF_URL]))
    cg.add(var.set_hardware_flow_control(config["hardware_flow_control"]))

    uart_comp = await cg.get_variable(config["uart_id"])
    rst = await cg.get_variable(config[CONF_RST_SWITCH])

    cg.add(var.set_uart(uart_comp))
    if CONF_BL_SWITCH in config:
        cg.add(var.set_bl_switch(await cg.get_variable(config[CONF_BL_SWITCH])))
    cg.add(var.set_ezsp_bootloader(config["bootloader_entry"] == "ezsp"))
    cg.add(var.set_rst_switch(rst))
    if config[CONF_BAUD_RATE] > 0:
        cg.add(var.set_runtime_baud_rate(config[CONF_BAUD_RATE]))
    if CONF_BOOT_BAUD_RATE in config:
        cg.add(var.set_bootloader_baud_rate(config[CONF_BOOT_BAUD_RATE]))

    if CONF_DEBUG in config:
        cg.add(var.set_verbose(config[CONF_DEBUG]))
    if CONF_SHOW_PROGRESS in config:
        cg.add(var.set_show_progress(config[CONF_SHOW_PROGRESS]))
    if CONF_PROGRESS_STEP in config:
        cg.add(var.set_progress_step(config[CONF_PROGRESS_STEP]))

    if CONF_FW_VERSION_TEXT in config:
        fw_txt = await cg.get_variable(config[CONF_FW_VERSION_TEXT])
        cg.add(var.set_fw_text_sensor(fw_txt))
    if CONF_LATEST_FW_TEXT in config:
        latest_txt = await cg.get_variable(config[CONF_LATEST_FW_TEXT])
        cg.add(var.set_latest_text_sensor(latest_txt))

    if CONF_EZSP_PROTOCOL_TEXT in config:
        t = await cg.get_variable(config[CONF_EZSP_PROTOCOL_TEXT])
        cg.add(var.set_ezsp_protocol_text(t))
    if CONF_INSTALLED_STACK_TEXT in config:
        t = await cg.get_variable(config[CONF_INSTALLED_STACK_TEXT])
        cg.add(var.set_installed_stack_text(t))
    if CONF_XNCP_VERSION_TEXT in config:
        t = await cg.get_variable(config[CONF_XNCP_VERSION_TEXT])
        cg.add(var.set_xncp_version_text(t))
    if CONF_IEEE_TEXT in config:
        t = await cg.get_variable(config[CONF_IEEE_TEXT])
        cg.add(var.set_ieee_text(t))
    if CONF_MANUF_ID_TEXT in config:
        t = await cg.get_variable(config[CONF_MANUF_ID_TEXT])
        cg.add(var.set_manuf_id_text(t))
    if CONF_BOARD_NAME_TEXT in config:
        t = await cg.get_variable(config[CONF_BOARD_NAME_TEXT])
        cg.add(var.set_board_name_text(t))
    if CONF_MFG_STRING_TEXT in config:
        t = await cg.get_variable(config[CONF_MFG_STRING_TEXT])
        cg.add(var.set_mfg_string_text(t))
    if CONF_CHIP_TEXT in config:
        t = await cg.get_variable(config[CONF_CHIP_TEXT])
        cg.add(var.set_chip_text(t))

    if CONF_VARIANT in config:
        v = config[CONF_VARIANT]
        m = {"auto": 0, "mgm24": 1, "bm24": 2, "flat": 3}
        cg.add(var.set_variant(m.get(v, 0)))
    if CONF_PAUSE_SWITCH in config:
        ps = await cg.get_variable(config[CONF_PAUSE_SWITCH])
        cg.add(var.set_pause_switch(ps))
    if CONF_BUSY_BINARY_SENSOR in config:
        bs = await cg.get_variable(config[CONF_BUSY_BINARY_SENSOR])
        cg.add(var.set_busy_sensor(bs))

# Old probe actions removed to keep component lean
