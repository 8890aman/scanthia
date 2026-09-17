#include "ConfigDock.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

namespace meda {

namespace {

const char* kField =
    "QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox {"
    "  background: #1A1E24; color: #DCE2E9;"
    "  border: 1px solid #2E3540; border-radius: 3px; padding: 4px 6px; }"
    "QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus"
    "  { border-color: #4DA3E8; }";

QFrame* section(const QString& title, QWidget* parent, QVBoxLayout** lay)
{
    auto* f = new QFrame(parent);
    f->setObjectName("CfgSection");
    f->setStyleSheet(
        "QFrame#CfgSection { background: #131519; border-radius: 4px; }");
    auto* v = new QVBoxLayout(f);
    v->setContentsMargins(12, 10, 12, 12);
    v->setSpacing(8);
    auto* t = new QLabel(title, f);
    t->setStyleSheet(
        "color: #4DA3E8; font-size: 10px; font-weight: 600;"
        "letter-spacing: 1.2px;");
    v->addWidget(t);
    *lay = v;
    return f;
}

} // namespace

ConfigDock::ConfigDock(QWidget* parent)
    : QWidget(parent)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* body = new QWidget;
    auto* root = new QVBoxLayout(body);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(10);

    // --- Receiver -----------------------------------------------------
    QVBoxLayout* rl;
    auto* recv = section(tr("RECEIVER (C-STORE SCP)"), body, &rl);
    auto* form1 = new QFormLayout;
    m_aet = new QLineEdit(recv);
    m_aet->setPlaceholderText("SCANTHIA");
    m_port = new QSpinBox(recv);
    m_port->setRange(1, 65535);
    form1->addRow(tr("AE Title"), m_aet);
    form1->addRow(tr("Port"), m_port);
    rl->addLayout(form1);

    // --- DICOMweb ------------------------------------------------------
    QVBoxLayout* wl;
    auto* web = section(tr("DICOMWEB"), body, &wl);
    auto* form2 = new QFormLayout;
    m_webUrl = new QLineEdit(web);
    m_webUrl->setPlaceholderText("http://host:8042/dicom-web");
    form2->addRow(tr("Base URL"), m_webUrl);
    wl->addLayout(form2);

    // --- Viewer --------------------------------------------------------
    QVBoxLayout* vl;
    auto* view = section(tr("VIEWER"), body, &vl);
    auto* form3 = new QFormLayout;
    m_sliceSort = new QComboBox(view);
    m_sliceSort->addItems({tr("Slice Position"), tr("Instance Number"),
                           tr("Acquisition Time"), tr("Filename")});
    m_memBudget = new QDoubleSpinBox(view);
    m_memBudget->setRange(0.5, 32.0);
    m_memBudget->setSingleStep(0.5);
    m_memBudget->setSuffix(" GB");
    m_cineFps = new QSpinBox(view);
    m_cineFps->setRange(2, 60);
    m_openIn = new QComboBox(view);
    m_openIn->addItems({tr("Single view (acquired plane)"),
                        tr("MPR (3 views + 3D)")});
    m_showPlanes = new QCheckBox(tr("Show MPR cursor planes in 3D"), view);
    m_showPlanes->setStyleSheet("color: #DCE2E9; font-size: 11px;");
    m_showScale = new QCheckBox(tr("Show scale ruler (mm)"), view);
    m_showScale->setStyleSheet("color: #DCE2E9; font-size: 11px;");
    form3->addRow(tr("Slice sort"), m_sliceSort);
    form3->addRow(tr("Open series in"), m_openIn);
    form3->addRow(tr("Memory budget"), m_memBudget);
    form3->addRow(tr("Cine FPS"), m_cineFps);
    form3->addRow(QString(), m_showPlanes);
    form3->addRow(QString(), m_showScale);
    vl->addLayout(form3);

    root->addWidget(recv);
    root->addWidget(web);
    root->addWidget(view);
    root->addStretch();

    auto* apply = new QPushButton(tr("Apply"), body);
    apply->setCursor(Qt::PointingHandCursor);
    apply->setStyleSheet(
        "QPushButton { background: #1A1E24; color: #DCE2E9;"
        "  border: 1px solid #2E3540; border-radius: 3px;"
        "  padding: 8px 14px; font-size: 11px; font-weight: 600; }"
        "QPushButton:hover { border-color: #4DA3E8; color: #4DA3E8; }"
        "QPushButton:pressed { background: #2E3540; }");
    root->addWidget(apply);
    connect(apply, &QPushButton::clicked, this, &ConfigDock::applyAll);

    body->setStyleSheet(
        QStringLiteral("QWidget { background: #0B0D10; }"
                       "QLabel { color: #8E99A6; font-size: 11px; }") +
        kField);
    scroll->setWidget(body);
    outer->addWidget(scroll);

    reload();
}

void ConfigDock::reload()
{
    QSettings s;
    m_aet->setText(s.value("scp/aet", "Scanthia").toString());
    m_port->setValue(s.value("scp/port", 11112).toInt());
    m_webUrl->setText(s.value("dicomweb/url").toString());
    m_sliceSort->setCurrentIndex(s.value("viewer/sliceSort", 0).toInt());
    m_openIn->setCurrentIndex(s.value("viewer/openIn", 0).toInt());
    m_memBudget->setValue(
        s.value("viewer/memBudgetGB", 4.0).toDouble());
    m_cineFps->setValue(s.value("viewer/cineFps", 15).toInt());
    m_showPlanes->setChecked(
        s.value("viewer/showPlanes", true).toBool());
    m_showScale->setChecked(
        s.value("viewer/showScale", true).toBool());
}

void ConfigDock::applyAll()
{
    QSettings s;
    s.setValue("scp/aet", m_aet->text().trimmed());
    s.setValue("scp/port", m_port->value());
    s.setValue("dicomweb/url", m_webUrl->text().trimmed());
    s.setValue("viewer/sliceSort", m_sliceSort->currentIndex());
    s.setValue("viewer/openIn", m_openIn->currentIndex());
    s.setValue("viewer/memBudgetGB", m_memBudget->value());
    s.setValue("viewer/cineFps", m_cineFps->value());
    s.setValue("viewer/showPlanes", m_showPlanes->isChecked());
    s.setValue("viewer/showScale", m_showScale->isChecked());
    emit receiverChanged(m_aet->text().trimmed(), m_port->value());
    emit memoryBudgetChanged(
        static_cast<qint64>(m_memBudget->value() * (1 << 30)));
}

} // namespace meda
