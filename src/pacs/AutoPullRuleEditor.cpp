#include "AutoPullRuleEditor.h"
#include "DicomNodes.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <QtConcurrent/QtConcurrent>

namespace meda {

AutoPullRuleEditor::AutoPullRuleEditor(const AutoPullRule& rule, QWidget* parent)
    : QDialog(parent), m_rule(rule)
{
    setWindowTitle(tr("Auto-Pull Rule Editor"));
    resize(880, 640);

    auto* lay = new QVBoxLayout(this);

    // Name
    auto* nameBox = new QGroupBox(tr("Rule"), this);
    auto* nameForm = new QFormLayout(nameBox);
    m_name = new QLineEdit(rule.name, nameBox);
    nameForm->addRow(tr("Name"), m_name);
    lay->addWidget(nameBox);

    // Node
    auto* nodeBox = new QGroupBox(tr("PACS Node"), this);
    auto* nodeForm = new QFormLayout(nodeBox);
    m_nodeCombo = new QComboBox(nodeBox);
    m_nodeCombo->addItem(tr("(custom)"));
    for (const auto& n : DicomNodes().nodes())
        m_nodeCombo->addItem(n.name);
    nodeForm->addRow(tr("Saved Node"), m_nodeCombo);
    m_host = new QLineEdit(QString::fromStdString(rule.node.host), nodeBox);
    m_host->setPlaceholderText("127.0.0.1");
    m_port = new QSpinBox(nodeBox);
    m_port->setRange(1, 65535);
    m_port->setValue(rule.node.port ? rule.node.port : 4242);
    m_calledAET = new QLineEdit(QString::fromStdString(rule.node.calledAET), nodeBox);
    m_calledAET->setPlaceholderText("ORTHANC");
    m_callingAET = new QLineEdit(QString::fromStdString(rule.node.callingAET), nodeBox);
    m_callingAET->setPlaceholderText("SCANTHIA");
    m_moveDestAET = new QLineEdit(rule.moveDestAET, nodeBox);
    m_moveDestAET->setPlaceholderText("SCANTHIA");
    nodeForm->addRow(tr("Host"), m_host);
    nodeForm->addRow(tr("Port"), m_port);
    nodeForm->addRow(tr("Called AET"), m_calledAET);
    nodeForm->addRow(tr("Calling AET"), m_callingAET);
    nodeForm->addRow(tr("Move Dest. AET"), m_moveDestAET);
    m_echoBtn = new QPushButton(tr("C-ECHO"), nodeBox);
    nodeForm->addRow("", m_echoBtn);
    // Selecting a saved node prefills all node fields.
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
    lay->addWidget(nodeBox);

    // Query
    auto* queryBox = new QGroupBox(tr("Query Filters"), this);
    auto* queryForm = new QFormLayout(queryBox);
    m_patientName = new QLineEdit(QString::fromStdString(rule.query.patientName), queryBox);
    m_patientID = new QLineEdit(QString::fromStdString(rule.query.patientID), queryBox);
    m_accession = new QLineEdit(QString::fromStdString(rule.query.accession), queryBox);
    m_studyDescription = new QLineEdit(QString::fromStdString(rule.query.studyDescription), queryBox);

    m_dateMode = new QComboBox(queryBox);
    m_dateMode->addItems({tr("Any date"), tr("Today"), tr("Yesterday"),
                          tr("Last 7 days"), tr("Last 30 days"),
                          tr("Last 90 days"), tr("Custom (below)")});
    m_studyDate = new QLineEdit(QString::fromStdString(rule.query.studyDate), queryBox);
    m_studyDate->setPlaceholderText("YYYYMMDD or range YYYYMMDD-YYYYMMDD");
    m_studyDate->setEnabled(false);
    connect(m_dateMode, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int i) { m_studyDate->setEnabled(i == 6); });

