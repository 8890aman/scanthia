#pragma once

#include <string>

namespace meda {

/// Which groups of identifying tags to strip. All default on — the
/// conservative profile.
struct AnonymizeProfile {
    bool patient     = true;   // name, ID, DOB, sex, age, address, phone
    bool dates       = true;   // study/series/acq/content dates + times
    bool institution = true;   // institution, physicians, operators, acc#
    bool device      = true;   // station name, device serial
    bool comments    = true;   // image/patient comments, history
    std::string patientName = "ANONYMOUS";
    std::string patientID   = "ANON";
};

/// Copy `in` to `out` with identifying tags stripped/replaced.
/// Keeps the original transfer syntax. Returns false and sets `err`
/// on failure.
bool anonymizeFile(const std::string& in, const std::string& out,
                   const AnonymizeProfile& p, std::string* err = nullptr);

} // namespace meda
