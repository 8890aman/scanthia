#include "Thumbnailer.h"

#include <itkExtractImageFilter.h>
#include <itkGDCMImageIO.h>
#include <itkImage.h>
#include <itkImageFileReader.h>
#include <itkImageSeriesReader.h>
#include <itkRescaleIntensityImageFilter.h>

#include <QBuffer>
#include <QImage>

#include <algorithm>

namespace meda {

namespace {

using Image2D = itk::Image<float, 2>;
using Image3D = itk::Image<float, 3>;

QByteArray imageToPng(Image2D::Pointer img, double ww, double wc, int size)
{
    auto region = img->GetLargestPossibleRegion();
    const int w = region.GetSize()[0];
    const int h = region.GetSize()[1];
    QImage qimg(w, h, QImage::Format_Grayscale8);
    const double lo = wc - ww / 2.0;
    const double inv = ww > 0 ? 255.0 / ww : 1.0;
    for (int y = 0; y < h; ++y) {
        uchar* line = qimg.scanLine(y);
        for (int x = 0; x < w; ++x) {
            Image2D::IndexType idx{{x, y}};
            line[x] = static_cast<uchar>(
                std::clamp((img->GetPixel(idx) - lo) * inv, 0.0, 255.0));
        }
    }
    QByteArray png;
    QBuffer buf(&png);
    buf.open(QIODevice::WriteOnly);
    qimg.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation)
        .save(&buf, "PNG");
    return png;
}

} // namespace

QByteArray Thumbnailer::renderPng(const QStringList& files, double ww,
                                  double wc, int size)
{
    if (files.isEmpty())
        return {};

    // Try the middle file as a 2D read first — fast path.
    const QString mid = files[files.size() / 2];
    try {
        auto reader = itk::ImageFileReader<Image2D>::New();
        reader->SetImageIO(itk::GDCMImageIO::New());
        reader->SetFileName(mid.toStdString());
        reader->Update();
        return imageToPng(reader->GetOutput(), ww, wc, size);
    } catch (...) {
        // Fall through to series read (multiframe / per-frame files).
    }

    try {
        std::vector<std::string> paths;
        for (const auto& f : files)
            paths.push_back(f.toStdString());
        auto reader = itk::ImageSeriesReader<Image3D>::New();
        reader->SetImageIO(itk::GDCMImageIO::New());
        reader->SetFileNames(paths);
        reader->Update();
        auto img = reader->GetOutput();

        auto region = img->GetLargestPossibleRegion();
        auto size3 = region.GetSize();
        Image3D::IndexType start{{0, 0, (long)(size3[2] / 2)}};
        Image3D::SizeType sliceSize{{size3[0], size3[1], 0}};
        Image3D::RegionType sliceRegion;
        sliceRegion.SetIndex(start);
        sliceRegion.SetSize(sliceSize);

        auto extract = itk::ExtractImageFilter<Image3D, Image2D>::New();
        extract->SetInput(img);
        extract->SetExtractionRegion(sliceRegion);
        extract->SetDirectionCollapseToSubmatrix();
        extract->Update();
        return imageToPng(extract->GetOutput(), ww, wc, size);
    } catch (...) {
        return {};
    }
}

} // namespace meda
