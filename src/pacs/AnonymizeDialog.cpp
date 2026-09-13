#include "AnonymizeDialog.h"
#include "Anonymizer.h"

#include <QCheckBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace meda {

AnonymizeDialog::AnonymizeDialog(const QStringList& files, QWidget* parent)
    : QDialog(parent), m_files(files)
{
    setWindowTitle(tr("Anonymize Export"));
    setMinimumWidth(440);
    auto* lay = new QVBoxLayout(this);

    auto* hdr = new QLabel(
        tr("Anonymize %1 DICOM file(s) — copies are written to the "
           "output folder; originals are never modified.")
            .arg(files.size()), this);
    hdr->setStyleSheet("color: #8E99A6; font-size: 11px;");
    hdr->setWordWrap(true);
    lay->addWidget(hdr);

    auto* box = new QGroupBox(tr("Strip"), this);
    auto* bl = new QVBoxLayout(box);
    m_patient     = new QCheckBox(
        tr("Patient identity (name, ID, DOB, sex, age, address)"), box);
    m_dates       = new QCheckBox(tr("Dates && times"), box);
    m_institution = new QCheckBox(
        tr("Institution, physicians, accession number"), box);
    m_device      = new QCheckBox(
        tr("Device (station name, serial number)"), box);
    m_comments    = new QCheckBox(tr("Comments && history"), box);
    for (auto* c : {m_patient, m_dates, m_institution, m_device,
                    m_comments}) {
        c->setChecked(true);
        bl->addWidget(c);
    }
    lay->addWidget(box);

    auto* outRow = new QHBoxLayout;
    m_outDir = new QLineEdit(this);
    m_outDir->setPlaceholderText(tr("Output folder..."));
    auto* browse = new QPushButton(tr("Browse..."), this);
    outRow->addWidget(new QLabel(tr("Output:"), this));
    outRow->addWidget(m_outDir, 1);
    outRow->addWidget(browse);
    lay->addLayout(outRow);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(
            this, tr("Output Folder"));
        if (!d.isEmpty())
            m_outDir->setText(d);
    });

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, m_files.size());
    m_progress->setValue(0);
    lay->addWidget(m_progress);
    m_status = new QLabel(this);
    m_status->setStyleSheet("color: #8E99A6; font-size: 11px;");
    lay->addWidget(m_status);

    auto* btns = new QHBoxLayout;
    btns->addStretch();
    m_go = new QPushButton(tr("Anonymize"), this);
    m_go->setDefault(true);
    auto* close = new QPushButton(tr("Close"), this);
    btns->addWidget(m_go);
    btns->addWidget(close);
    lay->addLayout(btns);
    connect(m_go, &QPushButton::clicked, this, &AnonymizeDialog::run);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
}

void AnonymizeDialog::run()
{
    const QString dir = m_outDir->text().trimmed();
    if (dir.isEmpty()) {
        m_status->setText(tr("Choose an output folder."));
        return;
    }
    AnonymizeProfile prof;
    prof.patient     = m_patient->isChecked();
    prof.dates       = m_dates->isChecked();
    prof.institution = m_institution->isChecked();
    prof.device      = m_device->isChecked();
    prof.comments    = m_comments->isChecked();

    m_go->setEnabled(false);
    const auto files = m_files;
    auto* progress = m_progress;
    QtConcurrent::run([this, files, dir, prof, progress] {
        int ok = 0, failed = 0;
        for (int i = 0; i < files.size(); ++i) {
            const QString in = files[i];
            const QString out =
                dir + "/" + QFileInfo(in).fileName();
            std::string err;
            if (anonymizeFile(in.toStdString(), out.toStdString(),
                              prof, &err))
                ++ok;
            else
                ++failed;
            QMetaObject::invokeMethod(progress, "setValue",
                                      Qt::QueuedConnection,
                                      Q_ARG(int, i + 1));
        }
        QMetaObject::invokeMethod(this, [this, ok, failed, dir] {
            m_go->setEnabled(true);
            m_status->setText(
                tr("Done — %1 anonymized, %2 failed → %3")
                    .arg(ok).arg(failed).arg(dir));
        }, Qt::QueuedConnection);
    });
}

} // namespace meda
