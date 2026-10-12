# NOTE(Vita port): raised from 2.8.12 — CMake 4.x removed <3.5 compat.
cmake_minimum_required(VERSION 3.5)

project(StepMania)

if(VITA)
  # Global ARM flags for PS Vita (extern + src). ARM mode avoids Thumb-2
  # MOVW/MOVT relocations that vita-elf-create rejects.
  add_compile_options(-marm -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -ffunction-sections -fdata-sections)
endif()

include (StepmaniaCore.cmake)

# The external libraries need to be included.
add_subdirectory(extern)

# The internal libraries and eventual executable to be used.
add_subdirectory(src)
