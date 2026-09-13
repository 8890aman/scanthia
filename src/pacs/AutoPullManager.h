#pragma once

#include "AutoPullRules.h"

#include <QDialog>

class QTableWidget;
class QLabel;

namespace meda {

/// Manager for saved auto-pull rules: list, add, edit, delete, run now,
/// view history. Lives in the Tools menu.
class AutoPullManager : public QDialog {
    Q_OBJECT
public:
    explicit AutoPullManager(QWidget* parent = nullptr);

private slots:
    void onAdd();
    void onEdit();
    void onDelete();
    void onRunNow();
    void onViewLog();
    void onToggleEnabled();

private:
    void refresh();

    AutoPullRules  m_store;
    QTableWidget*  m_table;
    QLabel*        m_status;

    QString currentId() const;
};

} // namespace meda
