#include "MainWindow.h"
#include "Theme.h"

#include "DicomLoader.h"
#include "StudyDatabase.h"
#include "SliceViewer.h"
#include "MprWidget.h"
#include "VolumeWidget.h"
#include "PacsDialog.h"
#include "DicomWebDialog.h"
#include "StoreScp.h"
#include "AutoPullManager.h"
#include "AutoPullRuleEditor.h"
#include "AutoPullRules.h"
#include "TaskScheduler.h"
#include "BurnMediaDialog.h"
#include "DicomNodes.h"
#include "NodesDialog.h"
#include "AnonymizeDialog.h"
#include "Segmentation.h"
#include "AiPlugin.h"
#include "PluginManager.h"
#include "InferenceEngine.h"

#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QPushButton>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMenu>
#include <QSlider>
#include <QToolButton>
#include <QWidgetAction>
#include <QMessageBox>
#include <QProcess>
#include <QProgressBar>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QSvgRenderer>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QProcess>
#include <QBuffer>
#include <QPrinter>
#include <QPrintDialog>
#include <QPainter>
#include <QToolBar>
#include <QTreeWidget>
#include <QFile>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QPixmap>
#include <QDataStream>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QPainter>
#include <QtMath>
#include <cmath>
#include <QStyledItemDelegate>

#include <QtConcurrent/QtConcurrent>

#include <vtkImageData.h>
#include <vtkLookupTable.h>
#include <vtkPointData.h>

#ifdef _WIN32
#include <windows.h>
#endif

namespace meda {

namespace {

/// Library item roles beyond UserRole (which holds the series UID).
constexpr int RoleModality = Qt::UserRole + 1;
constexpr int RoleSpec     = Qt::UserRole + 2;   // "46 IMG · 512x512"
constexpr int RoleIsStudy  = Qt::UserRole + 3;   // study node (not series)
constexpr int RoleIsRemote = Qt::UserRole + 4;   // remote PACS item

/// Draw a circular-arrow "refresh" icon at runtime — no unicode glyph
/// stand-ins, consistent 1.5px stroke with the instrument look.
QIcon makeRefreshIcon(const QColor& c, int px = 14)
{
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(c, 1.5);
    p.setPen(pen);
    // Arc ~270° with an arrowhead at the end.
    const QRectF r(2.0, 2.0, px - 4.0, px - 4.0);
    p.drawArc(r, 40 * 16, 300 * 16);
    // Arrowhead at arc end (~340° → top-right).
    const double a = qDegreesToRadians(-20.0);
    const QPointF tip(r.center().x() + (r.width() / 2) * std::cos(a),
                      r.center().y() - (r.height() / 2) * std::sin(a));
    p.drawLine(tip, tip + QPointF(-4.5, -1.0));
    p.drawLine(tip, tip + QPointF(-1.0, 4.5));
    p.end();
    return QIcon(pm);
}

/// Draw a chevron (◂/▸) icon for dock collapse/expand chrome.
QIcon makeChevronIcon(bool pointsRight, const QColor& c, int px = 12)
{
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(c, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    const double mx = px / 2.0;
    if (pointsRight) {
        p.drawLine(QPointF(mx - 2, 2), QPointF(mx + 2, px / 2.0));
        p.drawLine(QPointF(mx + 2, px / 2.0), QPointF(mx - 2, px - 2));
    } else {
        p.drawLine(QPointF(mx + 2, 2), QPointF(mx - 2, px / 2.0));
        p.drawLine(QPointF(mx - 2, px / 2.0), QPointF(mx + 2, px - 2));
    }
    p.end();
    return QIcon(pm);
}

/// Slim vertical rail shown when a dock is collapsed: rotated label +
/// a chevron. One click expands the dock again.
class RailButton : public QToolButton {
public:
    RailButton(const QString& text, QWidget* parent = nullptr)
        : QToolButton(parent), m_text(text)
    {
        setFixedWidth(24);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        setCursor(Qt::PointingHandCursor);
    }
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        // Rail face — instrument ground with a bezel hairline on the
        // dock-facing edge.
        p.fillRect(rect(), QColor(0x1A, 0x1E, 0x24));
        p.setPen(QColor(0x2E, 0x35, 0x40));
        p.drawLine(rect().topRight(), rect().bottomRight());
        if (underMouse())
            p.fillRect(rect(), QColor(0x3D, 0x47, 0x50, 60));
        // Rotated label.
        p.save();
        p.translate(width() / 2.0 + 3, height() / 2.0);
        p.rotate(-90);
        QFont f = font();
        f.setPointSize(8);
        f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.setPen(QColor(0x8E, 0x99, 0xA6));
        p.drawText(QRect(-height() / 2, -10, height(), 20),
                   Qt::AlignCenter, m_text);
        p.restore();
        // Chevron at the top pointing inward (expand direction).
        p.setPen(QPen(QColor(0x4D, 0xA3, 0xE8), 1.6,
                      Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const double mx = width() / 2.0;
        p.drawLine(QPointF(mx + 2, 6), QPointF(mx - 2, 10));
        p.drawLine(QPointF(mx - 2, 10), QPointF(mx + 2, 14));
    }
private:
    QString m_text;
};

/// Renders library rows as instrument spec plates: study rows are
/// small-caps legends with a hairline; series rows get a fixed thumb
/// slot, a modality chip, and a two-line spec label.
class LibraryDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* p, const QStyleOptionViewItem& opt,
               const QModelIndex& idx) const override
    {
        p->save();
        const QRect r = opt.rect;
        const bool sel = opt.state & QStyle::State_Selected;
        const bool hov = opt.state & QStyle::State_MouseOver;
        const bool isStudy = !idx.parent().isValid();

        if (sel) {
            p->fillRect(r, QColor(0x22, 0x27, 0x2E));
            p->fillRect(QRect(r.left(), r.top(), 2, r.height()),
                        QColor(0x4D, 0xA3, 0xE8));
        } else if (hov) {
            p->fillRect(r, QColor(0x22, 0x27, 0x2E));
        }

        if (isStudy) {
            // Legend row: hairline + small-caps title.
            p->setPen(QColor(0x2A, 0x31, 0x38));
            p->drawLine(r.left() + 4, r.top(), r.right() - 4, r.top());
            QFont f = opt.font;
            f.setPixelSize(10);
            f.setBold(true);
            f.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
            p->setFont(f);
            p->setPen(QColor(0x8B, 0x96, 0xA0));
            const QString t =
                p->fontMetrics().elidedText(
                    idx.data(Qt::DisplayRole).toString().toUpper(),
                    Qt::ElideRight, r.width() - 16);
            p->drawText(r.adjusted(10, 0, -6, 0),
                        Qt::AlignVCenter | Qt::AlignLeft, t);
            p->restore();
            return;
        }

        // Fixed thumbnail slot, black-backed and letterboxed.
        const int ts = r.height() - 14;
        const QRect thumb(r.left() + 10, r.top() + 7, ts, ts);
        p->fillRect(thumb, QColor(0, 0, 0));
        const QPixmap pm =
            idx.data(Qt::DecorationRole).value<QPixmap>();
        if (!pm.isNull()) {
            const QPixmap sc = pm.scaled(thumb.size(), Qt::KeepAspectRatio,
                                         Qt::SmoothTransformation);
            p->drawPixmap(thumb.x() + (thumb.width()  - sc.width())  / 2,
                          thumb.y() + (thumb.height() - sc.height()) / 2,
                          sc);
        }
        p->setPen(QColor(0x2A, 0x31, 0x38));
        p->drawRect(thumb);

        const int tx = thumb.right() + 10;
        const QRect textRect(tx, r.top() + 8, r.right() - tx - 8,
                             r.height() - 16);

        // Modality chip.
        const QString mod = idx.data(RoleModality).toString();
        static const QHash<QString, QColor> modColor = {
            {"CT", {0x4D, 0xA3, 0xE8}}, {"MR", {0xB5, 0x7C, 0xE0}},
            {"PT", {0xE8, 0xA3, 0x3D}}, {"NM", {0xFF, 0x7A, 0x45}},
            {"US", {0x5B, 0x9B, 0xFF}}, {"SR", {0x8B, 0x96, 0xA0}},
        };
        const QColor mc = modColor.value(mod, QColor(0x8B, 0x96, 0xA0));
        QFont chipF = opt.font;
        chipF.setPixelSize(9);
        chipF.setBold(true);
        p->setFont(chipF);
        const int cw = p->fontMetrics().horizontalAdvance(mod) + 8;
        const QRect chip(textRect.left(), textRect.top(), cw, 13);
        p->setPen(QPen(mc, 1));
        p->drawRect(chip);
        p->setPen(mc);
        p->drawText(chip, Qt::AlignCenter, mod);

        // Line 1: series description.
        QFont nameF = opt.font;
        nameF.setPixelSize(12);
        p->setFont(nameF);
        p->setPen(QColor(0xE6, 0xEB, 0xEF));
        const QString name =
            p->fontMetrics().elidedText(
                idx.data(Qt::DisplayRole).toString(),
                Qt::ElideRight, textRect.width());
        p->drawText(QRect(textRect.left(), textRect.top() + 16,
                          textRect.width(), 18),
                    Qt::AlignVCenter | Qt::AlignLeft, name);

        // Line 2: spec readout.
        QFont specF = opt.font;
        specF.setPixelSize(10);
        p->setFont(specF);
        p->setPen(QColor(0x8B, 0x96, 0xA0));
        p->drawText(QRect(textRect.left(), textRect.top() + 36,
                          textRect.width(), 16),
                    Qt::AlignVCenter | Qt::AlignLeft,
                    idx.data(RoleSpec).toString());
        p->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& opt,
                   const QModelIndex& idx) const override
    {
        return {opt.rect.width(), idx.parent().isValid() ? 64 : 28};
    }
};

/// Render a Lucide SVG tinted to a palette color. Lucide strokes use
/// "currentColor" — we swap in the hex and rasterize at 2x for HiDPI.
QPixmap iconPixmap(const QString& name, const QColor& color)
{
    QFile f(":/icons/" + name + ".svg");
    QPixmap pm(32, 32);
    pm.fill(Qt::transparent);
    if (!f.open(QIODevice::ReadOnly))
        return pm;
    QString svg = QString::fromUtf8(f.readAll());
    svg.replace("currentColor", color.name());
    QSvgRenderer renderer(svg.toUtf8());
    QPainter p(&pm);
    renderer.render(&p);
    pm.setDevicePixelRatio(2.0);
    return pm;
}

/// A submenu that stays open while checkable actions are toggled —
/// lets users tick several models in one visit, then run them.
class StickyMenu : public QMenu {
public:
    using QMenu::QMenu;
protected:
    void mouseReleaseEvent(QMouseEvent* e) override
    {
        QAction* a = activeAction();
        if (a && a->isCheckable()) {
            a->trigger();          // toggle, keep the menu open
            return;
        }
        QMenu::mouseReleaseEvent(e);
    }
};

/// Tool icon: muted when idle, amber when armed.
QIcon toolIcon(const QString& name)
{
    QIcon ico;
    ico.addPixmap(iconPixmap(name, QColor(0x8B, 0x96, 0xA0)),
                  QIcon::Normal, QIcon::Off);
    ico.addPixmap(iconPixmap(name, QColor(0xE8, 0xA3, 0x3D)),
                  QIcon::Normal, QIcon::On);
    ico.addPixmap(iconPixmap(name, QColor(0xE6, 0xEB, 0xEF)),
                  QIcon::Active, QIcon::Off);
    return ico;
}

} // namespace

MainWindow::MainWindow()
{
    setWindowTitle("Scanthia");
    resize(1400, 900);

    m_stack = new QStackedWidget(this);
    m_singleView = new SliceViewer(this);
    m_compareView = new SliceViewer(this);
    m_compareView->hide();
    m_compareView->setAcceptDrops(true);
    m_compareView->installEventFilter(this);   // drop target for library drags
    m_compareView->setInfoText(
        tr("Drag a series\nfrom the library\nhere to compare"));
    auto* singlePage = new QWidget(this);
    auto* singleLay = new QHBoxLayout(singlePage);
    m_singleLay = singleLay;
    singleLay->setContentsMargins(0, 0, 0, 0);
    singleLay->setSpacing(2);
    singleLay->addWidget(m_singleView, 1);
    singleLay->addWidget(m_compareView, 1);
    m_mpr = new MprWidget(this);
    m_stack->addWidget(singlePage);
    m_stack->addWidget(m_mpr);
    setCentralWidget(m_stack);

    // Linked scrolling between the two compare panes (both directions,
    // guarded against signal bounce).
    auto linkScroll = [this](SliceViewer* src, SliceViewer* dst) {
        connect(src, &SliceViewer::sliceChanged, this,
                [this, dst](int s) {
                    if (m_compareLinked)
                        return;
                    m_compareLinked = true;
                    dst->setSlice(s);
                    m_compareLinked = false;
                });
    };
    linkScroll(m_singleView, m_compareView);
    linkScroll(m_compareView, m_singleView);
    setLayout(1); // default to MPR — this is our differentiator

    m_storeScp = new StoreScp(this);
    m_plugins = new PluginManager(this);
    m_engine = new InferenceEngine();
    m_db = new StudyDatabase();
    const QString dbPath =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
        "/Scanthia.db";
    QDir().mkpath(QFileInfo(dbPath).absolutePath());
    const bool dbOk = m_db->open(dbPath);

    // Single view: crosshair tool also shows lines locally.
    connect(m_singleView, &SliceViewer::crosshairMoved, this,
            [this](std::array<double,3> ijk) {
                m_singleView->setCrosshairIjk(ijk);
            });

    m_modelStatus = new QLabel(this);
    m_modelStatus->setText(tr("AI: none"));
    statusBar()->addPermanentWidget(m_modelStatus);

    m_statusLabel = new QLabel(this);
    m_progress = new QProgressBar(this);
    m_progress->setMaximumWidth(320);
    m_progress->setMinimumHeight(16);
    m_progress->setTextVisible(true);
    m_progress->setFormat("%p%");
    // Global theme styles it — square, bezel border, green tape.
    m_progress->setVisible(false);
    statusBar()->addPermanentWidget(m_statusLabel, 1);
    statusBar()->addPermanentWidget(m_progress);
    if (!dbOk)
        m_statusLabel->setText(tr("Warning: study database unavailable"));

    buildDock();
    buildMenus();
    buildToolbar();

    for (auto* v : {m_singleView, m_compareView,
                    m_mpr->viewer(Orientation::Axial),
                    m_mpr->viewer(Orientation::Sagittal),
                    m_mpr->viewer(Orientation::Coronal)}) {
        connect(v, &SliceViewer::voxelHovered, this,
                &MainWindow::updateHover);
        connect(v, &SliceViewer::toolDeselectRequested, this,
                [this] { setTool(Tool::WindowLevel); });
    }
    connect(m_mpr, &MprWidget::voxelHovered, this,
            &MainWindow::updateHover);

    // Debounced annotation autosave — draw, wait 500ms, persist.
    m_annoSaveTimer = new QTimer(this);
    m_annoSaveTimer->setSingleShot(true);
    m_annoSaveTimer->setInterval(500);
    connect(m_annoSaveTimer, &QTimer::timeout, this,
            &MainWindow::saveAnnotations);
    connect(m_mpr, &MprWidget::annotationsChanged, this,
            [this] { m_annoSaveTimer->start(); });
    connect(m_singleView, &SliceViewer::annotationsChanged, this,
            [this] { m_annoSaveTimer->start(); });

    m_plugins->discover();
    for (auto* p : m_plugins->plugins())
        m_pluginMenu->addAction(p->name(), this,
                                [this, n = p->name()] { runPlugin(n); });

    // Track the 'Z' zoom-modifier key app-wide so Z+wheel zooms in
    // whichever pane the mouse is over, regardless of keyboard focus.
    qApp->installEventFilter(this);
}

bool MainWindow::eventFilter(QObject* o, QEvent* e)
{
    if (e->type() == QEvent::KeyPress ||
        e->type() == QEvent::KeyRelease) {
        auto* ke = static_cast<QKeyEvent*>(e);
        if (ke->key() == Qt::Key_Z &&
            ke->modifiers() == Qt::NoModifier) {
            const bool held = (e->type() == QEvent::KeyPress);
            m_singleView->setZoomKeyHeld(held);
            m_compareView->setZoomKeyHeld(held);
            for (auto* v : {m_mpr->viewer(Orientation::Axial),
                            m_mpr->viewer(Orientation::Sagittal),
                            m_mpr->viewer(Orientation::Coronal)})
                v->setZoomKeyHeld(held);
        }
    }
    // Drop a library series onto the compare pane → load it there.
    if (o == m_compareView &&
        (e->type() == QEvent::DragEnter || e->type() == QEvent::Drop)) {
        auto* de = static_cast<QDropEvent*>(e);
        static const QString fmt =
            "application/x-qabstractitemmodeldatalist";
        if (e->type() == QEvent::DragEnter) {
            if (de->mimeData()->hasFormat(fmt))
                de->acceptProposedAction();
            return true;
        }
        // Decode the dragged item's UserRole (series UID).
        QByteArray ba = de->mimeData()->data(fmt);
        QDataStream ds(&ba, QIODevice::ReadOnly);
        QString uid;
        while (!ds.atEnd()) {
            int row, col;
            QMap<int, QVariant> roles;
            ds >> row >> col >> roles;
            if (roles.contains(Qt::UserRole))
                uid = roles[Qt::UserRole].toString();
        }
        de->acceptProposedAction();
        if (!uid.isEmpty())
            loadCompareSeries(uid);
        return true;
    }
    // Esc on the compare pane closes it.
    if (o == m_compareView && e->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape) {
        m_compareView->hide();
        if (m_compareAction)
            m_compareAction->setChecked(false);
        return true;
    }
    // Closing the detached compare window reattaches it to the layout.
    if (o == m_compareWindow && e->type() == QEvent::Close) {
        popOutCompare(false);
        return true;
    }
    return QMainWindow::eventFilter(o, e);
}

void MainWindow::buildDock()
{
    auto* dock = new QDockWidget(tr("Library"), this);
    // Source bar — an instrument header: spaced-caps legend, a state
    // tag, a styled combo, and a square refresh button over a hairline.
    auto* body = new QWidget(dock);
    auto* bodyLay = new QVBoxLayout(body);
    bodyLay->setContentsMargins(6, 6, 6, 4);
    bodyLay->setSpacing(5);
    // Row 1: SOURCE legend + live state tag.
    auto* headRow = new QHBoxLayout;
    auto* srcLabel = new QLabel(tr("S O U R C E"), body);
    srcLabel->setStyleSheet(
        "color: #8E99A6; font-size: 10px; font-weight: 600;");
    m_sourceTag = new QLabel(tr("LOCAL"), body);
    m_sourceTag->setStyleSheet(
        "color: #4DA3E8; font-size: 10px; font-weight: 600;");
    headRow->addWidget(srcLabel);
    headRow->addStretch();
    headRow->addWidget(m_sourceTag);
    bodyLay->addLayout(headRow);
    // Row 2: combo + square refresh button.
    auto* srcRow = new QHBoxLayout;
    srcRow->setSpacing(0);
    m_sourceCombo = new QComboBox(body);
    m_sourceCombo->addItem(tr("Local Library"));
    for (const auto& n : DicomNodes().nodes())
        m_sourceCombo->addItem(n.name);
    m_sourceCombo->addItem(tr("Manage Nodes..."));
    m_sourceCombo->setStyleSheet(
        "QComboBox { background: #1A1E24; color: #DCE2E9;"
        "  border: 1px solid #2E3540; padding: 5px 8px; font-size: 12px; }"
        "QComboBox:focus { border-color: #4DA3E8; }"
        "QComboBox::drop-down { border-left: 1px solid #2E3540;"
        "  width: 22px; }"
        "QComboBox QAbstractItemView { background: #1A1E24;"
        "  color: #DCE2E9; border: 1px solid #2E3540;"
        "  selection-background-color: #2E3540; outline: none; }");
    auto* srcRefresh = new QPushButton(body);
    srcRefresh->setIcon(makeRefreshIcon(QColor(0x8E, 0x99, 0xA6)));
    srcRefresh->setIconSize({14, 14});
    srcRefresh->setFixedSize(26, 26);
    srcRefresh->setToolTip(tr("Refresh / query source"));
    srcRefresh->setStyleSheet(
        "QPushButton { background: #1A1E24; border: 1px solid #2E3540;"
        "  border-left: none; padding: 0; }"
        "QPushButton:hover { background: #3D4750; }"
        "QPushButton:pressed { background: #2E3540; }");
    srcRow->addWidget(m_sourceCombo, 1);
    srcRow->addWidget(srcRefresh);
    bodyLay->addLayout(srcRow);
    // Remote filter (visible only when a remote source is selected).
    m_remoteFilter = new QLineEdit(body);
    m_remoteFilter->setPlaceholderText(
        tr("Remote filter — patient name / ID (empty = all)"));
    m_remoteFilter->setVisible(false);
    bodyLay->addWidget(m_remoteFilter);
    // Hairline separating the source bar from the study list.
    auto* hair = new QFrame(body);
    hair->setFrameShape(QFrame::HLine);
    hair->setStyleSheet("color: #2E3540;");
    bodyLay->addWidget(hair);
    m_library = new QTreeWidget(body);
    m_library->setHeaderHidden(true);
    m_library->setIconSize({72, 72});
    m_library->setDragEnabled(true);
    m_library->setDragDropMode(QAbstractItemView::DragOnly);
    m_library->setItemDelegate(new LibraryDelegate(m_library));
    m_library->setMouseTracking(true);   // hover state for the delegate
    m_library->setRootIsDecorated(true);
    bodyLay->addWidget(m_library, 1);
    dock->setWidget(body);
    addDockWidget(Qt::LeftDockWidgetArea, dock);
    m_library->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_library, &QTreeWidget::customContextMenuRequested, this,
            &MainWindow::onLibraryContextMenu);
    connect(m_library, &QTreeWidget::itemActivated, this,
            [this](QTreeWidgetItem* item, int) {
                if (item->data(0, RoleIsStudy).toInt()) {
                    // Remote study → retrieve it.
                    if (item->data(0, RoleIsRemote).toInt())
                        retrieveRemoteStudy(
                            item->data(0, Qt::UserRole).toString());
                    return;
                }
                const QString suid = item->data(0, Qt::UserRole).toString();
                if (!suid.isEmpty())
                    loadSeries(m_db->series(suid));
            });
    connect(m_sourceCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MainWindow::onSourceChanged);
    connect(srcRefresh, &QPushButton::clicked, this,
            &MainWindow::onSourceRefresh);
    connect(m_remoteFilter, &QLineEdit::returnPressed, this,
            &MainWindow::onSourceRefresh);
    refreshLibrary();
}

