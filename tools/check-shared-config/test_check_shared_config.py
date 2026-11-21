"""Tests for the shared-config check, through its command-line seam.

Each test builds fake build directories and a fake Zephyr module on disk, then
calls main() with the three build directories.

Run: python -m unittest discover -s tools/check-shared-config
"""
import contextlib
import io
import os
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

    def app_build(self, app, config):
        build = self.root / f"build-{app}"
        module = self.module.as_posix()
        self.write(build / "zephyr/.config", config)
        self.write(build / "CMakeCache.txt",
                   f"APPLICATION_SOURCE_DIR:PATH=C:/zw/repo/apps/{app}\n")
        self.write(build / "zephyr_modules.txt",
                   '"hal_espressif":"C:/zw/modules/hal/espressif":"C:/zw/modules/hal/espressif/zephyr"\n'
                   f'"plant":"{module}":"{module}"\n')
        return build

    def make_builds(self, main=CONFIG, active_wifi=CONFIG, static_sleep_map=CLEAN_MAP):
        main_build = self.app_build("main", main)
        active_wifi_build = self.app_build("active-wifi", active_wifi)
        static_sleep_build = self.root / "build-static-sleep"
        self.write(static_sleep_build / "CMakeCache.txt",
                   "APPLICATION_SOURCE_DIR:PATH=C:/zw/repo/apps/static-sleep\n")
        self.write(static_sleep_build / "zephyr/zephyr.map", static_sleep_map)
        return main_build, active_wifi_build, static_sleep_build

    def invoke(self, *builds):
        out = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
            code = check_shared_config.main([str(build) for build in builds])
        return code, out.getvalue()

    def run_check(self, **inputs):
        return self.invoke(*self.make_builds(**inputs))


class Parity(CheckCase):
    def test_identical_shared_config_and_clean_map_pass(self):
        code, _ = self.run_check()
        self.assertEqual(code, 0)

    def test_changed_plant_value_fails_naming_symbol_and_both_values(self):
        active_wifi = CONFIG.replace("CONFIG_PLANT_SOIL_DRY_ADC=3000", "CONFIG_PLANT_SOIL_DRY_ADC=2900")
        code, out = self.run_check(active_wifi=active_wifi)
        self.assertEqual(code, 1)
        self.assertIn("CONFIG_PLANT_SOIL_DRY_ADC", out)
        self.assertIn("3000", out)
        self.assertIn("2900", out)

    def test_symbol_present_in_only_one_config_fails(self):
        active_wifi = CONFIG.replace("CONFIG_PLANT_SOIL_DRY_ADC=3000\n", "")
        code, out = self.run_check(active_wifi=active_wifi)
        self.assertEqual(code, 1)
        self.assertIn("CONFIG_PLANT_SOIL_DRY_ADC", out)
        self.assertIn("<absent>", out)

    def test_not_set_is_a_value_distinct_from_absent_and_from_y(self):
        main = CONFIG + "# CONFIG_PLANT_NET_WIFI_PS_NONE is not set\n"
        active_wifi = CONFIG + "CONFIG_PLANT_NET_WIFI_PS_NONE=y\n"
        code, out = self.run_check(main=main, active_wifi=active_wifi)
        self.assertEqual(code, 1)
        self.assertIn("CONFIG_PLANT_NET_WIFI_PS_NONE: main=n active-wifi=y", out)

    def test_zephyr_symbol_from_snippet_is_compared(self):
        active_wifi = CONFIG.replace("CONFIG_MQTT_KEEPALIVE=60", "CONFIG_MQTT_KEEPALIVE=120")
        code, out = self.run_check(active_wifi=active_wifi)
        self.assertEqual(code, 1)
        self.assertIn("CONFIG_MQTT_KEEPALIVE: main=60 active-wifi=120", out)

    def test_zephyr_symbol_from_shield_is_compared(self):
        active_wifi = CONFIG.replace("CONFIG_I2C=y", "# CONFIG_I2C is not set")
        code, out = self.run_check(active_wifi=active_wifi)
        self.assertEqual(code, 1)
        self.assertIn("CONFIG_I2C: main=y active-wifi=n", out)

    def test_app_local_symbol_may_differ(self):
        main = CONFIG + "CONFIG_APP_READ_INTERVAL_S=60\n"
        active_wifi = CONFIG + "CONFIG_APP_PUBLISH_INTERVAL_S=60\nCONFIG_BT=y\n"
        code, _ = self.run_check(main=main, active_wifi=active_wifi)
        self.assertEqual(code, 0)


