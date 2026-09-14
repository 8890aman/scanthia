#pragma once

#include <QObject>
#include <QThread>

#include <memory>
#include <string>

namespace meda {

/// A C-STORE SCP running on a worker thread. Incoming objects are written
/// to a configurable directory; emits a signal per received object.
class StoreScp : public QObject {
    Q_OBJECT
public:
    explicit StoreScp(QObject* parent = nullptr);
    ~StoreScp() override;

    /// Start listening. Returns false if the port cannot be bound.
    bool start(uint16_t port, const std::string& aet,
               const std::string& outputDir);
    /// Stop, then start again — used when the port/AET config changes.
    bool restart(uint16_t port, const std::string& aet,
                 const std::string& outputDir);
    void stop();
    bool isRunning() const;

    uint16_t    port() const { return m_port; }
    std::string aet() const { return m_aet; }
    std::string outputDir() const { return m_outputDir; }

signals:
    void fileReceived(const QString& path);
    void stopped();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    QThread*     m_thread = nullptr;
    uint16_t     m_port = 0;
    std::string  m_aet;
    std::string  m_outputDir;
};

} // namespace meda
