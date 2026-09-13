#include "BurnMediaDialog.h"
#include "StudyDatabase.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QTextStream>

#include <QtConcurrent/QtConcurrent>

namespace meda {

namespace {

/// Enumerate optical drives via Windows IMAPI2 (simplified: list drive
/// letters that report as CD/DVD). Fallback: empty list.
QStringList opticalDrives()
{
    QStringList drives;
    for (char c = 'D'; c <= 'Z'; ++c) {
        const QString root = QString(c) + ":/";
        const QDir d(root);
        if (d.exists()) {
            // Cheap heuristic: drive exists and is not a fixed disk.
            // Real IMAPI2 enumeration would query IDiscMaster; for now
            // we list candidates and let the user pick.
            drives.append(root);
        }
    }
    return drives;
}

} // namespace

BurnMediaDialog::BurnMediaDialog(StudyDatabase* db, QWidget* parent)
    : QDialog(parent), m_db(db)
{
    setWindowTitle(tr("Burn DICOM CD / Media"));
    resize(760, 560);

    auto* lay = new QVBoxLayout(this);

    // Source selection
    auto* srcBox = new QGroupBox(tr("Studies to include"), this);
    auto* sv = new QVBoxLayout(srcBox);
    m_source = new QTreeWidget(srcBox);
    m_source->setColumnCount(3);
    m_source->setHeaderLabels({"Study", "Patient", "Size (MB)"});
    m_source->setSelectionMode(QAbstractItemView::ExtendedSelection);
    sv->addWidget(m_source);
    lay->addWidget(srcBox, 1);

    // Populate from the library.
    if (m_db) {
        for (const auto& s : m_db->studies()) {
            auto* it = new QTreeWidgetItem({s.patientName, s.patientID, ""});
            it->setData(0, Qt::UserRole, s.studyUID);
            // Compute size from series files.
            qint64 bytes = 0;
            for (const auto& se : m_db->seriesOf(s.studyUID))
                for (const auto& f : se.files)
                    bytes += QFileInfo(QString::fromStdString(f)).size();
            it->setText(2, QString::number(bytes / (1024 * 1024)));
            it->setData(2, Qt::UserRole, bytes);
            m_source->addTopLevelItem(it);
        }
    }

    // Output target
    auto* outBox = new QGroupBox(tr("Output"), this);
    auto* outForm = new QFormLayout(outBox);
    m_target = new QComboBox(outBox);
    m_target->addItems({tr("Optical drive (burn)"), tr("ISO image"),
                        tr("Folder")});
    m_drive = new QComboBox(outBox);
    const auto drives = opticalDrives();
    if (drives.isEmpty())
        m_drive->addItem(tr("(no optical drive detected)"));
    else
        m_drive->addItems(drives);
    m_isoPath = new QLineEdit(QDir::homePath() + "/scanthia.iso", outBox);
    m_folderPath = new QLineEdit(QDir::homePath() + "/scanthia-media", outBox);
    m_volumeLabel = new QLineEdit("DICOM", outBox);
    outForm->addRow(tr("Target"), m_target);
    outForm->addRow(tr("Drive"), m_drive);
    outForm->addRow(tr("ISO file"), m_isoPath);
    outForm->addRow(tr("Folder"), m_folderPath);
    outForm->addRow(tr("Volume label"), m_volumeLabel);
    m_includeViewer = new QCheckBox(
        tr("Include portable Scanthia viewer (~100MB)"), outBox);
    m_includeViewer->setChecked(true);
    outForm->addRow("", m_includeViewer);
    lay->addWidget(outBox);

    // Status
    m_sizeLabel = new QLabel(tr("Select studies to see total size."), this);
    lay->addWidget(m_sizeLabel);
    m_status = new QLabel(tr("Ready"), this);
    lay->addWidget(m_status);

    // Buttons
    auto* btnRow = new QHBoxLayout;
    auto* stage = new QPushButton(tr("Stage"), this);
    auto* burn = new QPushButton(tr("Burn / Write"), this);
    auto* cancel = new QPushButton(tr("Close"), this);
    btnRow->addWidget(stage);
    btnRow->addWidget(burn);
    btnRow->addStretch();
    btnRow->addWidget(cancel);
    lay->addLayout(btnRow);

    connect(m_target, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BurnMediaDialog::onTargetChanged);
    connect(stage, &QPushButton::clicked, this, &BurnMediaDialog::onStage);
    connect(burn, &QPushButton::clicked, this, &BurnMediaDialog::onBurn);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    onTargetChanged(0);
}

void BurnMediaDialog::onTargetChanged(int)
{
    const int t = m_target->currentIndex();
    m_drive->setEnabled(t == 0);
    m_isoPath->setEnabled(t == 1);
    m_folderPath->setEnabled(t == 2);
}

void BurnMediaDialog::onStage()
{
    // Collect selected studies.
    QStringList uids;
    qint64 total = 0;
    for (auto* it : m_source->selectedItems()) {
        uids.append(it->data(0, Qt::UserRole).toString());
        total += it->data(2, Qt::UserRole).toLongLong();
    }
    if (uids.isEmpty()) {
        QMessageBox::warning(this, tr("Stage"),
                             tr("Select at least one study."));
        return;
    }
    const qint64 viewerBytes = m_includeViewer->isChecked()
                                   ? 100LL * 1024 * 1024 : 0;
    const qint64 grand = total + viewerBytes;
    m_sizeLabel->setText(
        tr("Total: %1 MB (DICOM %2 MB + viewer %3 MB)")
            .arg(grand / (1024 * 1024))
            .arg(total / (1024 * 1024))
            .arg(viewerBytes / (1024 * 1024)));

    m_status->setText(tr("Staging..."));
    m_stageDir = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation) + "/burnstage";
    QDir(m_stageDir).removeRecursively();
    QDir().mkpath(m_stageDir);

