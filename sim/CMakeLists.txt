cmake_minimum_required(VERSION 3.16)
project(m5stopwatch_sim C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

set(ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/..")
set(MAIN_DIR "${ROOT_DIR}/main")
set(COMP_DIR "${ROOT_DIR}/components")

# LVGL configuration
set(LV_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/lv_conf.h" CACHE STRING "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)

add_subdirectory("${COMP_DIR}/lvgl" lvgl)
target_compile_options(lvgl PRIVATE -sUSE_SDL=2)
target_include_directories(lvgl PUBLIC "${CMAKE_CURRENT_LIST_DIR}")

# Smooth UI Toolkit, Mooncake, Mooncake Log
set(SMOOTH_UI_TOOLKIT_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SMOOTH_UI_TOOLKIT_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(MOONCAKE_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(MOONCAKE_LOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
add_subdirectory("${COMP_DIR}/mooncake_log" mooncake_log)
add_subdirectory("${COMP_DIR}/mooncake" mooncake)
add_subdirectory("${COMP_DIR}/smooth_ui_toolkit" smooth_ui_toolkit)

# Collect UI & asset sources
file(GLOB_RECURSE ASSET_SRCS
    "${MAIN_DIR}/assets/*.c"
    "${MAIN_DIR}/assets/*.cc"
    "${MAIN_DIR}/assets/*.cpp"
)

file(GLOB_RECURSE APP_COMMON_SRCS
    "${MAIN_DIR}/apps/common/*.c"
    "${MAIN_DIR}/apps/common/*.cc"
    "${MAIN_DIR}/apps/common/*.cpp"
)
list(FILTER APP_COMMON_SRCS EXCLUDE REGEX ".*/apps/common/weather/.*")

file(GLOB_RECURSE APP_SRCS
    "${MAIN_DIR}/apps/app_launcher/*.cpp"
    "${MAIN_DIR}/apps/app_stopwatch/*.cpp"
    "${MAIN_DIR}/apps/app_alarm_clock/*.cpp"
    "${MAIN_DIR}/apps/app_imu/*.cpp"
    "${MAIN_DIR}/apps/app_badge/*.cpp"
    "${MAIN_DIR}/apps/app_setup/*.cpp"
)
list(FILTER APP_SRCS EXCLUDE REGEX ".*/apps/app_setup/workers/check_update\\.cpp$")

add_executable(m5stopwatch_sim
    "${CMAKE_CURRENT_LIST_DIR}/main_sim.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/hal_sim.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/app_stubs.cpp"
    "${MAIN_DIR}/hal/utils/button/Button_Class.cpp"
    "${COMP_DIR}/M5GFX/src/lgfx/utility/lgfx_qrcode.c"
    ${ASSET_SRCS}
    ${APP_COMMON_SRCS}
    ${APP_SRCS}
)

target_include_directories(m5stopwatch_sim PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${CMAKE_CURRENT_LIST_DIR}/stubs"
    "${MAIN_DIR}"
    "${COMP_DIR}/ArduinoJson/src"
    "${COMP_DIR}/M5GFX/src"
)

target_link_libraries(m5stopwatch_sim PRIVATE
    lvgl
    smooth_ui_toolkit
    mooncake
    mooncake_log
)

target_compile_options(m5stopwatch_sim PRIVATE
    -sUSE_SDL=2
    -DLV_CONF_INCLUDE_SIMPLE
)

set_target_properties(m5stopwatch_sim PROPERTIES SUFFIX ".html")

target_link_options(m5stopwatch_sim PRIVATE
    -sUSE_SDL=2
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=67108864
    -sSTACK_SIZE=1048576
    -sEXPORTED_FUNCTIONS=['_main','_sim_set_button']
    -sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']
    "--shell-file=${CMAKE_CURRENT_LIST_DIR}/shell.html"
)
