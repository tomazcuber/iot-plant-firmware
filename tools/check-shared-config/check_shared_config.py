#!/usr/bin/env python3
"""Check that apps/main and apps/active-wifi share one configuration.

Run before each measurement campaign, and after any change to the shield, the
snippet or the shared modules (shared-modules spec, verification steps 2-3).

It needs three pristine builds. Always pass -p always: this script cannot tell
that a build directory is stale, and a stale one has already produced a false
difference (#9). Both apps need a credentials.conf (copy credentials.conf.example;
placeholder values are enough) and the same interval fragment:

    west build -p always -b esp32c6_devkitc/esp32c6/hpcore -d build-main apps/main -- -DEXTRA_CONF_FILE=interval-60.conf
    west build -p always -b esp32c6_devkitc/esp32c6/hpcore -d build-active-wifi apps/active-wifi -- -DEXTRA_CONF_FILE=interval-60.conf
    west build -p always -b esp32c6_devkitc/esp32c6/hpcore -d build-static-sleep apps/static-sleep
    python tools/check-shared-config/check_shared_config.py build-main build-active-wifi build-static-sleep

The two apps' .config files must agree on every CONFIG_PLANT_* symbol and on
every symbol assigned by plant_sensors.conf (SM-12) or plant-net.conf (SM-14).
Those two files are read from the plant module the builds were configured
against. A symbol present in only one .config is a difference. The static-sleep
zephyr.map must contain no plant_ symbol.

Exit status: 0 pass, 1 difference or plant_ symbol found, 2 could not check.
"""
import argparse
import re
import sys
from pathlib import Path

ASSIGNMENT = re.compile(r"^(CONFIG_\w+)=(.*)$")
NOT_SET = re.compile(r"^# (CONFIG_\w+) is not set$")
MODULE_DIR = re.compile(r'^"plant":"([^"]+)"', re.MULTILINE)
PLANT_TOKEN = re.compile(r"[\w.$-]*plant_[\w.$-]*")
FRAGMENTS = (
    Path("boards/shields/plant_sensors/plant_sensors.conf"),  # SM-12
    Path("snippets/plant-net/plant-net.conf"),  # SM-14
)
ABSENT = "<absent>"


class UnusableInput(Exception):
    """The check could not run, so neither a pass nor a failure would be true."""


def read_text(path):
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except OSError as error:
        raise UnusableInput(f"cannot read {path}: {error.strerror}") from error


def parse_config(text):
    """Return {symbol: value}. Kconfig's "is not set" form is the value n."""
    values = {}
    for line in text.splitlines():
        if match := ASSIGNMENT.match(line):
            values[match.group(1)] = match.group(2)
        elif match := NOT_SET.match(line):
            values[match.group(1)] = "n"
    return values


def module_dir(build):
    """Return the plant module checkout this build was configured against."""
    path = build / "zephyr_modules.txt"
    match = MODULE_DIR.search(read_text(path))
    if not match:
        raise UnusableInput(f"{path} does not list the plant module")
    return Path(match.group(1))


def fragment_assignments(module):
    """Return every symbol the shield and snippet fragments assign."""
    assigned = {}
    for fragment in FRAGMENTS:
        path = module / fragment
        values = parse_config(read_text(path))
        if not values:
            raise UnusableInput(f"{path} assigns no symbols")
        assigned.update(values)
    return assigned


def check(main_build, wifi_build, sleep_build):
    """Print each failure and return the process exit status."""
    main_config = parse_config(read_text(main_build / "zephyr" / ".config"))
    wifi_config = parse_config(read_text(wifi_build / "zephyr" / ".config"))
    sleep_map = read_text(sleep_build / "zephyr" / "zephyr.map")

    module = module_dir(main_build)
    if module_dir(wifi_build) != module:
        raise UnusableInput("the two app builds used different plant module checkouts")
    assigned = fragment_assignments(module)

    not_applied = sorted(s for s, value in assigned.items()
                         if value != "n" and s not in main_config and s not in wifi_config)
    if not_applied:
        raise UnusableInput("neither app build has " + ", ".join(not_applied)
                            + "; did both opt into the shield and snippet?")

    plant = {s for s in main_config.keys() | wifi_config.keys() if s.startswith("CONFIG_PLANT_")}
    compared = plant | assigned.keys()
    differing = sorted(s for s in compared if main_config.get(s) != wifi_config.get(s))
    for symbol in differing:
        print(f"DIFF {symbol}: main={main_config.get(symbol, ABSENT)} "
              f"active-wifi={wifi_config.get(symbol, ABSENT)}")

    leaked = sorted(set(PLANT_TOKEN.findall(sleep_map)))
    for token in leaked:
        print(f"LEAK static-sleep zephyr.map contains {token}")

    if differing or leaked:
        print(f"FAIL: {len(differing)} differing symbol(s), {len(leaked)} plant_ symbol(s)")
        return 1
    print(f"OK: {len(compared)} shared symbols identical (fragments from {module}); "
          "no plant_ symbol in static-sleep zephyr.map")
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("main_build", type=Path, help="apps/main build directory")
    parser.add_argument("active_wifi_build", type=Path, help="apps/active-wifi build directory")
    parser.add_argument("static_sleep_build", type=Path, help="apps/static-sleep build directory")
    args = parser.parse_args(argv)
    try:
        return check(args.main_build, args.active_wifi_build, args.static_sleep_build)
    except UnusableInput as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