void MainWindow::refreshLibrary()
{
    if (!m_db || !m_db->isOpen())
        return;
    // When a remote source is active the tree shows PACS results —
    // leave it alone.
    if (m_sourceCombo && m_sourceCombo->currentIndex() > 0 &&
        m_sourceCombo->currentText() != tr("Manage Nodes..."))
        return;
    m_library->clear();
    for (const auto& study : m_db->studies()) {
        auto* studyItem = new QTreeWidgetItem(m_library);
        studyItem->setText(
            0, QString("%1  %2  %3 — %4 series")
                   .arg(study.patientName, study.studyDate,
                        study.description)
                   .arg(study.seriesCount));
        studyItem->setFlags(studyItem->flags() & ~Qt::ItemIsSelectable);
        studyItem->setData(0, Qt::UserRole, study.studyUID);
        studyItem->setData(0, RoleIsStudy, 1);
        for (const auto& s : m_db->seriesOf(study.studyUID)) {
            auto* item = new QTreeWidgetItem(studyItem);
            const QString desc =
                s.seriesDescription.empty()
                    ? QString("Series %1").arg(s.seriesNumber.c_str())
                    : QString::fromStdString(s.seriesDescription);
            item->setText(0, desc);
            item->setData(0, Qt::UserRole,
                          QString::fromStdString(s.seriesInstanceUID));
            item->setData(0, RoleModality,
                          QString::fromStdString(s.modality));
            item->setData(0, RoleSpec,
                          QString("%1 IMG · %2×%3")
                              .arg(s.instanceCount)
                              .arg(s.columns)
                              .arg(s.rows));
            const QByteArray png =
                m_db->thumbnail(item->data(0, Qt::UserRole).toString());
            if (!png.isEmpty()) {
                QPixmap pm;
                pm.loadFromData(png);
                item->setData(0, Qt::DecorationRole, pm);
            }
        }
        studyItem->setExpanded(true);
    }
}

// --- Remote (PACS) source in the library dock -------------------------

void MainWindow::onSourceChanged(int idx)
{
    const QString sel = m_sourceCombo->itemText(idx);
    if (sel == tr("Manage Nodes...")) {
        NodesDialog dlg(this);
        dlg.exec();
        // Rebuild the combo — the nodes list may have changed.
        m_sourceCombo->blockSignals(true);
        m_sourceCombo->clear();
        m_sourceCombo->addItem(tr("Local Library"));
        for (const auto& n : DicomNodes().nodes())
            m_sourceCombo->addItem(n.name);
        m_sourceCombo->addItem(tr("Manage Nodes..."));
        m_sourceCombo->setCurrentIndex(0);
        m_sourceCombo->blockSignals(false);
        if (m_sourceTag) {
            m_sourceTag->setText(tr("LOCAL"));
            m_sourceTag->setStyleSheet(
                "color: #8E99A6; font-size: 10px; font-weight: 600;");
        }
        refreshLibrary();
        return;
    }
    m_remoteFilter->setVisible(idx > 0);
    if (idx == 0) {
        if (m_sourceTag) {
            m_sourceTag->setText(tr("LOCAL"));
            m_sourceTag->setStyleSheet(
                "color: #8E99A6; font-size: 10px; font-weight: 600;");
        }
        refreshLibrary();
        return;
    }
    if (m_sourceTag) {
        m_sourceTag->setText(tr("REMOTE"));
        m_sourceTag->setStyleSheet(
            "color: #E8A33D; font-size: 10px; font-weight: 600;");
    }
    // Remote node selected — run a study query.
    queryRemoteSource(m_remoteFilter->text());
}

