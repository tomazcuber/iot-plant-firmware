cmake_minimum_required(VERSION 3.20)

# Shared modules (SM-04): sensor devicetree + calibration, and the network stack.
set(SHIELD plant_sensors)
set(SNIPPET plant-net)

# WiFi credentials and broker are local-only (gitignored); see credentials.conf.example.
# The read interval comes from a committed fragment selected per condition:
#   west build -p always ... -- -DEXTRA_CONF_FILE=interval-60.conf
# Never pass -DCONFIG_... on the command line: CMake caches it and it silently
# overrides later builds.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/credentials.conf)
  list(APPEND EXTRA_CONF_FILE ${CMAKE_CURRENT_SOURCE_DIR}/credentials.conf)
else()
  message(FATAL_ERROR "Missing credentials.conf: copy credentials.conf.example "
                      "to credentials.conf and fill in SSID, PSK and broker.")
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(main CXX C)
target_sources(app PRIVATE src/main.cpp src/cycle.cpp src/sleep_esp32.c)
