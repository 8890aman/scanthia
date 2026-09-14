add_library(meda_pacs STATIC
    PacsClient.h
    PacsClient.cpp
    StoreScp.h
    StoreScp.cpp
    PacsDialog.h
    PacsDialog.cpp
    DicomWebClient.h
    DicomWebClient.cpp
    DicomWebDialog.h
    DicomWebDialog.cpp
    AutoPullRules.h
    AutoPullRules.cpp
    AutoPullRuleEditor.h
    AutoPullRuleEditor.cpp
    AutoPullManager.h
    AutoPullManager.cpp
    AutoPullRunner.h
    AutoPullRunner.cpp
    TaskScheduler.h
    TaskScheduler.cpp
    BurnMediaDialog.h
    BurnMediaDialog.cpp
    DicomNodes.h
    DicomNodes.cpp
    NodesDialog.h
    NodesDialog.cpp
    Anonymizer.h
    Anonymizer.cpp
    AnonymizeDialog.h
    AnonymizeDialog.cpp
    ReceiverDock.h
    ReceiverDock.cpp
)

target_include_directories(meda_pacs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(meda_pacs PUBLIC
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::Network
    meda_db
    DCMTK::dcmnet
    DCMTK::dcmdata
    DCMTK::ofstd
    DCMTK::oflog
)
