add_library(meda_core STATIC
    Types.h
    Volume.h
    Volume.cpp
)

target_include_directories(meda_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(meda_core PUBLIC
    ${ITK_LIBRARIES}
    VTK::CommonCore
    VTK::CommonDataModel
    VTK::ImagingGeneral
    VTK::ImagingMath
)
