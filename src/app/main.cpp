#include "MainWindow.h"
#include "Theme.h"
#include "AutoPullRunner.h"
#include "AutoPullRules.h"
#include "TaskScheduler.h"

#include <QApplication>
#include <QCoreApplication>
#include <QLocale>
#include <QStandardPaths>
#include <QSurfaceFormat>
#include <QTranslator>

#include <QVTKOpenGLNativeWidget.h>

#include <vtkAutoInit.h>
#include <vtkFileOutputWindow.h>
#include <vtkOutputWindow.h>
VTK_MODULE_INIT(vtkRenderingOpenGL2)
VTK_MODULE_INIT(vtkRenderingFreeType)
VTK_MODULE_INIT(vtkRenderingVolumeOpenGL2)
VTK_MODULE_INIT(vtkInteractionStyle)

int main(int argc, char** argv)
{
    // Required before QApplication for QVTKOpenGLNativeWidget.
    // NOTE: do not set samples>0 — MSAA breaks the QVTK FBO on some drivers.
    QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());

    // Route VTK warnings/errors to a log file instead of popping up
    // vtkOutputWindow on every odd DICOM file.
    auto logWin = vtkSmartPointer<vtkFileOutputWindow>::New();
    logWin->SetFileName(
        (QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
         "/vtk.log").toUtf8().constData());
    vtkOutputWindow::SetInstance(logWin);

    // Headless auto-pull mode: launched by Task Scheduler or "Run Now".
    // Runs without a GUI so the workstation doesn't need the app open.
    // Parse argv directly — no QApplication needed yet.
    {
        QStringList preArgs;
        for (int i = 0; i < argc; ++i)
            preArgs.append(QString::fromLocal8Bit(argv[i]));
        const int ai = preArgs.indexOf("--autopull");
        if (ai >= 0 && ai + 1 < preArgs.size()) {
            // QCoreApplication only — no GUI, no display needed. Runs
            // under Task Scheduler even when no user is logged in.
            int ac = argc;
            QCoreApplication app(ac, argv);
            app.setApplicationName("Scanthia");
            app.setOrganizationName("Scanthia");
            return meda::AutoPullRunner::run(preArgs.at(ai + 1));
        }
    }

    QApplication app(argc, argv);
    app.setApplicationName("Scanthia");
    app.setOrganizationName("Scanthia");

    // i18n — load scanthia_<locale>.qm from the app dir or translations/.
    // Falls back to English (built-in strings) when no .qm is present.
    {
        auto* tx = new QTranslator(&app);
        const QString locale = QLocale().name();           // e.g. "fr_FR"
        const QString shortLoc = locale.split('_').first(); // e.g. "fr"
        for (const auto& base : {QString("scanthia_%1").arg(locale),
                                 QString("scanthia_%1").arg(shortLoc)}) {
            for (const auto& dir : {QCoreApplication::applicationDirPath(),
                                    QCoreApplication::applicationDirPath()
                                        + "/translations"}) {
                if (tx->load(base, dir)) {
                    app.installTranslator(tx);
                    goto loaded;
                }
            }
        }
        loaded:;
    }

    meda::applyTheme(app);
    app.setWindowIcon(QIcon(":/icons/Scanthia.png"));

    meda::MainWindow w;
    w.show();
    // --open <dir>: index and load the first series found (testing).
    const QStringList args = app.arguments();
    const int oi = args.indexOf("--open");
    if (oi > 0 && oi + 1 < args.size())
        w.debugOpen(args.at(oi + 1));
    // scanthia:// URL — registered by the installer for RIS/HIS links.
    for (const auto& a : args)
        if (a.startsWith("scanthia:", Qt::CaseInsensitive)) {
            w.openUrl(a);
            break;
        }
    return app.exec();
}
