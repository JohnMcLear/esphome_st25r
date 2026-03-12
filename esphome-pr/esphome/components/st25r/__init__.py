from esphome import automation, pins
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor as binary_sensor_
from esphome.components import sensor as sensor_
from esphome.const import (
    CONF_ID,
    CONF_ON_TAG,
    CONF_ON_TAG_REMOVED,
    CONF_TRIGGER_ID,
    CONF_IRQ_PIN,
    CONF_RESET_PIN,
    CONF_STATUS,
)

CODEOWNERS = ["@JohnMcLear"]
AUTO_LOAD = ["binary_sensor", "sensor", "nfc"]
MULTI_CONF = True

CONF_ST25R_ID = "st25r_id"
CONF_RF_FIELD_ENABLED = "rf_field_enabled"
CONF_RF_POWER = "rf_power"
CONF_SUPPLY_3V3 = "supply_3v3"
CONF_FIELD_STRENGTH = "field_strength"
CONF_KNOWN_UIDS = "known_uids"
CONF_ON_TAG_SCAN = "on_tag_scan"
CONF_AAT_ENABLED = "aat_enabled"


def validate_uid(value):
    value = cv.string_strict(value).upper()
    if len(value) % 2 != 0:
        raise cv.Invalid("UID must be an even number of hex characters")
    try:
        bytes.fromhex(value)
    except ValueError as exc:
        raise cv.Invalid(f"Invalid hex UID: {exc}") from exc
    return value


st25r_ns = cg.esphome_ns.namespace("st25r")
ST25R = st25r_ns.class_("ST25R", cg.PollingComponent)

ST25RTagTrigger = st25r_ns.class_("ST25RTagTrigger", automation.Trigger.template(cg.std_string))
ST25RTagScanTrigger = st25r_ns.class_("ST25RTagScanTrigger", automation.Trigger.template())
ST25RTagRemovedTrigger = st25r_ns.class_(
    "ST25RTagRemovedTrigger", automation.Trigger.template(cg.std_string)
)

ST25R_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(ST25R),
        cv.Optional(CONF_IRQ_PIN): pins.internal_gpio_input_pin_schema,
        cv.Optional(CONF_RESET_PIN): pins.gpio_output_pin_schema,
        cv.Optional(CONF_RF_FIELD_ENABLED, default=True): cv.boolean,
        cv.Optional(CONF_RF_POWER, default=15): cv.int_range(min=0, max=15),
        cv.Optional(CONF_SUPPLY_3V3, default=True): cv.boolean,
        cv.Optional(CONF_AAT_ENABLED, default=False): cv.boolean,
        cv.Optional(CONF_STATUS): binary_sensor_.binary_sensor_schema(),
        cv.Optional(CONF_FIELD_STRENGTH): sensor_.sensor_schema(),
        cv.Optional(CONF_ON_TAG): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(ST25RTagTrigger),
            }
        ),
        cv.Optional(CONF_ON_TAG_SCAN): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(ST25RTagScanTrigger),
            }
        ),
        cv.Optional(CONF_ON_TAG_REMOVED): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(ST25RTagRemovedTrigger),
            }
        ),
        cv.Optional(CONF_KNOWN_UIDS): cv.ensure_list(validate_uid),
    }
).extend(cv.polling_component_schema("1s"))


async def setup_st25r(var, config):
    await cg.register_component(var, config)

    if CONF_IRQ_PIN in config:
        irq = await cg.gpio_pin_expression(config[CONF_IRQ_PIN])
        cg.add(var.set_irq_pin(irq))

    if CONF_RESET_PIN in config:
        reset = await cg.gpio_pin_expression(config[CONF_RESET_PIN])
        cg.add(var.set_reset_pin(reset))

    cg.add(var.set_rf_field_enabled(config[CONF_RF_FIELD_ENABLED]))
    cg.add(var.set_rf_power(config[CONF_RF_POWER]))
    cg.add(var.set_supply_3v3(config[CONF_SUPPLY_3V3]))
    cg.add(var.set_aat_enabled(config[CONF_AAT_ENABLED]))

    if CONF_STATUS in config:
        sens = await binary_sensor_.new_binary_sensor(config[CONF_STATUS])
        cg.add(var.set_status_binary_sensor(sens))

    if CONF_FIELD_STRENGTH in config:
        sens = await sensor_.new_sensor(config[CONF_FIELD_STRENGTH])
        cg.add(var.set_field_strength_sensor(sens))

    for conf in config.get(CONF_ON_TAG, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        cg.add(var.register_on_tag_trigger(trigger))
        await automation.build_automation(trigger, [(cg.std_string, "x")], conf)

    for conf in config.get(CONF_ON_TAG_SCAN, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        cg.add(var.register_on_tag_scan_trigger(trigger))
        await automation.build_automation(trigger, [], conf)

    for conf in config.get(CONF_ON_TAG_REMOVED, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        cg.add(var.register_on_tag_removed_trigger(trigger))
        await automation.build_automation(trigger, [(cg.std_string, "x")], conf)

    for uid in config.get(CONF_KNOWN_UIDS, []):
        cg.add(var.add_known_uid(uid))
