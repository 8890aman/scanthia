add_library(meda_io STATIC
    DicomLoader.h
    DicomLoader.cpp
)

target_include_directories(meda_io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(meda_io PUBLIC
    meda_core
    ${ITK_LIBRARIES}
    VTK::IOImage
)
