# Vita preset for StepMania 5.0: configure StepMania 5.0.12 for PS Vita.
# Usage (Windows PowerShell, VitaSDK installed):
#
#   $env:VITASDK = "C:\path\to\vitasdk"
#   cmake -S <stepmania-5.0.12> -B <stepmania-5.0.12>\Build-Vita\build `
#     --toolchain "$env:VITASDK/share/vita.toolchain.cmake" `
#     -C <stepmania-5.0.12>\Build-Vita\vita-preset.cmake `
#     -DVITA=ON -G Ninja "-DCMAKE_MAKE_PROGRAM=<ninja.exe>"
#
# Passed via -C (initial cache). Sets Vita-specific cache entries BEFORE
# StepmaniaCore.cmake runs.

# --- Mark Vita build ---
set(VITA ON CACHE BOOL "Build for PS Vita" FORCE)
set(WITH_GLES2 ON CACHE BOOL "Use GLES2-capable renderer (vitaGL)" FORCE)

# --- Disable desktop-only options ---
set(WITH_NETWORKING OFF CACHE BOOL "No online play on stage 1" FORCE)
set(WITH_FFMPEG OFF CACHE BOOL "No video playback on stage 1 (MovieTexture_Null)" FORCE)
set(WITH_SSE2 OFF CACHE BOOL "SSE2 N/A on ARM" FORCE)
set(WITH_LTO OFF CACHE BOOL "" FORCE)
set(WITH_JPEG ON CACHE BOOL "Bundled libjpeg" FORCE)
set(WITH_MP3 ON CACHE BOOL "Bundled libmad" FORCE)
set(WITH_OGG ON CACHE BOOL "Bundled ogg/vorbis" FORCE)
set(WITH_WAV ON CACHE BOOL "" FORCE)
set(WITH_GPL_LIBS ON CACHE BOOL "" FORCE)
set(WITH_PORTABLE_TOMCRYPT ON CACHE BOOL "Portable tomcrypt (no asm)" FORCE)
set(WITH_LOGGING_TIMING_DATA OFF CACHE BOOL "" FORCE)
set(WITH_FULL_RELEASE OFF CACHE BOOL "" FORCE)
set(WITH_EXTERNAL_WARNINGS OFF CACHE BOOL "" FORCE)

# --- Vita memory: keep debug info lean ---
set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)

# --- Global compile definitions for Vita ---
add_compile_definitions(VITA)
add_compile_definitions(__vita__)
add_compile_definitions(HAVE_GLES2)
add_compile_definitions(WITH_GLES2)

# --- VitaSDK system headers (vitaGL etc. come from vdpm-installed SDK) ---
# NOTE: evaluated at configure time; $ENV{VITASDK} must be set.
if(DEFINED ENV{VITASDK})
  include_directories("$ENV{VITASDK}/arm-vita-eabi/include")
endif()

# ARMv7-A flags suitable for Vita (Cortex-A9, NEON, hard float).
add_compile_options(-march=armv7-a -mtune=cortex-a9 -mfpu=neon -mfloat-abi=hard -ffast-math)
add_compile_options(-Wno-psabi -Wno-unused-parameter -Wno-unused-variable)

message(STATUS "-- Vita preset loaded (5.0, VITA=ON, desktop deps OFF)")
