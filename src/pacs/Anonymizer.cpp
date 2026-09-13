#include "Anonymizer.h"

#include <dcmtk/dcmdata/dctk.h>
#include <dcmtk/dcmdata/dcmetinf.h>

namespace meda {

namespace {

void clearTag(DcmDataset* ds, const DcmTagKey& k)
{
    ds->putAndInsertString(k, "");
}

void removeTag(DcmDataset* ds, const DcmTagKey& k)
{
    ds->remove(k);
}

} // namespace

bool anonymizeFile(const std::string& in, const std::string& out,
                   const AnonymizeProfile& p, std::string* err)
{
    DcmFileFormat ff;
    OFCondition st = ff.loadFile(in.c_str());
    if (st.bad()) {
        if (err) *err = st.text();
        return false;
    }
    DcmDataset* ds = ff.getDataset();

    if (p.patient) {
        ds->putAndInsertString(DCM_PatientName, p.patientName.c_str());
        ds->putAndInsertString(DCM_PatientID,   p.patientID.c_str());
        clearTag(ds, DCM_PatientBirthDate);
        clearTag(ds, DCM_PatientSex);
        clearTag(ds, DCM_PatientAge);
        removeTag(ds, DCM_PatientAddress);
        removeTag(ds, DCM_PatientTelephoneNumbers);
        removeTag(ds, DCM_OtherPatientIDsSequence);
        removeTag(ds, DCM_OtherPatientNames);
    }
    if (p.dates) {
        clearTag(ds, DCM_StudyDate);
        clearTag(ds, DCM_SeriesDate);
        clearTag(ds, DCM_AcquisitionDate);
        clearTag(ds, DCM_ContentDate);
        clearTag(ds, DCM_StudyTime);
        clearTag(ds, DCM_SeriesTime);
        clearTag(ds, DCM_AcquisitionTime);
        clearTag(ds, DCM_ContentTime);
    }
    if (p.institution) {
        clearTag(ds, DCM_InstitutionName);
        removeTag(ds, DCM_InstitutionAddress);
        clearTag(ds, DCM_ReferringPhysicianName);
        clearTag(ds, DCM_OperatorsName);
        clearTag(ds, DCM_PerformingPhysicianName);
        removeTag(ds, DCM_InstitutionalDepartmentName);
        clearTag(ds, DCM_AccessionNumber);
    }
    if (p.device) {
        clearTag(ds, DCM_StationName);
        removeTag(ds, DCM_DeviceSerialNumber);
    }
    if (p.comments) {
        clearTag(ds, DCM_ImageComments);
        clearTag(ds, DCM_PatientComments);
        clearTag(ds, DCM_AdditionalPatientHistory);
    }

    st = ff.saveFile(out.c_str(), ff.getDataset()->getOriginalXfer());
    if (st.bad()) {
        if (err) *err = st.text();
        return false;
    }
    return true;
}

} // namespace meda
