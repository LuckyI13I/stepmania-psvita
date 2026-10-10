if(VITA)
  # PS Vita: system libs from VitaSDK (vdpm) for zlib/png;
  # skip desktop-only and empty-submodule targets.
  # Bundled (populated, Vita-clean): lua, json, mad, jpeg, zlib, png, ogg,
  # vorbis, vorbisfile, pcre, tomcrypt (1.18.2), tommath (1.2.1), miniz.
  # (System vdpm libpng/libz are -fPIC and leave R_ARM_BASE_PREL relocs
  # that vita-elf-create rejects; bundled builds are non-PIC.)
  include(CMakeProject-lua.cmake)
  include(CMakeProject-json.cmake)
  include(CMakeProject-mad.cmake)
  include(CMakeProject-jpeg.cmake)
  include(CMakeProject-zlib.cmake)
  include(CMakeProject-png.cmake)
  include(CMakeProject-ogg.cmake)
  include(CMakeProject-vorbis.cmake)
  include(CMakeProject-vorbisfile.cmake)
  include(CMakeProject-pcre.cmake)
  include(CMakeProject-tomcrypt.cmake)
  include(CMakeProject-tommath.cmake)
  include(CMakeProject-miniz.cmake)
  # Skipped on Vita (N/A):
  # - glew: no GLX on Vita, vitaGL provides GL API
  # - ixwebsocket + mbedtls: no network on stage 1, submodule empty
  # - ffmpeg: no video playback on stage 1 (MovieTexture_Null)
else()
  include(CMakeProject-lua.cmake)
  include(CMakeProject-glew.cmake)
  include(CMakeProject-json.cmake)
  include(CMakeProject-mad.cmake)
  include(CMakeProject-zlib.cmake)
  include(CMakeProject-jpeg.cmake)
  include(CMakeProject-ogg.cmake)
  include(CMakeProject-vorbis.cmake)
  include(CMakeProject-vorbisfile.cmake)
  include(CMakeProject-pcre.cmake)
  include(CMakeProject-tomcrypt.cmake)
  include(CMakeProject-tommath.cmake)
  include(CMakeProject-png.cmake)
  include(CMakeProject-ixwebsocket.cmake)
  include(CMakeProject-miniz.cmake)
endif()
