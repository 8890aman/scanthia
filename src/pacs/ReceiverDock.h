#pragma once

#include <QWidget>
#include <QTreeWidget>
#include <QLineEdit>
#include <QSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QTimer>

namespace meda {

namespace rd { class StatusDot; }

/// Dockable receiver panel: this machine's listening config (AE title,
/// port, IPs, status) on top, and a live queue of inbound C-STORE
/// objects below.
class ReceiverDock : public QWidget {
    Q_OBJECT
public:
    explicit ReceiverDock(QWidget* parent = nullptr);

    /// Fill the config fields (called once at startup with persisted
    /// values) and set the listening state shown in the status row.
    void setConfig(const QString& aet, int port, const QString& inDir);
    void setListening(bool on, const QString& statusDetail = QString());

    /// Append a received object to the queue. `path` is the file the
    /// SCP just wrote; we show the tail of it plus time and size.
    void addReceived(const QString& path);

signals:
    /// User pressed Apply — host should restart the SCP with these.
    void applyRequested(const QString& aet, int port);

private:
    void rebuildIps();

    QLineEdit*   m_aetEdit;
    QSpinBox*    m_portSpin;
    QLabel*      m_ipLabel;
    rd::StatusDot* m_statusDot;
    QLabel*      m_statusText;
    QLabel*      m_dirLabel;
    QPushButton* m_applyBtn;
    QTreeWidget* m_queue;
};

} // namespace meda
