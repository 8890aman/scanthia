#include "InferenceEngine.h"

#include <onnxruntime_cxx_api.h>

#include <itkIdentityTransform.h>
#include <itkImageRegionIterator.h>
#include <itkImageToVTKImageFilter.h>
#include <itkResampleImageFilter.h>
#include <itkLinearInterpolateImageFunction.h>
#include <itkNearestNeighborInterpolateImageFunction.h>
#include <itkConstantPadImageFilter.h>
#include <itkRegionOfInterestImageFilter.h>
#include <itkPasteImageFilter.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#endif

namespace meda {

using LabelImage = itk::Image<unsigned char, 3>;

struct InferenceEngine::Impl {
    std::unique_ptr<Ort::Env> env;
    Ort::SessionOptions opts;
    std::unique_ptr<Ort::Session> session;
    Ort::AllocatorWithDefaultOptions alloc;
    std::string inputName;
    std::string outputName;
    std::vector<int64_t> inputShape;
    ModelParams params;
    std::string provider = "CPU";   // active execution provider
};

InferenceEngine::InferenceEngine()
{
    // API-version negotiation: the link headers may target a newer ORT
    // API than the loaded onnxruntime.dll supports (e.g. a DirectML/CUDA
    // build dropped in for GPU). Start from the runtime's own version
    // string to avoid warning spam from unsupported GetApi() calls.
    // Must run BEFORE Impl is created — Ort::SessionOptions' ctor uses
    // the global api pointer.
    const OrtApiBase* base = OrtGetApiBase();
    uint32_t want = ORT_API_VERSION;
    if (base->GetVersionString) {
        unsigned maj = 0, min = 0;
        if (std::sscanf(base->GetVersionString(), "%u.%u", &maj, &min) == 2)
            want = std::min(want, maj == 1 ? min : ORT_API_VERSION);
    }
    const OrtApi* api = nullptr;
    for (uint32_t v = want; v >= 18 && !api; --v)
        api = base->GetApi(v);
    if (api)
        Ort::detail::Global::Api(api);
    m_impl = std::make_unique<Impl>();
    m_impl->opts.SetIntraOpNumThreads(0);
    m_impl->opts.SetGraphOptimizationLevel(ORT_ENABLE_ALL);

    // GPU acceleration, opportunistic: the stock build ships the CPU
    // runtime, but if a GPU-enabled onnxruntime.dll (CUDA/DirectML) is
    // placed next to the exe, its provider-append symbols exist and we
    // hook them in. No build-time dependency on GPU headers/libs.
#ifdef _WIN32
    HMODULE ort = GetModuleHandleW(L"onnxruntime.dll");
    if (ort) {
        using AppendFn = OrtStatusPtr (*)(OrtSessionOptions*, int);
        if (auto fn = reinterpret_cast<AppendFn>(GetProcAddress(
                ort, "OrtSessionOptionsAppendExecutionProvider_CUDA"))) {
            if (OrtStatus* st = fn(m_impl->opts, 0)) {
                Ort::GetApi().ReleaseStatus(st);
            } else {
                m_impl->provider = "CUDA";
            }
        }
        if (m_impl->provider == "CPU") {
            if (auto fn = reinterpret_cast<AppendFn>(GetProcAddress(
                    ort,
                    "OrtSessionOptionsAppendExecutionProvider_DML"))) {
                if (OrtStatus* st = fn(m_impl->opts, 0)) {
                    Ort::GetApi().ReleaseStatus(st);
                } else {
                    m_impl->provider = "DirectML";
                }
            }
        }
    }
#endif
}

InferenceEngine::~InferenceEngine() = default;

std::string InferenceEngine::providerName() const
{
    return m_impl->provider;
}

bool InferenceEngine::loadModel(const std::string& onnxPath,
                                std::string* err)
{
    try {
        if (!m_impl->env)
            m_impl->env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING,
                                                     "Scanthia");
#ifdef _WIN32
        std::wstring wpath(onnxPath.begin(), onnxPath.end());
        m_impl->session = std::make_unique<Ort::Session>(
            *m_impl->env, wpath.c_str(), m_impl->opts);
#else
        m_impl->session = std::make_unique<Ort::Session>(
            *m_impl->env, onnxPath.c_str(), m_impl->opts);
#endif
        auto inName =
            m_impl->session->GetInputNameAllocated(0, m_impl->alloc);
        m_impl->inputName = inName.get();
        // Pick the output with the largest element count — for models
        // with deep-supervision heads the main output is the biggest.
        const size_t nOut = m_impl->session->GetOutputCount();
        size_t best = 0;
        int64_t bestElems = 0;
        for (size_t i = 0; i < nOut; ++i) {
            auto sh = m_impl->session->GetOutputTypeInfo(i)
                          .GetTensorTypeAndShapeInfo()
                          .GetShape();
            int64_t elems = 1;
            for (auto d : sh)
                elems *= std::max<int64_t>(d, 1);
            if (elems > bestElems) {
                bestElems = elems;
                best = i;
            }
        }
        auto outName =
            m_impl->session->GetOutputNameAllocated(best, m_impl->alloc);
        m_impl->outputName = outName.get();
        m_impl->inputShape = m_impl->session->GetInputTypeInfo(0)
                                 .GetTensorTypeAndShapeInfo()
                                 .GetShape();
        loadPreprocessingParams(onnxPath);
        return true;
    } catch (const Ort::Exception& e) {
        if (err) *err = e.what();
        return false;
    }
}

