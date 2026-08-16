cmake_minimum_required(VERSION 3.20)
project(ssaver LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# --- Rust ttfx engine ---------------------------------------------------------
set(TTFX_C_DIR "${CMAKE_CURRENT_SOURCE_DIR}/rust/ttfx_c")
set(CARGO_TARGET_DIR "${CMAKE_CURRENT_BINARY_DIR}/rust")
set(TTFX_C_LIB "${CMAKE_CURRENT_BINARY_DIR}/libttfx_c.a")

find_program(CARGO cargo REQUIRED)

add_custom_command(
  OUTPUT "${TTFX_C_LIB}"
  COMMAND ${CMAKE_COMMAND} -E env
          "CARGO_TARGET_DIR=${CARGO_TARGET_DIR}"
          ${CARGO} build --release --manifest-path "${TTFX_C_DIR}/Cargo.toml"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CARGO_TARGET_DIR}/release/libttfx_c.a" "${TTFX_C_LIB}"
  WORKING_DIRECTORY "${TTFX_C_DIR}"
  DEPENDS
    "${TTFX_C_DIR}/src/lib.rs"
    "${TTFX_C_DIR}/src/vt.rs"
    "${TTFX_C_DIR}/Cargo.toml"
    "${TTFX_C_DIR}/include/ttfx_c.h"
  COMMENT "Building ttfx C bridge via cargo"
  USES_TERMINAL
)
add_custom_target(ttfx_c_build DEPENDS "${TTFX_C_LIB}")
add_library(ttfx_c STATIC IMPORTED GLOBAL)
set_target_properties(ttfx_c PROPERTIES
  IMPORTED_LOCATION "${TTFX_C_LIB}"
  INTERFACE_INCLUDE_DIRECTORIES "${TTFX_C_DIR}/include"
)
add_dependencies(ttfx_c ttfx_c_build)

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
)

add_library(ssaver_core STATIC ${SSAVER_CORE})
target_include_directories(ssaver_core PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${TTFX_C_DIR}/include"
)
target_link_libraries(ssaver_core PUBLIC ttfx_c pthread dl m)

add_executable(ssaver ${SSAVER_QT})
set_target_properties(ssaver PROPERTIES OUTPUT_NAME omarchy-launch-screensaver)
target_link_libraries(ssaver PRIVATE ssaver_core Qt6::Core Qt6::Gui LayerShellQt::Interface)
target_include_directories(ssaver PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(ssaver PRIVATE
  SSAVER_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")

# --- tests -------------------------------------------------------------------
enable_testing()
add_executable(test_fade tests/test_fade.cpp src/fade.cpp)
target_include_directories(test_fade PRIVATE src)
add_test(NAME fade COMMAND test_fade)

add_executable(test_pipeline tests/test_pipeline.cpp)
target_link_libraries(test_pipeline PRIVATE ssaver_core)
add_test(NAME pipeline COMMAND test_pipeline)


add_test(NAME cli_help COMMAND ${CMAKE_COMMAND}
  -DBIN=$<TARGET_FILE:ssaver>
  -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_help.cmake")

add_test(NAME headless_print COMMAND $<TARGET_FILE:ssaver>
  --headless --frames 10 --effect print)
set_tests_properties(headless_print PROPERTIES
  PASS_REGULAR_EXPRESSION "effect=print")

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
