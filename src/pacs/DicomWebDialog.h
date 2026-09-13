#pragma once

#include "DicomWebClient.h"

#include <QDialog>

class QLineEdit;
class QPushButton;
class QLabel;
class QTreeWidget;
class QTreeWidgetItem;
class QProgressBar;

namespace meda {

/// DICOMweb (QIDO-RS/WADO-RS) query & retrieve dialog — works against
/// Orthanc, dcm4chee, Google Healthcare API, etc.
class DicomWebDialog : public QDialog {
    Q_OBJECT
public:
    explicit DicomWebDialog(QWidget* parent = nullptr);

signals:
    /// Emitted when a series download finishes; path is the directory.
    void seriesRetrieved(const QString& path);

private:
    void onQuery();
    void onExpand(QTreeWidgetItem* item);
    void onRetrieve();

    DicomWebClient* m_client;
    QLineEdit*      m_url;
    QLineEdit*      m_patientName;
    QTreeWidget*    m_tree;
    QLabel*         m_status;
    QProgressBar*   m_progress;
    QPushButton*    m_queryBtn;
    QPushButton*    m_retrieveBtn;
};

} // namespace meda
