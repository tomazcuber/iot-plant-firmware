#!/usr/bin/env python3
"""Check that apps/main and apps/active-wifi share one build configuration.

Run before each measurement campaign, and after any change to the shield, the
snippet or the shared modules (shared-modules spec, verification steps 2-3).

It needs three pristine builds. Always pass -p always: a stale build directory
has already produced a false difference (#9), and this script only detects a
build older than the shield or snippet fragment, not other stale inputs. Both
apps need a credentials.conf (copy credentials.conf.example; placeholder values
are enough) and the same interval fragment:

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
from pathlib import Path, PurePosixPath

_ASSIGNMENT_RE = re.compile(r"^(CONFIG_\w+)=(.*)$")
_NOT_SET_RE = re.compile(r"^# (CONFIG_\w+) is not set$")
_MODULE_DIR_RE = re.compile(r'^"plant":"([^"]+)"', re.MULTILINE)
_SOURCE_DIR_RE = re.compile(r"^APPLICATION_SOURCE_DIR:[A-Z]+=(.*)$", re.MULTILINE)
_PLANT_TOKEN_RE = re.compile(r"[\w.$-]*plant_[\w.$-]*")
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
        if match := _ASSIGNMENT_RE.match(line):
            values[match.group(1)] = match.group(2)
        elif match := _NOT_SET_RE.match(line):
            values[match.group(1)] = "n"
    return values


def require_app(build, app):
    """Reject a build directory that was not built from apps/<app>."""
    path = build / "CMakeCache.txt"
    match = _SOURCE_DIR_RE.search(read_text(path))
    if not match:
        raise UnusableInput(f"{path} does not name an application")
    source = PurePosixPath(match.group(1).strip())
    if source.parts[-2:] != ("apps", app):
        raise UnusableInput(f"{build} was built from {source}, expected apps/{app}")


def module_dir(build):
    """Return the plant module checkout this build was configured against."""
    path = build / "zephyr_modules.txt"
    match = _MODULE_DIR_RE.search(read_text(path))
    if not match:
        raise UnusableInput(f"{path} does not list the plant module")
    return Path(match.group(1))


def fragment_assignments(module, config_paths):
    """Return every symbol the fragments assign, if no build predates them."""
    assigned = {}
    for fragment in FRAGMENTS:
        path = module / fragment
        values = parse_config(read_text(path))
        if not values:
            raise UnusableInput(f"{path} assigns no symbols")
        for config_path in config_paths:
            if path.stat().st_mtime > config_path.stat().st_mtime:
                raise UnusableInput(f"{path} changed after {config_path} was built; "
                                    "rebuild with -p always")
        assigned.update(values)
    return assigned


def config_differences(main_config, active_wifi_config, assigned):
    """Return (symbols compared, sorted symbols whose values differ)."""
    plant_symbols = {s for s in main_config.keys() | active_wifi_config.keys()
                     if s.startswith("CONFIG_PLANT_")}
    compared = plant_symbols | assigned.keys()
    return compared, sorted(s for s in compared
                            if main_config.get(s) != active_wifi_config.get(s))


def check(main_build, active_wifi_build, static_sleep_build):
    """Print each failure and return the process exit status."""
    require_app(main_build, "main")
    require_app(active_wifi_build, "active-wifi")
    require_app(static_sleep_build, "static-sleep")

    main_path = main_build / "zephyr" / ".config"
    active_wifi_path = active_wifi_build / "zephyr" / ".config"
    main_config = parse_config(read_text(main_path))
    active_wifi_config = parse_config(read_text(active_wifi_path))

    map_path = static_sleep_build / "zephyr" / "zephyr.map"
    static_sleep_map = read_text(map_path)
    if not static_sleep_map.strip():
        raise UnusableInput(f"{map_path} is empty")

    module = module_dir(main_build)
    if module_dir(active_wifi_build) != module:
        raise UnusableInput("the two app builds used different plant module checkouts")
    assigned = fragment_assignments(module, (main_path, active_wifi_path))

    not_applied = sorted(s for s, value in assigned.items()
                         if value != "n" and s not in main_config and s not in active_wifi_config)
    if not_applied:
        raise UnusableInput("neither app build has " + ", ".join(not_applied)
                            + "; did both opt into the shield and snippet?")

    compared, differing = config_differences(main_config, active_wifi_config, assigned)
    for symbol in differing:
        print(f"DIFF {symbol}: main={main_config.get(symbol, ABSENT)} "
              f"active-wifi={active_wifi_config.get(symbol, ABSENT)}")

    leaked = sorted(set(_PLANT_TOKEN_RE.findall(static_sleep_map)))
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