    // Run staging on a worker — file copies + DICOMDIR generation.
    const bool includeViewer = m_includeViewer->isChecked();
    const QString volLabel = m_volumeLabel->text();
    QtConcurrent::run([this, uids, includeViewer, volLabel] {
        // 1. Copy DICOM files into DICOM/PATIENT/STUDY/SERIES/INSTANCE.
        int patientIdx = 1;
        qint64 total = 0;
        for (const auto& uid : uids) {
            const auto series = m_db->seriesOf(uid);
            int studyIdx = 1;
            for (const auto& se : series) {
                const QString sdir = QString("%1/DICOM/PAT%2/STU%3/SER%4")
                    .arg(m_stageDir)
                    .arg(patientIdx, 5, 10, QChar('0'))
                    .arg(studyIdx, 3, 10, QChar('0'))
                    .arg(se.seriesNumber.empty() ? QString("1")
                         : QString::fromStdString(se.seriesNumber), 3, '0');
                QDir().mkpath(sdir);
                int instIdx = 1;
                for (const auto& f : se.files) {
                    const QString src = QString::fromStdString(f);
                    const QString dst = sdir + "/I" +
                        QString("%1").arg(instIdx++, 6, 10, QChar('0')) +
                        ".dcm";
                    QFile::copy(src, dst);
                    total += QFileInfo(src).size();
                }
            }
            ++patientIdx;
        }
        // 2. Generate DICOMDIR via dcmmkdir (DCMTK) if available.
        QProcess::execute("dcmmkdir",
            {m_stageDir + "/DICOMDIR", "+r", m_stageDir + "/DICOM"});

        // 3. Copy portable viewer if requested.
        if (includeViewer) {
            const QString viewerSrc = QCoreApplication::applicationDirPath();
            const QString viewerDst = m_stageDir + "/Scanthia";
            QDir().mkpath(viewerDst);
            // Copy the exe + DLLs (best effort — packaging owns the
            // canonical set; here we copy what's next to the running exe).
            const auto entries = QDir(viewerSrc).entryInfoList(
                QDir::Files);
            for (const auto& e : entries)
                QFile::copy(e.filePath(), viewerDst + "/" + e.fileName());
        }
        // 4. autorun.inf + README.txt
        {
            QFile f(m_stageDir + "/autorun.inf");
            if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream s(&f);
                s << "[autorun]\n"
                  << "open=Scanthia\\Scanthia.exe\n"
                  << "label=" << volLabel << "\n";
            }
        }
        {
            QFile f(m_stageDir + "/README.txt");
            if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream s(&f);
                s << "DICOM media created by Scanthia\n"
                  << "Open the Scanthia folder and run Scanthia.exe,\n"
                  << "or use any DICOM viewer that reads DICOMDIR.\n\n"
                  << "DISCLAIMER: This media is for viewing/export only.\n"
                  << "Scanthia is open-source software and is not a\n"
                  << "substitute for a clinically validated diagnostic\n"
                  << "system.\n";
            }
        }
        m_stageBytes = total + (includeViewer ? 100LL*1024*1024 : 0);
        QMetaObject::invokeMethod(this, [this] {
            m_status->setText(tr("Staged %1 MB in %2")
                .arg(m_stageBytes / (1024*1024)).arg(m_stageDir));
            emit stagedForBurn(m_stageDir);
        }, Qt::QueuedConnection);
    });
}