class StaticSleepMap(CheckCase):
    def test_plant_symbol_in_map_fails_naming_it(self):
        static_sleep_map = CLEAN_MAP + " .text.plant_sensors_init  0x42000010  0x20 lib.a(sensors.c.obj)\n"
        code, out = self.run_check(static_sleep_map=static_sleep_map)
        self.assertEqual(code, 1)
        self.assertIn("plant_sensors_init", out)


class CannotCheck(CheckCase):
    """A check that cannot run must never read as a pass (exit 2, not 0 or 1)."""

    def assert_cannot_check_without(self, relative_path, expected_in_output):
        builds = self.make_builds()
        (self.root / relative_path).unlink()
        code, out = self.invoke(*builds)
        self.assertEqual(code, 2)
        self.assertIn(expected_in_output, out)

    def test_missing_config(self):
        self.assert_cannot_check_without("build-active-wifi/zephyr/.config", ".config")

    def test_missing_map_is_not_a_clean_map(self):
        self.assert_cannot_check_without("build-static-sleep/zephyr/zephyr.map", "zephyr.map")

    def test_missing_module_record(self):
        self.assert_cannot_check_without("build-main/zephyr_modules.txt", "zephyr_modules.txt")

    def test_missing_fragment(self):
        self.assert_cannot_check_without("module/snippets/plant-net/plant-net.conf", "plant-net.conf")

    def test_missing_cmake_cache(self):
        self.assert_cannot_check_without("build-static-sleep/CMakeCache.txt", "CMakeCache.txt")

    def test_empty_map_is_not_a_clean_map(self):
        code, out = self.run_check(static_sleep_map="")
        self.assertEqual(code, 2)
        self.assertIn("zephyr.map", out)

    def test_builds_from_different_module_checkouts(self):
        builds = self.make_builds()
        other = (self.root / "other-module").as_posix()
        self.write(builds[1] / "zephyr_modules.txt", f'"plant":"{other}":"{other}"\n')
        code, out = self.invoke(*builds)
        self.assertEqual(code, 2)
        self.assertIn("different", out)

    def test_builds_that_did_not_opt_in_are_not_a_vacuous_pass(self):
        code, out = self.run_check(main="CONFIG_BT=y\n", active_wifi="CONFIG_BT=y\n")
        self.assertEqual(code, 2)
        self.assertIn("CONFIG_PLANT_SENSORS", out)

    def test_same_app_build_given_twice(self):
        main_build, _, static_sleep_build = self.make_builds()
        code, out = self.invoke(main_build, main_build, static_sleep_build)
        self.assertEqual(code, 2)
        self.assertIn("apps/active-wifi", out)

    def test_wrong_app_build_for_static_sleep(self):
        main_build, active_wifi_build, _ = self.make_builds()
        code, out = self.invoke(main_build, active_wifi_build, active_wifi_build)
        self.assertEqual(code, 2)
        self.assertIn("apps/static-sleep", out)

    def test_fragment_changed_after_the_build(self):
        builds = self.make_builds()
        fragment = self.module / "snippets/plant-net/plant-net.conf"
        later = (builds[1] / "zephyr/.config").stat().st_mtime + 60
        os.utime(fragment, (later, later))
        code, out = self.invoke(*builds)
        self.assertEqual(code, 2)
        self.assertIn("plant-net.conf", out)
        self.assertIn("-p always", out)


if __name__ == "__main__":
    unittest.main()
