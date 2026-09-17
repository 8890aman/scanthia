add_library(meda_io STATIC
    DicomLoader.h
    DicomLoader.cpp
)

# DCMTK for tags ITK doesn't surface (functional-group sequences).
target_include_directories(meda_io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(meda_io PUBLIC
    meda_core
    ${ITK_LIBRARIES}
    VTK::IOImage
    DCMTK::dcmdata
    DCMTK::ofstd
)
