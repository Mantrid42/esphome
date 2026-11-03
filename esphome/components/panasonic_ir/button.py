import esphome.codegen as cg
from esphome.components import button
import esphome.config_validation as cv
from esphome.types import ConfigType

from . import CONF_TIMERS_ID, RkrTimers

DEPENDENCIES = ["panasonic_ir"]
CONF_APPLY_TIMERS = "apply_timers"
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_TIMERS_ID): cv.use_id(RkrTimers),
        cv.Required(CONF_APPLY_TIMERS): button.button_schema(button.ApplyButton),
    }
)


async def to_code(config: ConfigType) -> None:
    timers = await cg.get_variable(config[CONF_TIMERS_ID])
    buttons = button.sub_buttons(config, parent=timers)
    await buttons(CONF_APPLY_TIMERS, None, apply="apply()")
