#include "PacsDialog.h"
#include "DicomNodes.h"

#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
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
    m_retrieveBtn = new QPushButton(tr("Retrieve Study"), this);
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

    m_results = new QTableWidget(this);
    m_results->setColumnCount(7);
    m_results->setHorizontalHeaderLabels(
        {"Patient", "ID", "Study Date", "Accession", "Modalities",
         "Description", "Series"});
    m_results->horizontalHeader()->setStretchLastSection(true);
    m_results->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_results->setSelectionMode(QAbstractItemView::SingleSelection);
    m_results->setEditTriggers(QAbstractItemView::NoEditTriggers);

    m_status = new QLabel(tr("Ready"), this);

    auto* top = new QHBoxLayout;
    top->addWidget(nodeBox);
    top->addWidget(queryBox);

    auto* lay = new QVBoxLayout(this);
    lay->addLayout(top);
    lay->addLayout(btnRow);
    lay->addWidget(m_results, 1);
    lay->addWidget(m_status);

    connect(m_echoBtn, &QPushButton::clicked, this, &PacsDialog::onEcho);
    connect(m_queryBtn, &QPushButton::clicked, this, &PacsDialog::onQuery);
    connect(m_retrieveBtn, &QPushButton::clicked, this,
            &PacsDialog::onRetrieve);
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
                m_results->item(row, 0)->setData(
                    Qt::UserRole, r.studyInstanceUID.c_str());
                ++row;
            }
            m_status->setText(tr("%1 studies found").arg(results.size()));
        }, Qt::QueuedConnection);
    });
}

void PacsDialog::onRetrieve()
{
    const int row = m_results->currentRow();
    if (row < 0) {
        m_status->setText(tr("Select a study first"));
        return;
    }
    const QString uid =
        m_results->item(row, 0)->data(Qt::UserRole).toString();
    const bool useGet = m_retrieveMethod->currentText() == "C-GET";
    const PacsNode node = currentNode();
    const QString dest = m_moveDestAET->text();

    const QString outDir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
        "/downloads/" + uid;
    m_status->setText(tr("Retrieving study..."));

    QtConcurrent::run([this, node, uid, useGet, dest, outDir] {
        std::string err;
        const bool ok =
            useGet
                ? PacsClient().retrieveStudyGet(node, uid.toStdString(),
                                                outDir.toStdString(), &err)
                : PacsClient().retrieveStudyMove(node, uid.toStdString(),
                                                 dest.toStdString(), &err);
        QMetaObject::invokeMethod(this, [this, ok, err, outDir] {
            if (!ok) {
                m_status->setText(tr("Retrieve failed: %1").arg(err.c_str()));
                return;
            }
            m_status->setText(tr("Retrieved to %1").arg(outDir));
            emit studyRetrieved(outDir);
        }, Qt::QueuedConnection);
    });
}

} // namespace meda
