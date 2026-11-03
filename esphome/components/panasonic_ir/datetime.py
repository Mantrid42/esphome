import esphome.codegen as cg
from esphome.components import datetime
import esphome.config_validation as cv
from esphome.types import ConfigType

from . import CONF_TIMER, CONF_TIMERS_ID, TIMER_SLOTS, RkrTimers
from .climate import panasonic_ir_ns

DEPENDENCIES = ["panasonic_ir"]
RkrTimerTime = panasonic_ir_ns.class_("RkrTimerTime", datetime.TimeEntity, cg.Component)

CONFIG_SCHEMA = (
    datetime.time_schema(RkrTimerTime)
    .extend(
        {
            cv.GenerateID(CONF_TIMERS_ID): cv.use_id(RkrTimers),
            cv.Required(CONF_TIMER): cv.enum(TIMER_SLOTS, upper=True),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config: ConfigType) -> None:
    timers = await cg.get_variable(config[CONF_TIMERS_ID])
    var = await datetime.new_datetime(config, timers, config[CONF_TIMER])
    await cg.register_component(var, config)
