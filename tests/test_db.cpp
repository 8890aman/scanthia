// StudyDatabase round-trip test: index the synthetic series, verify
// study/series records and thumbnail generation.

#include "StudyDatabase.h"

#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QTemporaryDir>

#include <cassert>
#include <cstdio>

using namespace meda;

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    const QString dataDir =
        argc > 1 ? argv[1] : "tests/data/ct_series";
    if (!QDir(dataDir).exists()) {
        std::puts("no test data — run tests/gen_test_data.py first; skipping");
        return 0;
    }

    QTemporaryDir tmp;
    StudyDatabase db;
    const bool opened = db.open(tmp.path() + "/t.db");
    assert(opened);

    const int n = db.indexDirectory(dataDir);
    assert(n >= 1);

    auto studies = db.studies();
    assert(studies.size() >= 1);
    const auto& st = studies.first();
    assert(st.patientName == "Test^Synthetic");
    assert(st.seriesCount >= 1);

    auto sers = db.seriesOf(st.studyUID);
    assert(sers.size() >= 1);
    const auto& s = sers.first();
    assert(s.instanceCount == 48);
    assert(s.files.size() == 48);
    assert(s.modality == "CT");
    assert(s.hasWindowing);

    // Lookup by series UID returns the same record.
    const auto s2 = db.series(QString::fromStdString(s.seriesInstanceUID));
    assert(s2.seriesInstanceUID == s.seriesInstanceUID);

    // Thumbnail was generated and is a valid PNG.
    const auto png = db.thumbnail(QString::fromStdString(s.seriesInstanceUID));
    assert(!png.isEmpty());
    QImage img;
    const bool imgOk = img.loadFromData(png);
    assert(imgOk);

    std::puts("DB tests passed.");
    return 0;
}
