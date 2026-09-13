add_library(meda_render STATIC
    SliceInteractionStyle.h
    SliceInteractionStyle.cpp
    SliceViewer.h
    SliceViewer.cpp
    MprWidget.h
    MprWidget.cpp
    VolumeWidget.h
    VolumeWidget.cpp
)

target_include_directories(meda_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(meda_render PUBLIC
    meda_core
    meda_seg
    Qt6::Widgets
    Qt6::OpenGLWidgets
    VTK::GUISupportQt
    VTK::RenderingCore
    VTK::RenderingOpenGL2
    VTK::RenderingImage
    VTK::RenderingAnnotation
    VTK::RenderingVolume
    VTK::RenderingVolumeOpenGL2
    VTK::InteractionStyle
    VTK::InteractionWidgets
    VTK::ImagingCore
    VTK::ImagingGeneral
    VTK::CommonColor
)
