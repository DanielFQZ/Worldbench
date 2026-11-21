#include "Editor.h"
#include "Viewport.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QMutex>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>
#include <kddockwidgets/Config.h>
#include <kddockwidgets/qtquick/Platform.h>

namespace {
QString logPath;
QMutex logMutex;
void logMessage(QtMsgType type, const QMessageLogContext &, const QString &message) {
    QMutexLocker lock(&logMutex);
    QFile f(logPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        f.write((QDateTime::currentDateTime().toString(Qt::ISODate) + QString(" [%1] ").arg(type) + message +
                 "\n")
                    .toUtf8());
    }
    fprintf(stderr, "%s\n", message.toUtf8().constData());
    fflush(stderr);
}
} // namespace
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    app.setOrganizationName("Worldbench");
    app.setApplicationName("Worldbench");
    app.setApplicationVersion("0.13.0");
    const auto local = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(local);
    logPath = local + "/Worldbench.log";
    QFile old(logPath);
    if (old.size() > 2 * 1024 * 1024) {
        QFile::remove(logPath + ".previous");
        old.rename(logPath + ".previous");
    }
    qInstallMessageHandler(logMessage);
    qInfo("Worldbench 0.13.0 starting (D3D11 / Qt 6.8.3)");
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
    QQuickStyle::setStyle("Basic");
    KDDockWidgets::initFrontend(KDDockWidgets::FrontendType::QtQuick);
    qmlRegisterType<Viewport>("Worldbench", 1, 0, "VoxelViewport");
    qmlRegisterUncreatableType<Editor>("Worldbench", 1, 0, "WorldEditor",
                                       "Use the application editor instance");
    Editor editor;
    editor.setTool(-1);
    QQmlApplicationEngine engine;
    KDDockWidgets::QtQuick::Platform::instance()->setQmlEngine(&engine);
    engine.rootContext()->setContextProperty("editor", &editor);
    engine.load(QUrl("qrc:/main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    // CI smoke path uses the same renderer and window; it does not replace manual tests.
    const auto args = app.arguments();
    const int openIndex = args.indexOf("--open");
    if (openIndex >= 0 && !args.value(openIndex + 1).isEmpty())
        editor.open(QUrl::fromLocalFile(args.value(openIndex + 1)));
    else
        editor.initializeResources();
    if (args.contains("--model-preview")) {
        if (auto *viewport = engine.rootObjects().first()->findChild<Viewport *>("voxelViewport"))
            viewport->applyCamera({{"position", QJsonArray{9, 11, 17}}, {"yaw", -27}, {"pitch", -18}});
    }
    const int smoke = args.indexOf("--smoke-test");
    if (smoke >= 0) {
        const QString capture = args.value(smoke + 1);
        auto *timer = new QTimer(&app);
        timer->setInterval(500);
        QObject::connect(timer, &QTimer::timeout, &app, [&app, &engine, &editor, capture, timer] {
            if (editor.assetBusy() || editor.meshBusy() || editor.scene()->chunks.empty())
                return;
            timer->stop();
            QTimer::singleShot(800, &app, [&app, &engine, capture] {
                auto *w = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
                if (!w) {
                    app.exit(2);
                    return;
                }
                auto *viewport = w->findChild<Viewport *>("voxelViewport");
                if (!viewport || viewport->width() < 100 || viewport->height() < 100 ||
                    viewport->frameCounter()->load() == 0) {
                    qCritical("Voxel renderer was not visible or did not execute");
                    app.exit(5);
                    return;
                }
                const auto image = w->grabWindow();
                if (image.isNull() || (!capture.isEmpty() && !image.save(capture))) {
                    app.exit(3);
                    return;
                }
                qInfo("Renderer smoke capture succeeded");
                app.exit(0);
            });
        });
        timer->start();
        QTimer::singleShot(120000, &app, [&app] { app.exit(4); });
    }
    return app.exec();
}
