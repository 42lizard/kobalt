# Shared by desktop and PocketBook hosts. The renderer and Widgets remain identical.
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
kobalt_publication_library(standalone-publication)
add_library(reader-fonts STATIC "${SRC}/assets/assets.cpp" "${SRC}/assets/bundled_fonts.cpp"
    "${SRC}/assets/system_fonts.cpp")
target_link_libraries(reader-fonts PUBLIC Qt6::Gui PRIVATE z)
set(KOBALT_DEPENDENCY_PREFIX "${KOBALT_TEST_DEPENDENCIES}")
add_subdirectory("${SRC}/interfaces/reader/ui" reader-widgets)
include("${KOBALT_ROOT}/cmake/EngineSources.cmake")
add_library(standalone-model STATIC ${KOBALT_ENGINE_SOURCES})
target_compile_definitions(standalone-model PRIVATE KOBALT_VERSION="${KOBALT_VERSION}")
target_include_directories(standalone-model PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(standalone-model SYSTEM PRIVATE "${CRENGINE_SOURCE}/crengine/include" "${CRENGINE_BUILD}"
    "${KOBALT_TEST_DEPENDENCIES}/include" "${KOBALT_TEST_DEPENDENCIES}/include/harfbuzz"
    "${KOBALT_TEST_DEPENDENCIES}/include/freetype2" "${KOBALT_TEST_PNG_INCLUDE}")
target_link_libraries(standalone-model PUBLIC standalone-publication reader-fonts Qt6::Gui "${CRENGINE_BUILD}/libcrengine.a"
    "${KOBALT_TEST_DEPENDENCIES}/lib/libharfbuzz.a" "${KOBALT_TEST_DEPENDENCIES}/lib/libxxhash.a"
    "${KOBALT_TEST_DEPENDENCIES}/lib/libutf8proc.a" "${KOBALT_TEST_DEPENDENCIES}/lib/libjpeg.a"
    "${KOBALT_TEST_DEPENDENCIES}/lib/liblunasvg.a" "${KOBALT_TEST_DEPENDENCIES}/lib/libfreetype.a"
    "${KOBALT_TEST_DEPENDENCIES}/lib/libfribidi.a" "${KOBALT_TEST_DEPENDENCIES}/lib/libunibreak.a"
    "${KOBALT_TEST_PNG}" z)
kobalt_link_private_freetype(standalone-model "${KOBALT_TEST_DEPENDENCIES}")
kobalt_reading_library(standalone-reading standalone-model standalone-publication "native-public-v1")
include("${SRC}/interfaces/reader/Sources.cmake")
# The Android controller, start screen and canvas are Qt-only. Its JNI
# services and activity lifecycle are replaced by the desktop host.
add_library(standalone-runtime STATIC
    "${SRC}/platform/android/reader.cpp" "${SRC}/platform/android/start_screen.cpp"
    ${KOBALT_HOST_SOURCES}
    "${SRC}/platform/api/reader_orientation.cpp" "${SRC}/support/diagnostics.cpp"
    "${SRC}/publication/native_resources.cpp" "${SRC}/publication/resource_server.cpp" "${SRC}/publication/plain_archive.cpp"
    ${KOBALT_READER_INTERFACE_SOURCES})
target_include_directories(standalone-runtime PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(standalone-runtime SYSTEM PRIVATE "${KOBALT_TEST_DEPENDENCIES}/include")
qt_add_resources(standalone-runtime desktop_artwork PREFIX "/desktop" BASE "${KOBALT_ROOT}/docs/assets"
    FILES "${KOBALT_ROOT}/docs/assets/kobalt-android.svg")
target_compile_definitions(standalone-runtime PUBLIC KOBALT_READER_HARDWARE KOBALT_VERSION="${KOBALT_VERSION}")
target_link_libraries(standalone-runtime PUBLIC reader-widgets standalone-reading Qt6::Network)
if(KOBALT_NO_SENSORS)
    target_compile_definitions(standalone-runtime PUBLIC KOBALT_NO_SENSORS)
else()
    target_link_libraries(standalone-runtime PUBLIC Qt6::Sensors)
endif()
