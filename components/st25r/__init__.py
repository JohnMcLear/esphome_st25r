from esphome import automation
import esphome.codegen as cg
from esphome.components import nfc
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_ON_FINISHED_WRITE,
    CONF_ON_TAG,
    CONF_ON_TAG_REMOVED,
    CONF_TRIGGER_ID,
)

CODEOWNERS = ["@JohnMcLear"]
AUTO_LOAD = ["binary_sensor", "nfc"]
MULTI_CONF = True

CONF_ST25R_ID = "st25r_id"
CONF_RF_FIELD_ENABLED = "rf_field_enabled"

st25r_ns = cg.esphome_ns.namespace("st25r")
ST25R = st25r_ns.class_("ST25R", cg.PollingComponent)

ST25ROnFinishedWriteTrigger = st25r_ns.class_(
    "ST25ROnFinishedWriteTrigger", automation.Trigger.template()
)

ST25RIsWritingCondition = st25r_ns.class_(
    "ST25RIsWritingCondition", automation.Condition
)

ST25R_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(ST25R),
        cv.Optional(CONF_ON_TAG): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(nfc.NfcOnTagTrigger),
            }
        ),
        cv.Optional(CONF_ON_FINISHED_WRITE): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(
                    ST25ROnFinishedWriteTrigger
                ),
            }
        ),
        cv.Optional(CONF_ON_TAG_REMOVED): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(nfc.NfcOnTagTrigger),
            }
        ),
        cv.Optional(CONF_RF_FIELD_ENABLED, default=False): cv.boolean,
    }
).extend(cv.polling_component_schema("1s"))


async def setup_st25r(var, config):
    await cg.register_component(var, config)
    cg.add(var.set_rf_field_enabled(config[CONF_RF_FIELD_ENABLED]))
    
    for conf in config.get(CONF_ON_TAG, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        cg.add(var.register_ontag_trigger(trigger))
        await automation.build_automation(
            trigger, [(cg.std_string, "x"), (nfc.NfcTag, "tag")], conf
        )

    for conf in config.get(CONF_ON_TAG_REMOVED, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        cg.add(var.register_ontagremoved_trigger(trigger))
        await automation.build_automation(
            trigger, [(cg.std_string, "x"), (nfc.NfcTag, "tag")], conf
        )

    for conf in config.get(CONF_ON_FINISHED_WRITE, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [], conf)


@automation.register_condition(
    "st25r.is_writing",
    ST25RIsWritingCondition,
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(ST25R),
        }
    ),
)
async def st25r_is_writing_to_code(config, condition_id, template_arg, args):
    var = cg.new_Pvariable(condition_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var
