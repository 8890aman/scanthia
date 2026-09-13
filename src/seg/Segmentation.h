#pragma once

#include "Types.h"
#include "Volume.h"

#include <vtkImageData.h>
#include <vtkLookupTable.h>
#include <vtkSmartPointer.h>

#include <map>
#include <string>
#include <vector>

namespace meda {

/// A labelmap overlay: one scalar label index per voxel on the reference
/// volume grid, plus a color table and per-segment names.
struct Segmentation {
    vtkSmartPointer<vtkImageData>    labelmap;
    vtkSmartPointer<vtkLookupTable>  lut;
    std::map<int, std::string>       labelNames;

    static vtkSmartPointer<vtkLookupTable> makeLabelLut(int maxLabel);
};

/// Loaders for segmentation data.
class SegmentationLoader {
public:
    /// NIfTI / NRRD / MetaIO labelmap, resampled onto the reference grid
    /// with nearest-neighbor interpolation.
    static Segmentation loadLabelmap(const std::string& path,
                                     const VolumePtr& ref);

    /// DICOM SEG object. Parses PerFrameFunctionalGroups to map each frame
    /// to its segment and physical position.
    static Segmentation loadDicomSeg(const std::string& path,
                                     const VolumePtr& ref);
};

} // namespace meda
