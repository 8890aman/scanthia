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
endif()
