#pragma once

#include "Types.h"
#include "Volume.h"
#include "Segmentation.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace meda {

/// ONNX Runtime inference over a loaded Volume. Supports 2D models
/// ([1,C,H,W], run per slice) and 3D models ([1,C,D,H,W], single shot on a
/// resized/cropped volume — caller is responsible for matching shapes).
class InferenceEngine {
public:
    InferenceEngine();
    ~InferenceEngine();

    bool loadModel(const std::string& onnxPath, std::string* err = nullptr);
    bool isLoaded() const;
    std::string inputName() const;
    std::vector<int64_t> inputShape() const;
    /// Active ORT execution provider: "CPU", "CUDA", or "DirectML".
    std::string providerName() const;

    /// Preprocessing params read from the model's pre_processing.ini
    /// (Raidionics convention). Defaults match a generic CT slab model.
    struct ModelParams {
        double spacing = 1.0;                  // iso resample spacing (mm)
        float  clipLo  = -100.f, clipHi = 300.f; // HU clip range
        std::vector<std::string> classNames;   // label names (index 0 = bg)
    };
    const ModelParams& modelParams() const;

    /// Run the model and return a per-voxel label index volume on the
    /// reference grid (argmax over classes for multi-class outputs).
    /// progress(done, total) is invoked per inference tile.
    /// Throws std::runtime_error on shape mismatches or ORT failures.
    Segmentation runSegmentation(
        const VolumePtr& ref,
        std::function<void(int, int)> progress = nullptr);

private:
    /// Read pre_processing.ini next to the .onnx (or <base>.ini) for
    /// spacing/clip/class names.
    void loadPreprocessingParams(const std::string& onnxPath);

    /// Channel-last 3D models [N,X,Y,Z,C] (Raidionics-style): resample to
    /// the model spacing, sliding-window slab inference over the whole
    /// volume, clip HU per model config, argmax back onto the ref grid.
    Segmentation runChannelLast3D(const VolumePtr& ref,
                                  const std::vector<int64_t>& shape,
                                  std::function<void(int, int)> progress);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace meda
