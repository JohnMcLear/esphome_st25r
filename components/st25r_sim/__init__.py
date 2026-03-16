import esphome.codegen as cg
from esphome.components import st25r
import esphome.config_validation as cv
from esphome.const import CONF_ID

AUTO_LOAD = ["st25r"]
CODEOWNERS = ["@JohnMcLear"]
MULTI_CONF = True

CONF_SOCKET_PATH = "socket_path"
CONF_IC_IDENTITY = "ic_identity"

st25r_sim_ns = cg.esphome_ns.namespace("st25r_sim")
ST25RSim = st25r_sim_ns.class_("ST25RSim", st25r.ST25R)

CONFIG_SCHEMA = st25r.ST25R_SCHEMA.extend(
    {
        cv.GenerateID(): cv.declare_id(ST25RSim),
        cv.Optional(CONF_SOCKET_PATH, default="/tmp/st25r_sim.sock"): cv.string,
        cv.Optional(CONF_IC_IDENTITY, default=0x28): cv.hex_int,
    }
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await st25r.setup_st25r(var, config)
    cg.add(var.set_socket_path(config[CONF_SOCKET_PATH]))
    cg.add(var.set_ic_identity(config[CONF_IC_IDENTITY]))