    m_modalityCombo = new QComboBox(queryBox);
    m_modalityCombo->addItems({tr("Any"), "CT", "MR", "US", "XR", "MG",
                               "PT", "NM", "XA", "RF", "OT"});
    m_modalityCombo->setEditable(true);
    // Restore modality selection.
    const QString mod = QString::fromStdString(rule.query.modality);
    if (mod.isEmpty())
        m_modalityCombo->setCurrentIndex(0);
    else {
        const int i = m_modalityCombo->findText(mod);
        if (i >= 0) m_modalityCombo->setCurrentIndex(i);
        else m_modalityCombo->setEditText(mod);
    }

    queryForm->addRow(tr("Patient Name"), m_patientName);
    queryForm->addRow(tr("Patient ID"), m_patientID);
    queryForm->addRow(tr("Study Date"), m_dateMode);
    queryForm->addRow(tr("  (custom)"), m_studyDate);
    queryForm->addRow(tr("Accession"), m_accession);
    queryForm->addRow(tr("Modality"), m_modalityCombo);
    queryForm->addRow(tr("Study Description"), m_studyDescription);
    lay->addWidget(queryBox);

    // Limits
    auto* limBox = new QGroupBox(tr("Limits"), this);
    auto* limForm = new QFormLayout(limBox);
    m_maxStudies = new QSpinBox(limBox);
    m_maxStudies->setRange(1, 500);
    m_maxStudies->setValue(rule.maxStudies ? rule.maxStudies : 10);
    m_skipDuplicates = new QCheckBox(
        tr("Skip studies already in local library"), limBox);
    m_skipDuplicates->setChecked(rule.skipDuplicates);
    m_retrieveMethod = new QComboBox(limBox);
    m_retrieveMethod->addItems({"C-GET", "C-MOVE"});
    m_retrieveMethod->setCurrentText(
        rule.retrieveMethod.isEmpty() ? "C-GET" : rule.retrieveMethod);
    limForm->addRow(tr("Max studies to retrieve"), m_maxStudies);
    limForm->addRow(tr("Retrieve method"), m_retrieveMethod);
    limForm->addRow("", m_skipDuplicates);
    lay->addWidget(limBox);

    // Schedule
    auto* schedBox = new QGroupBox(tr("Schedule"), this);
    auto* schedForm = new QFormLayout(schedBox);
    m_schedule = new QComboBox(schedBox);
    m_schedule->addItems({tr("Run once"), tr("Daily"), tr("Weekdays"),
                          tr("Weekly (choose days)")});
    m_schedule->setCurrentIndex(static_cast<int>(rule.schedule));
    m_startTime = new QDateTimeEdit(
        rule.startTime.isValid() ? rule.startTime
                                  : QDateTime::currentDateTime().addSecs(3600),
        schedBox);
    m_startTime->setDisplayFormat("yyyy-MM-dd HH:mm");
    m_startTime->setCalendarPopup(true);
    schedForm->addRow(tr("Schedule"), m_schedule);
    schedForm->addRow(tr("Start / time"), m_startTime);
    lay->addWidget(schedBox);

    // Preview matches
    auto* previewBox = new QGroupBox(tr("Preview Matches"), this);
    auto* pv = new QVBoxLayout(previewBox);
    auto* pvRow = new QHBoxLayout;
    m_previewBtn = new QPushButton(tr("Preview Matches"), previewBox);
    m_echoBtn = new QPushButton(tr("Echo"), previewBox);
    pvRow->addWidget(m_previewBtn);
    pvRow->addWidget(m_echoBtn);
    pvRow->addStretch();
    m_previewStatus = new QLabel(tr("Not queried yet"), previewBox);
    pvRow->addWidget(m_previewStatus);
    pv->addLayout(pvRow);
    m_preview = new QTableWidget(previewBox);
    m_preview->setColumnCount(6);
    m_preview->setHorizontalHeaderLabels(
        {"Patient", "ID", "Study Date", "Accession",
         "Modalities", "Description"});
    m_preview->horizontalHeader()->setStretchLastSection(true);
    m_preview->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_preview->setSelectionBehavior(QAbstractItemView::SelectRows);
    pv->addWidget(m_preview, 1);
    lay->addWidget(previewBox, 1);

