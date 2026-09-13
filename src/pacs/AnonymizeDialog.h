#pragma once

#include <QDialog>
#include <QStringList>

class QCheckBox;
class QLineEdit;
class QProgressBar;
class QLabel;
class QPushButton;

namespace meda {

/// "Anonymize Export" — pick which tag groups to strip, choose an
/// output folder, writes anonymized copies (originals untouched).
class AnonymizeDialog : public QDialog {
    Q_OBJECT
public:
    AnonymizeDialog(const QStringList& files, QWidget* parent = nullptr);

private:
    void run();

    QStringList  m_files;
    QCheckBox*   m_patient;
    QCheckBox*   m_dates;
    QCheckBox*   m_institution;
    QCheckBox*   m_device;
    QCheckBox*   m_comments;
    QLineEdit*   m_outDir;
    QProgressBar* m_progress;
    QLabel*      m_status;
    QPushButton* m_go;
};

} // namespace meda
