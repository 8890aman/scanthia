#include "PacsDialog.h"
#include "DicomNodes.h"

#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTableWidget>
#include <QVBoxLayout>

#include <QtConcurrent/QtConcurrent>

namespace meda {

PacsDialog::PacsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("PACS Query / Retrieve"));
    resize(900, 600);

    auto* nodeBox = new QGroupBox(tr("PACS Node"), this);
    auto* nodeForm = new QFormLayout(nodeBox);
    // Saved-node picker — same store the library source combo uses.
    m_nodeCombo = new QComboBox(nodeBox);
    m_nodeCombo->addItem(tr("(custom)"));
    for (const auto& n : DicomNodes().nodes())
        m_nodeCombo->addItem(n.name);
    m_host = new QLineEdit("127.0.0.1", nodeBox);
    m_port = new QSpinBox(nodeBox);
    m_port->setRange(1, 65535);
    m_port->setValue(4242);
    m_calledAET = new QLineEdit("ORTHANC", nodeBox);
    m_callingAET = new QLineEdit("MEDAVIEW", nodeBox);
    m_moveDestAET = new QLineEdit("MEDAVIEW", nodeBox);
    m_moveDestAET->setToolTip(
        tr("AE title the PACS will C-STORE to (this viewer's store SCP)"));
    nodeForm->addRow(tr("Saved Node"), m_nodeCombo);
    nodeForm->addRow(tr("Host"), m_host);
    nodeForm->addRow(tr("Port"), m_port);
    nodeForm->addRow(tr("Called AET"), m_calledAET);
    nodeForm->addRow(tr("Calling AET"), m_callingAET);
    nodeForm->addRow(tr("Move Dest. AET"), m_moveDestAET);
    // Selecting a saved node prefills all fields.
    connect(m_nodeCombo, &QComboBox::currentTextChanged, this,
            [this](const QString& name) {
                if (name == tr("(custom)")) return;
                const auto n = DicomNodes().get(name);
                m_host->setText(QString::fromStdString(n.node.host));
                m_port->setValue(n.node.port);
                m_calledAET->setText(QString::fromStdString(n.node.calledAET));
                m_callingAET->setText(QString::fromStdString(n.node.callingAET));
                if (!n.moveDestAET.isEmpty())
                    m_moveDestAET->setText(n.moveDestAET);
                if (!n.retrieveMethod.isEmpty())
                    m_retrieveMethod->setCurrentText(n.retrieveMethod);
            });

    auto* queryBox = new QGroupBox(tr("Query"), this);
    auto* queryForm = new QFormLayout(queryBox);
    m_patientName = new QLineEdit(queryBox);
    m_patientID = new QLineEdit(queryBox);

    // Date mode: presets that resolve to DICOM date syntax, plus a
    // "Custom" mode that exposes the raw study-date field.
    m_dateMode = new QComboBox(queryBox);
    m_dateMode->addItems({tr("Any date"), tr("Today"), tr("Yesterday"),
                          tr("Last 7 days"), tr("Last 30 days"),
                          tr("Last 90 days"), tr("Custom (below)")});
    m_dateMode->setCurrentIndex(0);
    m_studyDate = new QLineEdit(queryBox);
    m_studyDate->setPlaceholderText("YYYYMMDD or range YYYYMMDD-YYYYMMDD");
    m_studyDate->setEnabled(false);
    connect(m_dateMode, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int i) {
                m_studyDate->setEnabled(i == 6);  // Custom
            });

    m_accession = new QLineEdit(queryBox);

    // Modality dropdown — common modalities plus "Any".
    m_modalityCombo = new QComboBox(queryBox);
    m_modalityCombo->addItems({tr("Any"), "CT", "MR", "US", "XR", "MG",
                               "PT", "NM", "XA", "RF", "OT"});
    m_modalityCombo->setEditable(true);
    m_modalityCombo->setCurrentIndex(0);

    m_studyDescription = new QLineEdit(queryBox);

    queryForm->addRow(tr("Patient Name"), m_patientName);
    queryForm->addRow(tr("Patient ID"), m_patientID);
    queryForm->addRow(tr("Study Date"), m_dateMode);
    queryForm->addRow(tr("  (custom)"), m_studyDate);
    queryForm->addRow(tr("Accession"), m_accession);
    queryForm->addRow(tr("Modality"), m_modalityCombo);
    queryForm->addRow(tr("Study Description"), m_studyDescription);

    m_echoBtn = new QPushButton(tr("C-ECHO"), this);
    m_queryBtn = new QPushButton(tr("Query"), this);
    m_retrieveBtn = new QPushButton(tr("Retrieve Selected"), this);
    m_retrieveBtn->setEnabled(false);
    m_selectAllBtn = new QPushButton(tr("Select All"), this);
    m_clearBtn = new QPushButton(tr("Clear"), this);
    m_retrieveMethod = new QComboBox(this);
    m_retrieveMethod->addItems({"C-MOVE", "C-GET"});
    m_retrieveMethod->setToolTip(
        tr("C-MOVE pushes to our store SCP; C-GET pulls on this association"));

    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(m_echoBtn);
    btnRow->addWidget(m_queryBtn);
    btnRow->addWidget(m_retrieveMethod);
    btnRow->addWidget(m_retrieveBtn);
    btnRow->addStretch();
    btnRow->addWidget(m_selectAllBtn);
    btnRow->addWidget(m_clearBtn);

    m_results = new QTableWidget(this);
    m_results->setColumnCount(7);
    m_results->setHorizontalHeaderLabels(
        {"Patient", "ID", "Study Date", "Accession", "Modalities",
         "Description", "Series"});
    m_results->horizontalHeader()->setStretchLastSection(true);
    m_results->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_results->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_results->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // Status row: text on the left, green progress tape on the right.
    auto* statusRow = new QHBoxLayout;
    m_status = new QLabel(tr("Ready"), this);
    m_progress = new QProgressBar(this);
    m_progress->setMaximumWidth(260);
    m_progress->setMinimumHeight(14);
    m_progress->setTextVisible(true);
    m_progress->setFormat("%v / %m");
    m_progress->setVisible(false);
    statusRow->addWidget(m_status, 1);
    statusRow->addWidget(m_progress);

    auto* top = new QHBoxLayout;
    top->addWidget(nodeBox);
    top->addWidget(queryBox);

    auto* lay = new QVBoxLayout(this);
    lay->addLayout(top);
    lay->addLayout(btnRow);
    lay->addWidget(m_results, 1);
    lay->addLayout(statusRow);

    connect(m_echoBtn, &QPushButton::clicked, this, &PacsDialog::onEcho);
    connect(m_queryBtn, &QPushButton::clicked, this, &PacsDialog::onQuery);
    connect(m_retrieveBtn, &QPushButton::clicked, this,
            &PacsDialog::onRetrieve);
    connect(m_selectAllBtn, &QPushButton::clicked, this, [this] {
        for (int r = 0; r < m_results->rowCount(); ++r)
            if (auto* it = m_results->item(r, 0))
                it->setCheckState(Qt::Checked);
        updateRetrieveButton();
    });
    connect(m_clearBtn, &QPushButton::clicked, this, [this] {
        for (int r = 0; r < m_results->rowCount(); ++r)
            if (auto* it = m_results->item(r, 0))
                it->setCheckState(Qt::Unchecked);
        updateRetrieveButton();
    });
    connect(m_results, &QTableWidget::itemChanged, this,
            [this](QTableWidgetItem* it) {
        if (it && it->column() == 0)
            updateRetrieveButton();
    });
}

