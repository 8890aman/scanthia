#include "AutoPullManager.h"
#include "AutoPullRuleEditor.h"

#include <QCoreApplication>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace meda {

AutoPullManager::AutoPullManager(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Auto-Pull Rules"));
    resize(900, 480);

    auto* lay = new QVBoxLayout(this);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(7);
    m_table->setHorizontalHeaderLabels(
        {"Enabled", "Name", "Node", "Filters", "Schedule",
         "Last Run", "Status"});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    lay->addWidget(m_table, 1);

    auto* row = new QHBoxLayout;
    auto* add = new QPushButton(tr("Add..."), this);
    auto* edit = new QPushButton(tr("Edit..."), this);
    auto* del = new QPushButton(tr("Delete"), this);
    auto* toggle = new QPushButton(tr("Enable / Disable"), this);
    auto* runNow = new QPushButton(tr("Run Now"), this);
    auto* log = new QPushButton(tr("View Log"), this);
    row->addWidget(add);
    row->addWidget(edit);
    row->addWidget(del);
    row->addWidget(toggle);
    row->addWidget(runNow);
    row->addWidget(log);
    row->addStretch();
    lay->addLayout(row);

    m_status = new QLabel(tr("%1 rule(s)").arg(m_store.rules().size()), this);
    lay->addWidget(m_status);

    connect(add, &QPushButton::clicked, this, &AutoPullManager::onAdd);
    connect(edit, &QPushButton::clicked, this, &AutoPullManager::onEdit);
    connect(del, &QPushButton::clicked, this, &AutoPullManager::onDelete);
    connect(toggle, &QPushButton::clicked, this, &AutoPullManager::onToggleEnabled);
    connect(runNow, &QPushButton::clicked, this, &AutoPullManager::onRunNow);
    connect(log, &QPushButton::clicked, this, &AutoPullManager::onViewLog);

    refresh();
}

void AutoPullManager::refresh()
{
    m_table->setRowCount(0);
    const auto rules = m_store.rules();
    for (const auto& r : rules) {
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        auto set = [&](int col, const QString& s) {
            m_table->setItem(row, col, new QTableWidgetItem(s));
        };
        set(0, r.enabled ? tr("Yes") : tr("No"));
        set(1, r.name);
        set(2, QString::fromStdString(r.node.host) + ":" +
                  QString::number(r.node.port));
        // Filter summary
        QString f;
        if (!r.query.modality.empty()) f += QString::fromStdString(r.query.modality) + " ";
        if (!r.query.studyDate.empty()) f += tr("date:%1 ").arg(QString::fromStdString(r.query.studyDate));
        if (!r.query.patientID.empty()) f += tr("ID:%1 ").arg(QString::fromStdString(r.query.patientID));
        if (f.isEmpty()) f = tr("(any)");
        set(3, f);
        const char* schedName[] = {"Once", "Daily", "Weekdays", "Weekly"};
        set(4, QString(schedName[static_cast<int>(r.schedule)]) +
              " " + r.startTime.toString("yyyy-MM-dd HH:mm"));
        set(5, r.lastRun.toString("yyyy-MM-dd HH:mm"));
        set(6, r.lastStatus.isEmpty() ? tr("Never run") : r.lastStatus);
        m_table->item(row, 0)->setData(Qt::UserRole, r.id);
    }
    m_status->setText(tr("%1 rule(s)").arg(rules.size()));
}

QString AutoPullManager::currentId() const
{
    const int row = m_table->currentRow();
    if (row < 0) return {};
    return m_table->item(row, 0)->data(Qt::UserRole).toString();
}

void AutoPullManager::onAdd()
{
    AutoPullRule r;
    r.name = tr("New rule");
    r.node.callingAET = "SCANTHIA";
    AutoPullRuleEditor dlg(r, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    r = dlg.result();
    m_store.upsert(r);
    m_store.save();
    refresh();
}

void AutoPullManager::onEdit()
{
    const QString id = currentId();
    if (id.isEmpty()) return;
    AutoPullRule r = m_store.get(id);
    AutoPullRuleEditor dlg(r, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    AutoPullRule edited = dlg.result();
    edited.id = id;  // preserve id
    m_store.upsert(edited);
    m_store.save();
    refresh();
}

void AutoPullManager::onDelete()
{
    const QString id = currentId();
    if (id.isEmpty()) return;
    const auto r = m_store.get(id);
    if (QMessageBox::question(this, tr("Delete rule"),
            tr("Delete rule \"%1\"?").arg(r.name)) != QMessageBox::Yes)
        return;
    m_store.remove(id);
    m_store.save();
    refresh();
}

void AutoPullManager::onToggleEnabled()
{
    const QString id = currentId();
    if (id.isEmpty()) return;
    auto r = m_store.get(id);
    r.enabled = !r.enabled;
    m_store.upsert(r);
    m_store.save();
    refresh();
}

void AutoPullManager::onRunNow()
{
    const QString id = currentId();
    if (id.isEmpty()) return;
    // Run the same headless path the scheduler uses, but in-process so
    // the user sees immediate feedback.
    const auto r = m_store.get(id);
    m_status->setText(tr("Running rule \"%1\"...").arg(r.name));
    // The actual retrieval is performed by AutoPullRunner (see main.cpp
    // --autopull). Here we just launch the same binary headless.
    const QString exe = QCoreApplication::applicationFilePath();
    QProcess::startDetached(exe, {"--autopull", id});
    QMessageBox::information(this, tr("Run Now"),
        tr("Rule \"%1\" launched in the background. "
           "Reopen this dialog to see the result.").arg(r.name));
}

void AutoPullManager::onViewLog()
{
    const QString id = currentId();
    if (id.isEmpty()) return;
    const auto lines = m_store.history(id);
    if (lines.isEmpty()) {
        QMessageBox::information(this, tr("Log"), tr("No history yet."));
        return;
    }
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Run History — %1").arg(m_store.get(id).name));
    dlg.resize(700, 400);
    auto* l = new QVBoxLayout(&dlg);
    auto* t = new QTableWidget(&dlg);
    t->setColumnCount(1);
    t->setHorizontalHeaderLabels({"Log"});
    t->horizontalHeader()->setStretchLastSection(true);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setRowCount(lines.size());
    for (int i = 0; i < lines.size(); ++i)
        t->setItem(i, 0, new QTableWidgetItem(lines[i]));
    l->addWidget(t);
    auto* close = new QPushButton(tr("Close"), &dlg);
    l->addWidget(close);
    connect(close, &QPushButton::clicked, &dlg, &QDialog::accept);
    dlg.exec();
}

} // namespace meda
