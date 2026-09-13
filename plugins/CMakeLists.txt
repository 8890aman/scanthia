# Bundled AI plugins. Each is a Qt plugin DLL loaded at runtime from
# <bindir>/plugins.

function(Scanthia_plugin name)
    add_library(${name} MODULE ${ARGN})
    target_link_libraries(${name} PRIVATE
        meda_ai meda_seg meda_core
        Qt6::Core
        ${ITK_LIBRARIES}
        VTK::IOImage
        VTK::CommonColor
    )
    set_target_properties(${name} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/plugins
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/plugins
    )
endfunction()

Scanthia_plugin(threshold_seg ThresholdSegPlugin.cpp)
