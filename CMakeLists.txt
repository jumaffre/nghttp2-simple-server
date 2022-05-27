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

add_executable(http2server ${CMAKE_CURRENT_SOURCE_DIR}/src/server.cpp)
target_include_directories(http2server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/nghttp2/includes)
target_link_libraries(http2server PRIVATE nghttp2_static)

add_executable(httpclient ${CMAKE_CURRENT_SOURCE_DIR}/src/client.cpp)
target_include_directories(httpclient PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/nghttp2/includes)
target_link_libraries(httpclient PRIVATE nghttp2_static)