import esphome.codegen as cg
from esphome.components import switch
import esphome.config_validation as cv
from esphome.types import ConfigType

from .climate import CONF_PANASONIC_IR_ID, PanasonicIrClimate, validate_rkr

DEPENDENCIES = ["climate.panasonic_ir"]

CONFIG_SCHEMA = cv.All(cv.Schema({
    cv.GenerateID(CONF_PANASONIC_IR_ID): cv.use_id(PanasonicIrClimate),
    cv.Optional("econavi"): switch.switch_schema(switch.ApplySwitch, default_restore_mode="DISABLED", block_inverted=True),
    cv.Optional("mild_dry"): switch.switch_schema(switch.ApplySwitch, default_restore_mode="DISABLED", block_inverted=True),
}), cv.has_at_least_one_key('econavi', 'mild_dry'))
FINAL_VALIDATE_SCHEMA = validate_rkr


async def to_code(config: ConfigType) -> None:
    hub = await cg.get_variable(config[CONF_PANASONIC_IR_ID])
    entities = switch.sub_switches(config, parent=hub)
    await entities("econavi", hub.set_econavi_switch, apply="set_econavi(state)")
    await entities("mild_dry", hub.set_mild_dry_switch, apply="set_mild_dry(state)")
