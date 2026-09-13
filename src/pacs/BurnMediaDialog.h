#pragma once

#include <QDialog>

class QTreeWidget;
class QLabel;
class QComboBox;
class QCheckBox;
class QSpinBox;
class QLineEdit;

namespace meda {

class StudyDatabase;

/// Burn DICOM CD / Media dialog. Stages a DICOMDIR + DICOM files +
/// optional portable viewer, then writes to optical drive (IMAPI2),
/// ISO, or folder. Reachable from Tools menu and library context menu.
class BurnMediaDialog : public QDialog {
    Q_OBJECT
public:
    explicit BurnMediaDialog(StudyDatabase* db, QWidget* parent = nullptr);

signals:
    /// Emitted after staging so the caller can re-index if needed.
    void stagedForBurn(const QString& dir);

private slots:
    void onStage();
    void onBurn();
    void onTargetChanged(int idx);

private:
    StudyDatabase* m_db;

    // Source selection
    QTreeWidget* m_source;
    // Output
    QComboBox*   m_target;       // Drive / ISO file / Folder
    QComboBox*   m_drive;        // optical drives (when available)
    QLineEdit*   m_isoPath;
    QLineEdit*   m_folderPath;
    QCheckBox*   m_includeViewer;
    QLineEdit*   m_volumeLabel;
    // Status
    QLabel*      m_sizeLabel;
    QLabel*      m_status;
    // Staging state
    QString      m_stageDir;
    qint64       m_stageBytes = 0;
};

} // namespace meda
