import esphome.codegen as cg
from esphome.components import button
import esphome.config_validation as cv
from esphome.types import ConfigType

from . import CONF_TIMERS_ID, RkrTimers

DEPENDENCIES = ["panasonic_ir"]
CONF_APPLY_TIMERS = "apply_timers"
CONF_APPLY_ON1 = "apply_on1"
CONF_APPLY_ON2 = "apply_on2"
CONF_APPLY_OFF1 = "apply_off1"
CONF_APPLY_OFF2 = "apply_off2"
CONF_CANCEL_ALL = "cancel_all"
CONF_POWERFUL_QUIET = "powerful_quiet"
CONF_NANOE_G = "nanoe_g"
CONF_IAUTO_X = "iauto_x"
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_TIMERS_ID): cv.use_id(RkrTimers),
        cv.Optional(CONF_CANCEL_ALL): button.button_schema(button.ApplyButton),
        cv.Optional(CONF_POWERFUL_QUIET): button.button_schema(button.ApplyButton),
        cv.Optional(CONF_NANOE_G): button.button_schema(button.ApplyButton),
        cv.Optional(CONF_IAUTO_X): button.button_schema(button.ApplyButton),
        cv.Optional(CONF_APPLY_ON1): button.button_schema(button.ApplyButton),
        cv.Optional(CONF_APPLY_ON2): button.button_schema(button.ApplyButton),
        cv.Optional(CONF_APPLY_OFF1): button.button_schema(button.ApplyButton),
        cv.Optional(CONF_APPLY_OFF2): button.button_schema(button.ApplyButton),
        cv.Optional(CONF_APPLY_TIMERS): button.button_schema(button.ApplyButton),
    }
)


async def to_code(config: ConfigType) -> None:
    timers = await cg.get_variable(config[CONF_TIMERS_ID])
    buttons = button.sub_buttons(config, parent=timers)
    await buttons(CONF_APPLY_TIMERS, None, apply="apply_all()")
    await buttons(
        CONF_APPLY_ON1, None, apply="apply(panasonic_ir::TimerSlot::TIMER_SLOT_ON1)"
    )
    await buttons(
        CONF_APPLY_ON2, None, apply="apply(panasonic_ir::TimerSlot::TIMER_SLOT_ON2)"
    )
    await buttons(
        CONF_APPLY_OFF1, None, apply="apply(panasonic_ir::TimerSlot::TIMER_SLOT_OFF1)"
    )
    await buttons(
        CONF_APPLY_OFF2, None, apply="apply(panasonic_ir::TimerSlot::TIMER_SLOT_OFF2)"
    )
    await buttons(CONF_CANCEL_ALL, None, apply="cancel_all()")
    await buttons(CONF_POWERFUL_QUIET, None, apply="feature(2)")
    await buttons(CONF_NANOE_G, None, apply="feature(3)")
    await buttons(CONF_IAUTO_X, None, apply="feature(4)")
