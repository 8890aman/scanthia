#pragma once

#include "Types.h"
#include "Volume.h"

#include <functional>
#include <string>
#include <vector>

namespace meda {

/// Discovers and loads DICOM series from disk. Heavy lifting is done by ITK's
/// GDCM reader; scanning groups files by SeriesInstanceUID.
class DicomLoader {
public:
    /// Scan a directory (recursively) and return one SeriesMeta per series
    /// found. Files belonging to each series are returned sorted in the
    /// order ITK will read them.
    static std::vector<SeriesMeta> scanDirectory(const std::string& dir);

    /// Load a series into memory as a float volume plus a VTK image that
    /// shares the same buffer. `progress` receives values in [0,1].
    /// Throws itk::ExceptionObject / std::runtime_error on failure.
    static VolumePtr loadSeries(const SeriesMeta& series,
                                std::function<void(float)> progress = {});

    /// Load an arbitrary list of DICOM files (must be one series).
    static VolumePtr loadFiles(const std::vector<std::string>& files,
                               std::function<void(float)> progress = {});

    /// Flip a loaded volume in index space: bit0 = x+y (180° in-plane
    /// rotation — the classic "upside-down" fix), bit1 = z (head↔feet).
    /// Both ITK and VTK images are flipped so AI/seg stay index-aligned.
    static VolumePtr flipVolume(const VolumePtr& vol, unsigned mask);

    /// How slice files are ordered before loading. Position (default)
    /// sorts by slice location along the stack axis; InstanceNumber,
    /// AcquisitionTime and Filename sort by that tag.
    enum class SliceSort { Position, InstanceNumber, AcquisitionTime,
                           Filename };
    /// Reorder a series' file list in place per `order`.
    static void sortFiles(std::vector<std::string>& files,
                          SliceSort order);

    /// Streaming load: allocates the full volume up front, then fills it
    /// slice-by-slice so the UI can display/scroll partial data.
    /// Falls back to the normal path when the series can't stream
    /// (non-identity direction, mixed geometry). Returns nullptr when
    /// cancelled.
    struct StreamCallbacks {
        /// Called on the WORKER thread as slices land. `vol` is the
        /// live Volume (vtkImageData fills progressively — call
        /// Modified() + Render on the GUI thread).
        std::function<void(VolumePtr, int loaded, int total)> onProgress;
        /// Return true to abort the load.
        std::function<bool()> shouldCancel;
        /// Max bytes for the in-memory volume (ITK + VTK buffers).
        /// 0 = unlimited. Default 1 GiB when unset — studies larger
        /// than this are downsampled in-plane then in-z to fit.
        size_t memoryBudgetBytes = 1ull << 30;
    };
    static VolumePtr loadSeriesStreaming(const SeriesMeta& series,
                                         const StreamCallbacks& cb);

    /// Resample `src` onto `grid`'s index space (ITK physical geometry —
    /// origins are respected). Returns a vtkImageData sized exactly like
    /// `grid`'s, ready to hand to a viewer's fusion channel.
    static vtkSmartPointer<vtkImageData> resampleOntoGrid(
        const VolumePtr& src, const VolumePtr& grid);

private:
    static SeriesMeta readSeriesHeader(const std::string& firstFile,
                                       const std::string& seriesUID,
                                       std::vector<std::string> files);
};

} // namespace meda
