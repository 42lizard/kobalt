cmake_minimum_required(VERSION 3.22)
project(KobaltDesktop LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network Sensors)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(KOBALT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(SRC "${KOBALT_ROOT}/src")
set(KOBALT_QT_MAJOR 6)
include("${KOBALT_ROOT}/cmake/Strings.cmake")
add_compile_options("-iquote${SRC}" -Wall -Wextra)
if(APPLE)
    add_compile_definitions(fdatasync=fsync)
endif()
set(KOBALT_HOST_SOURCES
    "${SRC}/platform/desktop/window.cpp" "${SRC}/platform/desktop/services.cpp" "${SRC}/platform/desktop/display.cpp")
include("${KOBALT_ROOT}/cmake/StandaloneReader.cmake")
add_executable(Kobalt MACOSX_BUNDLE "${SRC}/platform/desktop/main.cpp")
target_link_libraries(Kobalt PRIVATE standalone-runtime)
set_target_properties(Kobalt PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    MACOSX_BUNDLE_BUNDLE_NAME Kobalt MACOSX_BUNDLE_GUI_IDENTIFIER com.nicoverbruggen.kobalt.desktop
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${KOBALT_VERSION}")
file(GLOB notices "${KOBALT_ROOT}/resources/notices/*")
qt_add_resources(Kobalt notices PREFIX "/kobalt" BASE "${KOBALT_ROOT}/resources"
    FILES "${KOBALT_ROOT}/resources/acknowledgments.json" ${notices})

# Use the same SVG artwork as Android. The renderer's SVG library supplies
# the icon rasterizer, so building an icon needs no separate image toolchain.
add_executable(kobalt-icon icon.cpp)
target_include_directories(kobalt-icon PRIVATE "${KOBALT_TEST_DEPENDENCIES}/include")
target_link_libraries(kobalt-icon PRIVATE Qt6::Gui "${KOBALT_TEST_DEPENDENCIES}/lib/liblunasvg.a")

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(desktop-test "${KOBALT_ROOT}/tests/desktop/window_test.cpp")
    target_compile_options(desktop-test PRIVATE "-iquote${KOBALT_ROOT}/tests/support")
    target_link_libraries(desktop-test PRIVATE standalone-runtime Qt6::Test)
    target_compile_definitions(desktop-test PRIVATE KOBALT_TEST_ASSET_ROOT="${KOBALT_DESKTOP_ASSETS}"
        KOBALT_FIXTURES="${KOBALT_DESKTOP_FIXTURES}")
    add_test(NAME desktop-window COMMAND desktop-test)
    set_tests_properties(desktop-window PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=minimal:enable_fonts:freetype")
endif()
