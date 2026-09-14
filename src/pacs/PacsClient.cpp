#include "PacsClient.h"

#include <dcmtk/dcmnet/scu.h>
#include <dcmtk/dcmdata/dcdatset.h>
#include <dcmtk/dcmdata/dcfilefo.h>
#include <dcmtk/dcmdata/dcuid.h>

#include <filesystem>
#include <QDate>
#include <QString>

namespace meda {

namespace {

/// Resolve a human date token to DICOM StudyDate syntax.
///   "today"            -> "20240115"
///   "yesterday"       -> "20240114"
///   "last7d" / "lastNd"-> "20240108-20240115"
///   anything else     -> passed through (already DICOM syntax or empty)
std::string resolveDateToken(const std::string& tok)
{
    if (tok.empty())
        return {};
    const QString t = QString::fromStdString(tok).toLower().trimmed();
    const QDate today = QDate::currentDate();
    if (t == "today")
        return today.toString("yyyyMMdd").toStdString();
    if (t == "yesterday")
        return today.addDays(-1).toString("yyyyMMdd").toStdString();
    if (t.startsWith("last") && t.endsWith("d")) {
        bool ok = false;
        const int n = t.mid(4, t.length() - 5).toInt(&ok);
        if (ok && n > 0) {
            return (today.addDays(-n + 1).toString("yyyyMMdd") + "-" +
                    today.toString("yyyyMMdd")).toStdString();
        }
    }
    return tok;
}

} // namespace

namespace {

/// Storage SOP classes we accept during C-GET / C-MOVE inbound C-STOREs.
const char* kStorageSOPClasses[] = {
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
    UID_XRayRadiofluoroscopicImageStorage,
    UID_SegmentationStorage,
    UID_RTStructureSetStorage,
    UID_RTDoseStorage,
    UID_RTImageStorage,
};

OFString ofstr(const std::string& s) { return OFString(s.c_str()); }

OFList<OFString> defaultXfers()
{
    // Offer compressed syntaxes too — otherwise a PACS holding JPEG2000 /
    // JPEG-LS data cannot send it to us (or we cannot receive it via C-GET).
    OFList<OFString> l;
    l.push_back(UID_LittleEndianExplicitTransferSyntax);
    l.push_back(UID_BigEndianExplicitTransferSyntax);
    l.push_back(UID_LittleEndianImplicitTransferSyntax);
    l.push_back(UID_JPEG2000TransferSyntax);
    l.push_back(UID_JPEG2000LosslessOnlyTransferSyntax);
    l.push_back(UID_JPEGLSLosslessTransferSyntax);
    l.push_back(UID_JPEGLSLossyTransferSyntax);
    l.push_back(UID_JPEGProcess14SV1TransferSyntax);
    l.push_back(UID_JPEGProcess1TransferSyntax);
    l.push_back(UID_JPEGProcess2_4TransferSyntax);
    l.push_back(UID_RLELosslessTransferSyntax);
    return l;
}

std::string getStr(DcmDataset* ds, const DcmTagKey& key)
{
    OFString v;
    if (ds && ds->findAndGetOFString(key, v).good())
        return v.c_str();
    return {};
}

class MedaSCU : public DcmSCU {
public:
    /// Set connection parameters WITHOUT calling initNetwork() yet — DCMTK
    /// builds its presentation-context list during initNetwork(), so all
    /// addPresentationContext() calls must happen first, then initNetwork().
    void configure(const PacsNode& node)
    {
        setAETitle(ofstr(node.callingAET));
        setPeerHostName(ofstr(node.host));
        setPeerAETitle(ofstr(node.calledAET));
        setPeerPort(node.port);
        setDIMSEBlockingMode(DIMSE_BLOCKING);
        setDIMSETimeout(30);
        setACSETimeout(30);
    }

    /// Call after all addPresentationContext() calls.
    bool connect(std::string* err)
    {
        OFCondition cond = initNetwork();
        if (cond.bad()) {
            if (err) *err = cond.text();
            return false;
        }
        return true;
    }

    bool negotiate(std::string* err)
    {
        OFCondition cond = negotiateAssociation();
        if (cond.bad()) {
            if (err) *err = cond.text();
            closeAssociation(DCMSCU_ABORT_ASSOCIATION);
            return false;
        }
        return true;
    }