void MainWindow::onSourceRefresh()
{
    const int idx = m_sourceCombo->currentIndex();
    if (idx == 0) {
        refreshLibrary();
        return;
    }
    queryRemoteSource(m_remoteFilter->text());
}

void MainWindow::queryRemoteSource(const QString& filter)
{
    const QString name = m_sourceCombo->currentText();
    const auto dn = DicomNodes().get(name);
    if (dn.name.isEmpty())
        return;
    m_library->clear();
    m_statusLabel->setText(tr("Querying %1...").arg(name));

    StudyQuery q;
    // Filter box doubles as patient-name or patient-ID wildcard.
    const QString f = filter.trimmed();
    if (!f.isEmpty()) {
        // DICOM PN wildcard: wrap in * * if no wildcard given.
        QString pat = f;
        if (!pat.contains('*') && !pat.contains('?'))
            pat = "*" + pat + "*";
        q.patientName = pat.toStdString();
        q.patientID = f.toStdString();  // exact ID match also attempted
    }
    const PacsNode node = dn.node;

    QtConcurrent::run([this, node, q, name] {
        std::vector<StudyQueryResult> results;
        std::string err;
        const bool ok = PacsClient().queryStudies(node, q, results, &err);
        QMetaObject::invokeMethod(this, [this, ok, err, results, name] {
            // Only apply if the user hasn't switched back meanwhile.
            if (m_sourceCombo->currentText() != name)
                return;
            if (!ok) {
                m_statusLabel->setText(
                    tr("%1: query failed — %2").arg(name).arg(err.c_str()));
                return;
            }
            for (const auto& r : results) {
                auto* it = new QTreeWidgetItem(m_library);
                it->setText(0, QString("%1  %2  %3 — %4 series")
                                   .arg(QString::fromStdString(r.patientName),
                                        QString::fromStdString(r.studyDate),
                                        QString::fromStdString(r.studyDescription),
                                        QString::fromStdString(r.numSeries)));
                it->setData(0, Qt::UserRole,
                            QString::fromStdString(r.studyInstanceUID));
                it->setData(0, RoleIsStudy, 1);
                it->setData(0, RoleIsRemote, 1);
                it->setData(0, RoleModality,
                            QString::fromStdString(r.modalitiesInStudy));
                it->setData(0, RoleSpec,
                            tr("REMOTE · %1").arg(
                                QString::fromStdString(r.accessionNumber)));
            }
            m_statusLabel->setText(
                tr("%1: %2 studies").arg(name).arg(results.size()));
        }, Qt::QueuedConnection);
    });
}

void MainWindow::retrieveRemoteStudy(const QString& studyUID)
{
    const QString name = m_sourceCombo->currentText();
    const auto dn = DicomNodes().get(name);
    if (dn.name.isEmpty())
        return;
    const QString outDir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
        "/downloads/" + studyUID;
    const bool useGet = dn.retrieveMethod != "C-MOVE";
    const QString dest = dn.moveDestAET;
    const PacsNode node = dn.node;
    m_statusLabel->setText(tr("Retrieving study from %1...").arg(name));

    QtConcurrent::run([this, node, studyUID, outDir, useGet, dest] {
        std::string err;
        const bool ok =
            useGet ? PacsClient().retrieveStudyGet(
                         node, studyUID.toStdString(),
                         outDir.toStdString(), &err)
                   : PacsClient().retrieveStudyMove(
                         node, studyUID.toStdString(),
                         dest.toStdString(), &err);
        QMetaObject::invokeMethod(this, [this, ok, err, outDir, studyUID] {
            if (!ok) {
                m_statusLabel->setText(
                    tr("Retrieve failed: %1").arg(err.c_str()));
                return;
            }
            m_statusLabel->setText(tr("Retrieved — indexing..."));
            // After indexing, switch the library to Local (the remote
            // source view never shows locally-indexed series) and open
            // the first series of the study we just pulled.
            scanAndList(outDir, [this, studyUID] {
                m_sourceCombo->setCurrentIndex(0);   // -> Local Library
                for (const auto& s : m_db->seriesOf(studyUID)) {
                    loadSeries(m_db->series(
                        QString::fromStdString(s.seriesInstanceUID)));
                    break;   // open the first series only
                }
            });
        }, Qt::QueuedConnection);
    });
}

void MainWindow::buildMenus()
{
    auto* file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("Open DICOM &Folder..."), QKeySequence::Open,
                    this, &MainWindow::openFolder);
    file->addAction(tr("Open DICOM &Files..."), {},
                    this, &MainWindow::openFiles);
    file->addAction(tr("Open &Compare Series (right pane)..."), {},
                    this, &MainWindow::openCompareSeries);
    file->addSeparator();
    file->addAction(tr("Load &Segmentation..."), {},
                    this, &MainWindow::openSegmentation);
    file->addSeparator();
    file->addAction(tr("Save &Screenshot (PNG/JPEG)..."), {},
                    this, &MainWindow::exportScreenshot);
    file->addAction(tr("Export &Video (MP4)..."), {},
                    this, &MainWindow::exportVideo);
    file->addAction(tr("&Print / PDF..."), QKeySequence("Ctrl+P"),
                    this, &MainWindow::printView);
    file->addSeparator();
    file->addAction(tr("E&xit"), QKeySequence::Quit, this, &QWidget::close);

    auto* view = menuBar()->addMenu(tr("&View"));
    view->addAction(tr("&Single View"), this, [this] { setLayout(0); });
    view->addAction(tr("&MPR + 3D"), this, [this] { setLayout(1); });
    m_compareAction = view->addAction(
        tr("&Compare (split view)"), this, [this](bool on) {
            m_compareView->setVisible(on);
            if (on)
                m_stack->setCurrentIndex(0);
        });
    m_compareAction->setCheckable(true);
    m_popoutAction = view->addAction(
        tr("Pop Out Compare &Pane"), this,
        [this](bool on) { popOutCompare(on); });
    m_popoutAction->setCheckable(true);
    view->addSeparator();
    auto* presets = view->addMenu(tr("Window Presets"));
    for (const auto& p : kWindowPresets)
        presets->addAction(p.name, this,
                           [this, p] { applyPreset(p); });
    view->addSeparator();
    auto* volPresets = view->addMenu(tr("3D Presets"));
    for (const char* p : {"CT-Soft", "CT-Bone", "CT-Lung", "MIP"})
        volPresets->addAction(p, this, [this, p] {
            m_mpr->volumeView()->setPreset(p);
        });
    auto* planes = view->addAction(tr("Show MPR Planes"), this,
                                   [this](bool on) {
                                       m_mpr->volumeView()->setShowPlanes(on);
                                   });
    planes->setCheckable(true);
    planes->setChecked(true);

    m_segDockAction = view->addAction(
        tr("&AI Results Panel"), this, [this](bool on) {
            if (m_segDock)
                m_segDock->setVisible(on);
        });
    m_segDockAction->setCheckable(true);
    m_segDockAction->setChecked(true);
    m_segDockAction->setShortcut(QKeySequence("Alt+I"));

    auto* cross = view->addAction(tr("Crosshair Lines"), this, [this](bool on) {
        m_mpr->setCrosshairVisible(on);
        m_singleView->setShowCrosshair(on);
    });
    cross->setCheckable(true);
    cross->setChecked(false);
    cross->setShortcut(QKeySequence("Alt+C"));

    auto* link = view->addAction(tr("&Link MPR Zoom/Pan"), this,
                                 [this](bool on) {
                                     m_mpr->setViewsLinked(on);
                                 });
    link->setCheckable(true);
    link->setChecked(true);

    auto* detach = view->addMenu(tr("Detach &Pane"));
    const char* panes[] = {"Axial", "Sagittal", "Coronal", "3D Volume"};
    for (int i = 0; i < 4; ++i)
        detach->addAction(tr(panes[i]), this,
                          [this, i] { m_mpr->toggleDetach(i); });

    // Thick-slab MPR: Mean / MIP / MinIP at a selectable thickness.
    auto* slabMenu = view->addMenu(tr("Thick Slab (MPR)"));
    auto* slabGroup = new QActionGroup(this);
    struct SlabOpt { const char* label; int type; double mm; };
    const SlabOpt slabOpts[] = {
        {"Off",           0,  0.0},
        {"Average 5mm",   1,  5.0},
        {"Average 10mm",  1, 10.0},
        {"MIP 5mm",       2,  5.0},
        {"MIP 10mm",      2, 10.0},
        {"MIP 20mm",      2, 20.0},
        {"MinIP 10mm",    3, 10.0},
        {"MinIP 20mm",    3, 20.0},
    };
    for (const auto& opt : slabOpts) {
        auto* a = slabMenu->addAction(tr(opt.label), this, [this, opt] {
            m_mpr->setSlab(opt.type, opt.mm);
        });
        a->setCheckable(true);
        a->setChecked(opt.type == 0);
        slabGroup->addAction(a);
    }

    // Color lookup tables (applied after window/level).
    auto* lutMenu = view->addMenu(tr("Color Map"));
    auto* lutGroup = new QActionGroup(this);
    const char* luts[] = {"Grayscale", "Inverted Gray", "Hot Iron",
                          "Rainbow (PET)", "Bone"};
    for (int i = 0; i < 5; ++i) {
        auto* a = lutMenu->addAction(tr(luts[i]), this, [this, i] {
            m_mpr->setColorMap(i);
            m_singleView->setColorMap(i);
            m_compareView->setColorMap(i);
        });
        a->setCheckable(true);
        a->setChecked(i == 0);
        lutGroup->addAction(a);
    }
    // Quick invert toggle — I key flips Gray <-> Inverted Gray.
    auto* invert = view->addAction(
        tr("&Invert Grayscale"), QKeySequence("I"), this, [this, lutGroup] {
            const int cur = lutGroup->actions().indexOf(
                lutGroup->checkedAction());
            lutGroup->actions().at(cur == 1 ? 0 : 1)->trigger();
        });

    // Slice ordering — how files sort before the volume is built.
    auto* sortMenu = view->addMenu(tr("Slice Order"));
    auto* sortGroup = new QActionGroup(this);
    const char* sorts[] = {"Slice Position", "Instance Number",
                           "Acquisition Time", "Filename"};
    for (int i = 0; i < 4; ++i) {
        auto* a = sortMenu->addAction(tr(sorts[i]), this, [this, i, sorts] {
            m_sliceSort = i;
            m_statusLabel->setText(
                tr("Slice order: %1 — reload series to apply")
                    .arg(sorts[i]));
        });
        a->setCheckable(true);
        a->setChecked(i == 0);
        sortGroup->addAction(a);
    }

    // Fusion controls — opacity of the overlaid modality + clear.
    auto* fusMenu = view->addMenu(tr("Fusion Opacity"));
    auto* fusGroup = new QActionGroup(this);
    for (double pct : {25.0, 40.0, 50.0, 75.0, 100.0}) {
        auto* a = fusMenu->addAction(
            tr("%1%").arg(int(pct)), this, [this, pct] {
                m_mpr->setFusionOpacity(pct / 100.0);
                m_singleView->setFusionOpacity(pct / 100.0);
            });
        a->setCheckable(true);
        a->setChecked(pct == 50.0);
        fusGroup->addAction(a);
    }
    view->addAction(tr("Clear Fusion"), this, [this] { clearFusion(); });

    // Oblique MPR — rotate the active MPR pane's plane off-axis.
    view->addSeparator();
    view->addAction(
        tr("Rotate Plane &Left (Alt+←)"), this,
        [this] { m_mpr->setObliqueAngles(0, -5); });
    view->addAction(
        tr("Rotate Plane &Right (Alt+→)"), this,
        [this] { m_mpr->setObliqueAngles(0, 5); });
    view->addAction(
        tr("Rotate Plane &Up (Alt+↑)"), this,
        [this] { m_mpr->setObliqueAngles(5, 0); });
    view->addAction(
        tr("Rotate Plane &Down (Alt+↓)"), this,
        [this] { m_mpr->setObliqueAngles(-5, 0); });
    view->addAction(
        tr("&Reset Oblique (O)"), QKeySequence("O"), this,
        [this] { m_mpr->resetOblique(); });

    auto* tools = menuBar()->addMenu(tr("&Tools"));
    m_toolGroup = new QActionGroup(this);
    m_toolGroup->setExclusive(false); // we manage check state ourselves
    auto addTool = [&](const QString& label, Tool t, bool checked) {
        auto* a = tools->addAction(label, this, [this, t] {
            // Re-clicking the active tool toggles it off (back to W/L).
            setTool(m_activeTool == t ? Tool::WindowLevel : t);
        });
        a->setCheckable(true);
        a->setChecked(checked);
        m_toolGroup->addAction(a);
        return a;
    };
    addTool(tr("Window / Level"), Tool::WindowLevel, true);
    addTool(tr("Crosshair"), Tool::Crosshair, false);
    addTool(tr("Measure"), Tool::Measure, false);
    addTool(tr("ROI Stats"), Tool::Roi, false);
    addTool(tr("Angle"), Tool::Angle, false);
    addTool(tr("Pan"), Tool::Pan, false);
    addTool(tr("Zoom"), Tool::Zoom, false);
    tools->addSeparator();
    addTool(tr("Seg Brush"), Tool::Brush, false);
    addTool(tr("Seg Eraser"), Tool::Eraser, false);
    tools->addSeparator();
    m_cineAction = tools->addAction(tr("Cine"), this, [this](bool on) {
        m_mpr->setCinePlaying(on);
        m_singleView->setCinePlaying(on);
    });
    m_cineAction->setCheckable(true);
    m_cineAction->setShortcut(Qt::Key_Space);

    auto* speed = tools->addMenu(tr("Cine &Speed"));
    auto* speedGroup = new QActionGroup(this);
    for (int fps : {5, 10, 15, 30, 60}) {
        auto* a = speed->addAction(tr("%1 fps").arg(fps), this,
                                 [this, fps] { setCineFps(fps); });
        a->setCheckable(true);
        a->setChecked(fps == 15);
        speedGroup->addAction(a);
    }
    tools->addAction(tr("Cine &Faster  ]"), Qt::Key_BracketRight, this,
                     [this] { setCineFps(std::min(60, m_cineFps + 5)); });
    tools->addAction(tr("Cine &Slower  ["), Qt::Key_BracketLeft, this,
                     [this] { setCineFps(std::max(2, m_cineFps - 5)); });

    auto* smooth = tools->addAction(tr("&Sharpen (edge enhance)"), this,
                                    [this](bool on) {
                                        m_mpr->setSmoothing(on);
                                        m_singleView->setSmoothing(on);
                                        m_compareView->setSmoothing(on);
                                    });
    smooth->setCheckable(true);
    smooth->setShortcut(Qt::Key_D);

    tools->addAction(tr("Clear &Annotations"), this, [this] {
        m_mpr->clearAnnotations();
        m_singleView->clearAnnotations();
        m_compareView->clearAnnotations();
    });
    tools->addAction(tr("&Undo Annotation"), QKeySequence::Undo, this, [this] {
        m_mpr->undoLastAnnotation();
        m_singleView->undoAnnotation();
        m_compareView->undoAnnotation();
    });
    tools->addSeparator();
    // Flip shortcuts operate on the currently loaded series.
    tools->addAction(tr("&Flip 180°"), QKeySequence("Ctrl+F"), this, [this] {
        if (!m_seriesUid.isEmpty())
            setSeriesFlip(m_seriesUid, 1,
                          !(flipMaskFor(m_seriesUid) & 1));
    });
    tools->addAction(tr("Flip &Head↔Feet"), QKeySequence("Ctrl+Shift+F"),
                     this, [this] {
        if (!m_seriesUid.isEmpty())
            setSeriesFlip(m_seriesUid, 2,
                          !(flipMaskFor(m_seriesUid) & 2));
    });

    // Dedicated PACS top-level menu — nodes, query, auto-pull, media.
    auto* pacs = menuBar()->addMenu(tr("&PACS"));
    pacs->addAction(tr("&DICOM Nodes..."), this, [this] {
        NodesDialog dlg(this);
        dlg.exec();
        // Nodes may have changed — rebuild the source combo.
        if (m_sourceCombo) {
            m_sourceCombo->blockSignals(true);
            m_sourceCombo->clear();
            m_sourceCombo->addItem(tr("Local Library"));
            for (const auto& n : DicomNodes().nodes())
                m_sourceCombo->addItem(n.name);
            m_sourceCombo->addItem(tr("Manage Nodes..."));
            m_sourceCombo->blockSignals(false);
        }
    });
    pacs->addSeparator();
    pacs->addAction(tr("PACS &Query / Retrieve..."), this,
                    &MainWindow::openPacs);
    pacs->addAction(tr("DICOM&web (Orthanc / cloud PACS)..."), this,
                    [this] {
                        if (!m_webDialog) {
                            m_webDialog = new DicomWebDialog(this);
                            connect(m_webDialog,
                                    &DicomWebDialog::seriesRetrieved, this,
                                    [this](const QString& dir) {
                                        scanAndList(dir);
                                    });
                        }
                        m_webDialog->show();
                        m_webDialog->raise();
                    });
    pacs->addSeparator();
    pacs->addAction(tr("&Auto-Pull Rules..."), this, [this] {
        openAutoPullManager();
    });
    pacs->addAction(tr("&Sync Scheduled Tasks"), this, [this] {
        TaskScheduler::syncAll();
        m_statusLabel->setText(tr("Scheduled tasks synced."));
    });
    pacs->addSeparator();
    pacs->addAction(tr("Burn DICOM &CD / Media..."), this, [this] {
        openBurnMedia();
    });

    auto* ai = menuBar()->addMenu(tr("&AI"));
    ai->addAction(tr("Load ONNX &Model (one-off)..."), this,
                  &MainWindow::loadOnnxModel);
    ai->addAction(tr("&Run Model on Volume"), this,
                  &MainWindow::runAiModel);
    ai->addSeparator();
    auto* modelLib = new StickyMenu(tr("Model &Library"), ai);
    m_modelMenu = modelLib;
    ai->addMenu(modelLib);
    modelLib->addAction(tr("▶ Run Checked Models"), this,
                        &MainWindow::runSelectedModels);
    modelLib->addSeparator();
    refreshModelMenu();
    ai->addAction(tr("&Add Model to Library..."), this,
                  &MainWindow::addModelToLibrary);
    ai->addAction(tr("Run &Checked Models"), this,
                  &MainWindow::runSelectedModels);
    ai->addSeparator();
    m_pluginMenu = ai->addMenu(tr("Run Plugin"));
    ai->addSeparator();
    ai->addAction(tr("About Plugins..."), this, [this] {
        QStringList lines;
        for (auto* p : m_plugins->plugins())
            lines << QString("%1 %2 — %3")
                         .arg(p->name(), p->version(), p->description());
        QMessageBox::information(this, tr("AI Plugins"),
                                 lines.isEmpty() ? tr("No plugins found.")
                                                 : lines.join("\n"));
    });

    // Apply user-customized shortcuts (QSettings "shortcuts/<objectName>").
    applyCustomShortcuts();
}

