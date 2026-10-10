cmake_minimum_required(VERSION 3.16)
project(reader_widgets LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
if(NOT DEFINED KOBALT_QT_MAJOR)
    message(FATAL_ERROR "Build reader-widgets through the selected Kobalt application target")
endif()
add_library(reader-widgets STATIC
    style/theme.cpp
    style/font_data.cpp
    style/fonts.cpp
    controls/buttons.cpp
    controls/icons.cpp
    controls/text.cpp
    controls/rows.cpp
    controls/options.cpp
    controls/sliders.cpp
    controls/paging.cpp
    controls/panels.cpp
    controls/paged_text.cpp
    dialogs/modals.cpp)
set_target_properties(reader-widgets PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(reader-widgets PUBLIC Qt${KOBALT_QT_MAJOR}::Widgets reader-fonts)
target_compile_options(reader-widgets PRIVATE -Wall -Wextra -Werror -fvisibility=hidden)
get_filename_component(KOBALT_ICON_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
file(GLOB READER_ICON_SVGS CONFIGURE_DEPENDS "${KOBALT_ICON_ROOT}/resources/icons/lucide/*.svg")
set(READER_ICON_GEOMETRY "${CMAKE_CURRENT_BINARY_DIR}/reader_icon_vectors.h")
add_custom_command(OUTPUT "${READER_ICON_GEOMETRY}"
    COMMAND "${Python3_EXECUTABLE}" "${KOBALT_ICON_ROOT}/tools/ui_icon_vectors.py"
        "${KOBALT_ICON_ROOT}/resources/icons/lucide" "${READER_ICON_GEOMETRY}"
    DEPENDS "${KOBALT_ICON_ROOT}/tools/ui_icon_vectors.py"
        "${KOBALT_ICON_ROOT}/resources/icons/lucide/manifest.json" ${READER_ICON_SVGS}
    VERBATIM)
target_sources(reader-widgets PRIVATE "${READER_ICON_GEOMETRY}")
target_include_directories(reader-widgets PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
