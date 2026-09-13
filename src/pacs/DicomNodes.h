#pragma once

#include "PacsClient.h"

#include <QString>
#include <QList>

namespace meda {

/// A saved DICOM node (OsiriX "Location"). Persisted to
/// %APPDATA%/Scanthia/nodes.json.
struct DicomNode {
    QString name;               // human label, e.g. "Main PACS"
    PacsNode node;              // host, port, calledAET, callingAET
    QString moveDestAET;        // C-MOVE destination (our store SCP)
    QString retrieveMethod;     // "C-GET" or "C-MOVE"
    bool    isDefault = false;
};

/// JSON-backed store for named DICOM nodes. Singleton-ish — all dialogs
/// share the same on-disk file.
class DicomNodes {
public:
    DicomNodes();

    bool load();
    bool save() const;

    QList<DicomNode> nodes() const { return m_nodes; }
    void setNodes(const QList<DicomNode>& n) { m_nodes = n; }

    void upsert(const DicomNode& n);        // keyed on name
    void remove(const QString& name);
    DicomNode get(const QString& name) const;
    DicomNode defaultNode() const;

private:
    QString m_path;
    QList<DicomNode> m_nodes;
};

} // namespace meda
