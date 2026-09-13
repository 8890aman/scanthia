// Example Scanthia AI plugin: simple Hounsfield-range threshold segmenter.
// Demonstrates the IAiPlugin interface end-to-end without a neural model.
// A real plugin would load an ONNX model via InferenceEngine and postprocess.

#include "AiPlugin.h"
#include "InferenceEngine.h"

#include <QObject>

#include <itkImageRegionIterator.h>
#include <itkImageToVTKImageFilter.h>

#include <vtkImageData.h>

using namespace meda;

class ThresholdSegPlugin : public QObject, public IAiPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID IAiPlugin_iid)
    Q_INTERFACES(meda::IAiPlugin)
public:
    QString name() const override { return "Threshold Segmenter"; }
    QString description() const override
    {
        return "Demo: segments voxels in a Hounsfield range (default bone)";
    }
    QString version() const override { return "0.1"; }
    QStringList supportedModalities() const override { return {"CT"}; }

    Segmentation run(const VolumePtr& volume, InferenceEngine&) override
    {
        using LabelImage = itk::Image<unsigned char, 3>;
        auto label = LabelImage::New();
        label->SetRegions(volume->itkImage()->GetLargestPossibleRegion());
        label->SetSpacing(volume->itkImage()->GetSpacing());
        label->SetOrigin(volume->itkImage()->GetOrigin());
        label->SetDirection(volume->itkImage()->GetDirection());
        label->Allocate();
        label->FillBuffer(0);

        // Bone > ~300 HU, air < ~-400 HU as two labels.
        itk::ImageRegionConstIterator<ImageType> in(
            volume->itkImage(), volume->itkImage()->GetLargestPossibleRegion());
        itk::ImageRegionIterator<LabelImage> out(
            label, label->GetLargestPossibleRegion());
        for (in.GoToBegin(), out.GoToBegin(); !in.IsAtEnd(); ++in, ++out) {
            const float v = in.Get();
            if (v > 300.0f)       out.Set(1); // dense / bone
            else if (v < -400.0f) out.Set(2); // air
        }

        auto connector = itk::ImageToVTKImageFilter<LabelImage>::New();
        connector->SetInput(label);
        connector->Update();
        auto vtkLabel = vtkSmartPointer<vtkImageData>::New();
        vtkLabel->DeepCopy(connector->GetOutput());

        Segmentation seg;
        seg.labelmap = vtkLabel;
        seg.lut = Segmentation::makeLabelLut(2);
        seg.labelNames = {{1, "Bone"}, {2, "Air"}};
        return seg;
    }
};

#include "ThresholdSegPlugin.moc"
