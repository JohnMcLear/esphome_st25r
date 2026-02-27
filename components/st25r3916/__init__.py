import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins, automation
from esphome.components import spi
from esphome.const import (
    CONF_ID,
    CONF_IRQ_PIN,
    CONF_RESET_PIN,
)

DEPENDENCIES = ["spi"]
AUTO_LOAD = ["binary_sensor", "sensor"]
CODEOWNERS = ["@yourusername"]

CONF_ON_TAG = "on_tag"
CONF_ON_TAG_REMOVED = "on_tag_removed"

st25r3916_ns = cg.esphome_ns.namespace("st25r3916")
ST25R3916 = st25r3916_ns.class_("ST25R3916", cg.PollingComponent, spi.SPIDevice)

# Trigger types
ST25R3916TagTrigger = st25r3916_ns.class_(
    "ST25R3916TagTrigger", automation.Trigger.template(cg.std_string)
)
ST25R3916TagRemovedTrigger = st25r3916_ns.class_(
    "ST25R3916TagRemovedTrigger", automation.Trigger.template(cg.std_string)
)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ST25R3916),
            cv.Optional(CONF_RESET_PIN): pins.gpio_output_pin_schema,
            cv.Required(CONF_IRQ_PIN): pins.internal_gpio_input_pin_schema,
            cv.Optional(CONF_ON_TAG): automation.validate_automation(
                {
                    cv.GenerateID(): cv.declare_id(ST25R3916TagTrigger),
                }
            ),
            cv.Optional(CONF_ON_TAG_REMOVED): automation.validate_automation(
                {
                    cv.GenerateID(): cv.declare_id(ST25R3916TagRemovedTrigger),
                }
            ),
        }
    )
    .extend(cv.polling_component_schema("1s"))
    .extend(spi.spi_device_schema(cs_pin_required=True))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await spi.register_spi_device(var, config)

    if CONF_RESET_PIN in config:
        reset = await cg.gpio_pin_expression(config[CONF_RESET_PIN])
        cg.add(var.set_reset_pin(reset))

    irq = await cg.gpio_pin_expression(config[CONF_IRQ_PIN])
    cg.add(var.set_irq_pin(irq))

    for conf in config.get(CONF_ON_TAG, []):
        trigger = cg.new_Pvariable(conf[CONF_ID], var)
        await automation.build_automation(trigger, [(cg.std_string, "x")], conf)
        cg.add(var.register_on_tag_trigger(trigger))

    for conf in config.get(CONF_ON_TAG_REMOVED, []):
        trigger = cg.new_Pvariable(conf[CONF_ID], var)
        await automation.build_automation(trigger, [(cg.std_string, "x")], conf)
        cg.add(var.register_on_tag_removed_trigger(trigger))