    void release()
    {
        if (isConnected())
            closeAssociation(DCMSCU_RELEASE_ASSOCIATION);
    }
};

/// MedaSCU that reports sub-operation counts from each pending
/// C-MOVE / C-GET response — the basis for a real progress bar.
class ProgressSCU : public MedaSCU {
public:
    PacsClient::RetrieveProgress onProgress;

    OFCondition handleMOVEResponse(const T_ASC_PresentationContextID presID,
                                   RetrieveResponse* response,
                                   OFBool& waitForNextResponse) override
    {
        if (onProgress && response)
            onProgress(response->m_numberOfCompletedSubops,
                       response->m_numberOfRemainingSubops);
        return DcmSCU::handleMOVEResponse(presID, response,
                                        waitForNextResponse);
    }

    OFCondition handleCGETResponse(const T_ASC_PresentationContextID presID,
                                   RetrieveResponse* response,
                                   OFBool& continueCGETSession) override
    {
        if (onProgress && response)
            onProgress(response->m_numberOfCompletedSubops,
                       response->m_numberOfRemainingSubops);
        return DcmSCU::handleCGETResponse(presID, response,
                                        continueCGETSession);
    }
};

} // namespace

bool PacsClient::echo(const PacsNode& node, std::string* err)
{
    MedaSCU scu;
    scu.configure(node);
    OFCondition pc = scu.addPresentationContext(
        UID_VerificationSOPClass, defaultXfers());
    if (pc.bad()) {
        if (err) *err = std::string("addPresentationContext: ")
                            + pc.text();
        return false;
    }
    if (!scu.connect(err))
        return false;
    if (!scu.negotiate(err))
        return false;
    const T_ASC_PresentationContextID presID =
        scu.findPresentationContextID(UID_VerificationSOPClass, "");
    if (presID == 0) {
        if (err) *err = "No accepted presentation context for "
                        "Verification SOP Class";
        scu.release();
        return false;
    }
    OFCondition cond = scu.sendECHORequest(presID);
    scu.release();
    if (cond.bad()) {
        if (err) *err = cond.text();
        return false;
    }
    return true;
}

bool PacsClient::queryStudies(const PacsNode& node,
                              const std::string& patientName,
                              const std::string& patientID,
                              const std::string& studyDate,
                              const std::string& modality,
                              const std::string& accession,
                              std::vector<StudyQueryResult>& out,
                              std::string* err)
{
    MedaSCU scu;
    scu.configure(node);
    scu.addPresentationContext(
        UID_FINDPatientRootQueryRetrieveInformationModel, defaultXfers());
    if (!scu.connect(err))
        return false;
    if (!scu.negotiate(err))
        return false;

    DcmDataset query;
    query.putAndInsertString(DCM_QueryRetrieveLevel, "STUDY");
    query.putAndInsertString(DCM_PatientName, patientName.c_str());
    query.putAndInsertString(DCM_PatientID, patientID.c_str());
    query.putAndInsertString(DCM_StudyDate, studyDate.c_str());
    query.putAndInsertString(DCM_AccessionNumber, accession.c_str());
    query.putAndInsertString(DCM_ModalitiesInStudy, modality.c_str());
    // Return keys
    query.putAndInsertString(DCM_StudyInstanceUID, "");
    query.putAndInsertString(DCM_StudyDescription, "");
    query.putAndInsertString(DCM_StudyTime, "");
    query.putAndInsertString(DCM_PatientBirthDate, "");
    query.putAndInsertString(DCM_NumberOfStudyRelatedSeries, "");

    OFList<QRResponse*> responses;
    const T_ASC_PresentationContextID presID = scu.findPresentationContextID(
        UID_FINDPatientRootQueryRetrieveInformationModel, "");
    OFCondition cond = scu.sendFINDRequest(presID, &query, &responses);
    scu.release();

    if (cond.bad()) {
        if (err) *err = cond.text();
        return false;
    }
    for (auto* rsp : responses) {
        if (!rsp || !rsp->m_dataset)
            continue;
        StudyQueryResult r;
        r.patientName       = getStr(rsp->m_dataset, DCM_PatientName);
        r.patientID         = getStr(rsp->m_dataset, DCM_PatientID);
        r.birthDate         = getStr(rsp->m_dataset, DCM_PatientBirthDate);
        r.studyDate         = getStr(rsp->m_dataset, DCM_StudyDate);
        r.studyTime         = getStr(rsp->m_dataset, DCM_StudyTime);
        r.accessionNumber   = getStr(rsp->m_dataset, DCM_AccessionNumber);
        r.studyInstanceUID  = getStr(rsp->m_dataset, DCM_StudyInstanceUID);
        r.studyDescription  = getStr(rsp->m_dataset, DCM_StudyDescription);
        r.modalitiesInStudy = getStr(rsp->m_dataset, DCM_ModalitiesInStudy);
        r.numSeries         = getStr(rsp->m_dataset, DCM_NumberOfStudyRelatedSeries);
        out.push_back(std::move(r));
    }
    return true;
}

bool PacsClient::querySeries(const PacsNode& node,
                             const std::string& studyInstanceUID,
                             std::vector<SeriesQueryResult>& out,
                             std::string* err)
{
    MedaSCU scu;
    scu.configure(node);
    scu.addPresentationContext(
        UID_FINDPatientRootQueryRetrieveInformationModel, defaultXfers());
    if (!scu.connect(err))
        return false;
    if (!scu.negotiate(err))
        return false;

    DcmDataset query;
    query.putAndInsertString(DCM_QueryRetrieveLevel, "SERIES");
    query.putAndInsertString(DCM_StudyInstanceUID, studyInstanceUID.c_str());
    query.putAndInsertString(DCM_SeriesInstanceUID, "");
    query.putAndInsertString(DCM_SeriesDescription, "");
    query.putAndInsertString(DCM_Modality, "");
    query.putAndInsertString(DCM_SeriesNumber, "");
    query.putAndInsertString(DCM_NumberOfSeriesRelatedInstances, "");

    OFList<QRResponse*> responses;
    const T_ASC_PresentationContextID presID = scu.findPresentationContextID(
        UID_FINDPatientRootQueryRetrieveInformationModel, "");
    OFCondition cond = scu.sendFINDRequest(presID, &query, &responses);
    scu.release();

    if (cond.bad()) {
        if (err) *err = cond.text();
        return false;
    }
    for (auto* rsp : responses) {
        if (!rsp || !rsp->m_dataset)
            continue;
        SeriesQueryResult r;
        r.seriesInstanceUID = getStr(rsp->m_dataset, DCM_SeriesInstanceUID);
        r.seriesDescription = getStr(rsp->m_dataset, DCM_SeriesDescription);
        r.modality          = getStr(rsp->m_dataset, DCM_Modality);
        r.seriesNumber      = getStr(rsp->m_dataset, DCM_SeriesNumber);
        r.numInstances      = getStr(rsp->m_dataset, DCM_NumberOfSeriesRelatedInstances);
        out.push_back(std::move(r));
    }
    return true;
}

bool PacsClient::retrieveStudyMove(const PacsNode& node,
                                   const std::string& studyInstanceUID,
                                   const std::string& moveDestAET,
                                   std::string* err,
                                   const RetrieveProgress& onProgress)
{
    ProgressSCU scu;
    scu.onProgress = onProgress;
    scu.configure(node);
    scu.addPresentationContext(
        UID_MOVEPatientRootQueryRetrieveInformationModel, defaultXfers());
    if (!scu.connect(err))
        return false;
    if (!scu.negotiate(err))
        return false;

    DcmDataset query;
    query.putAndInsertString(DCM_QueryRetrieveLevel, "STUDY");
    query.putAndInsertString(DCM_StudyInstanceUID, studyInstanceUID.c_str());

    OFList<RetrieveResponse*> responses;
    const T_ASC_PresentationContextID presID = scu.findPresentationContextID(
        UID_MOVEPatientRootQueryRetrieveInformationModel, "");
    OFCondition cond =
        scu.sendMOVERequest(presID, ofstr(moveDestAET), &query, &responses);
    scu.release();

    if (cond.bad()) {
        if (err) *err = cond.text();
        return false;
    }
    return true;
}

bool PacsClient::retrieveStudyGet(const PacsNode& node,
                                  const std::string& studyInstanceUID,
                                  const std::string& outDir,
                                  std::string* err,
                                  const RetrieveProgress& onProgress)
{
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);

