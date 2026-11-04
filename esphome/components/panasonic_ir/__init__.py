import esphome.codegen as cg
from esphome.components import time
from esphome.components.const import CONF_CLIMATE_ID
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_MODEL, CONF_TIME_ID
import esphome.final_validate as fv
from esphome.types import ConfigType

from .climate import PanasonicIrClimate, panasonic_ir_ns

DOMAIN = "panasonic_ir"
DEPENDENCIES = ["climate.panasonic_ir", "time"]
CONF_ACCEPT_RECEIVED_TIMERS = "accept_received_timers"
CONF_TIMERS_ID = "timers_id"
CONF_TIMER = "timer"
RkrTimers = panasonic_ir_ns.class_("RkrTimers", cg.Component)
TimerSlot = panasonic_ir_ns.enum("TimerSlot", is_class=True)
TIMER_SLOTS = TimerSlot.options("ON1", "ON2", "OFF1", "OFF2", prefix="TIMER_SLOT_")

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(RkrTimers),
        cv.Required(CONF_CLIMATE_ID): cv.use_id(PanasonicIrClimate),
        cv.Optional(CONF_ACCEPT_RECEIVED_TIMERS, default=False): cv.boolean,
        cv.Required(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
    }
).extend(cv.COMPONENT_SCHEMA)


def _validate_rkr(config: ConfigType) -> None:
    if fv.get_declaring_config(config[CONF_CLIMATE_ID])[CONF_MODEL] != "RKR":
        raise cv.Invalid("HA timers are experimental and supported only for model RKR")


FINAL_VALIDATE_SCHEMA = _validate_rkr


async def to_code(config: ConfigType) -> None:
    climate = await cg.get_variable(config[CONF_CLIMATE_ID])
    clock = await cg.get_variable(config[CONF_TIME_ID])
    var = cg.new_Pvariable(config[CONF_ID], climate, clock)
    cg.add(var.set_accept_received_timers(config[CONF_ACCEPT_RECEIVED_TIMERS]))
    await cg.register_component(var, config)
    cg.add_define("USE_PANASONIC_RKR_TIMERS")
