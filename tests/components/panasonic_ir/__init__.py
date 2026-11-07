import esphome.codegen as cg
from esphome.types import ConfigType
from tests.testing_helpers import ComponentManifestOverride


def override_manifest(manifest: ComponentManifestOverride) -> None:
    manifest.auto_load = [
        "climate_ir",
        "climate",
        "time",
        "datetime",
        "switch",
        "select",
    ]
    manifest.dependencies = []
    manifest.multi_conf = False

    async def to_code_testing(config: ConfigType) -> None:
        cg.add_define("USE_PANASONIC_RKR_TIMERS")
        cg.add_define("USE_DATETIME_TIME")
        cg.add_define("ESPHOME_ENTITY_TIME_COUNT", 0)
        cg.add_define("USE_SWITCH")
        cg.add_define("ESPHOME_ENTITY_SWITCH_COUNT", 0)
        cg.add_define("USE_SELECT")
        cg.add_define("ESPHOME_ENTITY_SELECT_COUNT", 0)

    manifest.to_code = to_code_testing