    // Buttons
    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    auto* ok = new QPushButton(tr("Save Rule"), this);
    auto* cancel = new QPushButton(tr("Cancel"), this);
    btnRow->addWidget(ok);
    btnRow->addWidget(cancel);
    lay->addLayout(btnRow);

    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_previewBtn, &QPushButton::clicked, this, &AutoPullRuleEditor::onPreview);
    connect(m_echoBtn, &QPushButton::clicked, this, &AutoPullRuleEditor::onEcho);
}

AutoPullRule AutoPullRuleEditor::result() const
{
    AutoPullRule r = m_rule;
    r.name = m_name->text().trimmed();
    r.node.host = m_host->text().toStdString();
    r.node.port = static_cast<uint16_t>(m_port->value());
    r.node.calledAET = m_calledAET->text().toStdString();
    r.node.callingAET = m_callingAET->text().toStdString();
    r.moveDestAET = m_moveDestAET->text();

    r.query.patientName = m_patientName->text().toStdString();
    r.query.patientID = m_patientID->text().toStdString();
    r.query.accession = m_accession->text().toStdString();
    r.query.studyDescription = m_studyDescription->text().toStdString();
    const QString mod = m_modalityCombo->currentText().trimmed();
    r.query.modality = (mod == tr("Any") || mod.isEmpty())
                           ? std::string() : mod.toStdString();
    switch (m_dateMode->currentIndex()) {
    case 0: r.query.studyDate.clear(); break;
    case 1: r.query.studyDate = "today"; break;
    case 2: r.query.studyDate = "yesterday"; break;
    case 3: r.query.studyDate = "last7d"; break;
    case 4: r.query.studyDate = "last30d"; break;
    case 5: r.query.studyDate = "last90d"; break;
    case 6: r.query.studyDate = m_studyDate->text().toStdString(); break;
    }

    r.maxStudies = m_maxStudies->value();
    r.skipDuplicates = m_skipDuplicates->isChecked();
    r.retrieveMethod = m_retrieveMethod->currentText();

    r.schedule = static_cast<AutoPullRule::Schedule>(m_schedule->currentIndex());
    r.startTime = m_startTime->dateTime();
    return r;
}

void AutoPullRuleEditor::onEcho()
{
    m_previewStatus->setText(tr("Echoing..."));
    PacsNode n;
    n.host = m_host->text().toStdString();
    n.port = static_cast<uint16_t>(m_port->value());
    n.calledAET = m_calledAET->text().toStdString();
    n.callingAET = m_callingAET->text().toStdString();
    QtConcurrent::run([this, n] {
        std::string err;
        const bool ok = PacsClient().echo(n, &err);
        QMetaObject::invokeMethod(this, [this, ok, err] {
            m_previewStatus->setText(
                ok ? tr("C-ECHO OK") : tr("C-ECHO failed: %1").arg(err.c_str()));
        }, Qt::QueuedConnection);
    });
}

void AutoPullRuleEditor::onPreview()
{
    m_previewStatus->setText(tr("Querying..."));
    m_preview->setRowCount(0);
    const PacsNode node = result().node;
    const StudyQuery q = result().query;
    QtConcurrent::run([this, node, q] {
        std::vector<StudyQueryResult> results;
        std::string err;
        const bool ok = PacsClient().queryStudies(node, q, results, &err);
        QMetaObject::invokeMethod(this, [this, ok, err, results] {
            if (!ok) {
                m_previewStatus->setText(
                    tr("Query failed: %1").arg(err.c_str()));
                return;
            }
            m_preview->setRowCount(static_cast<int>(results.size()));
            int row = 0;
            for (const auto& r : results) {
                auto set = [&](int col, const std::string& s) {
                    m_preview->setItem(row, col, new QTableWidgetItem(s.c_str()));
                };
                set(0, r.patientName);
                set(1, r.patientID);
                set(2, r.studyDate);
                set(3, r.accessionNumber);
                set(4, r.modalitiesInStudy);
                set(5, r.studyDescription);
                ++row;
            }
            m_previewStatus->setText(
                tr("%1 studies match (will pull at most %2)")
                    .arg(results.size()).arg(m_maxStudies->value()));
        }, Qt::QueuedConnection);
    });
}

} // namespace meda
