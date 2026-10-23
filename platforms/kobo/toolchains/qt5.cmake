# Use the cross toolchain supplied by the prepared Kobo Qt5 builder image.
set(KOBALT_KOBO_QT5_TOOLCHAIN "/tc/qt5/toolchain.cmake" CACHE FILEPATH "Prepared Kobo Qt5 toolchain")
if(NOT EXISTS "${KOBALT_KOBO_QT5_TOOLCHAIN}")
    message(FATAL_ERROR "Prepared Kobo Qt5 toolchain missing. Run in the prepared Qt5 builder image; never build Qt.")
endif()
include("${KOBALT_KOBO_QT5_TOOLCHAIN}")
