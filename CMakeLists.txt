cmake_minimum_required(VERSION 3.20)
project(ssaver LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# --- ttfx C ABI (Rust cdylib / staticlib) ------------------------------------
set(TTFX_C_DIR "${CMAKE_CURRENT_SOURCE_DIR}/rust/ttfx_c")
set(TTFX_C_LIB "${CMAKE_CURRENT_BINARY_DIR}/rust/libttfx_c.a")
set(CARGO_TARGET_DIR "${CMAKE_CURRENT_BINARY_DIR}/rust")

find_program(CARGO cargo REQUIRED)
add_custom_command(
  OUTPUT "${TTFX_C_LIB}"
  COMMAND ${CMAKE_COMMAND} -E env
          "CARGO_TARGET_DIR=${CARGO_TARGET_DIR}"
          "PATH=$ENV{PATH}"
          ${CARGO} build --release --manifest-path "${TTFX_C_DIR}/Cargo.toml"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CARGO_TARGET_DIR}/release/libttfx_c.a" "${TTFX_C_LIB}"
  WORKING_DIRECTORY "${TTFX_C_DIR}"
  DEPENDS
    "${TTFX_C_DIR}/src/lib.rs"
    "${TTFX_C_DIR}/Cargo.toml"
    "${TTFX_C_DIR}/include/ttfx_c.h"
  COMMENT "Building ttfx_c staticlib via cargo"
  USES_TERMINAL
)
add_custom_target(ttfx_c_build DEPENDS "${TTFX_C_LIB}")
add_library(ttfx_c STATIC IMPORTED GLOBAL)
set_target_properties(ttfx_c PROPERTIES
  IMPORTED_LOCATION "${TTFX_C_LIB}"
  INTERFACE_INCLUDE_DIRECTORIES "${TTFX_C_DIR}/include"
)
add_dependencies(ttfx_c ttfx_c_build)

# --- libghostty-vt (Zig C lib) -----------------------------------------------
set(GHOSTTY_PREFIX "")
set(GHOSTTY_VT_LIB "")
foreach(_p
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ghostty-prefix-main"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ghostty-prefix")
  if(EXISTS "${_p}/include/ghostty/vt/terminal.h")
    if(EXISTS "${_p}/lib/libghostty-vt.so")
      set(GHOSTTY_PREFIX "${_p}")
      set(GHOSTTY_VT_LIB "${_p}/lib/libghostty-vt.so")
      break()
    elseif(EXISTS "${_p}/lib/libghostty-vt.a")
      set(GHOSTTY_PREFIX "${_p}")
      set(GHOSTTY_VT_LIB "${_p}/lib/libghostty-vt.a")
      break()
    endif()
  endif()
endforeach()
set(GHOSTTY_INCLUDE "${GHOSTTY_PREFIX}/include")
if(GHOSTTY_PREFIX AND GHOSTTY_VT_LIB)
  set(HAVE_LIBGHOSTTY ON)
  message(STATUS "libghostty-vt: ${GHOSTTY_VT_LIB}")
else()
  set(HAVE_LIBGHOSTTY OFF)
  message(STATUS "libghostty-vt not found — pipeline will use ANSI fallback (HAVE_LIBGHOSTTY=OFF)")
endif()

# --- Wayland layer-shell protocol --------------------------------------------
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT wayland-client)
find_program(WAYLAND_SCANNER wayland-scanner)
set(WLR_XML "${CMAKE_CURRENT_SOURCE_DIR}/third_party/wlr-layer-shell-unstable-v1.xml")
set(XDG_XML "/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml")
set(PROTO_DIR "${CMAKE_CURRENT_BINARY_DIR}/proto")
file(MAKE_DIRECTORY "${PROTO_DIR}")

