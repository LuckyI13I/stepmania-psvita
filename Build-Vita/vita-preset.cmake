# Vita preset: configure StepMania 5 for PS Vita cross-compile.
# Usage (Windows PowerShell, VitaSDK at C:\Users\troia\Desktop\aichatbotsPSVITA\vitasdk):
#
#   $env:VITASDK = "C:\Users\troia\Desktop\aichatbotsPSVITA\vitasdk"
#   cmake -S "C:\Users\troia\Desktop\Work\StepMania 5 PS VITA\stepmania-5_1-new" `
#     -B "C:\Users\troia\Desktop\Work\StepMania 5 PS VITA\stepmania-5_1-new\Build-Vita\build" `
#     --toolchain "$env:VITASDK/share/vita.toolchain.cmake" `
#     -C "<this file directory>/vita-preset.cmake"   # or -DVITA=ON (see below)
#
# This file is meant to be passed via -C (initial cache) OR included manually.
# It sets Vita-specific cache entries BEFORE StepmaniaCore.cmake runs.

# --- Mark Vita build ---
set(VITA ON CACHE BOOL "Build for PS Vita" FORCE)
set(WITH_GLES2 ON CACHE BOOL "Use GLES2 renderer (vitaGL)" FORCE)

# --- Disable desktop-only options (avoid X11/ALSA/Pulse/JACK/GTK/FFmpeg hard deps) ---
set(WITH_GTK3 OFF CACHE BOOL "" FORCE)
set(WITH_X11 OFF CACHE BOOL "" FORCE)
set(WITH_XRANDR OFF CACHE BOOL "" FORCE)
set(WITH_LIBXTST OFF CACHE BOOL "" FORCE)
set(WITH_XINERAMA OFF CACHE BOOL "" FORCE)
set(WITH_PULSEAUDIO OFF CACHE BOOL "" FORCE)
set(WITH_ALSA OFF CACHE BOOL "" FORCE)
set(WITH_JACK OFF CACHE BOOL "" FORCE)
set(WITH_OSS OFF CACHE BOOL "" FORCE)
set(WITH_CRASH_HANDLER OFF CACHE BOOL "" FORCE)
set(WITH_SSE2 OFF CACHE BOOL "SSE2 N/A on ARM" FORCE)
set(WITH_LTO OFF CACHE BOOL "" FORCE)
set(WITH_MINIMAID OFF CACHE BOOL "" FORCE)
set(WITH_PACDRIVE OFF CACHE BOOL "" FORCE)
set(WITH_PARALLEL_PORT OFF CACHE BOOL "" FORCE)
set(WITH_PROFILING OFF CACHE BOOL "" FORCE)
set(WITH_FULL_RELEASE OFF CACHE BOOL "" FORCE)
set(WITH_TEXTURE_GENERATOR OFF CACHE BOOL "" FORCE)

# --- Vita memory: newlib heap is small; keep debug info lean ---
set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)

# --- vitaGL paths ---
# Option A (recommended): `make install` in 3rdparty/vitaGL with VITASDK set.
#   installs libvitaGL.a -> $VITASDK/arm-vita-eabi/lib + vitaGL.h -> include.
# Option B (fallback): headers from 3rdparty/vitaGL/source, lib built separately.
# find_library/find_path below prefer the installed SDK copy.
set(VITAGL_ROOT "${CMAKE_CURRENT_LIST_DIR}/../3rdparty/vitaGL" CACHE PATH "vitaGL source root")

# Global compile definitions for Vita (also set in CMakeData-vita.cmake).
add_compile_definitions(VITA)
add_compile_definitions(__vita__)
add_compile_definitions(HAVE_GLES2)
add_compile_definitions(WITH_GLES2)

# Include dirs: prefer SDK-installed vitaGL, fallback to in-tree source.
# NOTE: evaluated at configure time; $ENV{VITASDK} must be set.
if(DEFINED ENV{VITASDK})
  include_directories("$ENV{VITASDK}/arm-vita-eabi/include")
endif()
include_directories("${VITAGL_ROOT}/source")

# ARMv7-A flags suitable for Vita (Cortex-A9, NEON, no LTO yet).
add_compile_options(-march=armv7-a -mtune=cortex-a9 -mfpu=neon -mfloat-abi=hard -ffast-math)
add_compile_options(-Wno-psabi -Wno-unused-parameter -Wno-unused-variable)

message(STATUS "-- Vita preset loaded (VITA=ON, GLES2 via vitaGL, desktop deps OFF)")