void MainWindow::buildToolbar()
{
    auto* tb = addToolBar(tr("Tools"));
    tb->setMovable(false);
    tb->setFloatable(false);
    tb->setStyle(new ToolBarStyle);   // wider icon↔text gap
    tb->setIconSize({16, 16});
    tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    static const QHash<QString, QString> iconFor = {
        {"Window / Level", "contrast"}, {"Crosshair", "crosshair"},
        {"Measure", "ruler"},         {"ROI Stats", "square-dashed"},
        {"Angle", "pencil-ruler"},    {"Pan", "hand"},
        {"Zoom", "zoom-in"},          {"Seg Brush", "paintbrush"},
        {"Seg Eraser", "eraser"},     {"Cine", "play"},
    };

    // Segmented tool strip — each tool is an instrument button.
    // Seg Brush gets a split button: click = tool, chevron = size flyout.
    for (auto* a : m_toolGroup->actions()) {
        const auto it = iconFor.constFind(a->text());
        if (it != iconFor.constEnd())
            a->setIcon(toolIcon(it.value()));
        if (a->text() == "Seg Brush") {
            auto* btn = new QToolButton(tb);
            btn->setDefaultAction(a);
            btn->setPopupMode(QToolButton::MenuButtonPopup);
            btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            // Slider flyout — brush radius 1–50 mm with live readout.
            auto* menu = new QMenu(btn);
            auto* flyout = new QWidget(menu);
            auto* fl = new QVBoxLayout(flyout);
            fl->setContentsMargins(10, 8, 10, 8);
            auto* cap = new QLabel(tr("BRUSH RADIUS"), flyout);
            cap->setStyleSheet(
                "color: #8E99A6; font-size: 10px; font-weight: 600;");
            fl->addWidget(cap);
            auto* row = new QHBoxLayout;
            auto* slider = new QSlider(Qt::Horizontal, flyout);
            slider->setRange(1, 50);
            slider->setValue(5);
            slider->setFixedWidth(140);
            auto* readout = new QLabel(tr("5 mm"), flyout);
            readout->setStyleSheet("font-family: 'Cascadia Mono',"
                                   " 'Consolas', monospace;");
            readout->setFixedWidth(44);
            row->addWidget(slider);
            row->addWidget(readout);
            fl->addLayout(row);
            // Quick sizes.
            auto* presetRow = new QHBoxLayout;
            for (int mm : {2, 5, 10, 20}) {
                auto* p = new QPushButton(QString::number(mm), flyout);
                p->setFixedHeight(22);
                p->setToolTip(tr("%1 mm").arg(mm));
                connect(p, &QPushButton::clicked, this, [&, slider, mm] {
                    slider->setValue(mm);
                });
                presetRow->addWidget(p);
            }
            fl->addLayout(presetRow);
            connect(slider, &QSlider::valueChanged, this,
                    [this, readout](int v) {
                        readout->setText(tr("%1 mm").arg(v));
                        m_mpr->setBrushRadiusMm(v);
                        m_singleView->setBrushRadiusMm(v);
                        m_compareView->setBrushRadiusMm(v);
                    });
            auto* wa = new QWidgetAction(menu);
            wa->setDefaultWidget(flyout);
            menu->addAction(wa);
            btn->setMenu(menu);
            tb->addWidget(btn);
        } else {
            tb->addAction(a);
        }
        // editing tools form their own cluster after navigation tools
        if (a->text() == "Zoom")
            tb->addSeparator();
    }
    // Cine as a split button: click = play/pause, chevron = FPS flyout.
    m_cineAction->setIcon(toolIcon("play"));
    {
        auto* btn = new QToolButton(tb);
        btn->setDefaultAction(m_cineAction);
        btn->setPopupMode(QToolButton::MenuButtonPopup);
        btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        auto* menu = new QMenu(btn);
        auto* cap = new QWidgetAction(menu);
        auto* capLabel = new QLabel(tr("CINE SPEED"), menu);
        capLabel->setStyleSheet(
            "color: #8E99A6; font-size: 10px; font-weight: 600;"
            "padding: 4px 12px;");
        capLabel->setEnabled(false);
        cap->setDefaultWidget(capLabel);
        menu->addAction(cap);
        auto* fpsGroup = new QActionGroup(menu);
        fpsGroup->setExclusive(true);
        for (int fps : {5, 10, 15, 30, 60}) {
            auto* fa = menu->addAction(tr("%1 fps").arg(fps));
            fa->setCheckable(true);
            fa->setChecked(fps == m_cineFps);
            fpsGroup->addAction(fa);
            connect(fa, &QAction::triggered, this,
                    [this, fps] { setCineFps(fps); });
        }
        btn->setMenu(menu);
        tb->addWidget(btn);
    }
    tb->addSeparator();

    // Segmentation edit readouts.
    tb->addWidget(new QLabel(tr("Label")));
    auto* labelSpin = new QSpinBox(this);
    labelSpin->setRange(1, 8);
    labelSpin->setValue(1);
    labelSpin->setFixedWidth(44);
    labelSpin->setToolTip(tr("Segmentation label to paint"));
    connect(labelSpin, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int v) {
                m_mpr->setEditLabel(v);
                m_singleView->setEditLabel(v);
                m_compareView->setEditLabel(v);
            });
    tb->addWidget(labelSpin);
}

void MainWindow::openFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Open DICOM Folder"));
    if (!dir.isEmpty())
        scanAndList(dir);
}

void MainWindow::openFiles()
{
    const auto files = QFileDialog::getOpenFileNames(
        this, tr("Open DICOM Files"), {}, tr("DICOM Files (*.dcm);;All (*)"));
    if (files.isEmpty())
        return;
    std::vector<std::string> paths;
    for (const auto& f : files)
        paths.push_back(f.toStdString());
    m_progress->setVisible(true);
    QtConcurrent::run([this, paths] {
        try {
            auto vol = DicomLoader::loadFiles(paths, {});
            QMetaObject::invokeMethod(this, [this, vol] {
                m_volume = vol;
                m_mpr->setVolume(vol);
                m_singleView->setVolume(vol, Orientation::Axial);
                m_progress->setVisible(false);
            }, Qt::QueuedConnection);
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, e] {
                m_progress->setVisible(false);
                QMessageBox::critical(this, tr("Load failed"), e.what());
            }, Qt::QueuedConnection);
        }
    });
}

