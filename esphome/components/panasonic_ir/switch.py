import esphome.codegen as cg
from esphome.components import switch
import esphome.config_validation as cv
from esphome.const import CONF_INITIAL_STATE, CONF_RESTORE_MODE, CONF_RESTORE_STATE
from esphome.types import ConfigType

from . import CONF_TIMER, CONF_TIMERS_ID, TIMER_SLOTS, RkrTimers
from .climate import panasonic_ir_ns

DEPENDENCIES = ["panasonic_ir"]
RkrTimerSwitch = panasonic_ir_ns.class_("RkrTimerSwitch", switch.Switch, cg.Component)

CONFIG_SCHEMA = (
    switch.switch_schema(
        RkrTimerSwitch, default_restore_mode="DISABLED", block_inverted=True
    )
    .extend(
        {
            cv.GenerateID(CONF_TIMERS_ID): cv.use_id(RkrTimers),
            cv.Required(CONF_TIMER): cv.enum(TIMER_SLOTS, upper=True),
            cv.Optional(CONF_RESTORE_MODE): cv.All(
                cv.one_of("DISABLED", upper=True), switch.validate_restore_mode
            ),
            cv.Optional(CONF_RESTORE_STATE): cv.invalid(
                "Timer intent is restored by the Panasonic timer controller"
            ),
            cv.Optional(CONF_INITIAL_STATE): cv.invalid(
                "Timer intent is restored by the Panasonic timer controller"
            ),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


CONF_ECONAVI = "econavi"
EconaviSwitch = panasonic_ir_ns.class_("EconaviSwitch", switch.Switch, cg.Component)
CONFIG_SCHEMA = cv.Any(
    cv.Schema(
        {
            cv.GenerateID(CONF_TIMERS_ID): cv.use_id(RkrTimers),
            cv.Required(CONF_ECONAVI): switch.switch_schema(
                EconaviSwitch, default_restore_mode="DISABLED", block_inverted=True
            ).extend(cv.COMPONENT_SCHEMA),
        }
    ),
    CONFIG_SCHEMA,
)


async def to_code(config: ConfigType) -> None:
    timers = await cg.get_variable(config[CONF_TIMERS_ID])
    if CONF_ECONAVI in config:
        conf = config[CONF_ECONAVI]
        var = await switch.new_switch(conf, timers)
        await cg.register_component(var, conf)
        return
    var = await switch.new_switch(config, timers, config[CONF_TIMER])
    await cg.register_component(var, config)
