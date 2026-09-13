// AI pipeline probe: load a DICOM series, run an ONNX model, report the
// resulting labelmap stats. Usage: probe_ai <dicom_dir> <model.onnx>
#include "DicomLoader.h"
#include "InferenceEngine.h"
#include "Volume.h"

#include <cstdio>
#include <vtkPointData.h>

using namespace meda;

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: probe_ai <dicom_dir> <model.onnx>\n");
        return 1;
    }
    try {
        auto series = DicomLoader::scanDirectory(argv[1]);
        if (series.empty()) {
            std::fprintf(stderr, "no series in %s\n", argv[1]);
            return 2;
        }
        auto vol = DicomLoader::loadSeries(series.front());
        std::printf("loaded %dx%dx%d\n", vol->extent()[0], vol->extent()[1],
                    vol->extent()[2]);

        InferenceEngine eng;
        std::string err;
        if (!eng.loadModel(argv[2], &err)) {
            std::fprintf(stderr, "model load failed: %s\n", err.c_str());
            return 3;
        }
        std::printf("model input shape:");
        for (auto d : eng.inputShape())
            std::printf(" %lld", (long long)d);
        std::printf("\n");

        auto seg = eng.runSegmentation(vol);
        auto* arr = seg.labelmap->GetPointData()->GetScalars();
        std::printf("labelmap: %lld voxels, range [%.0f, %.0f], %zu labels\n",
                    arr->GetNumberOfTuples(), arr->GetRange()[0],
                    arr->GetRange()[1], seg.labelNames.size());
        // Where do the labels sit vs the image?
        double csum[3] = {0, 0, 0};
        long n = 0;
        int ijk[3];
        for (ijk[2] = 0; ijk[2] < vol->extent()[2]; ++ijk[2])
            for (ijk[1] = 0; ijk[1] < vol->extent()[1]; ++ijk[1])
                for (ijk[0] = 0; ijk[0] < vol->extent()[0]; ++ijk[0])
                    if (seg.labelmap->GetScalarComponentAsDouble(
                            ijk[0], ijk[1], ijk[2], 0) > 0) {
                        csum[0] += ijk[0]; csum[1] += ijk[1];
                        csum[2] += ijk[2]; ++n;
                    }
        const auto e = vol->extent();
        std::printf("label centroid ijk: %.0f %.0f %.0f (n=%ld) vs image "
                    "center %.0f %.0f %.0f\n",
                    n ? csum[0]/n : 0, n ? csum[1]/n : 0,
                    n ? csum[2]/n : 0, n,
                    e[0]/2.0, e[1]/2.0, e[2]/2.0);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAILED: %s\n", e.what());
        return 4;
    }
}