void MainWindow::openCompareSeries()
{
    const auto files = QFileDialog::getOpenFileNames(
        this, tr("Open Compare Series"), {},
        tr("DICOM Files (*.dcm);;All (*)"));
    if (files.isEmpty())
        return;
    std::vector<std::string> paths;
    for (const auto& f : files)
        paths.push_back(f.toStdString());
    m_progress->setVisible(true);
    QtConcurrent::run([this, paths] {
        try {
            auto vol = DicomLoader::loadFiles(paths, {});
            QMetaObject::invokeMethod(this, [this, vol] {
                const auto ext = vol->extent();
                const Orientation o =
                    ext[0] <= 1 ? Orientation::Sagittal
                    : ext[1] <= 1 ? Orientation::Coronal
                                  : Orientation::Axial;
                m_compareView->setVolume(vol, o);
                m_compareView->setVisible(true);
                if (m_compareAction)
                    m_compareAction->setChecked(true);
                m_stack->setCurrentIndex(0);
                m_progress->setVisible(false);
                m_statusLabel->setText(tr("Compare series loaded"));
            }, Qt::QueuedConnection);
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, e] {
                m_progress->setVisible(false);
                QMessageBox::critical(this, tr("Load failed"), e.what());
            }, Qt::QueuedConnection);
        }
    });
}

void MainWindow::loadCompareSeries(const QString& seriesUID)
{
    const auto meta = m_db->series(seriesUID);
    if (meta.files.empty())
        return;
    m_progress->setVisible(true);
    QtConcurrent::run([this, meta] {
        try {
            auto vol = DicomLoader::loadSeries(meta, {});
            QMetaObject::invokeMethod(this, [this, vol] {
                // View 2D projections (scouts) along their thin axis —
                // same rule as the main view.
                const auto ext = vol->extent();
                const Orientation o =
                    ext[0] <= 1 ? Orientation::Sagittal
                    : ext[1] <= 1 ? Orientation::Coronal
                                  : Orientation::Axial;
                m_compareView->setVolume(vol, o);
                m_compareView->setVisible(true);
                if (m_compareAction)
                    m_compareAction->setChecked(true);
                m_stack->setCurrentIndex(0);
                const auto& m = vol->meta();
                m_compareView->setInfoText(
                    QString("%1\n%2  %3")
                        .arg(m.patientName.c_str(), m.modality.c_str(),
                             m.seriesDescription.c_str()));
                m_progress->setVisible(false);
                m_statusLabel->setText(
                    tr("Compare: %1").arg(m.seriesDescription.c_str()));
            }, Qt::QueuedConnection);
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, e] {
                m_progress->setVisible(false);
                QMessageBox::critical(this, tr("Load failed"), e.what());
            }, Qt::QueuedConnection);
        }
    });
}

void MainWindow::popOutCompare(bool on)
{
    if (on && !m_compareWindow) {
        // Float the compare viewer into its own top-level window —
        // drag it to a second monitor.
        m_singleLay->removeWidget(m_compareView);
        m_compareWindow = new QMainWindow(this, Qt::Window);
        m_compareWindow->setWindowTitle(tr("Scanthia — Compare"));
        m_compareWindow->setCentralWidget(m_compareView);
        m_compareWindow->resize(900, 900);
        m_compareWindow->installEventFilter(this);
        m_compareWindow->show();
        m_compareView->setVisible(true);
    } else if (!on && m_compareWindow) {
        // Reattach into the split view.
        m_singleLay->addWidget(m_compareView, 1);
        m_compareWindow->removeEventFilter(this);
        m_compareWindow->deleteLater();
        m_compareWindow = nullptr;
        m_compareView->setVisible(true);
    }
    if (m_popoutAction && m_popoutAction->isChecked() != on)
        m_popoutAction->setChecked(on);
}

void MainWindow::openPacs()
{
    if (!m_pacsDialog) {
        m_pacsDialog = new PacsDialog(this);
        connect(m_pacsDialog, &PacsDialog::studyRetrieved, this,
                [this](const QString& dir) { scanAndList(dir); });
        // Start the local C-STORE SCP so PACS can push studies to us.
        const QString dl =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
            "/incoming";
        QDir().mkpath(dl);
        if (!m_storeScp->start(11112, "Scanthia", dl.toStdString()))
            m_statusLabel->setText(
                tr("Warning: store SCP failed to start on port 11112"));
        // Index pushed studies: debounce a rescan 2s after the last file.
        m_incomingTimer = new QTimer(this);
        m_incomingTimer->setSingleShot(true);
        m_incomingTimer->setInterval(2000);
        connect(m_incomingTimer, &QTimer::timeout, this,
                [this, dl] { scanAndList(dl); });
        connect(m_storeScp, &StoreScp::fileReceived, this,
                [this](const QString&) { m_incomingTimer->start(); });
    }
    m_pacsDialog->show();
    m_pacsDialog->raise();
}

void MainWindow::openAutoPullManager()
{
    AutoPullManager dlg(this);
    dlg.exec();
    // After edits, sync the Windows scheduled tasks so the on-disk
    // rules match what Task Scheduler will actually run.
    TaskScheduler::syncAll();
    refreshLibrary();
}

void MainWindow::openBurnMedia()
{
    BurnMediaDialog dlg(m_db, this);
    connect(&dlg, &BurnMediaDialog::stagedForBurn, this,
            [this](const QString& dir) { scanAndList(dir); });
    dlg.exec();
}

void MainWindow::openSegmentation()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Load Segmentation"), {},
        tr("Segmentations (*.dcm *.nii *.nii.gz *.nrrd *.mha);;All (*)"));
    if (path.isEmpty() || !m_volume)
        return;
    try {
        Segmentation seg =
            path.endsWith(".dcm")
                ? SegmentationLoader::loadDicomSeg(path.toStdString(), m_volume)
                : SegmentationLoader::loadLabelmap(path.toStdString(), m_volume);
        applySegmentation(seg);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Segmentation load failed"), e.what());
    }
}

void MainWindow::scanAndList(const QString& dir, std::function<void()> onDone)
{
    m_statusLabel->setText(tr("Indexing %1...").arg(dir));
    QtConcurrent::run([this, dir, onDone] {
        const int n =
            m_db->isOpen() ? m_db->indexDirectory(dir)
                           : int(DicomLoader::scanDirectory(dir.toStdString())
                                     .size());
        QMetaObject::invokeMethod(this, [this, n, dir, onDone] {
            refreshLibrary();
            m_statusLabel->setText(
                tr("Indexed %1 series from %2").arg(n).arg(dir));
            if (onDone)
                onDone();
        }, Qt::QueuedConnection);
    });
}

void MainWindow::loadSeries(const SeriesMeta& meta)
{
    saveAnnotations();           // persist annotations on the old volume
    m_seriesUid = QString::fromStdString(meta.seriesInstanceUID);
    // Apply the chosen slice order before the volume is built.
    SeriesMeta sorted = meta;
    DicomLoader::sortFiles(sorted.files,
                           static_cast<DicomLoader::SliceSort>(m_sliceSort));
    m_statusLabel->setText(tr("Loading %1...").arg(meta.seriesDescription.c_str()));
    m_progress->setVisible(true);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);

    // Generation token: bumping it cancels any in-flight load (worker
    // checks between slices) and drops stale progress callbacks.
    const int gen = ++m_loadGen;

    DicomLoader::StreamCallbacks scb;
    scb.shouldCancel = [this, gen] { return gen != m_loadGen.load(); };
    scb.onProgress = [this, gen](VolumePtr vol, int loaded, int total) {
        QMetaObject::invokeMethod(this, [this, gen, vol, loaded, total] {
            if (gen != m_loadGen.load())
                return;                    // stale load — drop the tick
            m_progress->setValue(total > 0 ? loaded * 100 / total : 0);
            m_statusLabel->setText(
                tr("Loading — %1 / %2 slices").arg(loaded).arg(total));
            const bool attached =
                m_volume && m_volume->vtkImage() == vol->vtkImage();
            if (!attached) {
                // First tick — volume exists (slices still filling).
                m_volume = vol;
                const auto ext = vol->extent();
                const bool is2D =
                    ext[0] <= 1 || ext[1] <= 1 || ext[2] <= 1;
                if (is2D) {
                    const Orientation o =
                        ext[0] <= 1 ? Orientation::Sagittal
                        : ext[1] <= 1 ? Orientation::Coronal
                                      : Orientation::Axial;
                    m_singleView->setVolume(m_volume, o);
                    m_stack->setCurrentIndex(0);
                } else {
                    m_mpr->setVolume(m_volume);
                    m_singleView->setVolume(m_volume, Orientation::Axial);
                }
            }
            // New slice data in the shared buffer — repaint.
            vol->vtkImage()->Modified();
            m_mpr->refresh();
            m_singleView->refresh();
            m_compareView->refresh();
        }, Qt::QueuedConnection);
    };

    QtConcurrent::run([this, gen, sorted, scb] {
        try {
            auto vol = DicomLoader::loadSeriesStreaming(sorted, scb);
            if (!vol)
                return;                    // cancelled mid-load
            QMetaObject::invokeMethod(this, [this, gen, vol] {
                if (gen != m_loadGen.load())
                    return;
                const bool alreadyAttached =
                    m_volume && m_volume->vtkImage() == vol->vtkImage();
                m_volume = vol;
                // Apply persisted display flips on the GUI thread —
                // vtkImageFlip's big allocations raced with the render
                // thread when done inside the loader worker.
                if (const unsigned mask = flipMaskFor(m_seriesUid))
                    m_volume = DicomLoader::flipVolume(vol, mask);
                // New volume → drop any stale overlay from a previous run.
                if (m_overlayActive) {
                    m_overlayActive = false;
                    m_mpr->setOverlay(nullptr, nullptr, 0.4);
                    m_singleView->setOverlay(nullptr, nullptr, 0.4);
                    if (m_segPanel)
                        m_segPanel->clear();
                }
                // Fusion belongs to the previous volume's grid — clear it.
                if (m_fusionImg) {
                    m_fusionImg = nullptr;
                    m_fusionLut = nullptr;
                    m_mpr->setFusion(nullptr, nullptr, 0);
                    m_singleView->setFusion(nullptr, nullptr, 0);
                }
                if (!alreadyAttached ||
                    m_volume->vtkImage() != vol->vtkImage()) {
                    // Non-streamed path (or a flipped volume) — attach now.
                    const auto ext = m_volume->extent();
                    const bool is2D =
                        ext[0] <= 1 || ext[1] <= 1 || ext[2] <= 1;
                    if (is2D) {
                        const Orientation o =
                            ext[0] <= 1 ? Orientation::Sagittal
                            : ext[1] <= 1 ? Orientation::Coronal
                                          : Orientation::Axial;
                        m_singleView->setVolume(m_volume, o);
                        m_stack->setCurrentIndex(0);
                    } else {
                        m_mpr->setVolume(m_volume);
                        m_singleView->setVolume(m_volume,
                                                Orientation::Axial);
                    }
                } else {
                    // Streamed and already attached — final repaint.
                    vol->vtkImage()->Modified();
                    m_mpr->refresh();
                    m_singleView->refresh();
                }
                const auto& m = m_volume->meta();
                // Radiology-style top-left HUD:
                //   patient / ID / DOB / sex
                //   study date / accession / institution
                //   modality  series #  series desc  dims
                const auto ext3 = m_volume->extent();
                QString hud = QString::fromStdString(m.patientName) + "\n";
                hud += "ID: " + QString::fromStdString(m.patientID);
                if (!m.patientBirthDate.empty())
                    hud += "   DOB: " + QString::fromStdString(m.patientBirthDate);
                if (!m.patientSex.empty())
                    hud += "   " + QString::fromStdString(m.patientSex);
                hud += "\n";
                if (!m.studyDate.empty())
                    hud += QString::fromStdString(m.studyDate);
                if (!m.accessionNumber.empty())
                    hud += "   Acc: " + QString::fromStdString(m.accessionNumber);
                hud += "\n";
                hud += QString::fromStdString(m.modality);
                if (!m.seriesNumber.empty())
                    hud += "  Se:" + QString::fromStdString(m.seriesNumber);
                if (!m.seriesDescription.empty())
                    hud += "  " + QString::fromStdString(m.seriesDescription);
                hud += QString("  %1x%2x%3")
                           .arg(ext3[0]).arg(ext3[1]).arg(ext3[2]);
                m_singleView->setInfoText(hud);
                m_mpr->setInfoText(hud);
                // "FLIPPED" badge on every pane when a flip is persisted.
                const bool flipped = flipMaskFor(m_seriesUid) != 0;
                m_singleView->setFlipBadge(flipped);
                m_compareView->setFlipBadge(flipped);
                for (auto o : {Orientation::Axial, Orientation::Sagittal,
                               Orientation::Coronal})
                    m_mpr->viewer(o)->setFlipBadge(flipped);
                // "DOWNSAMPLED" badge when memory-safe mode kicked in.
                const bool ds = vol->downsampled();
                m_singleView->setDownsampleBadge(ds);
                m_compareView->setDownsampleBadge(ds);
                for (auto o : {Orientation::Axial, Orientation::Sagittal,
                               Orientation::Coronal})
                    m_mpr->viewer(o)->setDownsampleBadge(ds);
                if (ds) {
                    const auto fe = vol->fullExtent();
                    m_statusLabel->setText(
                        tr("Memory-safe mode: %1x%2x%3 (full: %4x%5x%6)")
                            .arg(ext3[0]).arg(ext3[1]).arg(ext3[2])
                            .arg(fe[0]).arg(fe[1]).arg(fe[2]));
                }
                m_progress->setVisible(false);
                loadAnnotations();
                applyHangingProtocol();
                m_statusLabel->setText(
                    tr("Loaded %1 (%2x%3x%4)")
                        .arg(m.seriesDescription.c_str())
                        .arg(vol->extent()[0])
                        .arg(vol->extent()[1])
                        .arg(vol->extent()[2]));
            }, Qt::QueuedConnection);
        } catch (const std::exception& e) {
            const QString what =
                QString::fromUtf8(e.what()) ==
                        QLatin1String("std::exception")
                    ? tr("The series could not be read (unsupported object "
                         "type or corrupt file).")
                    : QString::fromUtf8(e.what());
            QMetaObject::invokeMethod(this, [this, gen, what, sorted] {
                if (gen != m_loadGen.load())
                    return;
                m_progress->setVisible(false);
                QMessageBox::critical(
                    this, tr("Load failed"),
                    tr("%1\n\nSeries: %2")
                        .arg(what, sorted.seriesDescription.c_str()));
            }, Qt::QueuedConnection);
        }
    });
}

