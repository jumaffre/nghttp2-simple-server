cmake_minimum_required(VERSION 3.16)

project(
  nghttp2-simple-server
  VERSION 0.0.1
  LANGUAGES C CXX
)

set(ENABLE_STATIC_LIB ON)
set(PACKAGE_VERSION     "1.0.0")
set(PACKAGE_VERSION_NUM "1")
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nghttp2/includes/nghttp2/nghttp2ver.h.in ${CMAKE_CURRENT_SOURCE_DIR}/nghttp2/includes/nghttp2/nghttp2ver.h @ONLY)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/nghttp2)