PacsNode PacsDialog::currentNode() const
{
    PacsNode n;
    n.host = m_host->text().toStdString();
    n.port = static_cast<uint16_t>(m_port->value());
    n.calledAET = m_calledAET->text().toStdString();
    n.callingAET = m_callingAET->text().toStdString();
    return n;
}

StudyQuery PacsDialog::currentQuery() const
{
    StudyQuery q;
    q.patientName      = m_patientName->text().toStdString();
    q.patientID        = m_patientID->text().toStdString();
    q.accession        = m_accession->text().toStdString();
    q.studyDescription = m_studyDescription->text().toStdString();
    // Modality: "Any" → empty wildcard.
    const QString mod = m_modalityCombo->currentText().trimmed();
    q.modality = (mod == tr("Any") || mod.isEmpty()) ? std::string()
                                                     : mod.toStdString();
    // Date mode → DICOM date token (resolveDateToken in PacsClient
    // expands "today"/"lastNd"). Custom mode passes the raw field.
    switch (m_dateMode->currentIndex()) {
    case 0: q.studyDate.clear();                     break;  // Any
    case 1: q.studyDate = "today";                   break;
    case 2: q.studyDate = "yesterday";               break;
    case 3: q.studyDate = "last7d";                  break;
    case 4: q.studyDate = "last30d";                 break;
    case 5: q.studyDate = "last90d";                 break;
    case 6: q.studyDate = m_studyDate->text().toStdString(); break;
    }
    return q;
}

void PacsDialog::onEcho()
{
    m_status->setText(tr("Pinging %1...").arg(m_host->text()));
    const PacsNode node = currentNode();
    QtConcurrent::run([this, node] {
        std::string err;
        const bool ok = PacsClient().echo(node, &err);
        QMetaObject::invokeMethod(this, [this, ok, err] {
            m_status->setText(ok ? tr("C-ECHO successful")
                                 : tr("C-ECHO failed: %1").arg(err.c_str()));
        }, Qt::QueuedConnection);
    });
}

