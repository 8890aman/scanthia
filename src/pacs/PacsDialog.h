#pragma once

#include "PacsClient.h"

#include <QDialog>

class QLineEdit;
class QSpinBox;
class QTableWidget;
class QPushButton;
class QLabel;
class QComboBox;

namespace meda {

/// Query/retrieve dialog for DICOM PACS nodes.
class PacsDialog : public QDialog {
    Q_OBJECT
public:
    explicit PacsDialog(QWidget* parent = nullptr);

signals:
    /// Emitted when a retrieval finishes; path is the download directory.
    void studyRetrieved(const QString& path);

private:
    void onEcho();
    void onQuery();
    void onRetrieve();
    PacsNode currentNode() const;
    /// Build a StudyQuery from the current dialog filters.
    StudyQuery currentQuery() const;

    QLineEdit*   m_host;
    QSpinBox*    m_port;
    QComboBox*   m_nodeCombo;   // saved-node picker
    QLineEdit*   m_calledAET;
    QLineEdit*   m_callingAET;
    QLineEdit*   m_moveDestAET;
    QLineEdit*   m_patientName;
    QLineEdit*   m_patientID;
    QLineEdit*   m_studyDate;
    QLineEdit*   m_accession;
    QLineEdit*   m_studyDescription;
    QComboBox*   m_dateMode;
    QComboBox*   m_modalityCombo;
    QComboBox*   m_retrieveMethod;
    QTableWidget* m_results;
    QLabel*      m_status;
    QPushButton* m_echoBtn;
    QPushButton* m_queryBtn;
    QPushButton* m_retrieveBtn;
};

} // namespace meda
