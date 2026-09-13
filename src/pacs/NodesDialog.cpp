#include "NodesDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <QtConcurrent/QtConcurrent>

namespace meda {

NodesDialog::NodesDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("DICOM Nodes"));
    resize(820, 520);

    auto* lay = new QVBoxLayout(this);

    // Node list
    m_table = new QTableWidget(this);
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels(
        {"Name", "Host", "Port", "Called AE", "Method", "Default"});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    lay->addWidget(m_table, 1);

    // Edit fields
    auto* editBox = new QGroupBox(tr("Node"), this);
    auto* form = new QFormLayout(editBox);
    m_name = new QLineEdit(editBox);
    m_host = new QLineEdit(editBox);
    m_port = new QSpinBox(editBox);
    m_port->setRange(1, 65535);
    m_port->setValue(4242);
    m_calledAET = new QLineEdit(editBox);
    m_callingAET = new QLineEdit(editBox);
    m_moveDestAET = new QLineEdit(editBox);
    m_method = new QComboBox(editBox);
    m_method->addItems({"C-GET", "C-MOVE"});
    m_default = new QCheckBox(tr("Default node"), editBox);
    form->addRow(tr("Name"), m_name);
    form->addRow(tr("Host"), m_host);
    form->addRow(tr("Port"), m_port);
    form->addRow(tr("Called AET"), m_calledAET);
    form->addRow(tr("Calling AET"), m_callingAET);
    form->addRow(tr("Move Dest. AET"), m_moveDestAET);
    form->addRow(tr("Retrieve method"), m_method);
    form->addRow("", m_default);
    lay->addWidget(editBox);

    // Buttons
    auto* row = new QHBoxLayout;
    auto* add = new QPushButton(tr("Add / Update"), this);
    auto* del = new QPushButton(tr("Delete"), this);
    auto* echo = new QPushButton(tr("Echo"), this);
    auto* def = new QPushButton(tr("Set Default"), this);
    auto* close = new QPushButton(tr("Close"), this);
    row->addWidget(add);
    row->addWidget(del);
    row->addWidget(echo);
    row->addWidget(def);
    row->addStretch();
    row->addWidget(close);
    lay->addLayout(row);

    m_status = new QLabel(tr("Ready"), this);
    lay->addWidget(m_status);

    connect(m_table, &QTableWidget::itemSelectionChanged, this,
            &NodesDialog::onSelectionChanged);
    connect(add, &QPushButton::clicked, this, &NodesDialog::onAdd);
    connect(del, &QPushButton::clicked, this, &NodesDialog::onDelete);
    connect(echo, &QPushButton::clicked, this, &NodesDialog::onEcho);
    connect(def, &QPushButton::clicked, this, &NodesDialog::onSetDefault);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);

    refresh();
}

void NodesDialog::refresh()
{
    m_table->setRowCount(0);
    for (const auto& n : m_store.nodes()) {
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        auto set = [&](int col, const QString& s) {
            m_table->setItem(row, col, new QTableWidgetItem(s));
        };
        set(0, n.name);
        set(1, QString::fromStdString(n.node.host));
        set(2, QString::number(n.node.port));
        set(3, QString::fromStdString(n.node.calledAET));
        set(4, n.retrieveMethod);
        set(5, n.isDefault ? tr("Yes") : "");
    }
    m_status->setText(tr("%1 node(s)").arg(m_store.nodes().size()));
}

QString NodesDialog::currentName() const
{
    const int row = m_table->currentRow();
    if (row < 0) return {};
    return m_table->item(row, 0)->text();
}

void NodesDialog::onSelectionChanged()
{
    const QString name = currentName();
    if (name.isEmpty()) return;
    const auto n = m_store.get(name);
    m_name->setText(n.name);
    m_host->setText(QString::fromStdString(n.node.host));
    m_port->setValue(n.node.port);
    m_calledAET->setText(QString::fromStdString(n.node.calledAET));
    m_callingAET->setText(QString::fromStdString(n.node.callingAET));
    m_moveDestAET->setText(n.moveDestAET);
    m_method->setCurrentText(n.retrieveMethod);
    m_default->setChecked(n.isDefault);
}

void NodesDialog::onAdd()
{
    DicomNode n;
    n.name = m_name->text().trimmed();
    if (n.name.isEmpty()) {
        QMessageBox::warning(this, tr("DICOM Nodes"),
                             tr("Name is required."));
        return;
    }
    n.node.host = m_host->text().toStdString();
    n.node.port = static_cast<uint16_t>(m_port->value());
    n.node.calledAET = m_calledAET->text().toStdString();
    n.node.callingAET = m_callingAET->text().toStdString();
    n.moveDestAET = m_moveDestAET->text();
    n.retrieveMethod = m_method->currentText();
    n.isDefault = m_default->isChecked();
    if (n.isDefault) {
        // Only one default — clear others.
        auto list = m_store.nodes();
        for (auto& o : list) o.isDefault = false;
        m_store.setNodes(list);
    }
    m_store.upsert(n);
    m_store.save();
    refresh();
    m_status->setText(tr("Saved \"%1\"").arg(n.name));
}

void NodesDialog::onDelete()
{
    const QString name = currentName();
    if (name.isEmpty()) return;
    if (QMessageBox::question(this, tr("Delete"),
            tr("Delete node \"%1\"?").arg(name)) != QMessageBox::Yes)
        return;
    m_store.remove(name);
    m_store.save();
    refresh();
}

void NodesDialog::onSetDefault()
{
    const QString name = currentName();
    if (name.isEmpty()) return;
    auto list = m_store.nodes();
    for (auto& n : list) n.isDefault = (n.name == name);
    m_store.setNodes(list);
    m_store.save();
    refresh();
}

void NodesDialog::onEcho()
{
    const QString name = currentName();
    if (name.isEmpty()) return;
    const auto n = m_store.get(name);
    m_status->setText(tr("Echoing %1...").arg(name));
    QtConcurrent::run([this, n, name] {
        std::string err;
        const bool ok = PacsClient().echo(n.node, &err);
        QMetaObject::invokeMethod(this, [this, ok, err, name] {
            m_status->setText(
                ok ? tr("%1: C-ECHO OK").arg(name)
                   : tr("%1: C-ECHO failed — %2").arg(name).arg(err.c_str()));
        }, Qt::QueuedConnection);
    });
}

} // namespace meda
