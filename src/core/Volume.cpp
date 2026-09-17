#include "Volume.h"

#include <itkImageBase.h>

#include <vtkImageGaussianSmooth.h>
#include <vtkImageMathematics.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace meda {

Volume::Volume(ImageType::Pointer itkImage,
               vtkSmartPointer<vtkImageData> vtkImage, SeriesMeta meta)
    : m_itk(std::move(itkImage)), m_vtk(std::move(vtkImage)),
      m_meta(std::move(meta)) {}

std::array<int, 3> Volume::extent() const
{
    const int* e = m_vtk->GetExtent();
    return {e[1] - e[0] + 1, e[3] - e[2] + 1, e[5] - e[4] + 1};
}

std::array<double, 3> Volume::spacing() const
{
    const double* s = m_vtk->GetSpacing();
    return {s[0], s[1], s[2]};
}

std::array<double, 2> Volume::scalarRange() const
{
    const double* r = m_vtk->GetScalarRange();
    return {r[0], r[1]};
}

std::array<double, 2> Volume::autoWindowRange() const
{
    if (m_autoRange[0] >= 0.0)
        return m_autoRange;

    const auto* buf = m_itk->GetBufferPointer();
    const size_t nVox =
        m_itk->GetLargestPossibleRegion().GetNumberOfPixels();
    if (!buf || !nVox)
        return {0.0, 1.0};

    const bool hasPad = m_meta.hasPixelPadding;
    auto isPad = [&](double v) {
        return hasPad && v >= m_meta.padLo && v <= m_meta.padHi;
    };
    // Stride-sample big volumes — a few million voxels is plenty for a
    // histogram and keeps this off the load path.
    const size_t stride = std::max<size_t>(1, nVox / 4000000);

    // Pass 1: range excluding padding / non-finite voxels.
    double lo = std::numeric_limits<double>::max(), hi = -lo;
    size_t n = 0;
    for (size_t i = 0; i < nVox; i += stride) {
        const double v = buf[i];
        if (isPad(v) || !std::isfinite(v))
            continue;
        lo = std::min(lo, v);
        hi = std::max(hi, v);
        ++n;
    }
    if (!n || hi <= lo)
        return m_autoRange = {lo < hi ? lo : 0.0, hi > lo ? hi : 1.0};

    // Pass 2: 2048-bin histogram, take 0.5% / 99.5% percentiles — trims
    // single hot voxels and air/padding residue that min/max keep.
    constexpr int BINS = 2048;
    std::vector<uint64_t> hist(BINS, 0);
    const double inv = (BINS - 1) / (hi - lo);
    for (size_t i = 0; i < nVox; i += stride) {
        const double v = buf[i];
        if (isPad(v) || !std::isfinite(v))
            continue;
        hist[int((v - lo) * inv)]++;
    }
    const uint64_t tail = uint64_t(n * 0.005);
    uint64_t acc = 0;
    int bLo = 0, bHi = BINS - 1;
    for (int b = 0; b < BINS; ++b) {
        acc += hist[b];
        if (acc > tail) { bLo = b; break; }
    }
    acc = 0;
    for (int b = BINS - 1; b >= 0; --b) {
        acc += hist[b];
        if (acc > tail) { bHi = b; break; }
    }
    const double binW = (hi - lo) / (BINS - 1);
    m_autoRange = {lo + bLo * binW, lo + bHi * binW};
    if (m_autoRange[1] <= m_autoRange[0])
        m_autoRange = {lo, hi};
    return m_autoRange;
}

// Display space is index * spacing: volumes are reoriented to identity
// direction at load and the VTK image carries origin 0.
void Volume::ijkToWorld(const std::array<double,3>& ijk,
                        std::array<double,3>& world) const
{
    const double* s = m_vtk->GetSpacing();
    world = {ijk[0] * s[0], ijk[1] * s[1], ijk[2] * s[2]};
}

void Volume::worldToIjk(const std::array<double,3>& world,
                        std::array<double,3>& ijk) const
{
    const double* s = m_vtk->GetSpacing();
    ijk = {world[0] / s[0], world[1] / s[1], world[2] / s[2]};
}

int Volume::clampSlice(Orientation o, int slice) const
{
    const int n = sliceCount(o);
    return std::clamp(slice, 0, std::max(0, n - 1));
}

int Volume::sliceCount(Orientation o) const
{
    return extent()[static_cast<size_t>(axisOf(o))];
}

vtkImageData* Volume::smoothedVtk()
{
    if (!m_smoothed) {
        // Mild separable Gaussian, ~0.8 voxel stddev — knocks down CT
        // noise without destroying edges.
        auto g = vtkSmartPointer<vtkImageGaussianSmooth>::New();
        g->SetInputData(m_vtk);
        g->SetStandardDeviations(0.8, 0.8, 0.8);
        g->Update();
        m_smoothed = vtkSmartPointer<vtkImageData>::New();
        m_smoothed->DeepCopy(g->GetOutput());
    }
    return m_smoothed;
}

vtkImageData* Volume::sharpenedVtk()
{
    if (!m_sharpened) {
        // Unsharp mask: out = orig + 1.6 * (orig - blur).
        // Boosts edges, keeps midtones — the standard "sharpen".
        auto blur = vtkSmartPointer<vtkImageGaussianSmooth>::New();
        blur->SetInputData(m_vtk);
        blur->SetStandardDeviations(1.0, 1.0, 1.0);

        auto detail = vtkSmartPointer<vtkImageMathematics>::New();
        detail->SetOperationToSubtract();
        detail->SetInput1Data(m_vtk);
        detail->SetInputConnection(1, blur->GetOutputPort());

        auto boost = vtkSmartPointer<vtkImageMathematics>::New();
        boost->SetOperationToMultiplyByK();
        boost->SetConstantK(1.6);
        boost->SetInputConnection(0, detail->GetOutputPort());

        auto add = vtkSmartPointer<vtkImageMathematics>::New();
        add->SetOperationToAdd();
        add->SetInput1Data(m_vtk);
        add->SetInputConnection(1, boost->GetOutputPort());
        add->Update();

        m_sharpened = vtkSmartPointer<vtkImageData>::New();
        m_sharpened->DeepCopy(add->GetOutput());
    }
    return m_sharpened;
}

} // namespace meda
