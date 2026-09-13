#include "DicomLoader.h"

#include <itkGDCMImageIO.h>
#include <itkGDCMSeriesFileNames.h>
#include <itkImageFileReader.h>
#include <itkImageSeriesReader.h>
#include <itkJoinSeriesImageFilter.h>
#include <itkImageToVTKImageFilter.h>
#include <itkOrientImageFilter.h>
#include <itkMetaDataDictionary.h>
#include <itkMetaDataObject.h>
#include <itkCommand.h>
#include <itkResampleImageFilter.h>
#include <itkLinearInterpolateImageFunction.h>
#include <itkIdentityTransform.h>

#include <vtkImageData.h>
#include <vtkImageFlip.h>
#include <vtkSmartPointer.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>

namespace meda {

namespace {

template <typename T>
bool getTag(const itk::MetaDataDictionary& dict, const char* key, T& out)
{
    return itk::ExposeMetaData(dict, key, out);
}

std::string getTagString(const itk::MetaDataDictionary& dict, const char* key)
{
    std::string v;
    itk::ExposeMetaData(dict, key, v);
    // ITK keeps trailing spaces in DICOM strings.
    while (!v.empty() && (v.back() == ' ' || v.back() == '\0'))
        v.pop_back();
    return v;
}

/// Z position of a slice, read from the file header only. NaN on failure.
double slicePosition(const std::string& path)
{
    auto io = itk::GDCMImageIO::New();
    io->SetFileName(path);
    try {
        io->ReadImageInformation();
    } catch (...) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    auto ipp = getTagString(io->GetMetaDataDictionary(), "0020|0032");
    const auto lastSep = ipp.rfind('\\');
    if (lastSep == std::string::npos)
        return std::numeric_limits<double>::quiet_NaN();
    try {
        return std::stod(ipp.substr(lastSep + 1));
    } catch (...) {
        return std::numeric_limits<double>::quiet_NaN();
    }
}

/// Split a series that packs multiple acquisitions under one
/// SeriesInstanceUID (e.g. CT angio arterial + venous passes): the same
/// z position appears once per phase. Returns the original series when
/// positions are unique.
std::vector<SeriesMeta> splitByPhase(const SeriesMeta& meta)
{
    if (meta.files.size() < 2)
        return {meta};

    std::vector<long> zs;
    zs.reserve(meta.files.size());
    bool anyNan = false;
    for (const auto& f : meta.files) {
        const double z = slicePosition(f);
        if (std::isnan(z)) {
            anyNan = true;
            zs.push_back(0);
        } else {
            zs.push_back(std::lround(z * 100.0)); // 0.01 mm units
        }
    }
    if (anyNan)
        return {meta};

    std::map<long,int> count;
    int maxOcc = 0;
    for (long z : zs)
        maxOcc = std::max(maxOcc, ++count[z]);
    if (maxOcc <= 1)
        return {meta};

    // The i-th occurrence of a position belongs to phase i.
    std::vector<SeriesMeta> phases(maxOcc, meta);
    for (auto& ph : phases)
        ph.files.clear();
    std::map<long,int> seen;
    for (size_t i = 0; i < meta.files.size(); ++i)
        phases[seen[zs[i]]++].files.push_back(meta.files[i]);
    for (int p = 0; p < maxOcc; ++p) {
        phases[p].instanceCount = static_cast<int>(phases[p].files.size());
        if (p > 0) {
            phases[p].seriesInstanceUID += ".ph" + std::to_string(p + 1);
            phases[p].seriesDescription += " (Phase " + std::to_string(p + 1)
                                           + ")";
        }
    }
    return phases;
}

} // namespace

void DicomLoader::sortFiles(std::vector<std::string>& files,
                            SliceSort order)
{
    if (files.size() < 2 || order == SliceSort::Position)
        return;  // Position is the loader's native order.

    // Read just the tag we need from each header.
    auto tagOf = [](const std::string& path, const char* key) {
        auto io = itk::GDCMImageIO::New();
        io->SetFileName(path);
        try {
            io->ReadImageInformation();
        } catch (...) {
            return std::string();
        }
        return getTagString(io->GetMetaDataDictionary(), key);
    };

    std::vector<std::string> keys(files.size());
    switch (order) {
    case SliceSort::InstanceNumber:
        for (size_t i = 0; i < files.size(); ++i)
            keys[i] = tagOf(files[i], "0020|0013");
        break;
    case SliceSort::AcquisitionTime:
        for (size_t i = 0; i < files.size(); ++i) {
            keys[i] = tagOf(files[i], "0008|0032");  // AcquisitionTime
            if (keys[i].empty())
                keys[i] = tagOf(files[i], "0008|0033"); // ContentTime
        }
        break;
    case SliceSort::Filename:
        for (size_t i = 0; i < files.size(); ++i)
            keys[i] = files[i];  // full path — natural sort
        break;
    default:
        return;
    }
    // Stable sort by key; keep files with missing keys at the end.
    std::stable_sort(files.begin(), files.end(),
        [&](const std::string& a, const std::string& b) {
            const auto ka = keys[&a - files.data()];
            const auto kb = keys[&b - files.data()];
            if (ka.empty() && kb.empty()) return false;
            if (ka.empty()) return false;
            if (kb.empty()) return true;
            // Numeric when both parse as numbers.
            const char* pa = ka.c_str();
            const char* pb = kb.c_str();
            char* ea = nullptr;
            char* eb = nullptr;
            const double da = std::strtod(pa, &ea);
            const double db = std::strtod(pb, &eb);
            if (ea != pa && *ea == '\0' && eb != pb && *eb == '\0')
                return da < db;
            return ka < kb;
        });
}

std::vector<SeriesMeta> DicomLoader::scanDirectory(const std::string& dir)
{
    auto names = itk::GDCMSeriesFileNames::New();
    names->SetRecursive(true);
    names->SetDirectory(dir);
    names->SetUseSeriesDetails(true);
    names->AddSeriesRestriction("0008|0021"); // SeriesDate

    auto uids = names->GetSeriesUIDs();
    std::vector<SeriesMeta> out;
    for (const auto& uid : uids) {
        auto files = names->GetFileNames(uid);
        if (files.empty())
            continue;
        try {
            const std::string first = files.front();
            auto meta = readSeriesHeader(first, uid, std::move(files));
            // Skip non-image series — reports, presentation states, dose
            // reports, and objects that can't be viewed as volumes.
            static const std::set<std::string> nonImage = {
                "SR", "KO", "PR", "DOC", "RTSTRUCT", "REG", "FID", "RWV",
                "HANGING", "M3D", "PLAN"};
            if (nonImage.count(meta.modality) || meta.rows <= 0 ||
                meta.columns <= 0)
                continue;
            auto parts = splitByPhase(meta);
            out.insert(out.end(),
                       std::make_move_iterator(parts.begin()),
                       std::make_move_iterator(parts.end()));
        } catch (...) {
            // Unreadable / unsupported series — skip rather than abort scan.
        }
    }
    std::sort(out.begin(), out.end(), [](const SeriesMeta& a, const SeriesMeta& b) {
        if (a.patientName != b.patientName) return a.patientName < b.patientName;
        if (a.studyDate != b.studyDate)     return a.studyDate > b.studyDate;
        return a.seriesDescription < b.seriesDescription;
    });
    return out;
}

SeriesMeta DicomLoader::readSeriesHeader(const std::string& firstFile,
                                         const std::string& seriesUID,
                                         std::vector<std::string> files)
{
    auto io = itk::GDCMImageIO::New();
    io->SetFileName(firstFile);
    io->ReadImageInformation();

    const auto& dict = io->GetMetaDataDictionary();
    SeriesMeta m;
    m.seriesInstanceUID = seriesUID;
    m.files             = std::move(files);
    m.instanceCount     = static_cast<int>(m.files.size());
    m.studyInstanceUID  = getTagString(dict, "0020|000d");
    m.seriesDescription = getTagString(dict, "0008|103e");
    m.studyDescription  = getTagString(dict, "0008|1030");
    m.modality          = getTagString(dict, "0008|0060");
    m.patientName       = getTagString(dict, "0010|0010");
    m.patientID         = getTagString(dict, "0010|0020");
    m.patientBirthDate  = getTagString(dict, "0010|0030");
    m.patientSex        = getTagString(dict, "0010|0040");
    m.studyDate         = getTagString(dict, "0008|0020");
    m.accessionNumber   = getTagString(dict, "0008|0050");
    m.institution       = getTagString(dict, "0008|0080");
    m.seriesNumber      = getTagString(dict, "0020|0011");
    m.bodyPart          = getTagString(dict, "0018|0015");
    m.sopClassUID       = getTagString(dict, "0008|0016");
    m.rows              = static_cast<int>(io->GetDimensions(1));
    m.columns           = static_cast<int>(io->GetDimensions(0));

    // Window center/width may be multi-valued ("80\40"); take the first.
    std::string wc = getTagString(dict, "0028|1050");
    std::string ww = getTagString(dict, "0028|1051");
    if (!wc.empty() && !ww.empty()) {
        m.windowCenter  = std::stod(wc.substr(0, wc.find('\\')));
        m.windowWidth   = std::stod(ww.substr(0, ww.find('\\')));
        m.hasWindowing  = true;
    }
    const auto photometric = getTagString(dict, "0028|0004");
    m.monochrome1 = (photometric == "MONOCHROME1");
    return m;
}

VolumePtr DicomLoader::loadSeries(const SeriesMeta& series,
                                  std::function<void(float)> progress)
{
    return loadFiles(series.files, std::move(progress));
}

VolumePtr DicomLoader::loadFiles(const std::vector<std::string>& files,
                                 std::function<void(float)> progress)
{
    if (files.empty())
        throw std::runtime_error("DicomLoader: empty file list");

    // Multi-phase acquisitions (e.g. CT angio arterial + venous) are often
    // packed under one SeriesInstanceUID: the same slice position appears
    // twice, and ITK would stack both copies into a doubled volume.
    // Keep only the first file per z position.
    std::vector<std::string> use = files;
    {
        std::set<long> seen; // z position in 0.01mm units
        auto out = use.begin();
        size_t dup = 0;
        for (auto it = use.begin(); it != use.end(); ++it) {
            const double z = slicePosition(*it);
            if (!std::isnan(z)) {
                const long key = std::lround(z * 100.0);
                if (!seen.insert(key).second) {
                    ++dup;
                    continue;
                }
            }
            if (out != it)
                *out = std::move(*it);
            ++out;
        }
        use.erase(out, use.end());
        if (dup)
            std::fprintf(stderr,
                         "[Scanthia] dropped %zu duplicate-position slices "
                         "(multi-phase series)\n", dup);
    }

    using Reader = itk::ImageSeriesReader<ImageType>;
    ImageType::Pointer image;

    // Single-file series may be multi-frame (US/XA/cine): GDCM can read
    // the frames as a 3D volume directly, which makes cine work for
    // free. Try that path first; fall through to the series reader if
    // it isn't really multi-frame.
    if (use.size() == 1) {
        try {
            auto r3 = itk::ImageFileReader<ImageType>::New();
            r3->SetImageIO(itk::GDCMImageIO::New());
            r3->SetFileName(use.front());
            r3->Update();
            auto cand = r3->GetOutput();
            if (cand->GetLargestPossibleRegion().GetSize(2) > 1) {
                image = cand;
                image->DisconnectPipeline();
            }
        } catch (...) {
            // Not multi-frame — normal path below.
        }
    }

    if (!image) {
        auto reader = Reader::New();
    auto io = itk::GDCMImageIO::New();
    reader->SetImageIO(io);
    reader->SetFileNames(use);

    if (progress) {
        struct Bridge : itk::Command {
            using Self = Bridge;
            using Pointer = itk::SmartPointer<Self>;
            static Pointer New() { return new Self; }
            itkOverrideGetNameOfClassMacro(Bridge);
            std::function<void(float)> fn;
            itk::ProcessObject* obj = nullptr;
            void Execute(itk::Object*, const itk::EventObject& e) override
            {
                if (itk::ProgressEvent().CheckEvent(&e) && fn)
                    fn(obj->GetProgress());
            }
            void Execute(const itk::Object* o, const itk::EventObject& e) override
            {
                Execute(const_cast<itk::Object*>(o), e);
            }
        };
        auto bridge = Bridge::New();
        bridge->fn = std::move(progress);
        bridge->obj = reader;
        reader->AddObserver(itk::ProgressEvent(), bridge);
    }

    try {
        reader->Update();
        image = reader->GetOutput();
    } catch (const itk::ExceptionObject& e) {
        // Series with all slices at the same position (monitoring /
        // Smart Prep / perfusion) yield zero z-spacing and make ITK throw.
        // Fall back to stacking the 2D images with synthetic 1mm spacing
        // so they are still viewable as a scrollable stack.
        if (std::string(e.what()).find("Zero-valued spacing") ==
            std::string::npos)
            throw;

        using Image2D = itk::Image<float, 2>;
        using Joiner = itk::JoinSeriesImageFilter<Image2D, ImageType>;
        auto joiner = Joiner::New();
        joiner->SetSpacing(1.0);
        joiner->SetOrigin(0.0);
        for (const auto& f : use) {
            auto r2d = itk::ImageFileReader<Image2D>::New();
            r2d->SetImageIO(itk::GDCMImageIO::New());
            r2d->SetFileName(f);
            r2d->Update();
            joiner->PushBackInput(r2d->GetOutput());
        }
        joiner->Update();
        image = joiner->GetOutput();
        image->DisconnectPipeline();
    }
    } // !image — normal series path

    // Reorient every volume to canonical axial (identity direction). This
    // makes index space == patient space up to origin, which keeps MPR,
    // crosshair and measurement math trivially correct.
    auto orienter = itk::OrientImageFilter<ImageType, ImageType>::New();
    orienter->SetInput(image);
    orienter->UseImageDirectionOn();
    ImageType::DirectionType ident;
    ident.SetIdentity();
    orienter->SetDesiredCoordinateDirection(ident);
    orienter->Update();

    image = orienter->GetOutput();
    image->DisconnectPipeline();

    // Header metadata for the volume (defaults may differ from scan-time).
    SeriesMeta meta = readSeriesHeader(use.front(), "", use);

    auto connector = itk::ImageToVTKImageFilter<ImageType>::New();
    connector->SetInput(image);
    connector->Update();

    auto vtkImage = vtkSmartPointer<vtkImageData>::New();
    vtkImage->DeepCopy(connector->GetOutput());
    vtkImage->SetSpacing(image->GetSpacing()[0], image->GetSpacing()[1],
                         image->GetSpacing()[2]);
    vtkImage->SetOrigin(0, 0, 0); // patient position handled via ITK geometry

    return std::make_shared<Volume>(image, vtkImage, std::move(meta));
}

VolumePtr DicomLoader::loadSeriesStreaming(const SeriesMeta& series,
                                           const StreamCallbacks& cb)
{
    std::vector<std::string> files = series.files;
    if (files.empty())
        return nullptr;

    // --- Header pass: geometry, direction, z order (headers only) ------
    std::vector<double> zs(files.size());
    int rows = 0, cols = 0;
    double sx = 1, sy = 1;
    bool identityDir = true;
    for (size_t i = 0; i < files.size(); ++i) {
        auto io = itk::GDCMImageIO::New();
        io->SetFileName(files[i]);
        try {
            io->ReadImageInformation();
        } catch (...) {
            return loadSeries(series);   // unreadable → normal path
        }
        const auto& d = io->GetMetaDataDictionary();
        if (i == 0) {
            cols = static_cast<int>(io->GetDimensions(0));
            rows = static_cast<int>(io->GetDimensions(1));
            sx = io->GetSpacing(0);
            sy = io->GetSpacing(1);
            // Streaming writes slices in display order, so the source
            // must already be (near-)identity direction. Anything else
            // falls back to the reorienting loader.
            auto dirStr = getTagString(d, "0020|0037");
            if (!dirStr.empty()) {
                // "1\0\0\0\1\0" is the canonical identity string.
                std::vector<double> v;
                for (auto& part : [&] {
                        std::vector<std::string> out;
                        size_t p = 0;
                        while (p <= dirStr.size()) {
                            const auto s = dirStr.find('\\', p);
                            out.push_back(dirStr.substr(
                                p, s == std::string::npos
                                       ? std::string::npos : s - p));
                            if (s == std::string::npos) break;
                            p = s + 1;
                        }
                        return out;
                    }()) {
                    try { v.push_back(std::stod(part)); } catch (...) {}
                }
                if (v.size() == 6) {
                    const double ident[6] = {1,0,0,0,1,0};
                    for (int k = 0; k < 6; ++k)
                        if (std::abs(v[k] - ident[k]) > 0.01)
                            identityDir = false;
                }
            }
        }
        zs[i] = slicePosition(files[i]);
    }
    if (!identityDir || rows <= 0 || cols <= 0)
        return loadSeries(series);     // needs reorient → normal path

    // Sort by z position; drop duplicate-position files (multi-phase).
    {
        std::vector<size_t> ord(files.size());
        std::iota(ord.begin(), ord.end(), 0);
        std::stable_sort(ord.begin(), ord.end(), [&](size_t a, size_t b) {
            return zs[a] < zs[b];
        });
        std::vector<std::string> sorted;
        std::set<long> seen;
        for (size_t i : ord) {
            const long key = std::isnan(zs[i]) ? i
                                             : std::lround(zs[i] * 100.0);
            if (!seen.insert(key).second)
                continue;             // duplicate z — skip
            sorted.push_back(files[i]);
        }
        files = std::move(sorted);
    }
    const int nSlices = static_cast<int>(files.size());

    // --- Memory budget check ------------------------------------------
    // Full-res cost = voxels * 4 bytes * 2 (ITK + VTK buffers).
    const size_t fullBytes = size_t(cols) * rows * nSlices * 4 * 2;
    int strideXY = 1;     // in-plane stride (1 = full res)
    int strideZ  = 1;     // slice stride (1 = every slice)
    bool downsampled = false;
    if (cb.memoryBudgetBytes > 0 &&
        fullBytes > cb.memoryBudgetBytes) {
        downsampled = true;
        // Prefer in-plane stride first (preserves z for scrolling),
        // then z stride if still over budget.
        while (strideXY < 16 &&
               size_t(cols / strideXY) * (rows / strideXY) * nSlices
                   * 4 * 2 > cb.memoryBudgetBytes)
            ++strideXY;
        while (strideZ < 8 &&
               size_t(cols / strideXY) * (rows / strideXY)
                   * (nSlices / strideZ) * 4 * 2 > cb.memoryBudgetBytes)
            ++strideZ;
    }
    const int outCols   = cols / strideXY;
    const int outRows   = rows / strideXY;
    const int outSlices = nSlices / strideZ;
    if (outCols <= 0 || outRows <= 0 || outSlices <= 0)
        return loadSeries(series);   // pathological — bail

    // --- Preallocate ITK + VTK buffers (shared index layout) -----------
    auto itkImage = ImageType::New();
    {
        ImageType::SizeType sz;
        sz[0] = outCols; sz[1] = outRows; sz[2] = outSlices;
        ImageType::IndexType idx; idx.Fill(0);
        ImageType::RegionType reg; reg.SetIndex(idx); reg.SetSize(sz);
        itkImage->SetRegions(reg);
        ImageType::SpacingType spacing;
        spacing[0] = sx * strideXY;
        spacing[1] = sy * strideXY;
        spacing[2] = 1.0;       // z spacing fixed below
        itkImage->SetSpacing(spacing);
        ImageType::PointType origin; origin.Fill(0.0);
        itkImage->SetOrigin(origin);
        ImageType::DirectionType dir; dir.SetIdentity();
        itkImage->SetDirection(dir);
        itkImage->Allocate();
    }
    // Z spacing from first/last slice positions when available.
    if (nSlices > 1) {
        double dz = 1.0;
        if (!std::isnan(zs[0]) && !std::isnan(zs[1]))
            dz = std::abs(zs[1] - zs[0]);
        if (dz <= 0) dz = 1.0;
        ImageType::SpacingType spacing = itkImage->GetSpacing();
        spacing[2] = dz * strideZ;
        itkImage->SetSpacing(spacing);
    }

    auto vtkImage = vtkSmartPointer<vtkImageData>::New();
    vtkImage->SetDimensions(outCols, outRows, outSlices);
    const auto sp = itkImage->GetSpacing();
    vtkImage->SetSpacing(sp[0], sp[1], sp[2]);
    vtkImage->SetOrigin(0, 0, 0);
    vtkImage->AllocateScalars(VTK_FLOAT, 1);

    auto vol = std::make_shared<Volume>(
        itkImage, vtkImage,
        // Full header meta from the first file — the library DB meta
        // only carries a subset (patient/date/Se# were blank on the HUD).
        readSeriesHeader(files.front(), series.seriesInstanceUID, files));
    if (downsampled) {
        vol->setDownsampled(true);
        vol->setFullExtent({cols, rows, nSlices});
    } else {
        vol->setFullExtent({outCols, outRows, outSlices});
    }

    float* itkBuf = itkImage->GetBufferPointer();
    float* vtkBuf = static_cast<float*>(vtkImage->GetScalarPointer());
    const size_t outSliceVox = size_t(outRows) * outCols;

    // Fill with a background value: the first slice's minimum (air for
    // CT, near-zero for MR) so unloaded slices render dark.
    float fill = 0.f;
    {
        try {
            auto r = itk::ImageFileReader<itk::Image<float,2>>::New();
            r->SetImageIO(itk::GDCMImageIO::New());
            r->SetFileName(files.front());
            r->Update();
            const auto* p = r->GetOutput()->GetBufferPointer();
            fill = *std::min_element(p, p + size_t(rows) * cols);
        } catch (...) {}
    }
    std::fill(itkBuf, itkBuf + outSliceVox * outSlices, fill);
    std::fill(vtkBuf, vtkBuf + outSliceVox * outSlices, fill);

    if (cb.onProgress)
        cb.onProgress(vol, 0, outSlices);   // volume exists — attach early

    // --- Slice loop ----------------------------------------------------
    for (int k = 0; k < outSlices; ++k) {
        if (cb.shouldCancel && cb.shouldCancel())
            return nullptr;
        const int srcK = k * strideZ;   // z stride: skip slices
        try {
            auto r2 = itk::ImageFileReader<itk::Image<float, 2>>::New();
            r2->SetImageIO(itk::GDCMImageIO::New());
            r2->SetFileName(files[srcK]);
            r2->Update();
            const auto* src = r2->GetOutput();
            const auto rsz = src->GetLargestPossibleRegion().GetSize();
            if (int(rsz[0]) != cols || int(rsz[1]) != rows)
                continue;                // mixed-size series — skip slice
            const float* sp = src->GetBufferPointer();
            if (strideXY == 1) {
                // Fast path — full resolution, straight memcpy.
                std::memcpy(itkBuf + k * outSliceVox, sp,
                            outSliceVox * sizeof(float));
                std::memcpy(vtkBuf + k * outSliceVox, sp,
                            outSliceVox * sizeof(float));
            } else {
                // In-plane stride: copy every strideXY-th pixel.
                float* itkDst = itkBuf + k * outSliceVox;
                float* vtkDst = vtkBuf + k * outSliceVox;
                for (int y = 0; y < outRows; ++y) {
                    const float* row = sp + (y * strideXY) * cols;
                    for (int x = 0; x < outCols; ++x) {
                        const float v = row[x * strideXY];
                        *itkDst++ = v;
                        *vtkDst++ = v;
                    }
                }
            }
        } catch (...) {
            continue;                    // unreadable slice — stays `fill`
        }
        // Notify every few slices — enough for smooth fill without
        // flooding the GUI queue.
        if (cb.onProgress && (k % 4 == 3 || k == outSlices - 1))
            cb.onProgress(vol, k + 1, outSlices);
    }
    return vol;
}

vtkSmartPointer<vtkImageData> DicomLoader::resampleOntoGrid(
    const VolumePtr& src, const VolumePtr& grid)
{
    auto resample =
        itk::ResampleImageFilter<ImageType, ImageType>::New();
    resample->SetInput(src->itkImage());
    resample->SetTransform(
        itk::IdentityTransform<double, 3>::New());
    resample->SetInterpolator(
        itk::LinearInterpolateImageFunction<ImageType, double>::New());
    // Sample the source at the grid volume's physical coordinates —
    // origin/spacing/direction all come from the target image.
    resample->SetOutputParametersFromImage(grid->itkImage());
    resample->SetDefaultPixelValue(0.0);
    resample->Update();

    auto connector = itk::ImageToVTKImageFilter<ImageType>::New();
    connector->SetInput(resample->GetOutput());
    connector->Update();
    auto out = vtkSmartPointer<vtkImageData>::New();
    out->DeepCopy(connector->GetOutput());
    const auto sp = grid->spacing();
    out->SetSpacing(sp[0], sp[1], sp[2]);
    out->SetOrigin(0, 0, 0);   // index space == display space
    return out;
}

VolumePtr DicomLoader::flipVolume(const VolumePtr& vol, unsigned mask)
{
    if (!vol || !mask)
        return vol;

    // Flip the display buffer in index space, then convert back to ITK —
    // the two stay index-aligned by construction (itk::FlipImageFilter
    // proved unstable here on the worker thread).
    vtkSmartPointer<vtkImageData> v = vol->vtkImage();
    auto flipAxis = [](vtkImageData* in, int axis) {
        auto f = vtkSmartPointer<vtkImageFlip>::New();
        f->SetFilteredAxis(axis);
        f->SetInputData(in);
        f->Update();
        auto o = vtkSmartPointer<vtkImageData>::New();
        o->DeepCopy(f->GetOutput());
        return o;
    };
    if (mask & 1) {          // 180° in-plane rotation
        v = flipAxis(v, 0);
        v = flipAxis(v, 1);
    }
    if (mask & 2)            // head ↔ feet
        v = flipAxis(v, 2);

    // ITK side — plain index-space flip, no filters (FlipImageFilter and
    // VTKImageToImageFilter both crashed in this build). Display space is
    // index*spacing, so reversing indices is exactly a display flip.
    auto in  = vol->itkImage();
    auto out = ImageType::New();
    out->SetRegions(in->GetLargestPossibleRegion());
    out->SetSpacing(in->GetSpacing());
    out->SetOrigin(in->GetOrigin());
    out->SetDirection(in->GetDirection());
    out->Allocate();
    const auto size = in->GetLargestPossibleRegion().GetSize();
    const bool fx = (mask & 1), fy = (mask & 1), fz = (mask & 2);
    itk::ImageRegionConstIterator<ImageType> it(
        in, in->GetLargestPossibleRegion());
    for (it.GoToBegin(); !it.IsAtEnd(); ++it) {
        auto idx = it.GetIndex();
        if (fx) idx[0] = size[0] - 1 - idx[0];
        if (fy) idx[1] = size[1] - 1 - idx[1];
        if (fz) idx[2] = size[2] - 1 - idx[2];
        out->SetPixel(idx, it.Get());
    }

    return std::make_shared<Volume>(out, v, vol->meta());
}

} // namespace meda
