"""Tests for the shared-config check, through its command-line seam.

Each test builds fake build directories and a fake Zephyr module on disk, then
calls main() with the three build directories.

Run: python -m unittest discover -s tools/check-shared-config
"""
import contextlib
import io
import tempfile
import unittest
from pathlib import Path

import check_shared_config

SHIELD = """\
CONFIG_I2C=y
# Two-point soil calibration.
CONFIG_PLANT_SENSORS=y
CONFIG_PLANT_SOIL_DRY_ADC=3000
"""

SNIPPET = """\
# Shared network stack.
CONFIG_PLANT_NET=y
CONFIG_NET_IPV6=n
CONFIG_MQTT_KEEPALIVE=60
CONFIG_PLANT_NET_SNTP_SERVER="pool.ntp.org"
"""

CONFIG = """\
CONFIG_I2C=y
CONFIG_PLANT_SENSORS=y
CONFIG_PLANT_SOIL_DRY_ADC=3000
CONFIG_PLANT_NET=y
# CONFIG_NET_IPV6 is not set
CONFIG_MQTT_KEEPALIVE=60
CONFIG_PLANT_NET_SNTP_SERVER="pool.ntp.org"
"""

CLEAN_MAP = " .text.main  0x42000000  0x10 app/libapp.a(main.c.obj)\n"


class CheckCase(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name)
        self.module = self.root / "module"
        self.write(self.module / "boards/shields/plant_sensors/plant_sensors.conf", SHIELD)
        self.write(self.module / "snippets/plant-net/plant-net.conf", SNIPPET)

    def write(self, path, text):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")

    def app_build(self, name, config):
        build = self.root / name
        self.write(build / "zephyr/.config", config)
        module = self.module.as_posix()
        self.write(build / "zephyr_modules.txt",
                   '"hal_espressif":"C:/zw/modules/hal/espressif":"C:/zw/modules/hal/espressif/zephyr"\n'
                   f'"plant":"{module}":"{module}"\n')
        return build

    def run_check(self, main=CONFIG, wifi=CONFIG, sleep_map=CLEAN_MAP):
        main_build = self.app_build("build-main", main)
        wifi_build = self.app_build("build-active-wifi", wifi)
        sleep_build = self.root / "build-static-sleep"
        self.write(sleep_build / "zephyr/zephyr.map", sleep_map)
        out = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
            code = check_shared_config.main(
                [str(main_build), str(wifi_build), str(sleep_build)])
        return code, out.getvalue()


class Parity(CheckCase):
    def test_identical_shared_config_and_clean_map_pass(self):
        code, _ = self.run_check()
        self.assertEqual(code, 0)

    def test_changed_plant_value_fails_naming_symbol_and_both_values(self):
        wifi = CONFIG.replace("CONFIG_PLANT_SOIL_DRY_ADC=3000", "CONFIG_PLANT_SOIL_DRY_ADC=2900")
        code, out = self.run_check(wifi=wifi)
        self.assertEqual(code, 1)
        self.assertIn("CONFIG_PLANT_SOIL_DRY_ADC", out)
        self.assertIn("3000", out)
        self.assertIn("2900", out)

    def test_symbol_present_in_only_one_config_fails(self):
        wifi = CONFIG.replace("CONFIG_PLANT_SOIL_DRY_ADC=3000\n", "")
        code, out = self.run_check(wifi=wifi)
        self.assertEqual(code, 1)
        self.assertIn("CONFIG_PLANT_SOIL_DRY_ADC", out)
        self.assertIn("<absent>", out)

    def test_not_set_is_a_value_distinct_from_absent_and_from_y(self):
        main = CONFIG + "# CONFIG_PLANT_NET_WIFI_PS_NONE is not set\n"
        wifi = CONFIG + "CONFIG_PLANT_NET_WIFI_PS_NONE=y\n"
        code, out = self.run_check(main=main, wifi=wifi)
        self.assertEqual(code, 1)
        self.assertIn("CONFIG_PLANT_NET_WIFI_PS_NONE: main=n active-wifi=y", out)

    def test_zephyr_symbol_from_snippet_is_compared(self):
        wifi = CONFIG.replace("CONFIG_MQTT_KEEPALIVE=60", "CONFIG_MQTT_KEEPALIVE=120")
        code, out = self.run_check(wifi=wifi)
        self.assertEqual(code, 1)
        self.assertIn("CONFIG_MQTT_KEEPALIVE: main=60 active-wifi=120", out)

    def test_zephyr_symbol_from_shield_is_compared(self):
        wifi = CONFIG.replace("CONFIG_I2C=y", "# CONFIG_I2C is not set")
        code, out = self.run_check(wifi=wifi)
        self.assertEqual(code, 1)
        self.assertIn("CONFIG_I2C: main=y active-wifi=n", out)

    def test_app_local_symbol_may_differ(self):
        main = CONFIG + "CONFIG_APP_READ_INTERVAL_S=60\n"
        wifi = CONFIG + "CONFIG_APP_PUBLISH_INTERVAL_S=60\nCONFIG_BT=y\n"
        code, _ = self.run_check(main=main, wifi=wifi)
        self.assertEqual(code, 0)


