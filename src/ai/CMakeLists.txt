add_library(meda_ai STATIC
    InferenceEngine.h
    InferenceEngine.cpp
    AiPlugin.h
    PluginManager.h
    PluginManager.cpp
)

target_include_directories(meda_ai PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(meda_ai PUBLIC
    meda_core
    meda_seg
    Qt6::Core
    onnxruntime::onnxruntime
    ${ITK_LIBRARIES}
    VTK::IOImage
)