bool InferenceEngine::isLoaded() const { return m_impl->session != nullptr; }
std::string InferenceEngine::inputName() const { return m_impl->inputName; }
const InferenceEngine::ModelParams& InferenceEngine::modelParams() const
{
    return m_impl->params;
}

namespace {

/// Parse a flat key=value file (Raidionics pre_processing.ini style).
void parseIni(const std::string& path, InferenceEngine::ModelParams& p)
{
    std::ifstream f(path);
    if (!f)
        return;
    std::string line;
    while (std::getline(f, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        // trim whitespace
        auto trim = [](std::string& s) {
            while (!s.empty() && (s.back() == ' ' || s.back() == '\r'))
                s.pop_back();
            const auto i = s.find_first_not_of(' ');
            if (i != std::string::npos) s = s.substr(i);
        };
        trim(key); trim(val);
        if (key == "intensity_clipping_values") {
            std::sscanf(val.c_str(), "%f,%f", &p.clipLo, &p.clipHi);
        } else if (key == "output_spacing") {
            std::sscanf(val.c_str(), "%lf", &p.spacing);
        } else if (key == "classes") {
            std::istringstream ss(val);
            std::string tok;
            while (std::getline(ss, tok, ',')) {
                trim(tok);
                p.classNames.push_back(tok);
            }
        }
    }
}

} // namespace

void InferenceEngine::loadPreprocessingParams(const std::string& onnxPath)
{
    m_impl->params = ModelParams{};
    namespace fs = std::filesystem;
    const fs::path p(onnxPath);
    const fs::path dir = p.parent_path();
    // Try <base>.ini first (e.g. ct_lungs.ini beside ct_lungs.onnx),
    // then pre_processing.ini (Raidionics zip layout).
    for (const auto& cand : {p.stem().string() + ".ini",
                             std::string("pre_processing.ini")}) {
        const fs::path ini = dir / cand;
        if (fs::exists(ini)) {
            parseIni(ini.string(), m_impl->params);
            return;
        }
    }
}
std::vector<int64_t> InferenceEngine::inputShape() const
{
    return m_impl->inputShape;
}

namespace {

/// Min-max normalize a slice into a float tensor.
std::vector<float> normalizeSlice(const ImageType::Pointer& img, int k,
                                  int rows, int cols, float lo, float hi)
{
    std::vector<float> out(rows * cols);
    const float range = std::max(1e-6f, hi - lo);
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) {
            ImageType::IndexType idx{{c, r, k}};
            float v = img->GetPixel(idx);
            out[r * cols + c] =
                std::clamp((v - lo) / range, 0.0f, 1.0f);
        }
    return out;
}

} // namespace

