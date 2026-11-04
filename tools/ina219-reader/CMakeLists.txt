# INA219 current reader (RD-01). A bench instrument, not a measured build: it
# sets its own Kconfig, uses no plant_sensors shield, no plant-net snippet and
# links no plant_* module.
cmake_minimum_required(VERSION 3.20)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ina219-reader)

execute_process(
  COMMAND git describe --always --dirty
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  OUTPUT_VARIABLE READER_GIT
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
if(NOT READER_GIT)
  set(READER_GIT "unknown")
endif()

target_sources(app PRIVATE src/main.c)
target_compile_definitions(app PRIVATE READER_GIT="${READER_GIT}")
