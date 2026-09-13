// End-to-end pipeline probe: scan a DICOM dir, load the first series,
// and render the middle axial slice offscreen to probe_out.png.

#include "DicomLoader.h"

#include <vtkImageSlice.h>
#include <vtkImageResliceMapper.h>
#include <vtkPlane.h>
#include <vtkImageProperty.h>
#include <vtkPNGWriter.h>
#include <vtkRenderWindow.h>
#include <vtkRenderer.h>
#include <vtkSmartPointer.h>
#include <vtkWindowToImageFilter.h>
#include <vtkCamera.h>
#include <vtkAutoInit.h>
VTK_MODULE_INIT(vtkRenderingOpenGL2)
VTK_MODULE_INIT(vtkRenderingFreeType)
VTK_MODULE_INIT(vtkInteractionStyle)

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace meda;

int main(int argc, char** argv)
{
    const char* dir = argc > 1 ? argv[1] : "tests/data/ct_series";
    const char* outPng = argc > 2 ? argv[2] : "probe_out.png";

    auto series = DicomLoader::scanDirectory(dir);
    if (series.empty()) {
        std::fprintf(stderr, "no series found in %s\n", dir);
        return 1;
    }
    const auto& s = series.front();
    std::printf("series: %s %s %s x%d\n", s.modality.c_str(),
                s.patientName.c_str(), s.seriesDescription.c_str(),
                s.instanceCount);

    auto vol = DicomLoader::loadSeries(s, [](float p) {
        std::printf("\rload %.0f%%", p * 100);
    });
    std::printf("\n");

    auto ext = vol->extent();
    auto rng = vol->scalarRange();
    std::printf("extent %dx%dx%d  range %.0f..%.0f  WW %.0f WL %.0f\n",
                ext[0], ext[1], ext[2], rng[0], rng[1],
                vol->meta().windowWidth, vol->meta().windowCenter);

    // Flip regression: exercise DicomLoader::flipVolume on a live volume.
    {
        std::printf("flip test... ");
        auto fv = DicomLoader::flipVolume(vol, 3);
        auto fe = fv->extent();
        std::printf("extent %dx%dx%d ", fe[0], fe[1], fe[2]);
        // Corner voxel (0,0,0) after a full flip should equal the
        // original (x-1, y-1, z-1) voxel.
        const double a = fv->vtkImage()->GetScalarComponentAsDouble(
            0, 0, 0, 0);
        const double b = vol->vtkImage()->GetScalarComponentAsDouble(
            ext[0] - 1, ext[1] - 1, ext[2] - 1, 0);
        std::printf("vox %.0f vs %.0f\n", a, b);
        if (std::abs(a - b) > 1e-3)
            std::fprintf(stderr, "FLIP MISMATCH\n");
    }
    if (ext[0] != 128 || ext[2] != 48) {
        std::fprintf(stderr, "unexpected extent\n");
        return 2;
    }
    // Sphere centre should be ~1200 HU, corner air ~-1000.
    const double c = vol->vtkImage()->GetScalarComponentAsDouble(
        ext[0] / 2, ext[1] / 2, ext[2] / 2, 0);
    const double corner = vol->vtkImage()->GetScalarComponentAsDouble(0, 0, 0, 0);
    std::printf("centre %.0f  corner %.0f\n", c, corner);
    if (c < 1000 || corner > -500) {
        std::fprintf(stderr, "unexpected voxel values\n");
        return 3;
    }

    // Render the middle slice of each orientation to <outPng>_{ax,cor,sag}.png
    const int orientations[3] = {2, 1, 0}; // Z=axial, Y=coronal, X=sagittal
    const char* names[3] = {"axial", "coronal", "sagittal"};
    auto sp = vol->spacing();
    for (int t = 0; t < 3; ++t) {
        auto plane = vtkSmartPointer<vtkPlane>::New();
        double nrm[3] = {0, 0, 0};
        nrm[orientations[t]] = 1.0;
        double org[3] = {0, 0, 0};
        org[orientations[t]] = (ext[orientations[t]] / 2) * sp[orientations[t]];
        plane->SetNormal(nrm);
        plane->SetOrigin(org);
        auto mapper = vtkSmartPointer<vtkImageResliceMapper>::New();
        mapper->SetInputData(vol->vtkImage());
        mapper->SetSlicePlane(plane);
        mapper->SetResampleToScreenPixels(1);
        auto actor = vtkSmartPointer<vtkImageSlice>::New();
        actor->SetMapper(mapper);
        actor->GetProperty()->SetColorWindow(400);
        actor->GetProperty()->SetColorLevel(40);

        std::fflush(stdout);
        auto ren = vtkSmartPointer<vtkRenderer>::New();
        ren->AddActor(actor);
        auto win = vtkSmartPointer<vtkRenderWindow>::New();
        win->SetSize(512, 512);
        win->AddRenderer(ren);
        ren->ResetCamera();
        win->Render();

        char name[512];
        std::snprintf(name, sizeof(name), "%.*s_%s.png",
                      (int)std::strlen(outPng) - 4, outPng, names[t]);
        auto w2i = vtkSmartPointer<vtkWindowToImageFilter>::New();
        w2i->SetInput(win);
        auto writer = vtkSmartPointer<vtkPNGWriter>::New();
        writer->SetInputConnection(w2i->GetOutputPort());
        writer->SetFileName(name);
        writer->Write();
        std::printf("wrote %s\n", name);
    }
    return 0;
}