void MainWindow::setLayout(int which)
{
    m_stack->setCurrentIndex(which);
    if (m_volume) {
        if (which == 1)
            m_mpr->setVolume(m_volume);
        else
            m_singleView->setVolume(m_volume, Orientation::Axial);
    }
}

void MainWindow::applyPreset(const WindowPreset& p)
{
    SliceViewer* v = m_stack->currentIndex() == 0
        ? m_singleView : m_mpr->viewer(Orientation::Axial);
    if (!m_volume || !v)
        return;
    if (p.width < 0) {
        v->autoWindowLevel();
    } else {
        v->setWindowLevel(p.width, p.center);
    }
}

void MainWindow::setTool(Tool t)
{
    m_activeTool = t;
    m_singleView->setActiveTool(t);
    m_compareView->setActiveTool(t);
    m_mpr->setActiveTool(t);
    // Keep the toolbar/menu check state in sync (group is non-exclusive;
    // we manage it here so re-clicking a tool toggles it off).
    const Tool order[] = {Tool::WindowLevel, Tool::Crosshair,
                          Tool::Measure, Tool::Roi, Tool::Angle,
                          Tool::Pan, Tool::Zoom, Tool::Brush,
                          Tool::Eraser};
    const auto acts = m_toolGroup->actions();
    for (int i = 0; i < acts.size() && i < int(std::size(order)); ++i)
        acts[i]->setChecked(order[i] == t);
    // Crosshair lines show only when the Crosshair tool is active.
    const bool showCross = (t == Tool::Crosshair);
    m_singleView->setShowCrosshair(showCross);
    m_mpr->setCrosshairVisible(showCross);
    if (showCross && m_volume) {
        auto ext = m_volume->extent();
        m_singleView->setCrosshairIjk({ext[0] / 2.0, ext[1] / 2.0,
                                       ext[2] / 2.0});
    }
}

void MainWindow::setCineFps(int fps)
{
    m_cineFps = fps;
    m_mpr->setCineFps(fps);
    m_singleView->setCineFps(fps);
    m_compareView->setCineFps(fps);
    m_statusLabel->setText(tr("Cine: %1 fps").arg(fps));
}

void MainWindow::loadOnnxModel()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Load ONNX Model"), {}, tr("ONNX Models (*.onnx)"));
    if (path.isEmpty())
        return;
    std::string err;
    if (!m_engine->loadModel(path.toStdString(), &err)) {
        QMessageBox::critical(this, tr("Model load failed"), err.c_str());
        return;
    }
    m_modelPath = path;
    m_modelStatus->setText(
        tr("AI: %1 [%2]")
            .arg(QFileInfo(path).fileName())
            .arg(QString::fromStdString(m_engine->providerName())));
    m_statusLabel->setText(tr("Model loaded: %1").arg(path));
    // Auto-run on current volume if one is loaded.
    if (m_volume)
        runAiModel();
}

void MainWindow::runAiModel()
{
    if (!m_engine->isLoaded()) {
        QMessageBox::information(this, tr("No model loaded"),
                                 tr("Load an ONNX model first via AI → Load Model."));
        return;
    }
    if (!m_volume) {
        QMessageBox::information(this, tr("No volume loaded"),
                                 tr("Load a DICOM series first."));
        return;
    }
    m_statusLabel->setText(tr("Running inference..."));
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setVisible(true);
    setCursor(Qt::BusyCursor);
    QtConcurrent::run([this] {
        try {
            Segmentation seg = m_engine->runSegmentation(
                m_volume, [this](int done, int total) {
                    QMetaObject::invokeMethod(this, [this, done, total]() {
                        m_progress->setRange(0, total);
                        m_progress->setValue(done);
                        m_statusLabel->setText(
                            tr("Inference: tile %1 / %2").arg(done).arg(total));
                    }, Qt::QueuedConnection);
                });
            QMetaObject::invokeMethod(this, [this, seg = std::move(seg)]() {
                applySegmentation(seg);
                m_progress->setRange(0, 100);
                m_progress->setVisible(false);
                unsetCursor();
            }, Qt::QueuedConnection);
        } catch (const std::exception& e) {
            const QString msg = e.what();
            QMetaObject::invokeMethod(this, [this, msg]() {
                QMessageBox::critical(this, tr("Inference failed"), msg);
                m_progress->setRange(0, 100);
                m_progress->setVisible(false);
                unsetCursor();
                m_statusLabel->setText(tr("Inference failed"));
            }, Qt::QueuedConnection);
        }
    });
}

QString MainWindow::modelsDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + "/models";
}

void MainWindow::refreshModelMenu()
{
    // Remove only the model entries — keep the "Run Checked" header
    // action and separator that live at the top of the sticky menu.
    for (auto* a : m_modelMenu->actions())
        if (!a->data().toString().isEmpty())
            m_modelMenu->removeAction(a);
    QDir d(modelsDir());
    const auto files = d.entryList({"*.onnx"}, QDir::Files, QDir::Name);
    for (const auto& f : files) {
        auto* a = m_modelMenu->addAction(f);
        a->setCheckable(true);
        a->setData(d.absoluteFilePath(f));
    }
}

void MainWindow::addModelToLibrary()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Add ONNX Model to Library"), {},
        tr("ONNX Models (*.onnx)"));
    if (path.isEmpty())
        return;
    QDir().mkpath(modelsDir());
    const QFileInfo fi(path);
    const QString base = fi.completeBaseName();
    const QString dst = modelsDir() + "/" + fi.fileName();
    if (QFile::exists(dst))
        QFile::remove(dst);
    if (!QFile::copy(path, dst)) {
        QMessageBox::warning(this, tr("Add Model"),
                             tr("Could not copy model file."));
        return;
    }
    // Copy the sibling preprocessing config so the engine picks up the
    // right spacing/clip/classes: <base>.ini or pre_processing.ini.
    const QString dir = fi.absolutePath();
    for (const QString& cand :
         {base + ".ini", QStringLiteral("pre_processing.ini")}) {
        const QString src = dir + "/" + cand;
        if (QFile::exists(src)) {
            const QString dstIni = modelsDir() + "/" + base + ".ini";
            if (QFile::exists(dstIni))
                QFile::remove(dstIni);
            QFile::copy(src, dstIni);
            break;
        }
    }
    refreshModelMenu();
    m_statusLabel->setText(tr("Model added: %1").arg(fi.fileName()));
}

void MainWindow::runSelectedModels()
{
    if (!m_volume) {
        QMessageBox::information(this, tr("No volume loaded"),
                                 tr("Load a DICOM series first."));
        return;
    }
    QStringList paths;
    for (auto* a : m_modelMenu->actions())
        if (a->isCheckable() && a->isChecked())
            paths << a->data().toString();
    if (paths.isEmpty()) {
        QMessageBox::information(
            this, tr("No models selected"),
            tr("Check one or more models under AI → Model Library."));
        return;
    }
    m_statusLabel->setText(tr("Running %1 model(s)...").arg(paths.size()));
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setVisible(true);
    setCursor(Qt::BusyCursor);
    QtConcurrent::run([this, paths] {
        try {
            Segmentation merged;
            int offset = 0;
            int modelIdx = 0;
            for (const QString& p : paths) {
                InferenceEngine eng;
                std::string err;
                if (!eng.loadModel(p.toStdString(), &err))
                    throw std::runtime_error(p.toStdString() + ": " + err);
                const int mi = ++modelIdx;
                const QString mname = QFileInfo(p).completeBaseName();
                Segmentation seg = eng.runSegmentation(
                    m_volume, [this, mi, total = int(paths.size()),
                               mname](int done, int tot) {
                        QMetaObject::invokeMethod(this,
                            [this, mi, total, mname, done, tot]() {
                                m_progress->setRange(0, tot);
                                m_progress->setValue(done);
                                m_statusLabel->setText(
                                    tr("%1: model %2/%3, tile %4/%5")
                                        .arg(mname).arg(mi).arg(total)
                                        .arg(done).arg(tot));
                            }, Qt::QueuedConnection);
                    });
                if (!merged.labelmap) {
                    merged = seg; // first result becomes the accumulator
                } else {
                    // Merge with a class offset so each model's labels
                    // get distinct indices (later models win on overlap).
                    auto* dst = merged.labelmap->GetPointData()
                                    ->GetScalars();
                    auto* src = seg.labelmap->GetPointData()->GetScalars();
                    const auto nvox = dst->GetNumberOfTuples();
                    for (vtkIdType v = 0; v < nvox; ++v) {
                        const int s = int(src->GetTuple1(v));
                        if (s > 0)
                            dst->SetTuple1(v, s + offset);
                    }
                    for (auto& [k, name] : seg.labelNames)
                        merged.labelNames[offset + k] =
                            mname.toStdString() + ": " + name;
                }
                offset = merged.labelNames.empty()
                             ? offset
                             : merged.labelNames.rbegin()->first;
            }
            merged.lut = Segmentation::makeLabelLut(
                std::max(1, merged.labelNames.empty()
                                ? 1
                                : merged.labelNames.rbegin()->first));
            QMetaObject::invokeMethod(this,
                [this, merged = std::move(merged)]() {
                    applySegmentation(merged);
                    m_progress->setRange(0, 100);
                    m_progress->setVisible(false);
                    unsetCursor();
                }, Qt::QueuedConnection);
        } catch (const std::exception& e) {
            const QString msg = e.what();
            QMetaObject::invokeMethod(this, [this, msg]() {
                QMessageBox::critical(this, tr("Inference failed"), msg);
                m_progress->setRange(0, 100);
                m_progress->setVisible(false);
                unsetCursor();
                m_statusLabel->setText(tr("Inference failed"));
            }, Qt::QueuedConnection);
        }
    });
}

void MainWindow::runPlugin(const QString& name)
{
    auto* plugin = m_plugins->pluginNamed(name);
    if (!plugin || !m_volume)
        return;
    try {
        applySegmentation(plugin->run(m_volume, *m_engine));
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Plugin failed"), e.what());
    }
}

void MainWindow::applySegmentation(const Segmentation& seg)
{
    m_overlay = seg;
    m_overlayActive = true;
    m_mpr->setOverlay(seg.labelmap, seg.lut, 0.4);
    m_singleView->setOverlay(seg.labelmap, seg.lut, 0.4);
    populateSegPanel();
    QStringList names;
    for (auto& [id, n] : seg.labelNames)
        names << n.c_str();
    m_statusLabel->setText(tr("Segmentation: %1").arg(names.join(", ")));
}