set(HAVE_LAYER_SHELL OFF)
if(WAYLAND_SCANNER AND EXISTS "${WLR_XML}" AND EXISTS "${XDG_XML}" AND WAYLAND_CLIENT_FOUND)
  set(HAVE_LAYER_SHELL ON)
  set(PROTO_SRCS
    "${PROTO_DIR}/xdg-shell-protocol.c"
    "${PROTO_DIR}/wlr-layer-shell-unstable-v1-protocol.c"
  )
  add_custom_command(
    OUTPUT
      "${PROTO_DIR}/xdg-shell-client-protocol.h"
      "${PROTO_DIR}/xdg-shell-protocol.c"
    COMMAND ${WAYLAND_SCANNER} client-header "${XDG_XML}" "${PROTO_DIR}/xdg-shell-client-protocol.h"
    COMMAND ${WAYLAND_SCANNER} private-code "${XDG_XML}" "${PROTO_DIR}/xdg-shell-protocol.c"
    DEPENDS "${XDG_XML}"
  )
  add_custom_command(
    OUTPUT
      "${PROTO_DIR}/wlr-layer-shell-unstable-v1-client-protocol.h"
      "${PROTO_DIR}/wlr-layer-shell-unstable-v1-protocol.c"
    COMMAND ${WAYLAND_SCANNER} client-header "${WLR_XML}"
            "${PROTO_DIR}/wlr-layer-shell-unstable-v1-client-protocol.h"
    COMMAND ${WAYLAND_SCANNER} private-code "${WLR_XML}"
            "${PROTO_DIR}/wlr-layer-shell-unstable-v1-protocol.c"
    DEPENDS "${WLR_XML}"
  )
endif()

# --- Qt6 ---------------------------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Core Gui)
set(CMAKE_AUTOMOC ON)

# --- sources -----------------------------------------------------------------
set(SSAVER_CORE
  src/fade.cpp
  src/pipeline.cpp
  src/ghostty_host.c
)
set(SSAVER_QT
  src/main.cpp
  src/overlay_window.cpp
  src/args.cpp
  src/launch.cpp
)

add_library(ssaver_core STATIC ${SSAVER_CORE})
target_include_directories(ssaver_core PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${TTFX_C_DIR}/include"
)
target_link_libraries(ssaver_core PUBLIC ttfx_c pthread dl m)
if(HAVE_LIBGHOSTTY)
  target_compile_definitions(ssaver_core PUBLIC HAVE_LIBGHOSTTY)
  target_include_directories(ssaver_core PUBLIC "${GHOSTTY_INCLUDE}")
  target_link_libraries(ssaver_core PUBLIC "${GHOSTTY_VT_LIB}" stdc++)
endif()

add_executable(ssaver ${SSAVER_QT} src/layer_shell.c ${PROTO_SRCS})
set_target_properties(ssaver PROPERTIES OUTPUT_NAME omarchy-launch-screensaver)
target_link_libraries(ssaver PRIVATE ssaver_core Qt6::Core Qt6::Gui)
target_include_directories(ssaver PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(ssaver PRIVATE
  SSAVER_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
if(HAVE_LAYER_SHELL)
  target_compile_definitions(ssaver PRIVATE HAVE_LAYER_SHELL)
  target_include_directories(ssaver PRIVATE "${PROTO_DIR}")
  target_link_libraries(ssaver PRIVATE ${WAYLAND_CLIENT_LIBRARIES})
  target_include_directories(ssaver PRIVATE ${WAYLAND_CLIENT_INCLUDE_DIRS})
endif()

# --- tests -------------------------------------------------------------------
enable_testing()
add_executable(test_fade tests/test_fade.cpp src/fade.cpp)
target_include_directories(test_fade PRIVATE src)
add_test(NAME fade COMMAND test_fade)

add_executable(test_pipeline tests/test_pipeline.cpp)
target_link_libraries(test_pipeline PRIVATE ssaver_core)
add_test(NAME pipeline COMMAND test_pipeline)

if(HAVE_LIBGHOSTTY)
  set_target_properties(ssaver test_pipeline PROPERTIES
    BUILD_RPATH "${GHOSTTY_PREFIX}/lib"
    INSTALL_RPATH "${GHOSTTY_PREFIX}/lib")
endif()

add_test(NAME cli_help COMMAND ${CMAKE_COMMAND}
  -DBIN=$<TARGET_FILE:ssaver>
  -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_help.cmake")

if(HAVE_LIBGHOSTTY)
  add_test(NAME headless_print COMMAND $<TARGET_FILE:ssaver>
    --headless --frames 10 --effect print)
  set_tests_properties(headless_print PROPERTIES
    PASS_REGULAR_EXPRESSION "backend=libghostty")
endif()

install(TARGETS ssaver RUNTIME DESTINATION bin)

