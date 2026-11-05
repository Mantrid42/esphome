import esphome.codegen as cg
from esphome.components import button
import esphome.config_validation as cv
from esphome.types import ConfigType

from . import CONF_TIMERS_ID, RkrTimers

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_TIMERS_ID): cv.use_id(RkrTimers),
        cv.Required("cancel_all"): button.button_schema(button.ApplyButton),
    }
)


async def to_code(config: ConfigType) -> None:
    hub = await cg.get_variable(config[CONF_TIMERS_ID])
    await button.sub_buttons(config, parent=hub)(
        "cancel_all", None, apply="cancel_all()"
    )