void MainWindow::populateSegPanel()
{
    if (!m_segDock) {
        m_segDock = new QDockWidget(this);
        m_segPanel = new QTreeWidget(m_segDock);
        m_segPanel->setHeaderLabels({tr("Label"), tr("Voxels"), tr("mL")});
        m_segPanel->setRootIsDecorated(false);
        m_segDock->setWidget(m_segPanel);

        // Custom title bar: spaced legend + collapse chevron. Collapsing
        // swaps the panel for a slim rail — one click brings it back.
        auto* tbar = new QWidget(m_segDock);
        auto* tbl = new QHBoxLayout(tbar);
        tbl->setContentsMargins(8, 2, 2, 2);
        tbl->setSpacing(4);
        auto* ttl = new QLabel(tr("AI RESULTS"), tbar);
        ttl->setStyleSheet(
            "color: #8E99A6; font-size: 10px; font-weight: 600;");
        auto* collapseBtn = new QToolButton(tbar);
        collapseBtn->setIcon(makeChevronIcon(true, QColor(0x8E,0x99,0xA6)));
        collapseBtn->setIconSize({12, 12});
        collapseBtn->setFixedSize(22, 22);
        collapseBtn->setToolTip(tr("Collapse AI Results"));
        collapseBtn->setStyleSheet(
            "QToolButton { background: transparent; border: none; }"
            "QToolButton:hover { background: #3D4750; }");
        tbl->addWidget(ttl);
        tbl->addStretch();
        tbl->addWidget(collapseBtn);
        m_segDock->setTitleBarWidget(tbar);

        m_segRail = new RailButton(tr("AI RESULTS"), m_segDock);
        m_segRail->setToolTip(tr("Expand AI Results"));

        connect(collapseBtn, &QToolButton::clicked, this, [this] {
            m_segDock->setWidget(m_segRail);
            m_segRail->show();
        });
        connect(m_segRail, &QToolButton::clicked, this, [this] {
            m_segDock->setWidget(m_segPanel);
            m_segPanel->show();
        });

        addDockWidget(Qt::RightDockWidgetArea, m_segDock);
        connect(m_segDock, &QDockWidget::visibilityChanged, this,
                [this](bool v) {
                    if (m_segDockAction)
                        m_segDockAction->setChecked(v);
                    // Re-showing via the menu restores the full panel
                    // even if it was collapsed to the rail.
                    if (v && m_segDock->widget() == m_segRail) {
                        m_segDock->setWidget(m_segPanel);
                        m_segPanel->show();
                    }
                });
        connect(m_segPanel, &QTreeWidget::itemChanged, this,
                [this](QTreeWidgetItem* it, int) {
                    if (!m_overlay.lut)
                        return;
                    const int idx =
                        it->data(0, Qt::UserRole).toInt();
                    double rgba[4];
                    m_overlay.lut->GetTableValue(idx, rgba);
                    rgba[3] = (it->checkState(0) == Qt::Checked)
                                  ? 1.0 : 0.0;
                    m_overlay.lut->SetTableValue(idx, rgba);
                    m_overlay.lut->Modified();
                    m_mpr->setOverlay(m_overlay.labelmap,
                                      m_overlay.lut, 0.4);
                    m_singleView->setOverlay(m_overlay.labelmap,
                                             m_overlay.lut, 0.4);
                });
    }
    m_segPanel->blockSignals(true);
    m_segPanel->clear();

    // Count voxels per label and report volume in mL.
    auto* arr = m_overlay.labelmap->GetPointData()->GetScalars();
    std::map<int, long> counts;
    const auto nvox = arr->GetNumberOfTuples();
    for (vtkIdType v = 0; v < nvox; ++v) {
        const int l = int(arr->GetTuple1(v));
        if (l > 0)
            ++counts[l];
    }
    const auto sp = m_volume->spacing();
    const double mlPerVoxel = sp[0] * sp[1] * sp[2] / 1000.0;

    for (auto& [id, name] : m_overlay.labelNames) {
        auto* it = new QTreeWidgetItem(m_segPanel);
        it->setText(0, name.c_str());
        it->setText(1, QString::number(counts[id]));
        it->setText(2, QString::number(counts[id] * mlPerVoxel,
                                       'f', 1));
        it->setData(0, Qt::UserRole, id);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(0, Qt::Checked);
    }
    m_segPanel->blockSignals(false);
    m_segDock->setVisible(true);
    if (m_segDockAction)
        m_segDockAction->setChecked(true);
    if (counts.empty())
        m_statusLabel->setText(
            tr("Segmentation complete — model found no structures "
               "(all background)."));
}

void MainWindow::saveAnnotations()
{
    if (m_seriesUid.isEmpty())
        return;
    QJsonObject root;
    root["mpr"]    = m_mpr->annotationsToJson();
    root["single"] = m_singleView->annotationsToJson();
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + "/annotations";
    QDir().mkpath(dir);
    QFile f(dir + "/" + m_seriesUid + ".json");
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

void MainWindow::loadAnnotations()
{
    if (m_seriesUid.isEmpty())
        return;
    const QString path =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + "/annotations/" + m_seriesUid + ".json";
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return;
    const auto root = QJsonDocument::fromJson(f.readAll()).object();
    m_mpr->annotationsFromJson(root.value("mpr").toObject());
    m_singleView->annotationsFromJson(root.value("single").toObject());
}

void MainWindow::exportScreenshot()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save Screenshot"), "screenshot.png",
        tr("PNG Image (*.png);;JPEG Image (*.jpg *.jpeg)"));
    if (path.isEmpty())
        return;
    const QString fmt =
        path.endsWith(".jpg", Qt::CaseInsensitive) ||
        path.endsWith(".jpeg", Qt::CaseInsensitive) ? "JPG" : "PNG";
    if (!m_stack->grab().save(path, fmt.toUtf8().constData(), 95))
        QMessageBox::warning(this, tr("Screenshot"),
                             tr("Could not save %1").arg(path));
    else
        m_statusLabel->setText(tr("Screenshot saved: %1").arg(path));
}

void MainWindow::exportVideo()
{
    if (!m_volume)
        return;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Video"), "cine.mp4",
        tr("MP4 Video (*.mp4)"));
    if (path.isEmpty())
        return;

    auto* sv = m_singleView;
    const int n = sv->sliceCount();
    if (n < 2) {
        QMessageBox::information(this, tr("Export Video"),
            tr("The current view has a single slice."));
        return;
    }
    const int startSlice = sv->slice();

    m_progress->setVisible(true);
    m_progress->setRange(0, n);
    m_statusLabel->setText(tr("Rendering video..."));

    // Pipe PNG frames into ffmpeg → H.264 MP4.
    QProcess ff;
    ff.start("ffmpeg", {"-y", "-f", "image2pipe", "-vcodec", "png",
                        "-r", QString::number(m_cineFps),
                        "-i", "-", "-vcodec", "libx264",
                        "-pix_fmt", "yuv420p", path});
    if (!ff.waitForStarted(5000)) {
        m_progress->setVisible(false);
        QMessageBox::warning(this, tr("Export Video"),
                             tr("Could not start ffmpeg — is it installed?"));
        return;
    }
    for (int k = 0; k < n; ++k) {
        sv->setSlice(k);
        QCoreApplication::processEvents();   // let the frame draw
        const QImage img = sv->grab().toImage();
        QByteArray png;
        QBuffer buf(&png);
        img.save(&buf, "PNG");
        ff.write(png);
        ff.waitForBytesWritten();
        m_progress->setValue(k + 1);
    }
    ff.closeWriteChannel();
    ff.waitForFinished(-1);
    sv->setSlice(startSlice);
    m_progress->setVisible(false);
    if (ff.exitCode() == 0)
        m_statusLabel->setText(tr("Video saved: %1").arg(path));
    else
        QMessageBox::warning(this, tr("Export Video"),
            tr("ffmpeg failed:\n%1")
                .arg(QString::fromLocal8Bit(ff.readAllStandardError())));
}

