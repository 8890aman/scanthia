#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

namespace meda {

/// CPU-side thumbnail generation: reads the middle slice of a DICOM file
/// (or mid-slice of a series), applies window/level, returns a PNG blob.
/// No GL context required.
class Thumbnailer {
public:
    /// files: sorted file list of the series. Picks the middle file for
    /// single-frame series; falls back to a 3D read for multiframe.
    static QByteArray renderPng(const QStringList& files, double windowWidth,
                                double windowCenter, int size = 96);
};

} // namespace meda
