add_library(meda_seg STATIC
    Segmentation.h
    Segmentation.cpp
)

target_include_directories(meda_seg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(meda_seg PUBLIC
    meda_core
    ${ITK_LIBRARIES}
    VTK::IOImage
    VTK::CommonColor
    gdcmMSFF
    gdcmDICT
    gdcmIOD
    gdcmDSED
    gdcmCommon
)
