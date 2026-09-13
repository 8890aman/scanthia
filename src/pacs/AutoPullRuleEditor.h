#pragma once

#include "AutoPullRules.h"

#include <QDialog>

class QLineEdit;
class QSpinBox;
class QComboBox;
class QDateTimeEdit;
class QCheckBox;
class QTableWidget;
class QLabel;
class QPushButton;
class QGroupBox;
namespace meda {

/// Editor for a single AutoPullRule: node, query filters, schedule,
/// limits, and a live "preview matches" button.
class AutoPullRuleEditor : public QDialog {
    Q_OBJECT
public:
    explicit AutoPullRuleEditor(const AutoPullRule& rule, QWidget* parent = nullptr);

    AutoPullRule result() const;

private slots:
    void onPreview();
    void onEcho();

private:
    AutoPullRule m_rule;

    QLineEdit* m_name;
    // Node
    QComboBox* m_nodeCombo;     // saved-node picker
    QLineEdit* m_host;
    QSpinBox*  m_port;
    QLineEdit* m_calledAET;
    QLineEdit* m_callingAET;
    QLineEdit* m_moveDestAET;
    // Query
    QLineEdit* m_patientName;
    QLineEdit* m_patientID;
    QLineEdit* m_accession;
    QLineEdit* m_studyDescription;
    QComboBox* m_dateMode;
    QLineEdit* m_studyDate;
    QComboBox* m_modalityCombo;
    // Limits
    QSpinBox*  m_maxStudies;
    QCheckBox* m_skipDuplicates;
    QComboBox* m_retrieveMethod;
    // Schedule
    QComboBox*   m_schedule;
    QDateTimeEdit* m_startTime;
    // Preview
    QTableWidget* m_preview;
    QLabel*      m_previewStatus;
    QPushButton* m_previewBtn;
    QPushButton* m_echoBtn;
};

} // namespace meda
