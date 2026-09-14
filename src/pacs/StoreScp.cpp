#include "StoreScp.h"

#include <dcmtk/dcmnet/dstorscp.h>
#include <dcmtk/dcmdata/dcuid.h>
#include <dcmtk/ofstd/ofstd.h>

#include <atomic>
#include <functional>
#include <cstdio>
#include <QCoreApplication>

namespace meda {

namespace {

const char* kAcceptedSOPClasses[] = {
    UID_CTImageStorage,
    UID_EnhancedCTImageStorage,
    UID_MRImageStorage,
    UID_EnhancedMRImageStorage,
    UID_ComputedRadiographyImageStorage,
    UID_DigitalXRayImageStorageForPresentation,
    UID_DigitalMammographyXRayImageStorageForPresentation,
    UID_UltrasoundImageStorage,
    UID_UltrasoundMultiframeImageStorage,
    UID_SecondaryCaptureImageStorage,
    UID_PositronEmissionTomographyImageStorage,
    UID_NuclearMedicineImageStorage,
    UID_XRayAngiographicImageStorage,
    UID_SegmentationStorage,
    UID_RTStructureSetStorage,
    UID_RTDoseStorage,
    UID_VerificationSOPClass,
};

class MedaStorageSCP : public DcmStorageSCP {
public:
    std::atomic<bool>* running = nullptr;
    /// Called on the SCP thread after each object is written to disk.
    /// The owner forwards it as fileReceived to the GUI.
    std::function<void(const std::string&)> onStored;

    OFBool stopAfterCurrentAssociation() override
    {
        return running && !*running;
    }

    OFBool stopAfterConnectionTimeout() override
    {
        return running && !*running;
    }

    void notifyInstanceStored(const OFString& filename,
                              const OFString& /*sopClassUID*/,
                              const OFString& /*sopInstanceUID*/,
                              DcmDataset* /*dataset*/ = nullptr) const override
    {
        if (onStored)
            onStored(std::string(filename.c_str()));
    }
};

} // namespace

struct StoreScp::Impl {
    MedaStorageSCP scp;
    std::atomic<bool> running{false};
};

StoreScp::StoreScp(QObject* parent)
    : QObject(parent), m_impl(std::make_unique<Impl>())
{
}

StoreScp::~StoreScp()
{
    stop();
}

bool StoreScp::start(uint16_t port, const std::string& aet,
                   const std::string& outputDir)
{
    if (m_impl->running)
        return true;

    auto& scp = m_impl->scp;
    scp.setPort(port);
    scp.setAETitle(aet.c_str());
    if (scp.setOutputDirectory(outputDir.c_str()).bad())
        return false;
    scp.setFilenameGenerationMode(DcmStorageSCP::FGM_SOPInstanceUID);
    scp.setFilenameExtension(".dcm");
    scp.setDatasetStorageMode(DcmStorageSCP::DGM_StoreBitPreserving);
    scp.setEnableVerification();
    // Accept any called AE title — the PACS admin shouldn't have to
    // match our case ("scanthia" vs "Scanthia" both work).
    scp.setRespondWithCalledAETitle(OFTrue);
    // Non-blocking accept + short timeout so stop() can interrupt listen().
    scp.setConnectionBlockingMode(DUL_NOBLOCK);
    scp.setConnectionTimeout(1);
    scp.running = &m_impl->running;
    // Forward each stored instance to the GUI thread as fileReceived.
    scp.onStored = [this](const std::string& p) {
        emit fileReceived(QString::fromStdString(p));
    };

    OFList<OFString> xfers;
    xfers.push_back(UID_LittleEndianExplicitTransferSyntax);
    xfers.push_back(UID_LittleEndianImplicitTransferSyntax);
    xfers.push_back(UID_BigEndianExplicitTransferSyntax);
    xfers.push_back(UID_JPEG2000TransferSyntax);
    xfers.push_back(UID_JPEG2000LosslessOnlyTransferSyntax);
    xfers.push_back(UID_JPEGLSLosslessTransferSyntax);
    xfers.push_back(UID_JPEGLSLossyTransferSyntax);
    xfers.push_back(UID_JPEGProcess14SV1TransferSyntax);
    xfers.push_back(UID_JPEGProcess1TransferSyntax);
    xfers.push_back(UID_RLELosslessTransferSyntax);
    for (const char* sop : kAcceptedSOPClasses) {
        const auto r = scp.addPresentationContext(sop, xfers);
        if (r.bad()) {
            std::fprintf(stderr, "StoreScp: addPresentationContext failed for %s: %s\n",
                         sop, r.text());
            return false;
        }
    }
    // DCMTK 3.7.0's DcmStorageSCP requires a config-file-based profile.
    // Look beside the executable first (bundled for distribution), then
    // fall back to the MSYS2 system path (dev environment).
    {
        QString cfgCandidates[] = {
            QCoreApplication::applicationDirPath() + "/storescp.cfg",
            "C:/msys64/ucrt64/etc/storescp.cfg",
            QString()
        };
        bool loaded = false;
        for (int i = 0; !cfgCandidates[i].isNull() && !loaded; ++i) {
            const auto r = scp.loadAssociationCfgFile(
                OFString(cfgCandidates[i].toUtf8().constData()));
            if (r.good()) {
                const auto pr = scp.setAndCheckAssociationProfile("Default");
                if (pr.good()) {
                    loaded = true;
                    std::fprintf(stderr,
                        "StoreScp: loaded %s\n",
                        cfgCandidates[i].toUtf8().constData());
                }
            }
        }
        if (!loaded) {
            std::fprintf(stderr, "StoreScp: no valid config file found\n");
            return false;
        }
    }

    m_port = port;
    m_aet = aet;
    m_outputDir = outputDir;
    m_impl->running = true;

    m_thread = QThread::create([this] {
        // listen() blocks until stopAfterCurrentAssociation/forceTermination.
        m_impl->scp.listen();
        m_impl->running = false;
        emit stopped();
    });
    m_thread->start();
    return true;
}

void StoreScp::stop()
{
    if (!m_thread)
        return;
    m_impl->running = false;
    m_thread->wait(5000);
    m_thread->deleteLater();
    m_thread = nullptr;
}

bool StoreScp::isRunning() const
{
    return m_impl->running;
}

bool StoreScp::restart(uint16_t port, const std::string& aet,
                       const std::string& outputDir)
{
    stop();
    // New Impl each restart — DCMTK's DcmStorageSCP keeps presentation
    // contexts across clear(), so a fresh object avoids duplicates.
    m_impl = std::make_unique<Impl>();
    return start(port, aet, outputDir);
}

} // namespace meda