    ProgressSCU scu;
    scu.onProgress = onProgress;
    scu.configure(node);
    scu.setStorageDir(ofstr(outDir));
    scu.addPresentationContext(
        UID_GETPatientRootQueryRetrieveInformationModel, defaultXfers());
    // C-GET sub-operations arrive as C-STORE requests over THIS same
    // association, so we must also propose to act as SCP (dual role)
    // for every storage SOP class we're willing to receive.
    for (const char* sop : kStorageSOPClasses)
        scu.addPresentationContext(sop, defaultXfers(), ASC_SC_ROLE_SCUSCP);
    if (!scu.connect(err))
        return false;
    if (!scu.negotiate(err))
        return false;

    DcmDataset query;
    query.putAndInsertString(DCM_QueryRetrieveLevel, "STUDY");
    query.putAndInsertString(DCM_StudyInstanceUID, studyInstanceUID.c_str());

    OFList<RetrieveResponse*> responses;
    const T_ASC_PresentationContextID presID = scu.findPresentationContextID(
        UID_GETPatientRootQueryRetrieveInformationModel, "");
    OFCondition cond = scu.sendCGETRequest(presID, &query, &responses);
    scu.release();

    if (cond.bad()) {
        if (err) *err = cond.text();
        return false;
    }
    // sendCGETRequest() can report overall success even when every
    // sub-operation (C-STORE of an instance) failed — check the final
    // response's counters so callers don't index an empty directory.
    Uint16 completed = 0, failed = 0, warned = 0;
    for (auto* r : responses) {
        if (!r) continue;
        completed = r->m_numberOfCompletedSubops;
        failed    = r->m_numberOfFailedSubops;
        warned    = r->m_numberOfWarningSubops;
    }
    if (completed == 0 && (failed > 0 || warned > 0)) {
        if (err) *err = QString("C-GET: 0 completed, %1 failed, %2 warned "
                                "(check role negotiation / storage SOP "
                                "classes)").arg(failed).arg(warned)
                            .toStdString();
        return false;
    }
    return true;
}