namespace {

/// Resample an image to isotropic spacing for model input.
ImageType::Pointer resampleIso(const ImageType::Pointer& img, double spacing)
{
    using Filter = itk::ResampleImageFilter<ImageType, ImageType>;
    auto f = Filter::New();
    f->SetInput(img);
    f->SetTransform(itk::IdentityTransform<double, 3>::New());
    f->SetInterpolator(
        itk::LinearInterpolateImageFunction<ImageType>::New());
    const auto inSize = img->GetLargestPossibleRegion().GetSize();
    const auto inSp   = img->GetSpacing();
    const auto inOrg  = img->GetOrigin();
    const auto inDir  = img->GetDirection();
    ImageType::SizeType outSize;
    for (int i = 0; i < 3; ++i)
        outSize[i] = static_cast<ImageType::SizeValueType>(
            std::llround(inSize[i] * inSp[i] / spacing));
    const double outSp[3] = {spacing, spacing, spacing};
    f->SetOutputSpacing(outSp);
    f->SetOutputOrigin(inOrg);
    f->SetOutputDirection(inDir);
    f->SetSize(outSize);
    f->SetDefaultPixelValue(-1000.f);
    f->Update();
    ImageType::Pointer out = ImageType::New();
    out->Graft(f->GetOutput());
    return out;
}

/// Center-crop or pad an image to a target size (model slab).
ImageType::Pointer fitToSize(const ImageType::Pointer& img,
                             const ImageType::SizeType& target,
                             float padValue = -1000.f)
{
    ImageType::Pointer work = img;
    const auto size = img->GetLargestPossibleRegion().GetSize();
    // Pad first where too small.
    ImageType::SizeType loPad{}, hiPad{};
    bool needPad = false;
    for (int i = 0; i < 3; ++i) {
        if (size[i] < target[i]) {
            const auto d = target[i] - size[i];
            loPad[i] = d / 2;
            hiPad[i] = d - loPad[i];
            needPad = true;
        }
    }
    if (needPad) {
        auto pad = itk::ConstantPadImageFilter<ImageType, ImageType>::New();
        pad->SetInput(work);
        pad->SetPadLowerBound(loPad);
        pad->SetPadUpperBound(hiPad);
        pad->SetConstant(padValue);
        pad->Update();
        // Graft: standalone copy without pipeline linkage (Disconnect-
        // Pipeline crashes under this MinGW ITK build).
        ImageType::Pointer g = ImageType::New();
        g->Graft(pad->GetOutput());
        // Pad output has a non-zero start index (lower-bound padding
        // shifts it) — rebase to 0 so 0-based GetPixel stays in-bounds.
        auto psize = g->GetLargestPossibleRegion().GetSize();
        g->SetRegions(ImageType::RegionType{
            ImageType::IndexType::Filled(0), psize});
        work = g;
    }
    // Center-crop where too big.
    const auto cur = work->GetLargestPossibleRegion().GetSize();
    if (cur == target)
        return work;
    auto roi = itk::RegionOfInterestImageFilter<ImageType, ImageType>::New();
    ImageType::IndexType start;
    for (int i = 0; i < 3; ++i)
        start[i] = (cur[i] - target[i]) / 2;
    ImageType::RegionType region{start, target};
    roi->SetInput(work);
    roi->SetRegionOfInterest(region);
    roi->Update();
    ImageType::Pointer g = ImageType::New();
    g->Graft(roi->GetOutput());
    // ROI output keeps a non-zero start index — rebase to 0 so plain
    // 0-based GetPixel/iterators stay in-bounds.
    g->SetRegions(ImageType::RegionType{ImageType::IndexType::Filled(0),
                                        target});
    return g;
}

} // namespace

