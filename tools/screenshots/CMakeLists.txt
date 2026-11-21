# Host renderer for the documentation screenshots: the real UI sources with the
# hardware stubbed out. Run through tools/screenshots.sh.
cmake_minimum_required(VERSION 3.16)
project(openpuck_screenshots C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

include(FetchContent)

# Same LVGL release as dependencies.lock (lvgl/lvgl 9.6.0).
set(LV_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE STRING "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG v9.6.0
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(lvgl)

# Single-file PNG writer (stb_image_write), pinned to a commit and a hash.
FetchContent_Declare(stb_write
    URL https://raw.githubusercontent.com/nothings/stb/4e9b368bfc071e97206ea5c6adba68dd0cbf8897/stb_image_write.h
    URL_HASH SHA256=cbd5f0ad7a9cf4468affb36354a1d2338034f2c12473cf1a8e32053cb6914a05
    DOWNLOAD_NO_EXTRACT TRUE)
FetchContent_MakeAvailable(stb_write)

add_subdirectory(${ROOT}/components/puck_core puck_core)

add_executable(screenshots
    render.c
    scenes.c
    stubs/stubs.c
    ${ROOT}/main/ui.c
    ${ROOT}/main/ui_counter.c
    ${ROOT}/main/ui_menu.c
    ${ROOT}/main/ui_history.c
    ${ROOT}/main/ui_settings.c
    ${ROOT}/main/ui_colors.c
    ${ROOT}/main/ui_timer_set.c
    ${ROOT}/main/ui_dial.c
    ${ROOT}/main/counter.c
    ${ROOT}/main/logo.c
    ${ROOT}/main/fonts/puck_font_count_185.c
    ${ROOT}/main/fonts/puck_font_count_80.c
    ${ROOT}/main/fonts/puck_font_dial_80.c
    ${ROOT}/main/fonts/puck_font_small_32.c
    ${ROOT}/main/fonts/puck_font_timer_26.c)
# Stubs first, so they shadow the ESP-IDF headers; then the firmware headers.
target_include_directories(screenshots BEFORE PRIVATE stubs)
target_include_directories(screenshots PRIVATE ${ROOT}/main ${stb_write_SOURCE_DIR})
target_link_libraries(screenshots PRIVATE lvgl puck_core)
target_compile_definitions(screenshots PRIVATE
    SCREENSHOT_OUT_DEFAULT="${ROOT}/docs/images")
