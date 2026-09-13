add_executable(meda_tests
    test_volume.cpp
)

target_link_libraries(meda_tests PRIVATE
    meda_core
    meda_seg
    ${ITK_LIBRARIES}
    VTK::IOImage
    VTK::CommonColor
)

add_test(NAME volume COMMAND meda_tests)

add_executable(meda_test_db test_db.cpp)
target_link_libraries(meda_test_db PRIVATE
    meda_db
    Qt6::Core
    Qt6::Gui
    Qt6::Sql
)
add_test(NAME study_db COMMAND meda_test_db
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(meda_probe probe_pipeline.cpp)
target_link_libraries(meda_probe PRIVATE
    meda_io
    VTK::RenderingImage
    VTK::RenderingCore
    VTK::RenderingOpenGL2
    VTK::RenderingFreeType
    VTK::InteractionStyle
    VTK::IOImage
)

add_executable(meda_probe_ai probe_ai.cpp)
target_link_libraries(meda_probe_ai PRIVATE
    meda_io
    meda_ai
    VTK::CommonDataModel
)
if(WIN32 AND ONNXRUNTIME_DLL)
    add_custom_command(TARGET meda_probe_ai POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ONNXRUNTIME_DLL}" $<TARGET_FILE_DIR:meda_probe_ai>)
endif()
