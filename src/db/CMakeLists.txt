add_library(meda_db STATIC
    StudyDatabase.h
    StudyDatabase.cpp
    Thumbnailer.h
    Thumbnailer.cpp
)

target_include_directories(meda_db PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(meda_db PUBLIC
    meda_core
    meda_io
    Qt6::Core
    Qt6::Gui
    Qt6::Sql
    ${ITK_LIBRARIES}
)
