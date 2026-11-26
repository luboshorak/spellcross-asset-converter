# Build-time provisioning of a separate FFmpeg executable for Windows x64.
# Official FFmpeg download page links to Gyan's Windows builds:
# https://ffmpeg.org/download.html
# Pin the exact archive and SHA-256; no runtime network requests.
include(FetchContent)
set(SC_FFMPEG_VERSION "8.0.1")
set(SC_FFMPEG_RELEASE "https://github.com/GyanD/codexffmpeg/releases/tag/${SC_FFMPEG_VERSION}")
FetchContent_Declare(sc_ffmpeg_distribution
  URL "https://github.com/GyanD/codexffmpeg/releases/download/${SC_FFMPEG_VERSION}/ffmpeg-${SC_FFMPEG_VERSION}-essentials_build.zip"
  URL_HASH SHA256=e2aaeaa0fdbc397d4794828086424d4aaa2102cef1fb6874f6ffd29c0b88b673
  TLS_VERIFY ON
)
FetchContent_MakeAvailable(sc_ffmpeg_distribution)
set(_ffmpeg_dir "${sc_ffmpeg_distribution_SOURCE_DIR}")
# FetchContent normally strips the sole top-level archive directory.
if(NOT EXISTS "${_ffmpeg_dir}/bin/ffmpeg.exe")
  set(_ffmpeg_dir "${_ffmpeg_dir}/ffmpeg-${SC_FFMPEG_VERSION}-essentials_build")
endif()
set(SC_BUNDLED_FFMPEG "${_ffmpeg_dir}/bin/ffmpeg.exe")
if(NOT EXISTS "${SC_BUNDLED_FFMPEG}")
  message(FATAL_ERROR "Verified FFmpeg archive does not contain bin/ffmpeg.exe")
endif()
# Keep the original upstream licensing and build information beside the exe.
file(GLOB _ffmpeg_notices LIST_DIRECTORIES false
  "${_ffmpeg_dir}/LICENSE*" "${_ffmpeg_dir}/README*")
if(NOT _ffmpeg_notices)
  message(FATAL_ERROR "FFmpeg distribution is missing its license/readme")
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/FFmpeg-origin.txt"
  "Separate FFmpeg ${SC_FFMPEG_VERSION} Windows essentials distribution\n"
  "Upstream binary and build information: ${SC_FFMPEG_RELEASE}\n"
  "FFmpeg source: https://ffmpeg.org/download.html\n"
  "Gyan build information: https://www.gyan.dev/ffmpeg/builds/\n"
  "This executable is independently licensed, not covered by the converter MIT license.\n"
  "Before redistributing binaries, fulfill applicable GPL and corresponding-source obligations.\n")
add_custom_target(sc_ffmpeg_runtime ALL
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${SC_BUNDLED_FFMPEG}" "$<TARGET_FILE_DIR:SpellcrossConverter>/ffmpeg.exe"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:SpellcrossConverter>/ffmpeg-notices"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${_ffmpeg_notices} "${CMAKE_CURRENT_BINARY_DIR}/FFmpeg-origin.txt" "$<TARGET_FILE_DIR:SpellcrossConverter>/ffmpeg-notices"
  VERBATIM)
add_dependencies(sc_ffmpeg_runtime SpellcrossConverter)
# Install the locally provisioned executable along with its upstream notices.
install(PROGRAMS "${SC_BUNDLED_FFMPEG}" DESTINATION bin)
install(FILES ${_ffmpeg_notices} "${CMAKE_CURRENT_BINARY_DIR}/FFmpeg-origin.txt" DESTINATION bin/ffmpeg-notices)
message(STATUS "FFmpeg ${SC_FFMPEG_VERSION} will be staged beside SpellcrossConverter")
