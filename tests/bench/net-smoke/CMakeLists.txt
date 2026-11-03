cmake_minimum_required(VERSION 3.20)

# Shared network stack (SM-04): one line opts in to plant_net and its Kconfig.
set(SNIPPET plant-net)

# WiFi credentials are local-only (gitignored); see credentials.conf.example.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/credentials.conf)
  list(APPEND EXTRA_CONF_FILE ${CMAKE_CURRENT_SOURCE_DIR}/credentials.conf)
else()
  message(FATAL_ERROR "Missing credentials.conf: copy credentials.conf.example "
                      "to credentials.conf and fill in SSID and PSK.")
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(net-smoke)
target_sources(app PRIVATE src/main.c)
