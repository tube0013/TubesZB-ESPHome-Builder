import zlib
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select, uart, switch as switch_, text_sensor
from esphome.const import CONF_ID, CONF_URL
from esphome.automation import register_action

DEPENDENCIES = ["uart", "switch"]
# Only pull in what we actually need for linking
AUTO_LOAD = ["md5", "json", "zigbee_firmware_baud"]

CONF_BSL_SWITCH = "bsl_switch"
CONF_RST_SWITCH = "rst_switch"
CONF_BOOT_BAUD = "boot_baud_rate"
CONF_RESTORE_BAUD = "restore_baud_rate"
CONF_ERASE_MODE = "erase_mode"
CONF_DEBUG = "debug"
CONF_SHOW_PROGRESS = "show_progress"
CONF_PROGRESS_STEP = "progress_step"
CONF_FW_VERSION_TEXT = "fw_version_text"
CONF_IEEE_TEXT = "ieee_text"
CONF_LATEST_FW_TEXT = "latest_fw_version_text"
CONF_VARIANT_TEXT = "variant_text"
CONF_CHECK_ON_BOOT = "check_on_boot"
CONF_CHECK_INTERVAL_DAYS = "check_interval_days"
CONF_VARIANT = "variant"
CONF_DETECT_ON_BOOT = "detect_on_boot"
CONF_DETECT_ON_BOOT_DELAY_MS = "detect_on_boot_delay_ms"

cc2652_ns = cg.esphome_ns.namespace("cc2652_flasher")
CC2652Flasher = cc2652_ns.class_("CC2652Flasher", cg.Component)
UpdateFirmwareAction = cc2652_ns.class_("UpdateFirmwareAction")
CheckUpdateAction = cc2652_ns.class_("CheckUpdateAction")
EraseFlashAction = cc2652_ns.class_("EraseFlashAction")
DetectVariantAction = cc2652_ns.class_("DetectVariantAction")

ERASE_MODE = cv.one_of("sector", "bank", "auto", lower=True)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(CC2652Flasher),
        cv.Required(CONF_URL): cv.string_strict,
        cv.Required("uart_id"): cv.use_id(uart.UARTComponent),
        cv.Optional("runtime_baud_select"): cv.use_id(select.Select),
        cv.Required(CONF_BSL_SWITCH): cv.use_id(switch_.Switch),
        cv.Required(CONF_RST_SWITCH): cv.use_id(switch_.Switch),
        cv.Optional(CONF_BOOT_BAUD, default=0): cv.int_range(min=0),
        cv.Optional(CONF_RESTORE_BAUD, default=0): cv.int_range(min=0),
        cv.Optional(CONF_ERASE_MODE, default="bank"): ERASE_MODE,
        cv.Optional(CONF_DEBUG, default=False): cv.boolean,
        cv.Optional(CONF_SHOW_PROGRESS, default=True): cv.boolean,
        cv.Optional(CONF_PROGRESS_STEP, default=5): cv.int_range(min=1, max=50),
        cv.Optional(CONF_FW_VERSION_TEXT): cv.use_id(text_sensor.TextSensor),
        cv.Optional(CONF_IEEE_TEXT): cv.use_id(text_sensor.TextSensor),
        cv.Optional(CONF_LATEST_FW_TEXT): cv.use_id(text_sensor.TextSensor),
        cv.Optional(CONF_VARIANT_TEXT): cv.use_id(text_sensor.TextSensor),
        cv.Optional(CONF_CHECK_ON_BOOT, default=True): cv.boolean,
        cv.Optional(CONF_CHECK_INTERVAL_DAYS, default=7): cv.int_range(min=0, max=60),
        cv.Optional(CONF_DETECT_ON_BOOT, default=True): cv.boolean,
        cv.Optional(CONF_DETECT_ON_BOOT_DELAY_MS, default=0): cv.int_range(min=0, max=10000),
        # variant: auto | p2 | p7 | cc2652p2 | cc2652p7
        cv.Optional(CONF_VARIANT, default="auto"): cv.one_of("auto", "p2", "p7", "cc2652p2", "cc2652p7", lower=True),
    }
).extend(cv.COMPONENT_SCHEMA)


@register_action("cc2652_flasher.update_firmware", UpdateFirmwareAction, cv.Schema({cv.Required(CONF_ID): cv.use_id(CC2652Flasher)}), synchronous=False)
async def update_firmware_action_to_code(config, action_id, template_arg, args):
    act = cg.new_Pvariable(action_id)
    parent = await cg.get_variable(config[CONF_ID])
    cg.add(act.set_parent(parent))
    return act


@register_action(
    "cc2652_flasher.erase_flash",
    EraseFlashAction,
    cv.Schema({cv.Required(CONF_ID): cv.use_id(CC2652Flasher), cv.Optional(CONF_ERASE_MODE, default="auto"): ERASE_MODE}),
    synchronous=False,
)
async def erase_flash_action_to_code(config, action_id, template_arg, args):
    act = cg.new_Pvariable(action_id)
    parent = await cg.get_variable(config[CONF_ID])
    cg.add(act.set_parent(parent))
    erase_mode_val = {"sector": 0, "bank": 1, "auto": 2}[config.get(CONF_ERASE_MODE, "auto")]
    cg.add(act.set_mode(erase_mode_val))
    return act


