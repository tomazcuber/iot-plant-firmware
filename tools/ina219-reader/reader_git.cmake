# Writes ${OUT} (reader_git.h) with `git describe --always --dirty` of ${SRC}.
# Run at build time (CMakeLists.txt custom target), so the id follows commits
# made after the CMake configure step. Rewrites the file only when it changes.
execute_process(
  COMMAND git describe --always --dirty
  WORKING_DIRECTORY ${SRC}
  OUTPUT_VARIABLE READER_GIT
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
if(NOT READER_GIT)
  set(READER_GIT "unknown")
endif()
set(content "#define READER_GIT \"${READER_GIT}\"\n")
set(old "")
if(EXISTS ${OUT})
  file(READ ${OUT} old)
endif()
if(NOT "${old}" STREQUAL "${content}")
  file(WRITE ${OUT} "${content}")
endif()
