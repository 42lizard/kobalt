file(STRINGS "${PATCH_LIST}" PATCHES)
foreach(patch IN LISTS PATCHES)
    execute_process(COMMAND "${PATCH_EXECUTABLE}" --batch --forward -p1 -i "${patch}"
        WORKING_DIRECTORY "${SOURCE}" COMMAND_ERROR_IS_FATAL ANY)
endforeach()
if(NATIVE_ENGINE)
    file(APPEND "${SOURCE}/CMakeLists.txt" "\ntarget_compile_definitions(crengine PRIVATE FT_Load_Glyph=kobalt_font_load_glyph FT_Render_Glyph=kobalt_font_render_glyph)\n")
endif()
