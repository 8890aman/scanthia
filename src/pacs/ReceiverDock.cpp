#include "ReceiverDock.h"

#include <QHostAddress>
#include <QNetworkInterface>
#include <QFileInfo>
#include <QDateTime>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QFrame>
#include <QPainter>
#include <QtConcurrent>
#include <itkGDCMImageIO.h>
#include <itkMetaDataDictionary.h>
#include <itkMetaDataObject.h>

namespace meda {

namespace rd {

/// A small dot indicator — green when listening, red when not.
class StatusDot : public QWidget {
public:
    explicit StatusDot(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(10, 10);
    }
    void setOn(bool on) {
        m_on = on;
        update();
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(m_on ? QColor(0x2E, 0xCC, 0x71) : QColor(0xE0, 0x4F, 0x4F));
        p.drawEllipse(rect());
    }
private:
    bool m_on = false;
};

} // namespace rd

ReceiverDock::ReceiverDock(QWidget* parent)
    : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // --- Config section ------------------------------------------------
    auto* cfg = new QFrame(this);
    cfg->setObjectName("ReceiverConfig");
    cfg->setStyleSheet(
        "QFrame#ReceiverConfig { background: #131519; }");
    auto* cl = new QVBoxLayout(cfg);
    cl->setContentsMargins(12, 10, 12, 10);
    cl->setSpacing(8);

    // Header row: status dot + "RECEIVER" legend.
    auto* hdr = new QHBoxLayout();
    hdr->setSpacing(6);
    m_statusDot = new rd::StatusDot(cfg);
    m_statusText = new QLabel(tr("Stopped"), cfg);
    m_statusText->setStyleSheet(
        "color: #8E99A6; font-size: 10px; font-weight: 600;"
        "letter-spacing: 0.8px;");
    hdr->addWidget(m_statusDot);
    hdr->addWidget(m_statusText);
    hdr->addStretch();
    auto* hdrLegend = new QLabel(tr("RECEIVER"), cfg);
    hdrLegend->setStyleSheet(
        "color: #4DA3E8; font-size: 10px; font-weight: 600;"
        "letter-spacing: 1.2px;");
    hdr->addWidget(hdrLegend);
    cl->addLayout(hdr);

    // IPs row — show every IPv4 this machine has, so the user knows
    // what to type into the PACS send config.
    m_ipLabel = new QLabel(cfg);
    m_ipLabel->setWordWrap(true);
    m_ipLabel->setStyleSheet(
        "color: #B7BFC7; font-size: 11px; background: #1A1E24;"
        "border: 1px solid #2E3540; border-radius: 3px; padding: 6px 8px;");
    cl->addWidget(m_ipLabel);
    rebuildIps();

    // AE title + port grid.
    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(6);
    auto* aetLbl = new QLabel(tr("AE Title"), cfg);
    aetLbl->setStyleSheet("color: #8E99A6; font-size: 10px;");
    m_aetEdit = new QLineEdit("Scanthia", cfg);
    m_aetEdit->setStyleSheet(
        "QLineEdit { background: #1A1E24; color: #DCE2E9;"
        "  border: 1px solid #2E3540; border-radius: 3px; padding: 4px 6px; }"
        "QLineEdit:focus { border-color: #4DA3E8; }");
    auto* portLbl = new QLabel(tr("Port"), cfg);
    portLbl->setStyleSheet("color: #8E99A6; font-size: 10px;");
    m_portSpin = new QSpinBox(cfg);
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(11112);
    m_portSpin->setStyleSheet(
        "QSpinBox { background: #1A1E24; color: #DCE2E9;"
        "  border: 1px solid #2E3540; border-radius: 3px; padding: 4px 6px; }"
        "QSpinBox:focus { border-color: #4DA3E8; }");
    grid->addWidget(aetLbl, 0, 0);
    grid->addWidget(m_aetEdit, 1, 0);
    grid->addWidget(portLbl, 0, 1);
    grid->addWidget(m_portSpin, 1, 1);
    cl->addLayout(grid);

    // Incoming dir (read-only display).
    auto* dirLbl = new QLabel(tr("Incoming"), cfg);
    dirLbl->setStyleSheet("color: #8E99A6; font-size: 10px;");
    m_dirLabel = new QLabel(cfg);
    m_dirLabel->setWordWrap(true);
    m_dirLabel->setStyleSheet(
        "color: #B7BFC7; font-size: 10px; background: #1A1E24;"
        "border: 1px solid #2E3540; border-radius: 3px; padding: 4px 6px;");
    cl->addWidget(dirLbl);
    cl->addWidget(m_dirLabel);

