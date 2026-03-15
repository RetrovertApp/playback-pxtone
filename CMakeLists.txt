# PxTone Playback Plugin
#
# This plugin plays PxTone music files (ptcop, pttune) using Wohlstand's libpxtone.
# PxTone is a simple music creation tool by Pixel (Cave Story creator).

cmake_minimum_required(VERSION 3.16)
project(playback_pxtone C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(PlaybackPlugin)

# Download libpxtone
download_library(
    NAME pxtone
    URL "https://github.com/Wohlstand/libpxtone/archive/dec7b6015a3f2736603d02bfb5e4b9bd453fc41c.tar.gz"
    FILENAME "libpxtone-dec7b60.tar.gz"
    HASH "f1115e6004428db9bc88cde31514deda7058c268fc1f05d6fe9cf9b36514a389"
    EXTRACT_DIR "libpxtone-dec7b6015a3f2736603d02bfb5e4b9bd453fc41c"
    PATCHES patches/scope-capture.patch
)

# Build libpxtone as a static library
set(PXTONE_SRC
    ${pxtone_SOURCE_DIR}/src/pxtnData.cpp
    ${pxtone_SOURCE_DIR}/src/pxtnDelay.cpp
    ${pxtone_SOURCE_DIR}/src/pxtnError.cpp
    ${pxtone_SOURCE_DIR}/src/pxtnEvelist.cpp
    ${pxtone_SOURCE_DIR}/src/pxtnMaster.cpp
    ${pxtone_SOURCE_DIR}/src/pxtnMem.cpp
    ${pxtone_SOURCE_DIR}/src/pxtnOverDrive.cpp
    ${pxtone_SOURCE_DIR}/src/pxtnPulse_Frequency.cpp
    ${pxtone_SOURCE_DIR}/src/pxtnPulse_Noise.cpp
    ${pxtone_SOURCE_DIR}/src/pxtnPulse_NoiseBuilder.cpp
    ${pxtone_SOURCE_DIR}/src/pxtnPulse_Oggv.cpp
    ${pxtone_SOURCE_DIR}/src/pxtnPulse_Oscillator.cpp
    ${pxtone_SOURCE_DIR}/src/pxtnPulse_PCM.cpp
    ${pxtone_SOURCE_DIR}/src/pxtnService.cpp
    ${pxtone_SOURCE_DIR}/src/pxtnService_moo.cpp
    ${pxtone_SOURCE_DIR}/src/pxtnText.cpp
    ${pxtone_SOURCE_DIR}/src/pxtnUnit.cpp
    ${pxtone_SOURCE_DIR}/src/pxtnWoice.cpp
    ${pxtone_SOURCE_DIR}/src/pxtnWoice_io.cpp
    ${pxtone_SOURCE_DIR}/src/pxtnWoicePTV.cpp
    ${pxtone_SOURCE_DIR}/src/pxtoneNoise.cpp
)

add_library(pxtone_static STATIC ${PXTONE_SRC})

target_include_directories(pxtone_static PRIVATE
    ${pxtone_SOURCE_DIR}/src
    ${pxtone_SOURCE_DIR}/stb_vorbis
)

# Enable OGG Vorbis support via bundled stb_vorbis
target_compile_definitions(pxtone_static PRIVATE
    pxINCLUDE_OGGVORBIS
    pxINCLUDE_OGGVORBIS_STB
)

suppress_external_warnings(pxtone_static)
set_target_properties(pxtone_static PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD 98
)

# Build plugin shared library
add_library(pxtone_playback SHARED
    pxtone_plugin.cpp
)

target_include_directories(pxtone_playback PRIVATE
    ${pxtone_SOURCE_DIR}/src
)

target_link_libraries(pxtone_playback PRIVATE pxtone_static)

if(NOT WIN32)
    target_link_libraries(pxtone_playback PRIVATE stdc++)
endif()

target_compile_options(pxtone_playback PRIVATE
    -Wno-unused-parameter
)

set_target_properties(pxtone_playback PROPERTIES
    CXX_STANDARD 11
)

rv_set_plugin_properties(pxtone_playback)
