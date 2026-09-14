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
#include <itkConnectedComponentImageFilter.h>
#include <itkRelabelComponentImageFilter.h>

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

// Maximum classes we can accumulate in the sliding-window buffers.
static constexpr int nClassMax = 16;

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

/// 1D Gaussian weight for position i in a window of size n.
/// sigma = n/4 is the standard nnU-Net choice.
float gauss1D(int i, int n)
{
    const float sigma = std::max(1.0f, n / 4.0f);
    const float center = (n - 1) / 2.0f;
    const float d = static_cast<float>(i) - center;
    return std::exp(-0.5f * d * d / (sigma * sigma));
}

/// Apply softmax along the class dimension. p is [nClass, nVoxels].
void softmaxInPlace(float* p, int nClass, size_t nVoxels)
{
    for (size_t v = 0; v < nVoxels; ++v) {
        float mx = p[v];
        for (int c = 1; c < nClass; ++c)
            mx = std::max(mx, p[c * nVoxels + v]);
        float sum = 0.f;
        for (int c = 0; c < nClass; ++c) {
            p[c * nVoxels + v] = std::exp(p[c * nVoxels + v] - mx);
            sum += p[c * nVoxels + v];
        }
        const float inv = 1.f / sum;
        for (int c = 0; c < nClass; ++c)
            p[c * nVoxels + v] *= inv;
    }
}

/// Tile start positions with 50% overlap. Last tile is clamped back
/// so the grid exactly covers the dimension.
std::vector<int> tileStarts(int dim, int tile)
{
    if (dim <= tile)
        return {0};
    const int step = std::max(1, tile / 2);
    std::vector<int> starts;
    for (int s = 0; s < dim; s += step) {
        if (s + tile >= dim) {
            const int last = dim - tile;
            if (starts.empty() || starts.back() != last)
                starts.push_back(last);
            break;
        }
        starts.push_back(s);
    }
    return starts;
}

} // namespace

