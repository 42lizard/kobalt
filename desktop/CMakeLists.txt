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
file(READ "${CRENGINE_SOURCE}/cr3gui/data/html5.css" READER_HTML_CSS)
configure_file("${SRC}/engine/crengine/reader_styles.h.in" reader_styles.h @ONLY)
file(GLOB READER_HYPHENATION_PATTERNS RELATIVE "${CRENGINE_SOURCE}/cr3gui/data/hyph" "${CRENGINE_SOURCE}/cr3gui/data/hyph/*.pattern")
list(SORT READER_HYPHENATION_PATTERNS)
list(TRANSFORM READER_HYPHENATION_PATTERNS PREPEND "\"")
list(TRANSFORM READER_HYPHENATION_PATTERNS APPEND "\"")
list(JOIN READER_HYPHENATION_PATTERNS ", " READER_HYPHENATION_PATTERNS)
configure_file("${SRC}/engine/crengine/reader_hyphenation.h.in" reader_hyphenation.h @ONLY)
configure_file("${SRC}/support/build_info.h.in" build_info.h @ONLY)
include("${KOBALT_ROOT}/cmake/ReaderComponents.cmake")
kobalt_publication_library(desktop-publication)
add_library(reader-fonts STATIC "${SRC}/assets/assets.cpp" "${SRC}/assets/bundled_fonts.cpp")
target_link_libraries(reader-fonts PUBLIC Qt6::Gui)
set(KOBALT_DEPENDENCY_PREFIX "${KOBALT_TEST_DEPENDENCIES}")
add_subdirectory("${SRC}/interfaces/reader/ui" reader-widgets)
include("${KOBALT_ROOT}/cmake/EngineSources.cmake")
add_library(desktop-model STATIC ${KOBALT_ENGINE_SOURCES})
target_include_directories(desktop-model PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(desktop-model SYSTEM PRIVATE "${CRENGINE_SOURCE}/crengine/include" "${CRENGINE_BUILD}"
    "${KOBALT_TEST_DEPENDENCIES}/include" "${KOBALT_TEST_DEPENDENCIES}/include/harfbuzz"
    "${KOBALT_TEST_DEPENDENCIES}/include/freetype2" "${KOBALT_TEST_PNG_INCLUDE}")
target_link_libraries(desktop-model PUBLIC desktop-publication reader-fonts Qt6::Gui "${CRENGINE_BUILD}/libcrengine.a"
    "${KOBALT_TEST_DEPENDENCIES}/lib/libharfbuzz.a" "${KOBALT_TEST_DEPENDENCIES}/lib/libxxhash.a"
    "${KOBALT_TEST_DEPENDENCIES}/lib/libutf8proc.a" "${KOBALT_TEST_DEPENDENCIES}/lib/libjpeg.a"
    "${KOBALT_TEST_DEPENDENCIES}/lib/liblunasvg.a" "${KOBALT_TEST_DEPENDENCIES}/lib/libfreetype.a"
    "${KOBALT_TEST_DEPENDENCIES}/lib/libfribidi.a" "${KOBALT_TEST_DEPENDENCIES}/lib/libunibreak.a"
    "${KOBALT_TEST_PNG}" z)
kobalt_link_private_freetype(desktop-model "${KOBALT_TEST_DEPENDENCIES}")
kobalt_reading_library(desktop-reading desktop-model desktop-publication "native-public-v1")
include("${SRC}/interfaces/reader/Sources.cmake")
# The Android controller, start screen and canvas are Qt-only. Its JNI
# services and activity lifecycle are replaced by the desktop host.
add_library(desktop-runtime STATIC
    "${SRC}/platform/android/reader.cpp" "${SRC}/platform/android/start_screen.cpp"
    "${SRC}/platform/desktop/window.cpp" "${SRC}/platform/desktop/services.cpp" "${SRC}/platform/desktop/display.cpp"
    "${SRC}/platform/api/reader_orientation.cpp" "${SRC}/support/diagnostics.cpp"
    "${SRC}/publication/native_resources.cpp" "${SRC}/publication/resource_server.cpp" "${SRC}/publication/plain_archive.cpp"
    ${KOBALT_READER_INTERFACE_SOURCES})
target_include_directories(desktop-runtime PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(desktop-runtime SYSTEM PRIVATE "${KOBALT_TEST_DEPENDENCIES}/include")
qt_add_resources(desktop-runtime desktop_artwork PREFIX "/desktop" BASE "${KOBALT_ROOT}/docs/assets"
    FILES "${KOBALT_ROOT}/docs/assets/kobalt-android.svg")
target_compile_definitions(desktop-runtime PUBLIC KOBALT_READER_HARDWARE KOBALT_VERSION="${KOBALT_VERSION}")
target_link_libraries(desktop-runtime PUBLIC reader-widgets desktop-reading Qt6::Sensors Qt6::Network)
add_executable(Kobalt MACOSX_BUNDLE "${SRC}/platform/desktop/main.cpp")
target_link_libraries(Kobalt PRIVATE desktop-runtime)
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
    target_link_libraries(desktop-test PRIVATE desktop-runtime Qt6::Test)
    target_compile_definitions(desktop-test PRIVATE KOBALT_TEST_ASSET_ROOT="${KOBALT_DESKTOP_ASSETS}"
        KOBALT_FIXTURES="${KOBALT_DESKTOP_FIXTURES}")
    add_test(NAME desktop-window COMMAND desktop-test)
    set_tests_properties(desktop-window PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=minimal:enable_fonts:freetype")
endif()
