# The prepared SDK owns the compiler and system libraries. Kobalt adds only
# its application dependency prefix to the SDK's cross-compilation search roots.
include("$ENV{POCKETBOOK_QT_SDK}/cmake/pocketbook.toolchain.cmake")
if(KOBALT_DEPENDENCY_PREFIX)
    list(APPEND CMAKE_FIND_ROOT_PATH "${KOBALT_DEPENDENCY_PREFIX}")
endif()