void PacsDialog::onQuery()
{
    m_status->setText(tr("Querying..."));
    m_results->setRowCount(0);
    const PacsNode node = currentNode();
    const StudyQuery q = currentQuery();

    QtConcurrent::run([this, node, q] {
        std::vector<StudyQueryResult> results;
        std::string err;
        const bool ok = PacsClient().queryStudies(node, q, results, &err);
        QMetaObject::invokeMethod(this, [this, ok, err, results] {
            if (!ok) {
                m_status->setText(tr("Query failed: %1").arg(err.c_str()));
                return;
            }
            m_results->blockSignals(true);
            m_results->setRowCount(static_cast<int>(results.size()));
            int row = 0;
            for (const auto& r : results) {
                auto set = [&](int col, const std::string& s) {
                    m_results->setItem(row, col,
                                       new QTableWidgetItem(s.c_str()));
                };
                set(0, r.patientName);
                set(1, r.patientID);
                set(2, r.studyDate);
                set(3, r.accessionNumber);
                set(4, r.modalitiesInStudy);
                set(5, r.studyDescription);
                set(6, r.numSeries);
                // Checkbox on the Patient cell — tick to include the
                // study in the retrieve set.
                auto* patientItem = m_results->item(row, 0);
                patientItem->setFlags(patientItem->flags() |
                                      Qt::ItemIsUserCheckable);
                patientItem->setCheckState(Qt::Unchecked);
                patientItem->setData(
                    Qt::UserRole, r.studyInstanceUID.c_str());
                ++row;
            }
            m_results->blockSignals(false);
            m_status->setText(tr("%1 studies found").arg(results.size()));
            updateRetrieveButton();
        }, Qt::QueuedConnection);
    });
}

QList<int> PacsDialog::checkedRows() const
{
    QList<int> rows;
    for (int r = 0; r < m_results->rowCount(); ++r)
        if (auto* it = m_results->item(r, 0))
            if (it->checkState() == Qt::Checked)
                rows << r;
    return rows;
}

void PacsDialog::updateRetrieveButton()
{
    const int n = checkedRows().size();
    m_retrieveBtn->setEnabled(n > 0);
    m_retrieveBtn->setText(n > 0
        ? tr("Retrieve Selected (%1)").arg(n)
        : tr("Retrieve Selected"));
}

void PacsDialog::onRetrieve()
{
    // Checked rows are the retrieve set; fall back to the current
    // row when nothing is ticked (keeps single-click workflow).
    QList<int> rows = checkedRows();
    if (rows.isEmpty() && m_results->currentRow() >= 0)
        rows << m_results->currentRow();
    if (rows.isEmpty()) {
        m_status->setText(tr("Select a study first"));
        return;
    }

    QStringList uids;
    for (int r : rows)
        uids << m_results->item(r, 0)->data(Qt::UserRole).toString();

    const bool useGet = m_retrieveMethod->currentText() == "C-GET";
    const PacsNode node = currentNode();
    const QString dest = m_moveDestAET->text();
    const QString baseDir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
        "/downloads";

    m_retrieveBtn->setEnabled(false);
    m_progress->setVisible(true);
    m_progress->setRange(0, 0);   // indeterminate until first response

    QtConcurrent::run(
        [this, node, uids, useGet, dest, baseDir] {
        int done = 0, failed = 0;
        QStringList gotDirs;
        for (int i = 0; i < uids.size(); ++i) {
            const QString& uid = uids[i];
            const QString outDir = baseDir + "/" + uid;
            const int idx = i;
            QMetaObject::invokeMethod(this, [this, idx, n = uids.size()] {
                m_status->setText(
                    tr("Retrieving study %1 / %2 ...").arg(idx + 1).arg(n));
            }, Qt::QueuedConnection);

            std::string err;
            // Forward per-study subop counts to the green bar.
            const PacsClient::RetrieveProgress prog =
                [this](int d, int r) {
                    QMetaObject::invokeMethod(this, [this, d, r] {
                        m_progress->setRange(0, d + r);
                        m_progress->setValue(d);
                    }, Qt::QueuedConnection);
                };
            const bool ok =
                useGet
                    ? PacsClient().retrieveStudyGet(
                          node, uid.toStdString(), outDir.toStdString(),
                          &err, prog)
                    : PacsClient().retrieveStudyMove(
                          node, uid.toStdString(), dest.toStdString(),
                          &err, prog);
            if (ok) {
                ++done;
                gotDirs << outDir;
            } else {
                ++failed;
                QMetaObject::invokeMethod(this,
                    [this, e = QString::fromStdString(err)] {
                        m_status->setText(tr("Failed: %1").arg(e));
                    }, Qt::QueuedConnection);
            }
        }
        QMetaObject::invokeMethod(this,
            [this, done, failed, gotDirs] {
                m_progress->setVisible(false);
                m_retrieveBtn->setEnabled(true);
                m_status->setText(
                    tr("Retrieved %1 stud%2%3")
                        .arg(done)
                        .arg(done == 1 ? "y" : "ies")
                        .arg(failed
                                 ? tr(", %1 failed").arg(failed)
                                 : QString()));
                // Index each retrieved study into the library.
                for (const auto& d : gotDirs)
                    emit studyRetrieved(d);
            }, Qt::QueuedConnection);
    });
}

} // namespace meda
