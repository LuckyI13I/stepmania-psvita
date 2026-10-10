# CMakeData-vita.cmake — PS Vita platform sources for StepMania 5.
# Included from src/CMakeLists.txt when VITA is defined
# (see Build-Vita/vita-preset.cmake which sets -DVITA=ON).
#
# Design:
# - Reuse RageDisplay_GLES2 (vitaGL provides GLES2-compatible API).
# - Add Vita arch drivers: ArchHooks, LowLevelWindow, Input, Sound.
# - Force Null for MovieTexture/LoadingWindow/Dialog/Lights/MemoryCard(folder).
# - Desktop X11/ALSA/Pulse/JACK/OSS/GTK/D3D sources are NOT added.

message(STATUS "-- Configuring StepMania for PS Vita (vitaGL + VITASDK)")

# Vita compile definitions (propagated to all SM sources via add_compile_definitions
# in the preset, duplicated here for standalone includes).
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
  "arch/Vita/VitaShaccCgExt_Stubs.c"
)
list(APPEND SMDATA_ARCH_VITA_HPP
  "arch/Vita/ArchHooks_Vita.h"
  "arch/Vita/LowLevelWindow_Vita.h"
  "arch/Vita/InputHandler_Vita.h"
  "arch/Vita/RageSoundDriver_Vita.h"
  "arch/Vita/RageDisplay_Vita.h"
)
source_group("Arch Specific\\\\Vita" FILES ${SMDATA_ARCH_VITA_SRC} ${SMDATA_ARCH_VITA_HPP})

list(APPEND SMDATA_ALL_ARCH_SRC ${SMDATA_ARCH_VITA_SRC})
list(APPEND SMDATA_ALL_ARCH_HPP ${SMDATA_ARCH_VITA_HPP})

# --- GLES2 display (reused under vitaGL) ---
# RageDisplay_GLES2.cpp includes <GL/glew.h> on desktop; on Vita we compile it
# with HAVE_GL_HEADERS + vitaGL include path (see vita-preset.cmake).
# The file list itself is appended by the caller (src/CMakeLists Vita branch)
# to avoid double-adding on desktop builds.
set(SMDATA_VITA_GLES2_SRC "RageDisplay_GLES2.cpp" CACHE INTERNAL "")
set(SMDATA_VITA_GLES2_HPP "RageDisplay_GLES2.h" CACHE INTERNAL "")

# --- Vita build counter: increments on every build (see Build-Vita/). ---
# The header lands in SM_GENERATED_SRC_DIR (already on the include path).
# NOTE: add_dependencies(StepMania vita_build_number) is issued in
# src/CMakeLists.txt after add_executable().
add_custom_target(vita_build_number ALL
  COMMAND ${CMAKE_COMMAND}
    -DVITA_BUILD_COUNTER=${SM_ROOT_DIR}/Build-Vita/build-number.txt
    -DVITA_BUILD_HEADER=${SM_GENERATED_SRC_DIR}/vita_build_number.h
    -P ${SM_ROOT_DIR}/Build-Vita/bump-vita-build.cmake
  COMMENT "Bumping Vita build number")

# --- Drop FFmpeg movie texture (no video on stage 1, FFmpeg not ported) ---
# CMakeData-arch.cmake unconditionally adds MovieTexture_FFMpeg; remove it here.
list(REMOVE_ITEM SMDATA_ALL_ARCH_SRC
  "arch/MovieTexture/MovieTexture_FFMpeg.cpp")
list(REMOVE_ITEM SMDATA_ALL_ARCH_HPP
  "arch/MovieTexture/MovieTexture_FFMpeg.h")

# --- Drop desktop RageDisplay_OGL (needs GLX/GLEW); Vita uses GLES2 via vitaGL ---
list(REMOVE_ITEM SMDATA_ALL_RAGE_SRC
  "RageDisplay_OGL.cpp"
  "RageDisplay_OGL_Helpers.cpp")
list(REMOVE_ITEM SMDATA_ALL_RAGE_HPP
  "RageDisplay_OGL.h"
  "RageDisplay_OGL_Helpers.h")

# --- Threads: already added by CMakeData-arch.cmake via HAS_PTHREAD=TRUE ---
# (no duplicate add here; Vita newlib provides pthread-embedded).

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

# --- Link libs resolved in vita-preset / src/CMakeLists Vita branch ---
# Sce* stubs + vitaGL + bundled extern (lua, mad, pcre, tomcrypt, tommath,
# jpeg, png, jsoncpp, vorbis, ogg, miniz, ixwebsocket, zlib from VitaSDK).
set(SMDATA_VITA_LINK
  "vitaGL"
  "SceDisplay_stub"
  "SceGxm_stub"
  "SceCtrl_stub"
  "SceTouch_stub"
  "SceAudio_stub"
  "SceAudioOut_stub"
  "SceCommonDialog_stub"
  "SceAppMgr_stub"
  "SceAppUtil_stub"
  "ScePower_stub"
  "SceRtc_stub"
  "SceIo_stub"
  "SceKernel_stub"
  "SceNet_stub"
  "SceSysmodule_stub"
  "m" "pthread" "z"
  CACHE INTERNAL "")
