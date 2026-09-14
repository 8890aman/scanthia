cmake_minimum_required(VERSION 3.24)

project(Scanthia
    VERSION 0.1.0
    DESCRIPTION "Open-source medical imaging viewer"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

option(Scanthia_BUILD_TESTS "Build unit tests" ON)
option(Scanthia_BUILD_PLUGINS "Build bundled AI plugins" ON)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent OpenGLWidgets Sql Svg Network PrintSupport)

find_package(VTK REQUIRED COMPONENTS
    CommonCore
    CommonDataModel
    CommonColor
    RenderingCore
    RenderingOpenGL2
    RenderingImage
    RenderingAnnotation
    RenderingVolume
    RenderingVolumeOpenGL2
    InteractionStyle
    InteractionWidgets
    ImagingCore
    ImagingGeneral
    ImagingStatistics
    IOImage
    GUISupportQt
    FiltersCore
    FiltersGeneral
)

find_package(ITK REQUIRED COMPONENTS
    ITKCommon
    ITKIOImageBase
    ITKIOGDCM
    ITKIOMeta
    ITKIONIFTI
    ITKIONRRD
    ITKImageIntensity
    ITKVTK
)
include(${ITK_USE_FILE})

find_package(DCMTK REQUIRED)
find_package(GDCM REQUIRED)

# ONNX Runtime ships a CMake config on some platforms; fall back to manual
# discovery otherwise (MSYS2 ucrt64 package does not provide one).
find_package(onnxruntime CONFIG QUIET)
if(NOT onnxruntime_FOUND)
    find_library(ONNXRUNTIME_LIBRARY NAMES onnxruntime REQUIRED)
    find_path(ONNXRUNTIME_INCLUDE_DIR NAMES onnxruntime_cxx_api.h REQUIRED)
    add_library(onnxruntime::onnxruntime SHARED IMPORTED)
    set_target_properties(onnxruntime::onnxruntime PROPERTIES
        IMPORTED_LOCATION "${ONNXRUNTIME_LIBRARY}"
        IMPORTED_IMPLIB "${ONNXRUNTIME_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_INCLUDE_DIR}"
    )
endif()

if(WIN32)
    # Locate the real onnxruntime.dll once: Windows searches the app dir
    # before System32, where a stale inbox dll may shadow ours.
    # Prefer the vendored DirectML build so dev runs use the GPU too.
    find_file(ONNXRUNTIME_DLL NAMES onnxruntime.dll
              PATHS "${CMAKE_SOURCE_DIR}/thirdparty/onnxruntime-gpu"
              NO_DEFAULT_PATH)
    if(NOT ONNXRUNTIME_DLL)
        find_file(ONNXRUNTIME_DLL NAMES onnxruntime.dll
                  PATHS ${CMAKE_PREFIX_PATH} ENV PATH
                  PATH_SUFFIXES bin)
    endif()
endif()

# ---------------------------------------------------------------------------
# Project libraries
# ---------------------------------------------------------------------------

add_subdirectory(src/core)
add_subdirectory(src/io)
add_subdirectory(src/db)
add_subdirectory(src/render)
add_subdirectory(src/pacs)
add_subdirectory(src/seg)
add_subdirectory(src/ai)
add_subdirectory(src/app)

if(Scanthia_BUILD_PLUGINS)
    add_subdirectory(plugins)
endif()

if(Scanthia_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