    // Apply button — restarts the SCP with the new config.
    m_applyBtn = new QPushButton(tr("Apply & Restart"), cfg);
    m_applyBtn->setCursor(Qt::PointingHandCursor);
    m_applyBtn->setStyleSheet(
        "QPushButton { background: #1A1E24; color: #DCE2E9;"
        "  border: 1px solid #2E3540; border-radius: 3px;"
        "  padding: 6px 14px; font-size: 11px; }"
        "QPushButton:hover { border-color: #4DA3E8; color: #4DA3E8; }"
        "QPushButton:pressed { background: #2E3540; }");
    cl->addWidget(m_applyBtn);
    connect(m_applyBtn, &QPushButton::clicked, this, [this] {
        emit applyRequested(m_aetEdit->text().trimmed(),
                            m_portSpin->value());
    });

    root->addWidget(cfg);

    // --- Queue section -------------------------------------------------
    auto* qhdr = new QLabel(tr("  RECEIVE QUEUE"), this);
    qhdr->setStyleSheet(
        "color: #8E99A6; font-size: 10px; font-weight: 600;"
        "letter-spacing: 0.8px; background: #131519;"
        "padding: 8px 4px 4px;");
    root->addWidget(qhdr);

    m_queue = new QTreeWidget(this);
    m_queue->setColumnCount(4);
    m_queue->setHeaderLabels({tr("Time"), tr("Patient"), tr("Modality"), tr("Size")});
    m_queue->setRootIsDecorated(false);
    m_queue->setAlternatingRowColors(true);
    m_queue->setStyleSheet(
        "QTreeWidget { background: #131519; color: #DCE2E9; border: none; }"
        "QTreeWidget::item { padding: 4px 6px; }"
        "QTreeWidget::item:selected { background: #2E3540; }"
        "QHeaderView::section { background: #1A1E24; color: #8E99A6;"
        "  border: none; border-bottom: 1px solid #2E3540;"
        "  padding: 4px 6px; font-size: 10px; }");
    m_queue->header()->setStretchLastSection(false);
    m_queue->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_queue->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_queue->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_queue->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    root->addWidget(m_queue, 1);
}

void ReceiverDock::setConfig(const QString& aet, int port,
                             const QString& inDir)
{
    m_aetEdit->setText(aet);
    m_portSpin->setValue(port);
    m_dirLabel->setText(inDir);
}

void ReceiverDock::setListening(bool on, const QString& detail)
{
    m_statusDot->setOn(on);
    if (on) {
        m_statusText->setText(detail.isEmpty() ? tr("Listening")
                                               : detail);
        m_statusText->setStyleSheet(
            "color: #2ECC71; font-size: 10px; font-weight: 600;"
            "letter-spacing: 0.8px;");
    } else {
        m_statusText->setText(detail.isEmpty() ? tr("Stopped") : detail);
        m_statusText->setStyleSheet(
            "color: #8E99A6; font-size: 10px; font-weight: 600;"
            "letter-spacing: 0.8px;");
    }
}

void ReceiverDock::addReceived(const QString& path)
{
    const QFileInfo fi(path);
    const QString time = QDateTime::currentDateTime()
                             .toString("hh:mm:ss");
    const QString size = QString::number(fi.size() / 1024) + " KB";
    auto* it = new QTreeWidgetItem(m_queue,
        { time, tr("(indexing...)"), QString(), size });
    it->setData(0, Qt::UserRole, path);

    // Read patient/modality from the file header on a worker thread so
    // the SCP thread isn't blocked. Update the row when done.
    QtConcurrent::run([this, path, it]() {
        QString patient, modality;
        try {
            auto io = itk::GDCMImageIO::New();
            io->SetFileName(path.toStdString());
            io->ReadImageInformation();
            const auto& d = io->GetMetaDataDictionary();
            auto get = [&d](const char* k) -> QString {
                std::string v;
                itk::ExposeMetaData<std::string>(d, k, v);
                return QString::fromStdString(v);
            };
            patient  = get("0010|0010");
            modality = get("0008|0060");
        } catch (...) {}
        QMetaObject::invokeMethod(this, [this, it, patient, modality] {
            if (m_queue->topLevelItem(m_queue->indexOfTopLevelItem(it))
                == it) {
                it->setText(1, patient.isEmpty() ? tr("Unknown")
                                                  : patient);
                it->setText(2, modality.isEmpty() ? QString("--")
                                                   : modality);
            }
        }, Qt::QueuedConnection);
    });

    m_queue->scrollToBottom();
}

void ReceiverDock::rebuildIps()
{
    // List each IPv4 with its adapter name so the user can tell
    // LAN (Wi-Fi / Ethernet) from VPN (Twingate / Tailscale) at a glance.
    QStringList lines;
    for (const auto& iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp) ||
            (iface.flags() & QNetworkInterface::IsLoopBack))
            continue;
        for (const auto& entry : iface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol)
                continue;
            lines << tr("%1   —  %2")
                        .arg(entry.ip().toString())
                        .arg(iface.humanReadableName());
        }
    }
    if (lines.isEmpty())
        lines << tr("(no network interfaces)");
    m_ipLabel->setText(tr("Give the PACS one of these IPs:\n%1")
                       .arg(lines.join("\n")));
}

} // namespace meda
