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
    // Pixel padding (0028,0120 PixelPaddingValue + 0028,0121
    // PixelPaddingRangeLimit), converted to rescaled units. Voxels in
    // [padLo, padHi] are padding — excluded from auto-window/ROI stats.
    bool        hasPixelPadding = false;
    double      padLo = 0.0;
    double      padHi = 0.0;
    /// PT: multiply a rescaled pixel value by this to get SUVbw (g/mL).
    /// 0 = not computable (missing dose/weight/decay-correction tags).
    double      suvFactor = 0.0;
    std::vector<std::string> files;
};

/// Window/level preset. Modality-scoped —
/// Hounsfield windows only make sense for CT.
struct WindowPreset {
    const char* name;
    double      width;
    double      center;
    const char* modality;   // "" = any modality
    int         mode = 0;   // 0=fixed w/c  1=auto 1–99%  2=DICOM stored
                            // 3=full range
};

// Common clinical CT window presets,
// plus the modality-agnostic entries every viewer shows.
inline constexpr WindowPreset kWindowPresets[] = {
    {"Brain",         110.0,   35.0, "CT"},
    {"Abdomen",       320.0,   50.0, "CT"},
    {"Mediastinum",   400.0,   80.0, "CT"},
    {"Bone",         2000.0,  350.0, "CT"},
    {"Lung",         1500.0, -500.0, "CT"},
    {"MIP",           380.0,  120.0, "CT"},
    {"DICOM Default",   0.0,    0.0, "", 2},   // VOI WW/WL from the file
    {"Auto Level",      0.0,    0.0, "", 1},   // 1–99% percentile fit
    {"Full Range",      0.0,    0.0, "", 3},
};

} // namespace meda
