#pragma once

#include "DicomNodes.h"

#include <QDialog>

class QTableWidget;
class QLineEdit;
class QSpinBox;
class QComboBox;
class QCheckBox;
class QLabel;

namespace meda {

/// Manager for saved DICOM nodes (OsiriX "Locations"). Add, edit,
/// delete, echo-test, and set the default node. Stored in
/// %APPDATA%/Scanthia/nodes.json.
class NodesDialog : public QDialog {
    Q_OBJECT
public:
    explicit NodesDialog(QWidget* parent = nullptr);

private slots:
    void onAdd();
    void onDelete();
    void onEcho();
    void onSetDefault();
    void onSelectionChanged();

private:
    void refresh();
    void saveToStore();

    DicomNodes   m_store;
    QTableWidget* m_table;
    QLabel*      m_status;

    // Edit fields
    QLineEdit* m_name;
    QLineEdit* m_host;
    QSpinBox*  m_port;
    QLineEdit* m_calledAET;
    QLineEdit* m_callingAET;
    QLineEdit* m_moveDestAET;
    QComboBox* m_method;
    QCheckBox* m_default;

    QString currentName() const;
};

} // namespace meda
