import esphome.codegen as cg
from esphome.components import switch
import esphome.config_validation as cv
from esphome.const import CONF_INITIAL_STATE, CONF_RESTORE_MODE, CONF_RESTORE_STATE
from esphome.types import ConfigType

from . import CONF_TIMER, CONF_TIMERS_ID, TIMER_SLOTS, RkrTimers
from .climate import panasonic_ir_ns

DEPENDENCIES = ["panasonic_ir"]
RkrTimerSwitch = panasonic_ir_ns.class_("RkrTimerSwitch", switch.Switch, cg.Component)

from .climate import CONF_PANASONIC_IR_ID, PanasonicIrClimate, validate_rkr

DEPENDENCIES = ["climate.panasonic_ir"]

TIMER_SCHEMA = (
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


FEATURE_SCHEMA = cv.All(cv.Schema({
    cv.GenerateID(CONF_PANASONIC_IR_ID): cv.use_id(PanasonicIrClimate),
    cv.Optional("econavi"): switch.switch_schema(switch.ApplySwitch, default_restore_mode="DISABLED", block_inverted=True),
    cv.Optional("mild_dry"): switch.switch_schema(switch.ApplySwitch, default_restore_mode="DISABLED", block_inverted=True),
}), cv.has_at_least_one_key('econavi', 'mild_dry'))



CONFIG_SCHEMA = cv.Any(TIMER_SCHEMA, FEATURE_SCHEMA)


def final_validate(config: ConfigType) -> None:
    if CONF_PANASONIC_IR_ID in config:
        validate_rkr(config)


FINAL_VALIDATE_SCHEMA = final_validate


async def to_code(config: ConfigType) -> None:
    if CONF_PANASONIC_IR_ID in config:
        hub = await cg.get_variable(config[CONF_PANASONIC_IR_ID])
        entities = switch.sub_switches(config, parent=hub)
        await entities("econavi", hub.set_econavi_switch, apply="set_econavi(state)")
        await entities("mild_dry", hub.set_mild_dry_switch, apply="set_mild_dry(state)")
        return
    timers = await cg.get_variable(config[CONF_TIMERS_ID])
    var = await switch.new_switch(config, timers, config[CONF_TIMER])
    await cg.register_component(var, config)
