#pragma once

#include "Types.h"

#include <array>
#include <memory>

#include <itkImage.h>
#include <vtkImageData.h>
#include <vtkSmartPointer.h>

namespace meda {

using ImageType = itk::Image<float, 3>;

/// A loaded image volume. Keeps the ITK image (used by segmentation and AI
/// inference) and the VTK image (used by all rendering) alive together.
class Volume {
public:
    Volume(ImageType::Pointer itkImage, vtkSmartPointer<vtkImageData> vtkImage,
           SeriesMeta meta);

    vtkImageData*     vtkImage() const { return m_vtk; }
    ImageType::Pointer itkImage() const { return m_itk; }
    const SeriesMeta& meta() const { return m_meta; }

    /// True when the loaded volume was downsampled to fit a memory
    /// budget (in-plane and/or z stride). UI should warn the user.
    bool downsampled() const { return m_downsampled; }
    void setDownsampled(bool on) { m_downsampled = on; }
    /// Original (full-res) dimensions before downsampling. Equal to
    /// `extent()` when not downsampled.
    std::array<int, 3> fullExtent() const { return m_fullExtent; }
    void setFullExtent(const std::array<int, 3>& e) { m_fullExtent = e; }

    /// Voxel index bounds, e.g. {512,512,240}.
    std::array<int, 3> extent() const;
    /// Voxel spacing in mm.
    std::array<double, 3> spacing() const;
    /// Scalar range over the whole volume.
    std::array<double, 2> scalarRange() const;

    /// IJK <-> world (LPS patient) coordinates.
    void ijkToWorld(const std::array<double,3>& ijk, std::array<double,3>& world) const;
    void worldToIjk(const std::array<double,3>& world, std::array<double,3>& ijk) const;

    /// Clamp an index to the valid extent for a given orientation.
    int clampSlice(Orientation o, int slice) const;
    int sliceCount(Orientation o) const;

    /// Lazily computed lightly-smoothed copy for denoised viewing.
    /// Not used by measurements/AI — those always read the raw image.
    vtkImageData* smoothedVtk();
    /// Unsharp-masked copy: orig + 1.6*(orig - blur) — edge enhancement.
    vtkImageData* sharpenedVtk();

private:
    ImageType::Pointer              m_itk;
    vtkSmartPointer<vtkImageData>   m_vtk;
    vtkSmartPointer<vtkImageData>   m_smoothed;
    vtkSmartPointer<vtkImageData>   m_sharpened;
    SeriesMeta                      m_meta;
    bool                            m_downsampled = false;
    std::array<int, 3>              m_fullExtent{0, 0, 0};
};

using VolumePtr = std::shared_ptr<Volume>;

} // namespace meda