@register_action("cc2652_flasher.check_update", CheckUpdateAction, cv.Schema({cv.Required(CONF_ID): cv.use_id(CC2652Flasher)}), synchronous=False)
async def check_update_action_to_code(config, action_id, template_arg, args):
    act = cg.new_Pvariable(action_id)
    parent = await cg.get_variable(config[CONF_ID])
    cg.add(act.set_parent(parent))
    return act


@register_action("cc2652_flasher.detect_variant", DetectVariantAction, cv.Schema({cv.Required(CONF_ID): cv.use_id(CC2652Flasher)}), synchronous=False)
async def detect_variant_action_to_code(config, action_id, template_arg, args):
    act = cg.new_Pvariable(action_id)
    parent = await cg.get_variable(config[CONF_ID])
    cg.add(act.set_parent(parent))
    return act


async def to_code(config):
    # Ensure our C++ header is visible to the generated build for both local and GitHub layouts.
    cg.add_global(cg.RawStatement(
        "#if __has_include(\"esphome/components/cc2652_flasher/cc2652_flasher.h\")\n"
        "#  include \"esphome/components/cc2652_flasher/cc2652_flasher.h\"\n"
        "#elif __has_include(\"cc2652_flasher.h\")\n"
        "#  include \"cc2652_flasher.h\"\n"
        "#else\n"
        "#  error \"cc2652_flasher.h not found\"\n"
        "#endif"
    ))
    # Build system will compile cc2652_flasher.cpp (we also list it in YAML includes).
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    preference_hash = zlib.crc32(("cc2652_flasher:" + str(config[CONF_ID]) + ":runtime_baud").encode())
    cg.add(var.set_baud_preference_hash(preference_hash))
    if "runtime_baud_select" in config:
        cg.add(var.set_runtime_baud_select(await cg.get_variable(config["runtime_baud_select"])))

    cg.add(var.set_update_url(config[CONF_URL]))

    uart_comp = await cg.get_variable(config["uart_id"])
    bsl = await cg.get_variable(config[CONF_BSL_SWITCH])
    rst = await cg.get_variable(config[CONF_RST_SWITCH])

    cg.add(var.set_uart(uart_comp))
    cg.add(var.set_bsl_switch(bsl))
    cg.add(var.set_rst_switch(rst))

    if CONF_BOOT_BAUD in config and config[CONF_BOOT_BAUD] > 0:
        cg.add(var.set_boot_baud(config[CONF_BOOT_BAUD]))
    if CONF_RESTORE_BAUD in config and config[CONF_RESTORE_BAUD] > 0:
        cg.add(var.set_restore_baud(config[CONF_RESTORE_BAUD]))

    erase_mode_val = {"sector": 0, "bank": 1, "auto": 2}[config.get(CONF_ERASE_MODE, "sector")]
    cg.add(var.set_erase_mode(erase_mode_val))

    if CONF_DEBUG in config:
        cg.add(var.set_verbose(config[CONF_DEBUG]))
    if CONF_SHOW_PROGRESS in config:
        cg.add(var.set_show_progress(config[CONF_SHOW_PROGRESS]))
    if CONF_PROGRESS_STEP in config:
        cg.add(var.set_progress_step(config[CONF_PROGRESS_STEP]))

    if CONF_FW_VERSION_TEXT in config:
        fw_txt = await cg.get_variable(config[CONF_FW_VERSION_TEXT])
        cg.add(var.set_fw_text_sensor(fw_txt))
    if CONF_IEEE_TEXT in config:
        ieee_txt = await cg.get_variable(config[CONF_IEEE_TEXT])
        cg.add(var.set_ieee_text_sensor(ieee_txt))
    if CONF_LATEST_FW_TEXT in config:
        latest_txt = await cg.get_variable(config[CONF_LATEST_FW_TEXT])
        cg.add(var.set_latest_text_sensor(latest_txt))
    if CONF_VARIANT_TEXT in config:
        vtxt = await cg.get_variable(config[CONF_VARIANT_TEXT])
        cg.add(var.set_variant_text_sensor(vtxt))

    if CONF_CHECK_ON_BOOT in config:
        cg.add(var.set_check_on_boot(config[CONF_CHECK_ON_BOOT]))
    if CONF_CHECK_INTERVAL_DAYS in config:
        cg.add(var.set_check_interval_days(config[CONF_CHECK_INTERVAL_DAYS]))
    if CONF_DETECT_ON_BOOT in config:
        cg.add(var.set_detect_on_boot(config[CONF_DETECT_ON_BOOT]))
    if CONF_DETECT_ON_BOOT_DELAY_MS in config:
        cg.add(var.set_detect_on_boot_delay_ms(config[CONF_DETECT_ON_BOOT_DELAY_MS]))

    # Variant preference (0=auto, 2=P2, 7=P7)
    if CONF_VARIANT in config:
        vstr = config[CONF_VARIANT]
        vmap = {"auto": 0, "p2": 2, "cc2652p2": 2, "p7": 7, "cc2652p7": 7}
        cg.add(var.set_variant(vmap.get(vstr, 0)))
