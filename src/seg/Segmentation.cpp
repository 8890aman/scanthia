#include "Segmentation.h"

#include <itkImageFileReader.h>
#include <itkImageRegionConstIterator.h>
#include <itkImageToVTKImageFilter.h>
#include <itkNearestNeighborInterpolateImageFunction.h>
#include <itkResampleImageFilter.h>
#include <itkCastImageFilter.h>

#include <gdcmReader.h>
#include <gdcmDataSet.h>
#include <gdcmAttribute.h>
#include <gdcmSequenceOfItems.h>
#include <gdcmPixmapReader.h>

#include <cmath>
#include <stdexcept>

namespace meda {

using LabelImage = itk::Image<unsigned char, 3>;

vtkSmartPointer<vtkLookupTable> Segmentation::makeLabelLut(int maxLabel)
{
    auto lut = vtkSmartPointer<vtkLookupTable>::New();
    lut->SetNumberOfTableValues(std::max(2, maxLabel + 1));
    lut->SetRange(0, std::max(1, maxLabel));
    lut->SetTableValue(0, 0, 0, 0, 0); // background transparent
    // Deterministic distinct-ish colors via golden-ratio hue stepping.
    for (int i = 1; i <= maxLabel; ++i) {
        const double h = std::fmod(0.618034 * i, 1.0);
        lut->SetTableValue(i, 0.5 + 0.5 * std::cos(6.28318 * h),
                           0.5 + 0.5 * std::cos(6.28318 * (h + 0.33)),
                           0.5 + 0.5 * std::cos(6.28318 * (h + 0.67)), 1.0);
    }
    lut->Build();
    return lut;
}

Segmentation SegmentationLoader::loadLabelmap(const std::string& path,
                                              const VolumePtr& ref)
{
    using Reader = itk::ImageFileReader<LabelImage>;
    auto reader = Reader::New();
    reader->SetFileName(path);
    reader->Update();
    LabelImage::Pointer label = reader->GetOutput();

    // Resample onto the reference grid.
    auto interp = itk::NearestNeighborInterpolateImageFunction<LabelImage>::New();
    auto resample = itk::ResampleImageFilter<LabelImage, LabelImage>::New();
    resample->SetInput(label);
    resample->SetInterpolator(interp);
    resample->SetReferenceImage(ref->itkImage());
    resample->UseReferenceImageOn();
    resample->Update();
    label = resample->GetOutput();
    label->DisconnectPipeline();

    int maxLabel = 0;
    itk::ImageRegionConstIterator<LabelImage> it(label,
        label->GetLargestPossibleRegion());
    for (it.GoToBegin(); !it.IsAtEnd(); ++it)
        maxLabel = std::max<int>(maxLabel, it.Get());

    auto connector = itk::ImageToVTKImageFilter<LabelImage>::New();
    connector->SetInput(label);
    connector->Update();
    auto vtkLabel = vtkSmartPointer<vtkImageData>::New();
    vtkLabel->DeepCopy(connector->GetOutput());

    Segmentation seg;
    seg.labelmap = vtkLabel;
    seg.lut = Segmentation::makeLabelLut(maxLabel);
    for (int i = 1; i <= maxLabel; ++i)
        seg.labelNames[i] = "Label " + std::to_string(i);
    return seg;
}

Segmentation SegmentationLoader::loadDicomSeg(const std::string& path,
                                              const VolumePtr& ref)
{
    gdcm::Reader reader;
    reader.SetFileName(path.c_str());
    if (!reader.Read())
        throw std::runtime_error("Not a readable DICOM file: " + path);

    const gdcm::File& file = reader.GetFile();
    const gdcm::DataSet& ds = file.GetDataSet();

    // --- geometry of the SEG frames -------------------------------------
    gdcm::Attribute<0x0028, 0x0030> pixelSpacing;
    gdcm::Attribute<0x0020, 0x0037> orientation;
    gdcm::Attribute<0x0028, 0x0010> rowsAttr;
    gdcm::Attribute<0x0028, 0x0011> colsAttr;
    gdcm::Attribute<0x0028, 0x0100> bitsAttr;

    // Shared functional groups hold common spacing/orientation.
    if (ds.FindDataElement(gdcm::Tag(0x5200, 0x9229))) {
        const gdcm::SequenceOfItems* sfg =
            ds.GetDataElement(gdcm::Tag(0x5200, 0x9229)).GetValueAsSQ();
        if (sfg && sfg->GetNumberOfItems() > 0) {
            const gdcm::DataSet& item = sfg->GetItem(1).GetNestedDataSet();
            if (item.FindDataElement(gdcm::Tag(0x0028, 0x9110))) {
                const gdcm::SequenceOfItems* pms =
                    item.GetDataElement(gdcm::Tag(0x0028, 0x9110)).GetValueAsSQ();
                if (pms && pms->GetNumberOfItems() > 0)
                    pixelSpacing.SetFromDataElement(
                        pms->GetItem(1).GetNestedDataSet().GetDataElement(
                            gdcm::Tag(0x0028, 0x0030)));
            }
            if (item.FindDataElement(gdcm::Tag(0x0020, 0x9116))) {
                const gdcm::SequenceOfItems* pos =
                    item.GetDataElement(gdcm::Tag(0x0020, 0x9116)).GetValueAsSQ();
                if (pos && pos->GetNumberOfItems() > 0)
                    orientation.SetFromDataElement(
                        pos->GetItem(1).GetNestedDataSet().GetDataElement(
                            gdcm::Tag(0x0020, 0x0037)));
            }
        }
    }
    rowsAttr.SetFromDataSet(ds);
    colsAttr.SetFromDataSet(ds);
    bitsAttr.SetFromDataSet(ds);

    const int rows = rowsAttr.GetValue();
    const int cols = colsAttr.GetValue();
    const int bits = bitsAttr.GetValue();
    if (rows <= 0 || cols <= 0 || (bits != 1 && bits != 8))
        throw std::runtime_error("Unsupported SEG geometry");

    // --- segment names ---------------------------------------------------
    std::map<int, std::string> names;
    if (ds.FindDataElement(gdcm::Tag(0x0062, 0x0002))) { // SegmentSequence
        const gdcm::SequenceOfItems* segs =
            ds.GetDataElement(gdcm::Tag(0x0062, 0x0002)).GetValueAsSQ();
        if (segs) {
            for (unsigned i = 1; i <= segs->GetNumberOfItems(); ++i) {
                const gdcm::DataSet& s = segs->GetItem(i).GetNestedDataSet();
                gdcm::Attribute<0x0062, 0x0004> num;
                gdcm::Attribute<0x0062, 0x0005> label;
                num.SetFromDataSet(s);
                label.SetFromDataSet(s);
                names[num.GetValue()] = label.GetValue()
                    ? label.GetValue() : ("Segment " + std::to_string(i));
            }
        }
    }

    // --- raw pixel data --------------------------------------------------
    const gdcm::DataElement& pixDE =
        ds.GetDataElement(gdcm::Tag(0x7fe0, 0x0010));
    const gdcm::ByteValue* bv = pixDE.GetByteValue();
    if (!bv)
        throw std::runtime_error("SEG has no inline pixel data");
    std::vector<char> pixBuf(bv->GetLength());
    bv->GetBuffer(pixBuf.data(), bv->GetLength());

    // --- per-frame groups: segment number + plane position ---------------
    if (!ds.FindDataElement(gdcm::Tag(0x5200, 0x9230)))
        throw std::runtime_error("SEG has no PerFrameFunctionalGroupsSequence");
    const gdcm::SequenceOfItems* pfg =
        ds.GetDataElement(gdcm::Tag(0x5200, 0x9230)).GetValueAsSQ();
    const unsigned nFrames = pfg->GetNumberOfItems();

    // Output labelmap on the reference grid.
    auto label = LabelImage::New();
    label->SetRegions(ref->itkImage()->GetLargestPossibleRegion());
    label->SetSpacing(ref->itkImage()->GetSpacing());
    label->SetOrigin(ref->itkImage()->GetOrigin());
    label->SetDirection(ref->itkImage()->GetDirection());
    label->Allocate();
    label->FillBuffer(0);

    int maxLabel = 0;
    const double rowCos[3] = {orientation[0], orientation[1], orientation[2]};
    const double colCos[3] = {orientation[3], orientation[4], orientation[5]};
    const double rowSpacing = pixelSpacing[1]; // DICOM: row then column
    const double colSpacing = pixelSpacing[0];

    for (unsigned f = 1; f <= nFrames; ++f) {
        const gdcm::DataSet& frame = pfg->GetItem(f).GetNestedDataSet();

        int segNum = 0;
        if (frame.FindDataElement(gdcm::Tag(0x0062, 0x000a))) {
            const gdcm::SequenceOfItems* sid =
                frame.GetDataElement(gdcm::Tag(0x0062, 0x000a)).GetValueAsSQ();
            if (sid && sid->GetNumberOfItems() > 0) {
                gdcm::Attribute<0x0062, 0x000b> refSeg;
                refSeg.SetFromDataSet(
                    sid->GetItem(1).GetNestedDataSet());
                segNum = refSeg.GetValue();
            }
        }
        if (segNum == 0)
            continue;

        gdcm::Attribute<0x0020, 0x0032> pos;
        if (frame.FindDataElement(gdcm::Tag(0x0020, 0x9113))) {
            const gdcm::SequenceOfItems* pps =
                frame.GetDataElement(gdcm::Tag(0x0020, 0x9113)).GetValueAsSQ();
            if (pps && pps->GetNumberOfItems() > 0)
                pos.SetFromDataElement(
                    pps->GetItem(1).GetNestedDataSet().GetDataElement(
                        gdcm::Tag(0x0020, 0x0032)));
        }

        // Frame pixel origin in patient space.
        const double o[3] = {pos[0], pos[1], pos[2]};
        const char* frameData = pixBuf.data() +
            (bits == 1 ? (size_t)(f - 1) * rows * cols / 8
                       : (size_t)(f - 1) * rows * cols);

        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                bool on;
                if (bits == 1) {
                    const size_t idx = (size_t)r * cols + c;
                    on = (frameData[idx >> 3] >> (7 - (idx & 7))) & 1;
                } else {
                    on = frameData[(size_t)r * cols + c] != 0;
                }
                if (!on)
                    continue;
                LabelImage::PointType p;
                for (int d = 0; d < 3; ++d)
                    p[d] = o[d] + c * colSpacing * rowCos[d]
                               + r * rowSpacing * colCos[d];
                LabelImage::IndexType idx;
                if (ref->itkImage()->TransformPhysicalPointToIndex(p, idx))
                    label->SetPixel(idx, static_cast<unsigned char>(segNum));
            }
        }
        maxLabel = std::max(maxLabel, segNum);
    }

    auto connector = itk::ImageToVTKImageFilter<LabelImage>::New();
    connector->SetInput(label);
    connector->Update();
    auto vtkLabel = vtkSmartPointer<vtkImageData>::New();
    vtkLabel->DeepCopy(connector->GetOutput());

    Segmentation seg;
    seg.labelmap = vtkLabel;
    seg.lut = Segmentation::makeLabelLut(maxLabel);
    seg.labelNames = std::move(names);
    return seg;
}

} // namespace meda
