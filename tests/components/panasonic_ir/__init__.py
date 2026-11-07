import esphome.codegen as cg
from esphome.components.logger import request_log_listener
from esphome.types import ConfigType
from tests.testing_helpers import ComponentManifestOverride


def override_manifest(manifest: ComponentManifestOverride) -> None:
    manifest.auto_load = ["climate_ir", "climate", "switch"]

    async def to_code_testing(config: ConfigType) -> None:
        request_log_listener()
        cg.add_define("USE_SWITCH")
        cg.add_define("ESPHOME_ENTITY_SWITCH_COUNT", 0)

    manifest.to_code = to_code_testing
