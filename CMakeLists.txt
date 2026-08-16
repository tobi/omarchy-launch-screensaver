cmake_minimum_required(VERSION 3.20)
project(ssaver LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# --- Rust engine (ttfx + libghostty-vt crate) --------------------------------
set(TTFX_C_DIR "${CMAKE_CURRENT_SOURCE_DIR}/rust/ttfx_c")
set(CARGO_TARGET_DIR "${CMAKE_CURRENT_BINARY_DIR}/rust")
set(TTFX_C_SO "${CMAKE_CURRENT_BINARY_DIR}/libttfx_c.so")

find_program(CARGO cargo REQUIRED)
set(ZIG15 "$ENV{HOME}/.local/share/mise/installs/zig/0.15.2")

add_custom_command(
  OUTPUT "${TTFX_C_SO}"
  COMMAND ${CMAKE_COMMAND} -E env
          "CARGO_TARGET_DIR=${CARGO_TARGET_DIR}"
          "PATH=${ZIG15}:$ENV{PATH}"
          --unset=GHOSTTY_SOURCE_DIR
          ${CARGO} build --release --manifest-path "${TTFX_C_DIR}/Cargo.toml"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CARGO_TARGET_DIR}/release/libttfx_c.so" "${TTFX_C_SO}"
  WORKING_DIRECTORY "${TTFX_C_DIR}"
  DEPENDS
    "${TTFX_C_DIR}/src/lib.rs"
    "${TTFX_C_DIR}/src/vt.rs"
    "${TTFX_C_DIR}/Cargo.toml"
    "${TTFX_C_DIR}/include/ttfx_c.h"
  COMMENT "Building ttfx_c (libghostty-vt crate) via cargo"
  USES_TERMINAL
)
add_custom_target(ttfx_c_build DEPENDS "${TTFX_C_SO}")
add_library(ttfx_c SHARED IMPORTED GLOBAL)
set_target_properties(ttfx_c PROPERTIES
  IMPORTED_LOCATION "${TTFX_C_SO}"
  IMPORTED_NO_SONAME TRUE
  INTERFACE_INCLUDE_DIRECTORIES "${TTFX_C_DIR}/include"
)
add_dependencies(ttfx_c ttfx_c_build)

# --- Wayland layer-shell protocol --------------------------------------------
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT wayland-client)
find_program(WAYLAND_SCANNER wayland-scanner)
set(WLR_XML "${CMAKE_CURRENT_SOURCE_DIR}/third_party/wlr-layer-shell-unstable-v1.xml")
set(XDG_XML "/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml")
set(ALPHA_XML "/usr/share/wayland-protocols/staging/alpha-modifier/alpha-modifier-v1.xml")
set(PROTO_DIR "${CMAKE_CURRENT_BINARY_DIR}/proto")
file(MAKE_DIRECTORY "${PROTO_DIR}")

set(HAVE_LAYER_SHELL OFF)
set(HAVE_ALPHA_MOD OFF)
set(PROTO_SRCS "")
if(WAYLAND_SCANNER AND EXISTS "${WLR_XML}" AND EXISTS "${XDG_XML}" AND WAYLAND_CLIENT_FOUND)
  set(HAVE_LAYER_SHELL ON)
  list(APPEND PROTO_SRCS
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
if(WAYLAND_SCANNER AND EXISTS "${ALPHA_XML}" AND WAYLAND_CLIENT_FOUND)
  set(HAVE_ALPHA_MOD ON)
  list(APPEND PROTO_SRCS "${PROTO_DIR}/alpha-modifier-v1-protocol.c")
  add_custom_command(
    OUTPUT
      "${PROTO_DIR}/alpha-modifier-v1-client-protocol.h"
      "${PROTO_DIR}/alpha-modifier-v1-protocol.c"
    COMMAND ${WAYLAND_SCANNER} client-header "${ALPHA_XML}"
            "${PROTO_DIR}/alpha-modifier-v1-client-protocol.h"
    COMMAND ${WAYLAND_SCANNER} private-code "${ALPHA_XML}"
            "${PROTO_DIR}/alpha-modifier-v1-protocol.c"
    DEPENDS "${ALPHA_XML}"
  )
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui)
find_package(LayerShellQt REQUIRED)
set(CMAKE_AUTOMOC ON)

# --- sources -----------------------------------------------------------------
set(SSAVER_CORE
  src/fade.cpp
  src/pipeline.cpp
)
set(SSAVER_QT
  src/main.cpp
  src/overlay_window.cpp
  src/args.cpp
  src/launch.cpp
  src/alpha_mod.c
)

add_library(ssaver_core STATIC ${SSAVER_CORE})
target_include_directories(ssaver_core PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${TTFX_C_DIR}/include"
)
target_link_libraries(ssaver_core PUBLIC ttfx_c pthread dl m)

add_executable(ssaver ${SSAVER_QT} src/layer_shell.c ${PROTO_SRCS})
set_target_properties(ssaver PROPERTIES OUTPUT_NAME omarchy-launch-screensaver)
target_link_libraries(ssaver PRIVATE ssaver_core Qt6::Core Qt6::Gui LayerShellQt::Interface)
target_include_directories(ssaver PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(ssaver PRIVATE
  SSAVER_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
if(HAVE_LAYER_SHELL)
  target_compile_definitions(ssaver PRIVATE HAVE_LAYER_SHELL)
  target_include_directories(ssaver PRIVATE "${PROTO_DIR}")
  target_link_libraries(ssaver PRIVATE ${WAYLAND_CLIENT_LIBRARIES})
  target_include_directories(ssaver PRIVATE ${WAYLAND_CLIENT_INCLUDE_DIRS})
endif()
if(HAVE_ALPHA_MOD)
  target_compile_definitions(ssaver PRIVATE HAVE_ALPHA_MOD)
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

set_target_properties(ssaver test_pipeline PROPERTIES
  BUILD_RPATH "${CMAKE_CURRENT_BINARY_DIR}"
  INSTALL_RPATH "\$ORIGIN")

add_test(NAME cli_help COMMAND ${CMAKE_COMMAND}
  -DBIN=$<TARGET_FILE:ssaver>
  -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_help.cmake")

add_test(NAME headless_print COMMAND $<TARGET_FILE:ssaver>
  --headless --frames 10 --effect print)
set_tests_properties(headless_print PROPERTIES
  PASS_REGULAR_EXPRESSION "backend=libghostty")

# The old terminal-hosted screensaver crashed when its terminal disappeared:
# ttfx tried to report EIO on stderr, hit EIO again, and Rust aborted.  The
# embedded engine must remain independent of process stdout/stderr.
add_test(NAME headless_closed_stdio COMMAND /bin/sh -c
  "exec 1>&- 2>&-; '$<TARGET_FILE:ssaver>' --headless --frames 10 --effect print")

add_test(NAME headless_dense COMMAND $<TARGET_FILE:ssaver>
  --headless --frames 80 --effect highlight --cols 40 --rows 5 --seed 1
  --input "${CMAKE_CURRENT_SOURCE_DIR}/assets/screensaver.txt")
set_tests_properties(headless_dense PROPERTIES
  PASS_REGULAR_EXPRESSION "empty_rows=0")

install(TARGETS ssaver RUNTIME DESTINATION bin)
