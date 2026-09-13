#pragma once

#include <array>
#include <string>
#include <vector>

namespace meda {

enum class Orientation { Axial = 0, Sagittal = 1, Coronal = 2 };

/// The voxel-axis index a view slices along: axial cuts Z, sagittal X,
/// coronal Y. Never use static_cast<int>(Orientation) for indexing.
inline constexpr int axisOf(Orientation o)
{
    switch (o) {
    case Orientation::Axial:    return 2;
    case Orientation::Sagittal: return 0;
    case Orientation::Coronal:  return 1;
    }
    return 2;
}

enum class Tool { WindowLevel, Zoom, Pan, Measure, Crosshair, Roi, Angle,
                  Brush, Eraser };

/// Metadata describing one DICOM series discovered on disk or over the
/// network. Field names mirror the DICOM tags they come from.
struct SeriesMeta {
    std::string seriesInstanceUID;   // 0020,000E
    std::string studyInstanceUID;    // 0020,000D
    std::string sopClassUID;
    std::string seriesDescription;   // 0008,103E
    std::string studyDescription;    // 0008,1030
    std::string modality;            // 0008,0060
    std::string patientName;         // 0010,0010
    std::string patientID;           // 0010,0020
    std::string patientBirthDate;    // 0010,0030
    std::string patientSex;          // 0010,0040
    std::string studyDate;           // 0008,0020
    std::string accessionNumber;     // 0008,0050
    std::string institution;         // 0008,0080
    std::string seriesNumber;        // 0020,0011
    std::string bodyPart;            // 0018,0015
    int         rows = 0;
    int         columns = 0;
    int         instanceCount = 0;
    double      windowCenter = 40.0;
    double      windowWidth  = 400.0;
    bool        hasWindowing = false;
    bool        monochrome1  = false;
    std::vector<std::string> files;
};

/// Window/level preset for soft-tissue imaging.
struct WindowPreset {
    const char* name;
    double      width;
    double      center;
};

inline constexpr WindowPreset kWindowPresets[] = {
    {"Soft Tissue",  400.0,   50.0},
    {"Lung",        1500.0, -600.0},
    {"Bone",        2000.0,  300.0},
    {"Brain",         80.0,   40.0},
    {"Mediastinum",  350.0,   50.0},
    {"Abdomen",      400.0,   50.0},
    {"Liver",        150.0,   30.0},
    {"Full Range",    -1.0,    0.0},
};

} // namespace meda
