#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace meda {

struct PacsNode {
    std::string host;
    uint16_t    port = 104;
    std::string calledAET;   // remote AE title
    std::string callingAET;  // our AE title
};

struct StudyQueryResult {
    std::string patientName;
    std::string patientID;
    std::string birthDate;
    std::string studyDate;
    std::string studyTime;
    std::string accessionNumber;
    std::string studyInstanceUID;
    std::string studyDescription;
    std::string modalitiesInStudy;
    std::string numSeries;
};

struct SeriesQueryResult {
    std::string seriesInstanceUID;
    std::string seriesDescription;
    std::string modality;
    std::string seriesNumber;
    std::string numInstances;
};

/// Full study-level query filter. Empty strings act as wildcards.
/// `studyDate` follows DICOM convention: "YYYYMMDD" or range
/// "YYYYMMDD-YYYYMMDD" or relative "today"/"yesterday"/"lastNd" (N days).
struct StudyQuery {
    std::string patientName;
    std::string patientID;
    std::string studyDate;          // raw DICOM date or range
    std::string modality;
    std::string accession;
    std::string studyDescription;
    std::string referringPhysician;
};

/// DICOM networking client: C-ECHO, C-FIND (study/series level), C-MOVE and
/// C-GET study retrieval. Synchronous; call from a worker thread.
class PacsClient {
public:
    bool echo(const PacsNode& node, std::string* err = nullptr);

    /// Study-root C-FIND. Empty strings act as wildcards.
    bool queryStudies(const PacsNode& node,
                      const std::string& patientName,
                      const std::string& patientID,
                      const std::string& studyDate,
                      const std::string& modality,
                      const std::string& accession,
                      std::vector<StudyQueryResult>& out,
                      std::string* err = nullptr);

    /// Extended query with study description and referring physician.
    bool queryStudies(const PacsNode& node,
                      const StudyQuery& q,
                      std::vector<StudyQueryResult>& out,
                      std::string* err = nullptr);

    bool querySeries(const PacsNode& node,
                     const std::string& studyInstanceUID,
                     std::vector<SeriesQueryResult>& out,
                     std::string* err = nullptr);

    /// C-MOVE a study to `moveDestAET` (must be a registered destination on
    /// the PACS, usually this viewer's own store SCP).
    bool retrieveStudyMove(const PacsNode& node,
                           const std::string& studyInstanceUID,
                           const std::string& moveDestAET,
                           std::string* err = nullptr);

    /// C-GET a study; incoming objects are stored to `outDir`.
    bool retrieveStudyGet(const PacsNode& node,
                          const std::string& studyInstanceUID,
                          const std::string& outDir,
                          std::string* err = nullptr);
};

} // namespace meda
