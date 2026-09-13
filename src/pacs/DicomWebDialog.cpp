#include "DicomWebDialog.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace meda {

DicomWebDialog::DicomWebDialog(QWidget* parent)
    : QDialog(parent), m_client(new DicomWebClient)
{
    setWindowTitle(tr("DICOMweb Server"));
    resize(640, 520);

    auto* lay = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    m_url = new QLineEdit("http://localhost:8042/dicom-web", this);
    m_patientName = new QLineEdit(this);
    m_patientName->setPlaceholderText(tr("Patient name (blank = all)"));
    form->addRow(tr("Server"), m_url);
    form->addRow(tr("Patient"), m_patientName);
    lay->addLayout(form);

    auto* btnRow = new QHBoxLayout;
    m_queryBtn = new QPushButton(tr("Query"), this);
    m_retrieveBtn = new QPushButton(tr("Retrieve Series"), this);
    m_retrieveBtn->setEnabled(false);
    btnRow->addWidget(m_queryBtn);
    btnRow->addWidget(m_retrieveBtn);
    btnRow->addStretch();
    lay->addLayout(btnRow);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({tr("Study / Series"), tr("Modality"),
                             tr("Date"), tr("Instances")});
    m_tree->setRootIsDecorated(true);
    lay->addWidget(m_tree, 1);

    m_progress = new QProgressBar(this);
    m_progress->setVisible(false);
    lay->addWidget(m_progress);
    m_status = new QLabel(this);
    lay->addWidget(m_status);

    connect(m_queryBtn, &QPushButton::clicked, this,
            &DicomWebDialog::onQuery);
    connect(m_tree, &QTreeWidget::itemExpanded, this,
            &DicomWebDialog::onExpand);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, [this] {
        auto* it = m_tree->currentItem();
        m_retrieveBtn->setEnabled(it && it->parent());
    });
    connect(m_retrieveBtn, &QPushButton::clicked, this,
            &DicomWebDialog::onRetrieve);
}

void DicomWebDialog::onQuery()
{
    m_client->setBaseUrl(m_url->text());
    m_tree->clear();
    m_status->setText(tr("Querying..."));
    m_queryBtn->setEnabled(false);
    m_client->queryStudies(
        m_patientName->text(),
        [this](const QJsonArray& studies) {
            m_queryBtn->setEnabled(true);
            for (const auto& v : studies) {
                const auto o = v.toObject();
                auto* item = new QTreeWidgetItem(m_tree);
                item->setText(
                    0, QString("%1 — %2")
                           .arg(DicomWebClient::tag(o, "00100010"),
                                DicomWebClient::tag(o, "00081030")));
                item->setText(2, DicomWebClient::tag(o, "00080020"));
                item->setData(0, Qt::UserRole,
                              DicomWebClient::tag(o, "0020000D"));
                // Placeholder child so the expand arrow appears.
                new QTreeWidgetItem(item);
            }
            m_status->setText(tr("%1 studies").arg(studies.size()));
        },
        [this](const QString& err) {
            m_queryBtn->setEnabled(true);
            m_status->setText(err);
        });
}

void DicomWebDialog::onExpand(QTreeWidgetItem* item)
{
    // Lazy-load series the first time a study expands.
    if (item->childCount() == 1 &&
        item->child(0)->data(0, Qt::UserRole).isNull() &&
        item->child(0)->text(0).isEmpty()) {
        delete item->takeChild(0);
        const QString studyUid = item->data(0, Qt::UserRole).toString();
        m_client->querySeries(
            studyUid,
            [this, item](const QJsonArray& series) {
                for (const auto& v : series) {
                    const auto o = v.toObject();
                    auto* s = new QTreeWidgetItem(item);
                    s->setText(0, DicomWebClient::tag(o, "0008103E"));
                    s->setText(1, DicomWebClient::tag(o, "00080060"));
                    s->setText(3, DicomWebClient::tag(o, "00201209"));
                    s->setData(0, Qt::UserRole,
                               DicomWebClient::tag(o, "0020000E"));
                }
            },
            [this](const QString& err) { m_status->setText(err); });
    }
}

void DicomWebDialog::onRetrieve()
{
    auto* item = m_tree->currentItem();
    if (!item || !item->parent())
        return;
    const QString studyUid =
        item->parent()->data(0, Qt::UserRole).toString();
    const QString seriesUid = item->data(0, Qt::UserRole).toString();
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
        "/dicomweb/" + seriesUid;

    m_retrieveBtn->setEnabled(false);
    m_progress->setVisible(true);
    m_status->setText(tr("Downloading..."));
    m_client->retrieveSeries(
        studyUid, seriesUid, dir,
        [this](int done, int total) {
            m_progress->setRange(0, total);
            m_progress->setValue(done);
            m_status->setText(tr("Downloading %1/%2").arg(done).arg(total));
        },
        [this](const QString& d) {
            m_retrieveBtn->setEnabled(true);
            m_progress->setVisible(false);
            m_status->setText(tr("Downloaded — indexing..."));
            emit seriesRetrieved(d);
        },
        [this](const QString& err) {
            m_retrieveBtn->setEnabled(true);
            m_progress->setVisible(false);
            m_status->setText(err);
        });
}

} // namespace meda