void MainWindow::printView()
{
    const QImage img = m_stack->grab().toImage();
    if (img.isNull())
        return;
    QPrinter prn(QPrinter::HighResolution);
    QPrintDialog dlg(&prn, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    QPainter p(&prn);
    // Fit the capture to the page, preserving aspect.
    const QRect page = p.viewport();
    const QSize scaled = img.size().scaled(page.size(),
                                           Qt::KeepAspectRatio);
    const QRect target((page.width() - scaled.width()) / 2,
                       (page.height() - scaled.height()) / 2,
                       scaled.width(), scaled.height());
    p.drawImage(target, img);
    p.end();
    m_statusLabel->setText(tr("Sent to printer"));
}

void MainWindow::applyHangingProtocol()
{
    if (!m_volume)
        return;
    const auto& m = m_volume->meta();
    const QString hay = QString::fromStdString(
        m.modality + " " + m.bodyPart + " " + m.seriesDescription).toLower();

    // Color map for nuclear medicine.
    if (m.modality == "PT" || m.modality == "NM") {
        m_mpr->setColorMap(3);
        m_singleView->setColorMap(3);
    }
    // Anatomy-based window presets (skip if DICOM already carries WL).
    if (m.hasWindowing)
        return;
    auto preset = [this](double w, double c) {
        m_mpr->setWindowLevel(w, c);
        m_singleView->setWindowLevel(w, c);
    };
    if (hay.contains("lung") || hay.contains("hrct") ||
        hay.contains("chest") || hay.contains("thorax"))
        preset(1500, -600);          // Lung
    else if (hay.contains("bone") || hay.contains("spine") ||
             hay.contains("ct myelogram"))
        preset(2000, 300);           // Bone
    else if (hay.contains("brain") || hay.contains("head") ||
             hay.contains("ct angiogram"))
        preset(80, 40);              // Brain
    else if (hay.contains("liver") || hay.contains("abdomen") ||
             hay.contains("pelvis"))
        preset(150, 30);             // Liver/abdomen
}

void MainWindow::applyCustomShortcuts()
{
    // User overrides live in QSettings("shortcuts") as action.objectName
    // → QKeySequence string. Actions without an entry keep their
    // defaults. Set an objectName on any action you want remappable.
    QSettings s;
    s.beginGroup("shortcuts");
    for (auto* a : findChildren<QAction*>()) {
        const QString name = a->objectName();
        if (name.isEmpty())
            continue;
        const QVariant v = s.value(name);
        if (!v.isValid())
            continue;
        const QKeySequence ks(v.toString());
        if (!ks.isEmpty())
            a->setShortcut(ks);
    }
}

void MainWindow::updateHover(std::array<double,3> ijk, double value)
{
    m_statusLabel->setText(
        QString("[%1, %2, %3]  %4")
            .arg(int(ijk[0])).arg(int(ijk[1])).arg(int(ijk[2]))
            .arg(value, 0, 'f', 1));
}

void MainWindow::debugOpen(const QString& dir)
{
    scanAndList(dir);
    const auto studies = m_db->studies();
    for (const auto& st : studies) {
        const auto series = m_db->seriesOf(st.studyUID);
        if (!series.isEmpty()) {
            // Defer past the startup show/render pass.
            QTimer::singleShot(500, this, [this, series] {
                loadSeries(series.first());
            });
            return;
        }
    }
}

void MainWindow::openUrl(const QString& url)
{
    // scanthia://open?path=<dir> — index + load.
    // scanthia://study/<uid>  — find in library and load.
    QString u = url;
    if (u.startsWith("scanthia://", Qt::CaseInsensitive))
        u = u.mid(11);
    else if (u.startsWith("scanthia:", Qt::CaseInsensitive))
        u = u.mid(9);
    const int qi = u.indexOf('?');
    const QString cmd = qi < 0 ? u : u.left(qi);
    const QString query = qi < 0 ? QString() : u.mid(qi + 1);

    if (cmd.compare("open", Qt::CaseInsensitive) == 0) {
        QString path;
        for (const auto& kv : query.split('&')) {
            const int eq = kv.indexOf('=');
            if (kv.left(eq).compare("path", Qt::CaseInsensitive) == 0)
                path = QUrl::fromPercentEncoding(kv.mid(eq + 1).toUtf8());
        }
        if (!path.isEmpty())
            debugOpen(path);
        return;
    }
    if (cmd.startsWith("study/", Qt::CaseInsensitive)) {
        const QString uid = QUrl::fromPercentEncoding(
            cmd.mid(6).toUtf8());
        for (const auto& st : m_db->studies()) {
            if (st.studyUID != uid)
                continue;
            const auto series = m_db->seriesOf(st.studyUID);
            if (!series.isEmpty())
                loadSeries(series.first());
            return;
        }
        m_statusLabel->setText(tr("scanthia:// study not in library: %1")
                                   .arg(uid));
        return;
    }
    m_statusLabel->setText(tr("Unrecognized scanthia:// URL: %1").arg(url));
}

QStringList MainWindow::collectFiles(const QString& uid,
                                     bool wholeStudy) const
{
    QStringList out;
    if (wholeStudy) {
        for (const auto& s : m_db->seriesOf(uid))
            for (const auto& f : s.files)
                out << QString::fromStdString(f);
    } else {
        const auto s = m_db->series(uid);
        for (const auto& f : s.files)
            out << QString::fromStdString(f);
    }
    return out;
}

void MainWindow::onLibraryContextMenu(const QPoint& pos)
{
    auto* item = m_library->itemAt(pos);
    if (!item || !m_db)
        return;
    const QString uid = item->data(0, Qt::UserRole).toString();
    if (uid.isEmpty())
        return;
    const bool study = item->data(0, RoleIsStudy).toInt() != 0;
    const bool remote = item->data(0, RoleIsRemote).toInt() != 0;

    // Remote items: only retrieve actions — no local-library ops yet.
    if (remote) {
        QMenu menu(this);
        QAction* ret = menu.addAction(tr("Retrieve && Open Study"));
        QAction* retLib = menu.addAction(tr("Retrieve to Library"));
        QAction* sel = menu.exec(m_library->viewport()->mapToGlobal(pos));
        if (sel == ret || sel == retLib)
            retrieveRemoteStudy(uid);
        return;
    }

    QMenu menu(this);
    QAction* zip  = menu.addAction(
        study ? tr("Export Study to ZIP...") : tr("Export Series to ZIP..."));
    QAction* anon = menu.addAction(
        study ? tr("Anonymize Study Export...")
              : tr("Anonymize Series Export..."));
    QAction* send = menu.addAction(
        study ? tr("Send Study to DICOM Node...")
              : tr("Send Series to DICOM Node..."));
    QAction* burn = nullptr;
    if (study)
        burn = menu.addAction(tr("Burn Study to CD / Media..."));
    menu.addSeparator();
    QAction* del = menu.addAction(
        study ? tr("Delete Study from Library")
              : tr("Delete Series from Library"));

    // Per-series display flips (series items only).
    QAction* flip180 = nullptr;
    QAction* flipZ   = nullptr;
    QAction* fuse    = nullptr;
    if (!study) {
        menu.addSeparator();
        const unsigned mask = flipMaskFor(uid);
        flip180 = menu.addAction(tr("Flip 180° (fix upside-down)"));
        flip180->setCheckable(true);
        flip180->setChecked(mask & 1);
        flipZ = menu.addAction(tr("Flip Head ↔ Feet"));
        flipZ->setCheckable(true);
        flipZ->setChecked(mask & 2);
        // Fusion — overlay this series (e.g. PET) on the loaded volume.
        if (m_volume && uid != m_seriesUid) {
            menu.addSeparator();
            fuse = menu.addAction(
                tr("Fuse This Series Onto Current View"));
        }
    }

    QAction* sel = menu.exec(m_library->viewport()->mapToGlobal(pos));
    if (!sel)
        return;
    if (sel == zip)
        exportSeriesZip(uid, study);
    else if (sel == anon)
        anonymizeExport(uid, study);
    else if (sel == send)
        sendToNode(uid, study);
    else if (sel == del)
        deleteLibraryItem(uid, study);
    else if (sel == burn)
        openBurnMedia();
    else if (sel == flip180)
        setSeriesFlip(uid, 1, flip180->isChecked());
    else if (sel == flipZ)
        setSeriesFlip(uid, 2, flipZ->isChecked());
    else if (sel == fuse)
        fuseSeries(uid);
}

void MainWindow::fuseSeries(const QString& uid)
{
    if (!m_volume)
        return;
    const auto series = m_db->series(uid);
    if (series.files.empty())
        return;
    const int gen = m_loadGen.load();   // cancel if the base series swaps
    const QString name = QString::fromStdString(series.seriesDescription);
    m_statusLabel->setText(tr("Fusing %1...").arg(name));

    QtConcurrent::run([this, gen, series, name] {
        try {
            DicomLoader::StreamCallbacks cb;
            cb.shouldCancel = [this, gen] {
                return gen != m_loadGen.load();
            };
            auto fused = DicomLoader::loadSeriesStreaming(series, cb);
            if (!fused || gen != m_loadGen.load())
                return;
            auto img = DicomLoader::resampleOntoGrid(fused, m_volume);
            if (gen != m_loadGen.load())
                return;
            QMetaObject::invokeMethod(this, [this, img, name] {
                if (!m_volume)
                    return;
                // Hot-iron LUT with an alpha ramp — below ~25% of max
                // the fusion is transparent so anatomy shows through.
                double range[2];
                img->GetScalarRange(range);
                const double lo = range[0] + (range[1] - range[0]) * 0.25;
                auto lut = vtkSmartPointer<vtkLookupTable>::New();
                lut->SetNumberOfTableValues(256);
                lut->SetTableRange(lo, range[1]);
                lut->SetHueRange(0.0, 0.17);   // red → orange → yellow
                lut->SetSaturationRange(1.0, 1.0);
                lut->SetValueRange(0.4, 1.0);
                lut->SetAlphaRange(0.0, 1.0);  // fade in over the ramp
                lut->Build();
                m_fusionImg = img;
                m_fusionLut = lut;
                m_mpr->setFusion(img, lut, 0.5);
                m_singleView->setFusion(img, lut, 0.5);
                m_statusLabel->setText(tr("Fused: %1").arg(name));
            }, Qt::QueuedConnection);
        } catch (const std::exception& e) {
            const QString what = QString::fromUtf8(e.what());
            QMetaObject::invokeMethod(this, [this, what] {
                QMessageBox::warning(this, tr("Fusion"),
                                     tr("Could not fuse: %1").arg(what));
            }, Qt::QueuedConnection);
        }
    });
}

void MainWindow::clearFusion()
{
    m_fusionImg = nullptr;
    m_fusionLut = nullptr;
    m_mpr->setFusion(nullptr, nullptr, 0);
    m_singleView->setFusion(nullptr, nullptr, 0);
    m_statusLabel->setText(tr("Fusion cleared"));
}

void MainWindow::anonymizeExport(const QString& uid, bool wholeStudy)
{
    const QStringList files = collectFiles(uid, wholeStudy);
    if (files.isEmpty()) {
        QMessageBox::information(this, tr("Anonymize"),
                                 tr("No files recorded for this item."));
        return;
    }
    AnonymizeDialog dlg(files, this);
    dlg.exec();
}

unsigned MainWindow::flipMaskFor(const QString& seriesUID) const
{
    const auto list = QSettings().value("flips").toStringList();
    for (const auto& e : list)
        if (e.startsWith(seriesUID + "|"))
            return e.split('|').last().toUInt();
    return 0;
}

void MainWindow::setSeriesFlip(const QString& seriesUID, unsigned bit,
                               bool on)
{
    QSettings s;
    QStringList list = s.value("flips").toStringList();
    unsigned mask = 0;
    for (auto it = list.begin(); it != list.end();) {
        if (it->startsWith(seriesUID + "|")) {
            mask = it->split('|').last().toUInt();
            it = list.erase(it);
        } else
            ++it;
    }
    if (on)
        mask |= bit;
    else
        mask &= ~bit;
    if (mask)
        list << seriesUID + "|" + QString::number(mask);
    s.setValue("flips", list);

    // Reload if this series is currently on screen.
    if (m_seriesUid == seriesUID)
        loadSeries(m_db->series(seriesUID));
}

void MainWindow::exportSeriesZip(const QString& uid, bool wholeStudy)
{
    const QStringList files = collectFiles(uid, wholeStudy);
    if (files.isEmpty()) {
        QMessageBox::information(this, tr("Export"),
                                 tr("No files recorded for this item."));
        return;
    }
    const QString dst = QFileDialog::getSaveFileName(
        this, tr("Export to ZIP"), "scanthia-export.zip",
        tr("ZIP archives (*.zip)"));
    if (dst.isEmpty())
        return;

    // Long file lists blow the command-line limit — feed PowerShell a
    // paths file instead.
    QTemporaryFile listFile;
    if (!listFile.open())
        return;
    listFile.write(files.join('\n').toUtf8());
    listFile.flush();

    m_statusLabel->setText(tr("Packing %1 file(s)...").arg(files.size()));
    const QString ps = QStringLiteral(
        "Compress-Archive -LiteralPath (Get-Content -LiteralPath '%1') "
        "-DestinationPath '%2' -Force")
        .arg(listFile.fileName(), dst);
    const int rc = QProcess::execute(
        "powershell",
        {"-NoProfile", "-NonInteractive", "-Command", ps});
    m_statusLabel->setText(rc == 0
        ? tr("Exported %1 file(s) to %2").arg(files.size()).arg(dst)
        : tr("Export failed (PowerShell Compress-Archive rc=%1)").arg(rc));
}

void MainWindow::sendToNode(const QString& uid, bool wholeStudy)
{
    const QStringList files = collectFiles(uid, wholeStudy);
    if (files.isEmpty()) {
        QMessageBox::information(this, tr("Send to Node"),
                                 tr("No files recorded for this item."));
        return;
    }

    // Node picker: saved DICOM nodes from the store, plus a "Custom"
    // option for ad-hoc targets. OsiriX-style Locations integration.
    DicomNodes store;
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Send to DICOM Node"));
    auto* lay = new QFormLayout(&dlg);
    auto* nodeCombo = new QComboBox(&dlg);
    nodeCombo->addItem(tr("(custom)"));
    for (const auto& n : store.nodes())
        nodeCombo->addItem(n.name);
    auto* host    = new QLineEdit("localhost", &dlg);
    auto* port    = new QSpinBox(&dlg);
    auto* called  = new QLineEdit("ORTHANC", &dlg);
    auto* calling = new QLineEdit("SCANTHIA", &dlg);
    port->setRange(1, 65535);
    port->setValue(4242);
    lay->addRow(tr("Node"), nodeCombo);
    lay->addRow(tr("Host"), host);
    lay->addRow(tr("Port"), port);
    lay->addRow(tr("Called AE"), called);
    lay->addRow(tr("Calling AE"), calling);
    // Prefill from the default node.
    const auto dn = store.defaultNode();
    if (!dn.name.isEmpty()) {
        host->setText(QString::fromStdString(dn.node.host));
        port->setValue(dn.node.port);
        called->setText(QString::fromStdString(dn.node.calledAET));
        calling->setText(QString::fromStdString(dn.node.callingAET));
        nodeCombo->setCurrentText(dn.name);
    }
    connect(nodeCombo, &QComboBox::currentTextChanged, &dlg,
            [&](const QString& name) {
                if (name == tr("(custom)"))
                    return;
                const auto n = store.get(name);
                host->setText(QString::fromStdString(n.node.host));
                port->setValue(n.node.port);
                called->setText(QString::fromStdString(n.node.calledAET));
                calling->setText(QString::fromStdString(n.node.callingAET));
            });
    auto* btns = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    lay->addRow(btns);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted)
        return;

    // storescu ships next to the exe (packaged) or in ucrt64/bin (dev).
    QString scu = QCoreApplication::applicationDirPath() + "/storescu.exe";
    if (!QFile::exists(scu))
        scu = QStandardPaths::findExecutable("storescu");
    if (scu.isEmpty()) {
        QMessageBox::warning(this, tr("Send to Node"),
                             tr("storescu.exe not found."));
        return;
    }

    // storescu takes a directory with +sd — stage hardlinks so we send
    // exactly this series' files (no copy, same volume).
    auto* stage = new QTemporaryDir(
        QDir::tempPath() + "/scanthia-send-XXXXXX");
    if (!stage->isValid()) {
        QMessageBox::warning(this, tr("Send to Node"),
                             tr("Could not create temp directory."));
        return;
    }
    int i = 0;
    for (const auto& f : files) {
        const QString dst =
            stage->filePath(QString("f%1.dcm").arg(i++, 6, 10, QChar('0')));
        CreateHardLinkW(reinterpret_cast<LPCWSTR>(dst.utf16()),
                        reinterpret_cast<LPCWSTR>(f.utf16()), nullptr);
    }

    const QStringList args = {
        "-aet", calling->text(), "-aec", called->text(),
        "+sd", stage->path(), host->text(), port->text()};
    m_statusLabel->setText(tr("Sending %1 file(s) to %2...")
                               .arg(files.size()).arg(host->text()));
    auto* proc = new QProcess(this);
    connect(proc, &QProcess::finished, this,
            [this, proc, stage, n = files.size()](int rc, auto) {
                m_statusLabel->setText(
                    rc == 0 ? tr("Sent %1 file(s).").arg(n)
                            : tr("Send failed (storescu rc=%1): %2")
                                  .arg(rc)
                                  .arg(QString::fromLocal8Bit(
                                        proc->readAllStandardError())
                                        .left(200)));
                proc->deleteLater();
                delete stage;   // QTemporaryDir is not a QObject
            });
    proc->start(scu, args);
}

void MainWindow::deleteLibraryItem(const QString& uid, bool wholeStudy)
{
    const QString what = wholeStudy ? tr("study") : tr("series");
    if (QMessageBox::question(
            this, tr("Delete"),
            tr("Remove this %1 from the local library index?\n"
               "The source DICOM files are not deleted.").arg(what))
        != QMessageBox::Yes)
        return;
    if (wholeStudy)
        m_db->removeStudy(uid);
    else
        m_db->removeSeries(uid);
    refreshLibrary();
}

} // namespace meda