Segmentation InferenceEngine::runSegmentation(
    const VolumePtr& ref, std::function<void(int, int)> progress)
{
    if (!m_impl->session)
        throw std::runtime_error("No model loaded");
    auto shape = m_impl->inputShape;
    if (shape.size() != 4 && shape.size() != 5)
        throw std::runtime_error("Only 2D (NCHW) and 3D models "
                                 "are supported");

    // Channel-last 3D models (e.g. Raidionics ONNX exports):
    // input [N, X, Y, Z, 1] with a small last dim and spatial dims > 4.
    if (shape.size() == 5 && shape[4] > 0 && shape[4] <= 4 &&
        shape[1] > 4) {
        return runChannelLast3D(ref, shape, std::move(progress));
    }

    auto ext = ref->extent();
    LabelImage::Pointer label = LabelImage::New();
    label->SetRegions(ref->itkImage()->GetLargestPossibleRegion());
    label->SetSpacing(ref->itkImage()->GetSpacing());
    label->SetOrigin(ref->itkImage()->GetOrigin());
    label->SetDirection(ref->itkImage()->GetDirection());
    label->Allocate();
    label->FillBuffer(0);

    auto range = ref->scalarRange();

    Ort::MemoryInfo memInfo =
        Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    const char* inNames[] = {m_impl->inputName.c_str()};
    const char* outNames[] = {m_impl->outputName.c_str()};

    int maxLabel = 0;

    if (shape.size() == 4) {
        // 2D model: input [1,1,H,W], run per axial slice.
        // Non-positive dims are dynamic — use the actual slice size.
        const int64_t H = shape[2] > 0 ? shape[2] : ext[1];
        const int64_t W = shape[3] > 0 ? shape[3] : ext[0];
        if (H != ext[1] || W != ext[0])
            throw std::runtime_error(
                "Model input " + std::to_string(W) + "x" +
                std::to_string(H) + " does not match volume slice " +
                std::to_string(ext[0]) + "x" + std::to_string(ext[1]) +
                " (resize not yet implemented)");
        std::vector<int64_t> dims{1, 1, H, W};
        for (int k = 0; k < ext[2]; ++k) {
            auto data = normalizeSlice(ref->itkImage(), k, ext[1], ext[0],
                                       (float)range[0], (float)range[1]);
            auto inTensor = Ort::Value::CreateTensor<float>(
                memInfo, data.data(), data.size(), dims.data(), 4);
            auto outs = m_impl->session->Run(
                Ort::RunOptions{nullptr}, inNames, &inTensor, 1, outNames, 1);
            float* p = outs[0].GetTensorMutableData<float>();
            auto oshape =
                outs[0].GetTensorTypeAndShapeInfo().GetShape();
            const int64_t nClass = oshape.size() > 3 ? oshape[1] : 1;
            for (int r = 0; r < ext[1]; ++r)
                for (int c = 0; c < ext[0]; ++c) {
                    const size_t base = (size_t)r * ext[0] + c;
                    int best = 0;
                    float bestV = p[base];
                    for (int64_t cls = 1; cls < nClass; ++cls) {
                        float v = p[cls * ext[0] * ext[1] + base];
                        if (v > bestV) { bestV = v; best = (int)cls; }
                    }
                    if (nClass == 1)
                        best = bestV > 0.5f ? 1 : 0;
                    ImageType::IndexType idx{{c, r, k}};
                    label->SetPixel(idx, (unsigned char)best);
                    maxLabel = std::max(maxLabel, best);
                }
        }
    } else {
        // 3D model: [1,1,D,H,W] matching the full volume.
        const int64_t D = shape[2] > 0 ? shape[2] : ext[2];
        const int64_t H = shape[3] > 0 ? shape[3] : ext[1];
        const int64_t W = shape[4] > 0 ? shape[4] : ext[0];
        if (D != ext[2] || H != ext[1] || W != ext[0])
            throw std::runtime_error(
                "3D model input does not match volume dimensions");
        const size_t n = (size_t)D * H * W;
        std::vector<float> data(n);
        const float lo = (float)range[0];
        const float rng = std::max(1e-6f, (float)range[1] - lo);
        size_t i = 0;
        for (int k = 0; k < D; ++k)
            for (int r = 0; r < H; ++r)
                for (int c = 0; c < W; ++c, ++i) {
                    ImageType::IndexType idx{{c, r, k}};
                    data[i] = std::clamp(
                        (ref->itkImage()->GetPixel(idx) - lo) / rng,
                        0.0f, 1.0f);
                }
        std::vector<int64_t> dims{1, 1, D, H, W};
        auto inTensor = Ort::Value::CreateTensor<float>(
            memInfo, data.data(), data.size(), dims.data(), 5);
        auto outs = m_impl->session->Run(
            Ort::RunOptions{nullptr}, inNames, &inTensor, 1, outNames, 1);
        float* p = outs[0].GetTensorMutableData<float>();
        auto oshape = outs[0].GetTensorTypeAndShapeInfo().GetShape();
        const int64_t nClass = oshape.size() > 4 ? oshape[1] : 1;
        for (size_t v = 0; v < n; ++v) {
            int best = 0;
            float bestV = p[v];
            for (int64_t cls = 1; cls < nClass; ++cls) {
                float val = p[cls * n + v];
                if (val > bestV) { bestV = val; best = (int)cls; }
            }
            if (nClass == 1)
                best = bestV > 0.5f ? 1 : 0;
            const int k = (int)(v / ((size_t)W * H));
            const int rem = (int)(v % ((size_t)W * H));
            const int r = rem / (int)W;
            const int c = rem % (int)W;
            ImageType::IndexType idx{{c, r, k}};
            label->SetPixel(idx, (unsigned char)best);
            maxLabel = std::max(maxLabel, best);
        }
    }

    auto connector = itk::ImageToVTKImageFilter<LabelImage>::New();
    connector->SetInput(label);
    connector->Update();
    auto vtkLabel = vtkSmartPointer<vtkImageData>::New();
    vtkLabel->DeepCopy(connector->GetOutput());
    // Display space is index*spacing (see Volume::ijkToWorld) — drop
    // the patient-space origin the ITK image carried.
    vtkLabel->SetOrigin(0, 0, 0);

    Segmentation seg;
    seg.labelmap = vtkLabel;
    seg.lut = Segmentation::makeLabelLut(std::max(1, maxLabel));
    for (int i = 1; i <= maxLabel; ++i)
        seg.labelNames[i] = "Class " + std::to_string(i);
    return seg;
}

