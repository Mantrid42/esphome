"""Exercise timer preference accounting through real configuration and code generation."""

from pathlib import Path

import pytest

from esphome.__main__ import generate_cpp_contents
from esphome.components.esp8266.const import KEY_ESP8266, KEY_PREFERENCES
from esphome.config import read_config
from esphome.core import CORE


@pytest.mark.parametrize("restore_from_flash", [False, True])
@pytest.mark.parametrize("timers_enabled", [False, True])
def test_timer_storage_follows_platform_default(
    tmp_path: Path, restore_from_flash: bool, timers_enabled: bool
) -> None:
    config_file = tmp_path / "timers.yaml"
    content = f"""
esphome:
  name: timer-storage-test
esp8266:
  board: esp01_1m
  restore_from_flash: {str(restore_from_flash).lower()}
wifi:
  ssid: test-network
remote_transmitter:
  pin: GPIO4
  carrier_duty_percent: 50%
climate:
  - platform: panasonic_ir
    id: ac
    model: RKR
"""
    if timers_enabled:
        content += """
time:
  - platform: sntp
    id: timer_clock
panasonic_ir:
  climate_id: ac
  time_id: timer_clock
"""
    config_file.write_text(content, encoding="utf-8")
    CORE.config_path = config_file
    config = read_config({})
    assert config is not None
    CORE.config = config
    generate_cpp_contents(config)
    reservations = CORE.data[KEY_ESP8266].get(KEY_PREFERENCES, [])
    assert reservations.count((4, restore_from_flash)) == int(timers_enabled)
    assert (
        any(d.name == "USE_PANASONIC_RKR_TIMERS" for d in CORE.defines)
        is timers_enabled
    )
