# Cache compatibility follows the rendering dependencies, not the app release or UI.
# Bump the documentCacheChecksum format version for incompatible Kobalt parser changes.
file(GLOB cache_patches CONFIGURE_DEPENDS "${KOBALT_ROOT}/dependencies/patches/*")
set(cache_inputs "${KOBALT_ROOT}/dependencies/manifest.json"
    "${KOBALT_ROOT}/dependencies/CMakeLists.txt" ${cache_patches})
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${cache_inputs})
set(cache_hashes "")
foreach(input IN LISTS cache_inputs)
    file(SHA256 "${input}" digest)
    string(APPEND cache_hashes "${digest}")
endforeach()
string(SHA256 layout_policy "${cache_hashes}")
