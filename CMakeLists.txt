cmake_minimum_required(VERSION 3.20)
project(gstreamer_mojo VERSION 0.1.0 LANGUAGES C)

include(CTest)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET
  gstreamer-1.0>=1.20
  gstreamer-app-1.0>=1.20
  gstreamer-webrtc-1.0>=1.20
  gstreamer-sdp-1.0>=1.20)

set(GSTREAMER_MOJO_IMPLEMENTATION_TARGET gstreamer_mojo)
if(UNIX AND NOT APPLE)
  # Mojo 1.0's Linux executable exports multimedia symbols. A small public
  # shim deep-binds the actual bridge so GStreamer/GLib resolve consistently
  # inside that dependency graph instead of binding to the host executable.
  add_library(gstreamer_mojo_impl SHARED bridge/src/gstreamer_mojo.c)
  add_library(gstreamer_mojo SHARED bridge/src/gstreamer_mojo_shim.c)
  target_compile_definitions(gstreamer_mojo PRIVATE _GNU_SOURCE)
  target_compile_options(gstreamer_mojo PRIVATE -Wno-pedantic)
  target_link_libraries(gstreamer_mojo PRIVATE ${CMAKE_DL_LIBS})
  set(GSTREAMER_MOJO_IMPLEMENTATION_TARGET gstreamer_mojo_impl)
else()
  add_library(gstreamer_mojo SHARED bridge/src/gstreamer_mojo.c)
endif()

target_compile_features(gstreamer_mojo PRIVATE c_std_11)
target_compile_options(gstreamer_mojo PRIVATE -Wall -Wextra -Wpedantic)
target_include_directories(gstreamer_mojo PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/bridge/include>
  $<INSTALL_INTERFACE:include>)
set_target_properties(gstreamer_mojo PROPERTIES
  C_VISIBILITY_PRESET hidden
  SUFFIX ".so"
  VERSION ${PROJECT_VERSION}
  SOVERSION 1)

target_compile_features(${GSTREAMER_MOJO_IMPLEMENTATION_TARGET} PRIVATE c_std_11)
target_compile_options(${GSTREAMER_MOJO_IMPLEMENTATION_TARGET} PRIVATE
  -Wall -Wextra -Wpedantic)
target_compile_definitions(${GSTREAMER_MOJO_IMPLEMENTATION_TARGET} PRIVATE
  GST_USE_UNSTABLE_API)
target_include_directories(${GSTREAMER_MOJO_IMPLEMENTATION_TARGET} PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/bridge/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(${GSTREAMER_MOJO_IMPLEMENTATION_TARGET} PRIVATE
  PkgConfig::GSTREAMER)
if(UNIX AND NOT APPLE)
  set_target_properties(gstreamer_mojo_impl PROPERTIES
    C_VISIBILITY_PRESET hidden
    SUFFIX ".so")
endif()

install(TARGETS gstreamer_mojo
  LIBRARY DESTINATION lib RUNTIME DESTINATION bin)
if(UNIX AND NOT APPLE)
  install(TARGETS gstreamer_mojo_impl
    LIBRARY DESTINATION lib RUNTIME DESTINATION bin)
endif()
install(FILES bridge/include/gstreamer_mojo.h DESTINATION include)

if(BUILD_TESTING)
  add_executable(gstreamer_mojo_integration_tests tests/integration.c)
  target_link_libraries(gstreamer_mojo_integration_tests PRIVATE
    gstreamer_mojo PkgConfig::GSTREAMER)
  add_test(NAME native-integration COMMAND gstreamer_mojo_integration_tests)
  set_tests_properties(native-integration PROPERTIES
    ENVIRONMENT "GST_PLUGIN_FEATURE_RANK=openh264dec:MAX")
endif()
