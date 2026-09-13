// Smoke tests for Volume math and label LUT construction.
// Not a framework — plain asserts to keep dependencies minimal.

#include "Volume.h"
#include "Segmentation.h"

#include <itkImageToVTKImageFilter.h>

#include <cassert>
#include <cmath>
#include <cstdio>

using namespace meda;

static VolumePtr makeTestVolume()
{
    auto img = ImageType::New();
    ImageType::RegionType region;
    ImageType::SizeType size;
    size[0] = 64; size[1] = 64; size[2] = 32;
    region.SetSize(size);
    ImageType::IndexType index;
    index.Fill(0);
    region.SetIndex(index);
    img->SetRegions(region);
    ImageType::SpacingType spacing;
    spacing[0] = 1.5; spacing[1] = 1.5; spacing[2] = 3.0;
    img->SetSpacing(spacing);
    img->Allocate();
    img->FillBuffer(100.0f);

    auto connector = itk::ImageToVTKImageFilter<ImageType>::New();
    connector->SetInput(img);
    connector->Update();
    auto vtkImg = vtkSmartPointer<vtkImageData>::New();
    vtkImg->DeepCopy(connector->GetOutput());
    vtkImg->SetSpacing(1.5, 1.5, 3.0);
    vtkImg->SetOrigin(0, 0, 0);

    SeriesMeta meta;
    meta.modality = "CT";
    return std::make_shared<Volume>(img, vtkImg, meta);
}

int main()
{
    auto vol = makeTestVolume();

    auto ext = vol->extent();
    assert(ext[0] == 64 && ext[1] == 64 && ext[2] == 32);

    auto sp = vol->spacing();
    assert(std::abs(sp[0] - 1.5) < 1e-6 && std::abs(sp[2] - 3.0) < 1e-6);

    // IJK <-> world round-trip.
    std::array<double,3> w{}, back{};
    vol->ijkToWorld({10, 20, 5}, w);
    assert(std::abs(w[0] - 15.0) < 1e-6 && std::abs(w[2] - 15.0) < 1e-6);
    vol->worldToIjk(w, back);
    assert(std::abs(back[0] - 10) < 1e-6 && std::abs(back[2] - 5) < 1e-6);

    // Slice clamping per orientation.
    assert(vol->clampSlice(Orientation::Axial, 999) == 31);
    assert(vol->clampSlice(Orientation::Axial, -5) == 0);
    assert(vol->sliceCount(Orientation::Sagittal) == 64);

    // Label LUT: index 0 transparent, nonzero opaque.
    auto lut = Segmentation::makeLabelLut(4);
    double rgba[4];
    lut->GetTableValue(0, rgba);
    assert(rgba[3] == 0.0);
    lut->GetTableValue(2, rgba);
    assert(rgba[3] == 1.0);

    std::puts("All tests passed.");
    return 0;
}
