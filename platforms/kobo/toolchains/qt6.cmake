# Use the cross toolchain supplied by the prepared Kobo Qt6 builder image.
set(KOBALT_KOBO_QT6_TOOLCHAIN "/tc/arm-kobo-linux-gnueabihf/toolchain.cmake" CACHE FILEPATH "Prepared Kobo Qt6 toolchain")
if(NOT EXISTS "${KOBALT_KOBO_QT6_TOOLCHAIN}")
    message(FATAL_ERROR "Prepared Kobo Qt6 toolchain missing. Run in the prepared Qt6 builder image; never build Qt.")
endif()
include("${KOBALT_KOBO_QT6_TOOLCHAIN}")