class StaticSleepMap(CheckCase):
    def test_plant_symbol_in_map_fails_naming_it(self):
        sleep_map = CLEAN_MAP + " .text.plant_sensors_init  0x42000010  0x20 lib.a(sensors.c.obj)\n"
        code, out = self.run_check(sleep_map=sleep_map)
        self.assertEqual(code, 1)
        self.assertIn("plant_sensors_init", out)


class UnusableInput(CheckCase):
    """A check that cannot run must never read as a pass (exit 2, not 0 or 1)."""

    def assert_unusable(self, relative_path, expected_in_output):
        main_build = self.app_build("build-main", CONFIG)
        wifi_build = self.app_build("build-active-wifi", CONFIG)
        sleep_build = self.root / "build-static-sleep"
        self.write(sleep_build / "zephyr/zephyr.map", CLEAN_MAP)
        (self.root / relative_path).unlink()
        out = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
            code = check_shared_config.main(
                [str(main_build), str(wifi_build), str(sleep_build)])
        self.assertEqual(code, 2)
        self.assertIn(expected_in_output, out.getvalue())

    def test_missing_config(self):
        self.assert_unusable("build-active-wifi/zephyr/.config", ".config")

    def test_missing_map_is_not_a_clean_map(self):
        self.assert_unusable("build-static-sleep/zephyr/zephyr.map", "zephyr.map")

    def test_missing_module_record(self):
        self.assert_unusable("build-main/zephyr_modules.txt", "zephyr_modules.txt")

    def test_missing_fragment(self):
        self.assert_unusable("module/snippets/plant-net/plant-net.conf", "plant-net.conf")

    def test_builds_from_different_module_checkouts(self):
        main_build = self.app_build("build-main", CONFIG)
        wifi_build = self.app_build("build-active-wifi", CONFIG)
        other = (self.root / "other-module").as_posix()
        self.write(wifi_build / "zephyr_modules.txt", f'"plant":"{other}":"{other}"\n')
        sleep_build = self.root / "build-static-sleep"
        self.write(sleep_build / "zephyr/zephyr.map", CLEAN_MAP)
        out = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
            code = check_shared_config.main(
                [str(main_build), str(wifi_build), str(sleep_build)])
        self.assertEqual(code, 2)
        self.assertIn("different", out.getvalue())

    def test_builds_that_did_not_opt_in_are_not_a_vacuous_pass(self):
        code, out = self.run_check(main="CONFIG_BT=y\n", wifi="CONFIG_BT=y\n")
        self.assertEqual(code, 2)
        self.assertIn("CONFIG_PLANT_SENSORS", out)


if __name__ == "__main__":
    unittest.main()
