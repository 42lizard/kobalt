find_package(Python3 REQUIRED COMPONENTS Interpreter)
# Relative to this file, so the unit test project can include it too.
get_filename_component(KOBALT_STRINGS_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${KOBALT_STRINGS_ROOT}/resources/en.strings" "${KOBALT_STRINGS_ROOT}/tools/ui_strings.py")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${KOBALT_STRINGS_ROOT}/tools/ui_strings.py"
    --output "${CMAKE_BINARY_DIR}/generated/kobalt_strings.inc"
    RESULT_VARIABLE strings_result ERROR_VARIABLE strings_error)
if(NOT strings_result EQUAL 0)
    message(FATAL_ERROR "Invalid Kobalt strings: ${strings_error}")
endif()
include_directories("${CMAKE_BINARY_DIR}/generated")