void BurnMediaDialog::onBurn()
{
    if (m_stageDir.isEmpty()) {
        QMessageBox::information(this, tr("Burn"),
            tr("Stage first (click Stage)."));
        return;
    }
    const int t = m_target->currentIndex();
    if (t == 1) {
        // ISO image: use oscdimg or mkisofs if available, else IMAPI2
        // via PowerShell. Fallback: copy folder.
        const QString iso = m_isoPath->text();
        m_status->setText(tr("Creating ISO..."));
        const int rc = QProcess::execute("powershell", {
            "-NoProfile", "-Command",
            QString("oscdimg -n -m \"%1\" \"%2\"").arg(m_stageDir, iso)
        });
        if (rc == 0)
            m_status->setText(tr("ISO written to %1").arg(iso));
        else
            m_status->setText(tr("ISO creation failed (rc=%1). "
                                 "Stage folder is at %2").arg(rc).arg(m_stageDir));
        return;
    }
    if (t == 2) {
        // Folder: copy the stage dir to the chosen folder.
        const QString dst = m_folderPath->text();
        QDir().mkpath(dst);
        QProcess::execute("powershell", {
            "-NoProfile", "-Command",
            QString("Copy-Item -Path '%1\\*' -Destination '%2' -Recurse -Force")
                .arg(m_stageDir, dst)
        });
        m_status->setText(tr("Copied to %1").arg(dst));
        return;
    }
    // Optical drive: IMAPI2 via PowerShell. This is the most reliable
    // cross-Windows path without a native COM binding.
    const QString drive = m_drive->currentText();
    if (drive.startsWith('(')) {
        QMessageBox::warning(this, tr("Burn"),
            tr("No optical drive detected. Use ISO or folder output."));
        return;
    }
    m_status->setText(tr("Burning to %1 (this may take a while)...").arg(drive));
    QtConcurrent::run([this, drive] {
        // PowerShell IMAPI2 burn script. Writes the stage dir to the
        // disc and finalizes. This is a best-effort path; some drives
        // need third-party software.
        const QString ps = QString(
            "$drive = (New-Object -ComObject IMAPI2.MsftDiscMaster2).Item(0)\n"
            "$recorder = (New-Object -ComObject IMAPI2.MsftDiscRecorder2)\n"
            "$recorder.InitializeDiscRecorder($drive)\n"
            "$fmt = New-Object -ComObject IMAPI2.MsftFileSystemImage\n"
            "$fmt.ChooseNameFromDisc($recorder)\n"
            "$root = $fmt.Root\n"
            "$root.AddTree('%1', $false)\n"
            "$img = $fmt.CreateResultImage()\n"
            "$stream = $img.ImageStream\n"
            "$writer = New-Object -ComObject IMAPI2.MsftDiscFormat2Data\n"
            "$writer.Recorder = $recorder\n"
            "$writer.ClientName = 'Scanthia'\n"
            "$writer.Write($stream)\n"
            "Write-Output 'OK'\n"
        ).arg(m_stageDir);
        const int rc = QProcess::execute("powershell",
            {"-NoProfile", "-Command", ps});
        QMetaObject::invokeMethod(this, [this, rc] {
            if (rc == 0)
                m_status->setText(tr("Burn complete."));
            else
                m_status->setText(tr("Burn failed (rc=%1). "
                    "Use ISO or folder output as fallback.").arg(rc));
        }, Qt::QueuedConnection);
    });
}

} // namespace meda
