# INA219 current reader (RD-01). A bench instrument, not a measured build: it
# sets its own Kconfig, uses no plant_sensors shield, no plant-net snippet and
# links no plant_* module.
cmake_minimum_required(VERSION 3.20)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ina219-reader)

# READER_GIT is regenerated on every build (not at configure time), so the
# reported id can't be stale after a commit.
set(READER_GIT_H ${CMAKE_CURRENT_BINARY_DIR}/gen/reader_git.h)
add_custom_target(reader_git
  BYPRODUCTS ${READER_GIT_H}
  COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_CURRENT_SOURCE_DIR} -DOUT=${READER_GIT_H}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/reader_git.cmake
  COMMENT "Generating reader_git.h")
add_dependencies(app reader_git)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/gen)

target_sources(app PRIVATE src/main.c)