namespace {
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

namespace {

/// Remove small connected components per label. Processes each label
/// value independently so multi-class segmentations are cleaned without
/// merging different structures.
using CCImage = itk::Image<unsigned int, 3>;
void removeSmallComponents(LabelImage::Pointer label, unsigned int minSize)
{
    itk::ImageRegionIterator<LabelImage> it(label,
        label->GetLargestPossibleRegion());
    unsigned char maxLbl = 0;
    for (it.GoToBegin(); !it.IsAtEnd(); ++it)
        maxLbl = std::max(maxLbl, it.Get());
    if (maxLbl == 0) return;

    for (unsigned char lbl = 1; lbl <= maxLbl; ++lbl) {
        auto mask = LabelImage::New();
        mask->SetRegions(label->GetLargestPossibleRegion());
        mask->CopyInformation(label);
        mask->Allocate();
        mask->FillBuffer(0);
        for (it.GoToBegin(); !it.IsAtEnd(); ++it)
            mask->SetPixel(it.GetIndex(), it.Get() == lbl ? 1 : 0);

        auto cc = itk::ConnectedComponentImageFilter<LabelImage,
                                                      CCImage>::New();
        cc->SetInput(mask);
        auto relabel = itk::RelabelComponentImageFilter<CCImage,
                                                         CCImage>::New();
        relabel->SetInput(cc->GetOutput());
        relabel->SetMinimumObjectSize(minSize);
        relabel->Update();

        auto* out = relabel->GetOutput();
        itk::ImageRegionConstIterator<CCImage> oIt(out,
            out->GetLargestPossibleRegion());
        for (oIt.GoToBegin(), it.GoToBegin(); !oIt.IsAtEnd(); ++oIt, ++it) {
            if (it.Get() == lbl && oIt.Get() == 0)
                it.Set(0);
        }
    }
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

    const auto ext = ref->extent();
    const auto img = ref->itkImage();
    const auto range = ref->scalarRange();
    const float lo = (float)range[0];
    const float rng = std::max(1e-6f, (float)range[1] - lo);

    LabelImage::Pointer label = LabelImage::New();
    label->SetRegions(img->GetLargestPossibleRegion());
    label->SetSpacing(img->GetSpacing());
    label->SetOrigin(img->GetOrigin());
    label->SetDirection(img->GetDirection());
    label->Allocate();
    label->FillBuffer(0);

    Ort::MemoryInfo memInfo =
        Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    const char* inNames[] = {m_impl->inputName.c_str()};
    const char* outNames[] = {m_impl->outputName.c_str()};

    int maxLabel = 0;

    if (shape.size() == 4) {
        // ---- 2D model: [1,1,H,W] ----
        const int H = int(shape[2] > 0 ? shape[2] : ext[1]);
        const int W = int(shape[3] > 0 ? shape[3] : ext[0]);
        const int vR = ext[1], vC = ext[0];   // volume rows / cols
        std::vector<int64_t> dims{1, 1, H, W};

        if (H == vR && W == vC) {
            // Fast path: slice matches model input exactly.
            for (int k = 0; k < ext[2]; ++k) {
                auto data = normalizeSlice(img, k, vR, vC, lo, (float)range[1]);
                auto inT = Ort::Value::CreateTensor<float>(
                    memInfo, data.data(), data.size(), dims.data(), 4);
                auto outs = m_impl->session->Run(
                    Ort::RunOptions{nullptr}, inNames, &inT, 1, outNames, 1);
                float* p = outs[0].GetTensorMutableData<float>();
                const auto os = outs[0].GetTensorTypeAndShapeInfo().GetShape();
                const int nClass = int(os.size() > 3 ? os[1] : 1);
                for (int r = 0; r < vR; ++r)
                    for (int c = 0; c < vC; ++c) {
                        const size_t b = (size_t)r * vC + c;
                        int best = 0; float bv = p[b];
                        for (int cl = 1; cl < nClass; ++cl) {
                            float v = p[cl * vR * vC + b];
                            if (v > bv) { bv = v; best = cl; }
                        }
                        if (nClass == 1) best = bv > 0.5f ? 1 : 0;
                        label->SetPixel({{c, r, k}}, (unsigned char)best);
                        maxLabel = std::max(maxLabel, best);
                    }
                if (progress) progress(k + 1, ext[2]);
            }
        } else {
            // Sliding window with Gaussian-weighted overlap.
            const auto rs = tileStarts(vR, H);
            const auto cs = tileStarts(vC, W);
            const int totalTiles = ext[2] * int(rs.size() * cs.size());
            int tileNum = 0;
            int nClass = 1;  // tracked across tiles (fixed for a given model)
            for (int k = 0; k < ext[2]; ++k) {
                std::vector<float> probAcc(nClassMax * vR * vC, 0.f);
                std::vector<float> wAcc(vR * vC, 0.f);
                for (int r0 : rs)
                for (int c0 : cs) {
                    std::vector<float> data(H * W);
                    for (int r = 0; r < H; ++r)
                    for (int c = 0; c < W; ++c) {
                        int gr = r0 + r, gc = c0 + c;
                        float v;
                        if (gr < vR && gc < vC) {
                            v = img->GetPixel({{gc, gr, k}});
                        } else {
                            v = lo;
                        }
                        data[r * W + c] = std::clamp((v - lo) / rng, 0.f, 1.f);
                    }
                    auto inT = Ort::Value::CreateTensor<float>(
                        memInfo, data.data(), data.size(), dims.data(), 4);
                    auto outs = m_impl->session->Run(
                        Ort::RunOptions{nullptr}, inNames, &inT, 1, outNames, 1);
                    float* p = outs[0].GetTensorMutableData<float>();
                    const auto os = outs[0].GetTensorTypeAndShapeInfo().GetShape();
                    nClass = int(os.size() > 3 ? os[1] : 1);
                    if (nClass > nClassMax) continue;  // safety
                    // Softmax in-place so we accumulate probabilities.
                    softmaxInPlace(p, nClass, (size_t)H * W);
                    for (int r = 0; r < H; ++r)
                    for (int c = 0; c < W; ++c) {
                        int gr = r0 + r, gc = c0 + c;
                        if (gr >= vR || gc >= vC) continue;
                        const float w = gauss1D(r, H) * gauss1D(c, W);
                        const size_t tb = (size_t)r * W + c;
                        const size_t gb = (size_t)gr * vC + gc;
                        for (int cl = 0; cl < nClass; ++cl)
                            probAcc[cl * vR * vC + gb] += p[cl * H * W + tb] * w;
                        wAcc[gb] += w;
                    }
                    ++tileNum;
                    if (progress) progress(tileNum, totalTiles);
                }
                // Argmax of accumulated weighted probabilities.
                for (int r = 0; r < vR; ++r)
                for (int c = 0; c < vC; ++c) {
                    const size_t gb = (size_t)r * vC + c;
                    if (wAcc[gb] < 1e-6f) continue;
                    int best = 0; float bv = probAcc[gb] / wAcc[gb];
                    for (int cl = 1; cl < nClass; ++cl) {
                        float v = probAcc[cl * vR * vC + gb] / wAcc[gb];
                        if (v > bv) { bv = v; best = cl; }
                    }
                    if (nClass == 1) best = bv > 0.5f ? 1 : 0;
                    label->SetPixel({{c, r, k}}, (unsigned char)best);
                    maxLabel = std::max(maxLabel, best);
                }
            }
        }
    } else {
        // ---- 3D model: [1,1,D,H,W] ----
        const int D = int(shape[2] > 0 ? shape[2] : ext[2]);
        const int H = int(shape[3] > 0 ? shape[3] : ext[1]);
        const int W = int(shape[4] > 0 ? shape[4] : ext[0]);
        std::vector<int64_t> dims{1, 1, D, H, W};

        if (D == ext[2] && H == ext[1] && W == ext[0]) {
            // Fast path: volume matches model input exactly.
            const size_t n = (size_t)D * H * W;
            std::vector<float> data(n);
            size_t i = 0;
            for (int k = 0; k < D; ++k)
            for (int r = 0; r < H; ++r)
            for (int c = 0; c < W; ++c, ++i)
                data[i] = std::clamp(
                    (img->GetPixel({{c, r, k}}) - lo) / rng, 0.0f, 1.0f);
            auto inT = Ort::Value::CreateTensor<float>(
                memInfo, data.data(), data.size(), dims.data(), 5);
            auto outs = m_impl->session->Run(
                Ort::RunOptions{nullptr}, inNames, &inT, 1, outNames, 1);
            float* p = outs[0].GetTensorMutableData<float>();
            const auto os = outs[0].GetTensorTypeAndShapeInfo().GetShape();
            const int nClass = int(os.size() > 4 ? os[1] : 1);
            for (size_t v = 0; v < n; ++v) {
                int best = 0; float bv = p[v];
                for (int cl = 1; cl < nClass; ++cl) {
                    float val = p[cl * n + v];
                    if (val > bv) { bv = val; best = cl; }
                }
                if (nClass == 1) best = bv > 0.5f ? 1 : 0;
                const int k = int(v / ((size_t)W * H));
                const int rem = int(v % ((size_t)W * H));
                const int r = rem / W;
                const int c = rem % W;
                label->SetPixel({{c, r, k}}, (unsigned char)best);
                maxLabel = std::max(maxLabel, best);
            }
            if (progress) progress(1, 1);
        } else {
            // 3D sliding window with Gaussian-weighted overlap.
            const auto ks = tileStarts(ext[2], D);
            const auto rs = tileStarts(ext[1], H);
            const auto cs = tileStarts(ext[0], W);
            const size_t volN = (size_t)ext[2] * ext[1] * ext[0];
            const int totalTiles = int(ks.size() * rs.size() * cs.size());

            // Accumulators — may be large for big volumes.
            std::vector<float> probAcc, wAcc;
            int nClass = 1;
            probAcc.resize(nClassMax * volN, 0.f);
            wAcc.resize(volN, 0.f);

            int tileNum = 0;
            for (int k0 : ks)
            for (int r0 : rs)
            for (int c0 : cs) {
                std::vector<float> data((size_t)D * H * W);
                size_t i = 0;
                for (int k = 0; k < D; ++k)
                for (int r = 0; r < H; ++r)
                for (int c = 0; c < W; ++c, ++i) {
                    int gk = k0 + k, gr = r0 + r, gc = c0 + c;
                    float v;
                    if (gk < ext[2] && gr < ext[1] && gc < ext[0]) {
                        v = img->GetPixel({{gc, gr, gk}});
                    } else {
                        v = lo;
                    }
                    data[i] = std::clamp((v - lo) / rng, 0.f, 1.f);
                }
                auto inT = Ort::Value::CreateTensor<float>(
                    memInfo, data.data(), data.size(), dims.data(), 5);
                auto outs = m_impl->session->Run(
                    Ort::RunOptions{nullptr}, inNames, &inT, 1, outNames, 1);
                float* p = outs[0].GetTensorMutableData<float>();
                const auto os = outs[0].GetTensorTypeAndShapeInfo().GetShape();
                nClass = int(os.size() > 4 ? os[1] : 1);
                if (nClass > nClassMax) continue;
                softmaxInPlace(p, nClass, (size_t)D * H * W);
                for (int k = 0; k < D; ++k)
                for (int r = 0; r < H; ++r)
                for (int c = 0; c < W; ++c) {
                    int gk = k0 + k, gr = r0 + r, gc = c0 + c;
                    if (gk >= ext[2] || gr >= ext[1] || gc >= ext[0]) continue;
                    const float w = gauss1D(k, D) * gauss1D(r, H) * gauss1D(c, W);
                    const size_t tb = (size_t)k * H * W + r * W + c;
                    const size_t gb = (size_t)gk * ext[1] * ext[0] + gr * ext[0] + gc;
                    for (int cl = 0; cl < nClass; ++cl)
                        probAcc[cl * volN + gb] += p[cl * D * H * W + tb] * w;
                    wAcc[gb] += w;
                }
                ++tileNum;
                if (progress) progress(tileNum, totalTiles);
            }
            // Argmax of accumulated weighted probabilities.
            for (int k = 0; k < ext[2]; ++k)
            for (int r = 0; r < ext[1]; ++r)
            for (int c = 0; c < ext[0]; ++c) {
                const size_t gb = (size_t)k * ext[1] * ext[0] + r * ext[0] + c;
                if (wAcc[gb] < 1e-6f) continue;
                int best = 0; float bv = probAcc[gb] / wAcc[gb];
                for (int cl = 1; cl < nClass; ++cl) {
                    float v = probAcc[cl * volN + gb] / wAcc[gb];
                    if (v > bv) { bv = v; best = cl; }
                }
                if (nClass == 1) best = bv > 0.5f ? 1 : 0;
                label->SetPixel({{c, r, k}}, (unsigned char)best);
                maxLabel = std::max(maxLabel, best);
            }
        }
    }

    // Connected-component cleanup: remove speckle (< 50 voxels).
    removeSmallComponents(label, 50);

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

    // Tile start offsets per axis — 50% overlap for Gaussian blending.
    std::vector<long> starts[3];
    for (int i = 0; i < 3; ++i) {
        const long rs = long(resSize[i]), ts = long(target[i]);
        if (rs <= ts) {
            starts[i].push_back(0);
        } else {
            const long step = std::max(1L, ts / 2);
            for (long s = 0; s < rs; s += step) {
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

    // Probability + weight accumulators on the resampled grid.
    const size_t resN = static_cast<size_t>(resSize[0]) * resSize[1] * resSize[2];
    std::vector<float> probAcc, wAcc;
    probAcc.resize(nClassMax * resN, 0.f);
    wAcc.resize(resN, 0.f);

    const float lo  = pp.clipLo;
    const float rng = std::max(1e-6f, pp.clipHi - pp.clipLo);
    const size_t n = static_cast<size_t>(MX) * MY * MZ;

    Ort::MemoryInfo memInfo =
        Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::vector<int64_t> dims{1, MX, MY, MZ, 1};
    const char* inNames[]  = {m_impl->inputName.c_str()};
    const char* outNames[] = {m_impl->outputName.c_str()};

    int maxLabel = 0;
    int nClass = 1;
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
        float* p = outs[0].GetTensorMutableData<float>();
        const auto oshape =
            outs[0].GetTensorTypeAndShapeInfo().GetShape();
        nClass = int(oshape.back());
        if (nClass > nClassMax) { ++tileNum; continue; }
        // Softmax so we accumulate probabilities, not logits.
        // Channel-last layout: [N, X, Y, Z, C] — softmax along last dim.
        for (size_t v = 0; v < n; ++v) {
            float mx = p[v * nClass];
            for (int c = 1; c < nClass; ++c)
                mx = std::max(mx, p[v * nClass + c]);
            float sum = 0.f;
            for (int c = 0; c < nClass; ++c) {
                p[v * nClass + c] = std::exp(p[v * nClass + c] - mx);
                sum += p[v * nClass + c];
            }
            const float inv = 1.f / sum;
            for (int c = 0; c < nClass; ++c)
                p[v * nClass + c] *= inv;
        }

        // Accumulate weighted probabilities into the resampled grid.
        t = 0;
        for (int64_t x = 0; x < MX; ++x)
            for (int64_t y = 0; y < MY; ++y)
                for (int64_t z = 0; z < MZ; ++z, ++t) {
                    const long gi[3] = {static_cast<long>(sx + x - padOff[0]),
                                        static_cast<long>(sy + y - padOff[1]),
                                        static_cast<long>(sz + z - padOff[2])};
                    if (gi[0] < 0 || gi[0] >= long(resSize[0]) ||
                        gi[1] < 0 || gi[1] >= long(resSize[1]) ||
                        gi[2] < 0 || gi[2] >= long(resSize[2]))
                        continue;
                    const float w = gauss1D(int(x), int(MX)) *
                                   gauss1D(int(y), int(MY)) *
                                   gauss1D(int(z), int(MZ));
                    const size_t gb = static_cast<size_t>(gi[0]) +
                        static_cast<size_t>(gi[1]) * resSize[0] +
                        static_cast<size_t>(gi[2]) * resSize[0] * resSize[1];
                    for (int c = 0; c < nClass; ++c)
                        probAcc[c * resN + gb] += p[t * nClass + c] * w;
                    wAcc[gb] += w;
                    maxLabel = std::max(maxLabel, nClass - 1);
                }
        ++tileNum;
        if (progress)
            progress(tileNum, totalTiles);
    }

    // Argmax of accumulated weighted probabilities → label volume.
    auto labRes = LabelImage::New();
    labRes->SetRegions(ImageType::RegionType{
        ImageType::IndexType{{0, 0, 0}}, resSize});
    labRes->SetSpacing(resampled->GetSpacing());
    labRes->SetOrigin(resampled->GetOrigin());
    labRes->SetDirection(resampled->GetDirection());
    labRes->Allocate();
    labRes->FillBuffer(0);
    for (size_t z = 0; z < resSize[2]; ++z)
    for (size_t y = 0; y < resSize[1]; ++y)
    for (size_t x = 0; x < resSize[0]; ++x) {
        const size_t gb = x + y * resSize[0] + z * resSize[0] * resSize[1];
        if (wAcc[gb] < 1e-6f) continue;
        int best = 0; float bv = probAcc[gb] / wAcc[gb];
        for (int c = 1; c < nClass; ++c) {
            float v = probAcc[c * resN + gb] / wAcc[gb];
            if (v > bv) { bv = v; best = c; }
        }
        if (nClass == 1) best = bv > 0.5f ? 1 : 0;
        labRes->SetPixel(LabelImage::IndexType{{long(x), long(y), long(z)}},
                         static_cast<unsigned char>(best));
    }

    // Connected-component cleanup on the resampled grid.
    removeSmallComponents(labRes, 50);

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
