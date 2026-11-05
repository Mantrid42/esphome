import esphome.codegen as cg
from esphome.components import select
import esphome.config_validation as cv
from esphome.const import ICON_ARROW_EXPAND_VERTICAL
from esphome.types import ConfigType

from .climate import PanasonicIrClimate

DEPENDENCIES = ["climate.panasonic_ir"]

CONF_PANASONIC_IR_ID = "panasonic_ir_id"
CONF_VANE_POSITION = "vane_position"
CONF_HORIZONTAL_VANE_POSITION = "horizontal_vane_position"

# Option 0 is auto, the others are the fixed positions of the vertical vane from the highest to the lowest
VANE_POSITIONS = ["Swing", "Up", "High", "Middle", "Low", "Down"]
HORIZONTAL_VANE_POSITIONS = [
    "Swing",
    "Left",
    "Middle Left",
    "Middle",
    "Middle Right",
    "Right",
]

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(CONF_PANASONIC_IR_ID): cv.use_id(PanasonicIrClimate),
            cv.Optional(CONF_VANE_POSITION): select.select_schema(
                select.ApplySelect, icon=ICON_ARROW_EXPAND_VERTICAL
            ),
            cv.Optional(CONF_HORIZONTAL_VANE_POSITION): select.select_schema(
                select.ApplySelect, icon="mdi:arrow-expand-horizontal"
            ),
        }
    ),
    cv.has_at_least_one_key(CONF_VANE_POSITION, CONF_HORIZONTAL_VANE_POSITION),
)


async def to_code(config: ConfigType) -> None:
    var = await cg.get_variable(config[CONF_PANASONIC_IR_ID])
    selects = select.sub_selects(config, parent=var)
    await selects(
        CONF_VANE_POSITION,
        var.set_vane_select,
        options=VANE_POSITIONS,
        apply="set_vane_position(index)",
    )
    await selects(
        CONF_HORIZONTAL_VANE_POSITION,
        var.set_horizontal_vane_select,
        options=HORIZONTAL_VANE_POSITIONS,
        apply="set_horizontal_vane_position(index)",
    )
