import esphome.codegen as cg
from esphome.components import button
import esphome.config_validation as cv
from esphome.types import ConfigType

from . import CONF_TIMERS_ID, RkrTimers

from .climate import CONF_PANASONIC_IR_ID, PanasonicIrClimate, validate_rkr

DEPENDENCIES = ["climate.panasonic_ir"]

TIMER_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_TIMERS_ID): cv.use_id(RkrTimers),
        cv.Required("cancel_all"): button.button_schema(button.ApplyButton),
    }
)


FEATURE_SCHEMA = cv.All(cv.Schema({
    cv.GenerateID(CONF_PANASONIC_IR_ID): cv.use_id(PanasonicIrClimate),
    cv.Optional("powerful_quiet"): button.button_schema(button.ApplyButton),
    cv.Optional("nanoe_g"): button.button_schema(button.ApplyButton),
    cv.Optional("iauto_x"): button.button_schema(button.ApplyButton),
}), cv.has_at_least_one_key('powerful_quiet', 'nanoe_g', 'iauto_x'))



CONFIG_SCHEMA = cv.Any(TIMER_SCHEMA, FEATURE_SCHEMA)


def final_validate(config: ConfigType) -> None:
    if CONF_PANASONIC_IR_ID in config:
        validate_rkr(config)


FINAL_VALIDATE_SCHEMA = final_validate


async def to_code(config: ConfigType) -> None:
    if CONF_PANASONIC_IR_ID in config:
        hub = await cg.get_variable(config[CONF_PANASONIC_IR_ID])
        entities = button.sub_buttons(config, parent=hub)
        await entities("powerful_quiet", None, apply="send_powerful_quiet()")
        await entities("nanoe_g", None, apply="send_nanoe_g()")
        await entities("iauto_x", None, apply="enable_iauto()")
        return
    hub = await cg.get_variable(config[CONF_TIMERS_ID])
    await button.sub_buttons(config, parent=hub)(
        "cancel_all", None, apply="cancel_all()"
    )