Segmentation InferenceEngine::runChannelLast3D(
    const VolumePtr& ref, const std::vector<int64_t>& shape,
    std::function<void(int, int)> progress)
{
    const int64_t MX = shape[1], MY = shape[2], MZ = shape[3];
    const auto& pp = m_impl->params;

    // Preprocessing per the model's pre_processing.ini: iso resample,
    // then sliding-window slabs over the whole resampled volume.
    auto resampled = resampleIso(ref->itkImage(), pp.spacing);
    const auto resSize =
        resampled->GetLargestPossibleRegion().GetSize();

    ImageType::SizeType target{static_cast<size_t>(MX),
                               static_cast<size_t>(MY),
                               static_cast<size_t>(MZ)};

    // Tile start offsets per axis — non-overlapping, last tile clamped
    // back so the grid exactly covers the volume.
    std::vector<long> starts[3];
    for (int i = 0; i < 3; ++i) {
        const long rs = long(resSize[i]), ts = long(target[i]);
        if (rs <= ts) {
            starts[i].push_back(0);
        } else {
            for (long s = 0; s < rs; s += ts) {
                const long st = std::min(s, rs - ts);
                if (!starts[i].empty() && starts[i].back() == st)
                    break;
                starts[i].push_back(st);
                if (st + ts >= rs)
                    break;
            }
        }
    }
    const int totalTiles = int(starts[0].size() * starts[1].size() *
                               starts[2].size());

    // Centered padding offset per axis when the volume is smaller than
    // the slab: local index in the padded tile maps to
    // global = start + local - padOff.
    long padOff[3] = {0, 0, 0};
    for (int i = 0; i < 3; ++i)
        if (resSize[i] < target[i])
            padOff[i] = long((target[i] - resSize[i]) / 2);

    // Label volume on the resampled grid — results accumulate here.
    auto labRes = LabelImage::New();
    labRes->SetRegions(ImageType::RegionType{
        ImageType::IndexType{{0, 0, 0}}, resSize});
    labRes->SetSpacing(resampled->GetSpacing());
    labRes->SetOrigin(resampled->GetOrigin());
    labRes->SetDirection(resampled->GetDirection());
    labRes->Allocate();
    labRes->FillBuffer(0);

    const float lo  = pp.clipLo;
    const float rng = std::max(1e-6f, pp.clipHi - pp.clipLo);
    const size_t n = static_cast<size_t>(MX) * MY * MZ;

    Ort::MemoryInfo memInfo =
        Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::vector<int64_t> dims{1, MX, MY, MZ, 1};
    const char* inNames[]  = {m_impl->inputName.c_str()};
    const char* outNames[] = {m_impl->outputName.c_str()};

    int maxLabel = 0;
    int tileNum = 0;
    for (long sx : starts[0])
    for (long sy : starts[1])
    for (long sz : starts[2]) {
        // Extract the tile (crop + pad to model slab if needed).
        ImageType::IndexType start{{sx, sy, sz}};
        ImageType::SizeType rsz;
        for (int i = 0; i < 3; ++i)
            rsz[i] = std::min<size_t>(target[i],
                                      resSize[i] - size_t(start[i]));
        ImageType::Pointer tile;
        if (rsz == target) {
            auto roi = itk::RegionOfInterestImageFilter<ImageType,
                                                        ImageType>::New();
            roi->SetInput(resampled);
            roi->SetRegionOfInterest(ImageType::RegionType{start, rsz});
            roi->Update();
            tile = ImageType::New();
            tile->Graft(roi->GetOutput());
            tile->SetRegions(ImageType::RegionType{
                ImageType::IndexType::Filled(0), target});
        } else {
            auto roi = itk::RegionOfInterestImageFilter<ImageType,
                                                        ImageType>::New();
            roi->SetInput(resampled);
            roi->SetRegionOfInterest(ImageType::RegionType{start, rsz});
            roi->Update();
            ImageType::Pointer g = ImageType::New();
            g->Graft(roi->GetOutput());
            g->SetRegions(ImageType::RegionType{
                ImageType::IndexType::Filled(0), rsz});
            tile = fitToSize(g, target, pp.clipLo);
        }

        // Clip to the model's HU window -> [0,1], fill channel-last.
        std::vector<float> data(n);
        size_t t = 0;
        for (int64_t x = 0; x < MX; ++x)
            for (int64_t y = 0; y < MY; ++y)
                for (int64_t z = 0; z < MZ; ++z, ++t) {
                    const float v =
                        tile->GetPixel(ImageType::IndexType{{x, y, z}});
                    data[t] = std::clamp((v - lo) / rng, 0.f, 1.f);
                }

        auto inTensor = Ort::Value::CreateTensor<float>(
            memInfo, data.data(), data.size(), dims.data(), 5);
        auto outs = m_impl->session->Run(
            Ort::RunOptions{nullptr}, inNames, &inTensor, 1,
            outNames, 1);
        const float* p = outs[0].GetTensorData<float>();
        const auto oshape =
            outs[0].GetTensorTypeAndShapeInfo().GetShape();
        const int64_t nClass = oshape.back();

        // Argmax → write into labRes at the tile offset (only voxels
        // that fall inside the resampled volume — padding is dropped).
        t = 0;
        for (int64_t x = 0; x < MX; ++x)
            for (int64_t y = 0; y < MY; ++y)
                for (int64_t z = 0; z < MZ; ++z, ++t) {
                    int best = 0;
                    float bestV = p[t * nClass];
                    for (int64_t c = 1; c < nClass; ++c)
                        if (p[t * nClass + c] > bestV) {
                            bestV = p[t * nClass + c];
                            best = int(c);
                        }
                    if (best != 0) {
                        const long gi[3] = {sx + x - padOff[0],
                                            sy + y - padOff[1],
                                            sz + z - padOff[2]};
                        if (gi[0] >= 0 && gi[0] < long(resSize[0]) &&
                            gi[1] >= 0 && gi[1] < long(resSize[1]) &&
                            gi[2] >= 0 && gi[2] < long(resSize[2]))
                            labRes->SetPixel(
                                LabelImage::IndexType{{gi[0], gi[1],
                                                       gi[2]}},
                                static_cast<unsigned char>(best));
                    }
                    maxLabel = std::max(maxLabel, best);
                }
        ++tileNum;
        if (progress)
            progress(tileNum, totalTiles);
    }

    // Resample labels back onto the original volume grid.
    using LResample = itk::ResampleImageFilter<LabelImage, LabelImage>;
    auto lr = LResample::New();
    lr->SetInput(labRes);
    lr->SetTransform(itk::IdentityTransform<double, 3>::New());
    lr->SetInterpolator(
        itk::NearestNeighborInterpolateImageFunction<LabelImage>::New());
    const auto& out = ref->itkImage();
    lr->SetOutputSpacing(out->GetSpacing());
    lr->SetOutputOrigin(out->GetOrigin());
    lr->SetOutputDirection(out->GetDirection());
    lr->SetSize(out->GetLargestPossibleRegion().GetSize());
    lr->SetDefaultPixelValue(0);
    lr->Update();

    auto connector = itk::ImageToVTKImageFilter<LabelImage>::New();
    connector->SetInput(lr->GetOutput());
    connector->Update();
    auto vtkLabel = vtkSmartPointer<vtkImageData>::New();
    vtkLabel->DeepCopy(connector->GetOutput());
    // Display space is index*spacing — drop the patient-space origin.
    vtkLabel->SetOrigin(0, 0, 0);

    Segmentation seg;
    seg.labelmap = vtkLabel;
    seg.lut = Segmentation::makeLabelLut(std::max(1, maxLabel));
    for (int i = 1; i <= maxLabel; ++i) {
        // Use the model's class names when available (index 0 = bg).
        if (i < int(pp.classNames.size()) && !pp.classNames[i].empty())
            seg.labelNames[i] = pp.classNames[i];
        else
            seg.labelNames[i] = "Class " + std::to_string(i);
    }
    return seg;
}

} // namespace meda
