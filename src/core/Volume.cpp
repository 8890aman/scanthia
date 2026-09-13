#include "Volume.h"

#include <itkImageBase.h>

#include <vtkImageGaussianSmooth.h>
#include <vtkImageMathematics.h>

#include <algorithm>

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
