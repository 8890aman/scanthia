#pragma once

#include <QWidget>

class QLineEdit;
class QSpinBox;
class QComboBox;
class QDoubleSpinBox;
class QCheckBox;
class QPushButton;

namespace meda {

/// Dockable Scanthia configuration panel: receiver AE/port, DICOMweb
/// endpoint, memory budget, default slice sort, cine FPS. Everything
/// persists to QSettings and applies live where possible.
class ConfigDock : public QWidget {
    Q_OBJECT
public:
    explicit ConfigDock(QWidget* parent = nullptr);

    /// Load persisted values into the form.
    void reload();

signals:
    /// Emitted when the user applies receiver settings — MainWindow
    /// restarts the store SCP.
    void receiverChanged(const QString& aet, int port);
    /// Emitted when the memory budget changes — affects future loads.
    void memoryBudgetChanged(qint64 bytes);

private:
    void applyAll();

    // Receiver
    QLineEdit* m_aet;
    QSpinBox*  m_port;

    // DICOMweb
    QLineEdit* m_webUrl;

    // Viewer
    QComboBox*       m_sliceSort;
    QDoubleSpinBox*  m_memBudget;   // GB
    QSpinBox*        m_cineFps;
    QCheckBox*       m_showPlanes;
};

} // namespace meda
