import esphome.codegen as cg
from esphome.components import button
import esphome.config_validation as cv
from esphome.types import ConfigType

from .climate import CONF_PANASONIC_IR_ID, PanasonicIrClimate, validate_rkr

DEPENDENCIES = ["climate.panasonic_ir"]

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(CONF_PANASONIC_IR_ID): cv.use_id(PanasonicIrClimate),
            cv.Optional("powerful_quiet"): button.button_schema(button.ApplyButton),
            cv.Optional("nanoe_g"): button.button_schema(button.ApplyButton),
            cv.Optional("iauto_x"): button.button_schema(button.ApplyButton),
        }
    ),
    cv.has_at_least_one_key("powerful_quiet", "nanoe_g", "iauto_x"),
)
FINAL_VALIDATE_SCHEMA = validate_rkr


async def to_code(config: ConfigType) -> None:
    hub = await cg.get_variable(config[CONF_PANASONIC_IR_ID])
    entities = button.sub_buttons(config, parent=hub)
    await entities("powerful_quiet", None, apply="send_powerful_quiet()")
    await entities("nanoe_g", None, apply="send_nanoe_g()")
    await entities("iauto_x", None, apply="enable_iauto()")
