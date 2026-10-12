# CMakeData-vita.cmake — PS Vita platform sources for StepMania 5.0.
# Included from src/CMakeLists.txt when VITA is defined.
#
# Design (mirrors the 5.1 port):
# - Vita arch drivers in arch/Vita/ (display/input/sound/hooks/window).
# - miniz.c is vendored here (5.0 has no miniz; needed for data.zip).
# - Desktop RageDisplay_OGL is dropped (needs GLEW/GLX); GLES2 is only
#   added for WIN32/LINUX anyway. Vita uses RageDisplay_Vita.
# - Desktop X11/ALSA/Pulse/JACK/OSS/GTK/D3D sources are NOT added.

message(STATUS "-- Configuring StepMania 5.0 for PS Vita (vitaGL + VITASDK)")

add_compile_definitions(VITA)
add_compile_definitions(BACKTRACE_METHOD_TEXT="no backtrace")
add_compile_definitions(BACKTRACE_LOOKUP_METHOD_TEXT="dladdr")

# --- Vita arch drivers ---
list(APPEND SMDATA_ARCH_VITA_SRC
  "arch/Vita/ArchHooks_Vita.cpp"
  "arch/Vita/LowLevelWindow_Vita.cpp"
  "arch/Vita/InputHandler_Vita.cpp"
  "arch/Vita/RageSoundDriver_Vita.cpp"
  "arch/Vita/RageDisplay_Vita.cpp"
  "arch/Vita/VitaBootSplash.cpp"
  "arch/Vita/VitaShaccCgExt_Stubs.c"
  "arch/Vita/miniz.c"
)
list(APPEND SMDATA_ARCH_VITA_HPP
  "arch/Vita/ArchHooks_Vita.h"
  "arch/Vita/LowLevelWindow_Vita.h"
  "arch/Vita/InputHandler_Vita.h"
  "arch/Vita/RageSoundDriver_Vita.h"
  "arch/Vita/RageDisplay_Vita.h"
  "arch/Vita/VitaBootSplash.h"
  "arch/Vita/miniz.h"
)
source_group("Arch Specific\\\\Vita" FILES ${SMDATA_ARCH_VITA_SRC} ${SMDATA_ARCH_VITA_HPP})

list(APPEND SMDATA_ALL_ARCH_SRC ${SMDATA_ARCH_VITA_SRC})
list(APPEND SMDATA_ALL_ARCH_HPP ${SMDATA_ARCH_VITA_HPP})

# --- Drop desktop RageDisplay_OGL (needs GLEW/GLX); Vita uses RageDisplay_Vita ---
list(REMOVE_ITEM SMDATA_ALL_RAGE_SRC
  "RageDisplay_OGL.cpp"
  "RageDisplay_OGL_Helpers.cpp")
list(REMOVE_ITEM SMDATA_ALL_RAGE_HPP
  "RageDisplay_OGL.h"
  "RageDisplay_OGL_Helpers.h")

# --- Minimal OS layer: reuse Unix helpers where POSIX, skip X11/GTK/crash ---
list(APPEND SMDATA_OS_SRC
  "archutils/Unix/SpecialDirs.cpp"
  "archutils/Unix/GetSysInfo.cpp"
  "archutils/Common/PthreadHelpers.cpp"
)
list(APPEND SMDATA_OS_HPP
  "archutils/Unix/arch_setup.h"
  "archutils/Unix/SpecialDirs.h"
  "archutils/Unix/GetSysInfo.h"
  "archutils/Common/PthreadHelpers.h"
)

# --- Vita build counter: increments on every build (see Build-Vita/). ---
# The header lands in src/generated (on the include path for all configs).
# NOTE: add_dependencies(StepMania vita_build_number) is issued in
# src/CMakeLists.txt after add_executable().
add_custom_target(vita_build_number ALL
  COMMAND ${CMAKE_COMMAND}
    -DVITA_BUILD_COUNTER=${SM_ROOT_DIR}/Build-Vita/build-number.txt
    -DVITA_BUILD_HEADER=${SM_SRC_DIR}/generated/vita_build_number.h
    -P ${SM_ROOT_DIR}/Build-Vita/bump-vita-build.cmake
  COMMENT "Bumping Vita build number")