bool PacsClient::queryStudies(const PacsNode& node,
                              const StudyQuery& q,
                              std::vector<StudyQueryResult>& out,
                              std::string* err)
{
    MedaSCU scu;
    scu.configure(node);
    scu.addPresentationContext(
        UID_FINDPatientRootQueryRetrieveInformationModel, defaultXfers());
    if (!scu.connect(err))
        return false;
    if (!scu.negotiate(err))
        return false;

    DcmDataset query;
    query.putAndInsertString(DCM_QueryRetrieveLevel, "STUDY");
    query.putAndInsertString(DCM_PatientName, q.patientName.c_str());
    query.putAndInsertString(DCM_PatientID, q.patientID.c_str());
    query.putAndInsertString(DCM_StudyDate,
                             resolveDateToken(q.studyDate).c_str());
    query.putAndInsertString(DCM_AccessionNumber, q.accession.c_str());
    query.putAndInsertString(DCM_ModalitiesInStudy, q.modality.c_str());
    query.putAndInsertString(DCM_StudyDescription, q.studyDescription.c_str());
    query.putAndInsertString(DCM_ReferringPhysicianName,
                             q.referringPhysician.c_str());
    // Return keys
    query.putAndInsertString(DCM_StudyInstanceUID, "");
    query.putAndInsertString(DCM_StudyTime, "");
    query.putAndInsertString(DCM_PatientBirthDate, "");
    query.putAndInsertString(DCM_NumberOfStudyRelatedSeries, "");

    OFList<QRResponse*> responses;
    const T_ASC_PresentationContextID presID = scu.findPresentationContextID(
        UID_FINDPatientRootQueryRetrieveInformationModel, "");
    OFCondition cond = scu.sendFINDRequest(presID, &query, &responses);
    scu.release();

    if (cond.bad()) {
        if (err) *err = cond.text();
        return false;
    }
    for (auto* rsp : responses) {
        if (!rsp || !rsp->m_dataset)
            continue;
        StudyQueryResult r;
        r.patientName       = getStr(rsp->m_dataset, DCM_PatientName);
        r.patientID         = getStr(rsp->m_dataset, DCM_PatientID);
        r.birthDate         = getStr(rsp->m_dataset, DCM_PatientBirthDate);
        r.studyDate         = getStr(rsp->m_dataset, DCM_StudyDate);
        r.studyTime         = getStr(rsp->m_dataset, DCM_StudyTime);
        r.accessionNumber   = getStr(rsp->m_dataset, DCM_AccessionNumber);
        r.studyInstanceUID  = getStr(rsp->m_dataset, DCM_StudyInstanceUID);
        r.studyDescription  = getStr(rsp->m_dataset, DCM_StudyDescription);
        r.modalitiesInStudy = getStr(rsp->m_dataset, DCM_ModalitiesInStudy);
        r.numSeries         = getStr(rsp->m_dataset, DCM_NumberOfStudyRelatedSeries);
        out.push_back(std::move(r));
    }
    return true;
}

} // namespace meda
