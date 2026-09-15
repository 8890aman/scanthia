add_executable(Scanthia WIN32
    main.cpp
    MainWindow.h
    MainWindow.cpp
    icons.qrc
    Scanthia.rc
)

target_link_libraries(Scanthia PRIVATE
    meda_core
    meda_io
    meda_db
    meda_render
    meda_pacs
    meda_seg
    meda_ai
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::Network
    Qt6::OpenGLWidgets
    Qt6::Svg
    Qt6::PrintSupport
    VTK::GUISupportQt
    VTK::RenderingOpenGL2
    VTK::RenderingFreeType
    VTK::RenderingVolumeOpenGL2
    VTK::InteractionStyle
)

if(WIN32 AND ONNXRUNTIME_DLL)
    # Copy onnxruntime next to the exe so the System32 shadow is bypassed.
    add_custom_command(TARGET Scanthia POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ONNXRUNTIME_DLL}" $<TARGET_FILE_DIR:Scanthia>)
    # DirectML builds need DirectML.dll + providers_shared alongside.
    get_filename_component(_ort_dir "${ONNXRUNTIME_DLL}" DIRECTORY)
    foreach(_dep DirectML.dll onnxruntime_providers_shared.dll)
        if(EXISTS "${_ort_dir}/${_dep}")
            add_custom_command(TARGET Scanthia POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_ort_dir}/${_dep}" $<TARGET_FILE_DIR:Scanthia>)
        endif()
    endforeach()
endif()

# Bundle the DCMTK storescp.cfg beside the exe — DcmStorageSCP needs it
# to configure presentation contexts (SOP classes + transfer syntaxes).
add_custom_command(TARGET Scanthia POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/resources/storescp.cfg"
            $<TARGET_FILE_DIR:Scanthia>/storescp.cfg)
