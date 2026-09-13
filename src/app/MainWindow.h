#pragma once

#include "Types.h"
#include "Volume.h"
#include "Segmentation.h"

#include <QMainWindow>

#include <vtkLookupTable.h>

#include <atomic>
#include <functional>

class QTreeWidget;
class QStackedWidget;
class QLabel;
class QProgressBar;
class QActionGroup;
class QAction;
class QDockWidget;
class QHBoxLayout;
class QComboBox;
class QLineEdit;
class QToolButton;

namespace meda {

class SliceViewer;
class MprWidget;
class PacsDialog;
class DicomWebDialog;
class StoreScp;
class PluginManager;
class InferenceEngine;
class StudyDatabase;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    /// Testing hook: index a directory and load its first series.
    void debugOpen(const QString& dir);
    /// Handle a scanthia:// URL — open?path=<dir> or study/<uid>.
    void openUrl(const QString& url);

private:
    void buildMenus();
    void buildDock();
    void buildToolbar();
    void openFolder();
    void openFiles();
    void openCompareSeries();
    void openPacs();
    void openAutoPullManager();
    void openBurnMedia();
    void openSegmentation();
    /// Index `dir` into the local library DB. `onDone` (if set) runs on
    /// the GUI thread after refreshLibrary() — used to auto-open a
    /// just-retrieved study once its series are indexed.
    void scanAndList(const QString& dir,
                     std::function<void()> onDone = nullptr);
    void refreshLibrary();
    void loadSeries(const SeriesMeta& meta);
    void loadCompareSeries(const QString& seriesUID);
    void setLayout(int which); // 0 = single, 1 = MPR
    void applyPreset(const WindowPreset& p);
    void setTool(Tool t);
    void runPlugin(const QString& name);
    void loadOnnxModel();
    void runAiModel();
    void runSelectedModels();
    void addModelToLibrary();
    void refreshModelMenu();
    void populateSegPanel();
    void onLibraryContextMenu(const QPoint& pos);
    void onSourceChanged(int idx);
    void onSourceRefresh();
    void queryRemoteSource(const QString& filter);
    void retrieveRemoteStudy(const QString& studyUID);
    void exportSeriesZip(const QString& seriesUID, bool wholeStudy);
    /// Load `seriesUID`'s volume and fuse it (color overlay) onto the
    /// currently displayed volume.
    void fuseSeries(const QString& seriesUID);
    /// Remove the fusion layer from all viewers.
    void clearFusion();
    /// Strip identifying tags into anonymized copies (export dialog).
    void anonymizeExport(const QString& seriesUID, bool wholeStudy);
    /// Cine-loop the single view through all slices → MP4 via ffmpeg.
    void exportVideo();
    /// Print the current view, or save it as a PDF.
    void printView();
    void sendToNode(const QString& seriesUID, bool wholeStudy);
    void deleteLibraryItem(const QString& seriesUID, bool wholeStudy);
    QStringList collectFiles(const QString& uid, bool wholeStudy) const;
    unsigned flipMaskFor(const QString& seriesUID) const;
    void setSeriesFlip(const QString& seriesUID, unsigned bit, bool on);
    QString modelsDir() const;
    void applySegmentation(const Segmentation& seg);
    void saveAnnotations();
    void loadAnnotations();
    void exportScreenshot();
    void applyHangingProtocol();
    /// Apply user-customized shortcuts from QSettings("shortcuts") to
    /// all registered actions. Called once at startup; actions keep
    /// their default shortcuts when no override is set.
    void applyCustomShortcuts();
    bool eventFilter(QObject* o, QEvent* e) override;
    void updateHover(std::array<double,3> ijk, double value);

    VolumePtr     m_volume;
    QStackedWidget* m_stack;
    SliceViewer*  m_singleView;
    SliceViewer*  m_compareView = nullptr;   // right pane in compare mode
    MprWidget*    m_mpr;
    bool          m_compareLinked = false;   // scroll-link guard
    QTreeWidget*  m_library;
    QComboBox*    m_sourceCombo = nullptr;  // dock source selector
    QLineEdit*    m_remoteFilter = nullptr; // remote query filter
    QLabel*       m_sourceTag = nullptr;    // LOCAL/REMOTE state tag
    QLabel*       m_statusLabel;
    QProgressBar* m_progress;
    PacsDialog*   m_pacsDialog = nullptr;
    DicomWebDialog* m_webDialog = nullptr;
    StoreScp*     m_storeScp;
    PluginManager* m_plugins;
    InferenceEngine* m_engine;
    StudyDatabase* m_db;
    QActionGroup* m_toolGroup;
    QAction*      m_cineAction = nullptr;
    QAction*      m_compareAction = nullptr;
    QAction*      m_popoutAction = nullptr;
    QMainWindow*  m_compareWindow = nullptr;
    QHBoxLayout*  m_singleLay = nullptr;
    void popOutCompare(bool on);
    QMenu*        m_pluginMenu;
    QTimer*       m_incomingTimer = nullptr;
    bool          m_overlayActive = false;
    Segmentation  m_overlay;
    // Fusion layer (e.g. PET on CT) — kept alive for the viewer pipeline.
    vtkSmartPointer<vtkImageData>    m_fusionImg;
    vtkSmartPointer<vtkLookupTable>  m_fusionLut;
    QString       m_modelPath;       // loaded ONNX model path
    QLabel*       m_modelStatus;     // shows loaded model name in status bar
    QMenu*        m_modelMenu = nullptr;
    QDockWidget*  m_segDock = nullptr;
    QAction*      m_segDockAction = nullptr;
    QTreeWidget*  m_segPanel = nullptr;
    QToolButton*  m_segRail = nullptr;  // slim rail when collapsed
    QString       m_seriesUid;       // current series UID (annotation key)
    QTimer*       m_annoSaveTimer = nullptr;
    Tool          m_activeTool = Tool::WindowLevel;
    int           m_cineFps = 15;
    int           m_sliceSort = 0;   // 0=position 1=instance# 2=time 3=name
    std::atomic<int> m_loadGen{0};   // cancels in-flight series loads
    void setCineFps(int fps);
};

} // namespace meda
