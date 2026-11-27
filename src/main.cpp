#include "WindowsStartup.h"
#include "Editor.h"
#include "Viewport.h"
#include "UpdateService.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonArray>
#include <QMutex>
#include <QPainter>
#include <QPalette>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QSettings>
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
#ifdef Q_OS_WIN
    std::wstring startupError;
    const auto startupResult = wb::startup::ensureDesktopUser(startupError);
    if (startupResult == wb::startup::Result::Relaunched) return 0;
    if (startupResult == wb::startup::Result::Failed) {
        MessageBoxW(nullptr, startupError.c_str(), L"Worldbench startup", MB_OK | MB_ICONERROR);
        return 1;
    }
#endif
    QGuiApplication app(argc, argv);
#ifdef Q_OS_WIN
    const auto startupContext = wb::startup::context(GetCurrentProcess());
    const int probe = app.arguments().indexOf("--startup-check");
    if (probe >= 0) {
        QFile report(app.arguments().value(probe + 1));
        if (!report.open(QIODevice::WriteOnly)) return 2;
        report.write(QByteArray("{\"elevated\":") + (startupContext.elevated ? "true" : "false") +
            ",\"integrity\":" + QByteArray::number(startupContext.integrity) + "}");
        return 0;
    }
#endif
    app.setOrganizationName("Worldbench");
    app.setApplicationName("Worldbench");
    app.setApplicationVersion(QStringLiteral(WORLDBENCH_APP_VERSION));
    // Keep user-editable settings and diagnostics together in Documents so a
    // normal uninstall can leave the creator's preferences and resource
    // choices intact. Migrate the old native settings store once.
    const int automationIndex = app.arguments().indexOf("--automation-test");
    const bool isolatedAutomation = automationIndex >= 0;
    const auto documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const auto dataRoot = isolatedAutomation ? app.arguments().value(automationIndex + 1)
                                            : QDir(documents).filePath("Worldbench");
    if (isolatedAutomation && (dataRoot.isEmpty() || !QDir::isAbsolutePath(dataRoot) ||
        !QFileInfo(dataRoot).isDir() || !QDir(dataRoot).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty())) {
        fprintf(stderr, "--automation-test requires an existing empty absolute directory\n");
        return 2;
    }
    QDir().mkpath(dataRoot);
    QSettings oldSettings(QSettings::NativeFormat, QSettings::UserScope, "Worldbench", "Worldbench");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dataRoot);
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, dataRoot);
    QSettings migrated;
    const auto settingsFile = migrated.fileName();
    if (!isolatedAutomation && !QFileInfo::exists(settingsFile) && !oldSettings.allKeys().isEmpty()) {
        for (const auto &key : oldSettings.allKeys())
            migrated.setValue(key, oldSettings.value(key));
        migrated.sync();
    }
    // KDDockWidgets floating frames use the application palette rather than
    // inheriting the QML window palette. Keep both surfaces on the same theme.
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#282828"));
    palette.setColor(QPalette::WindowText, QColor("#e2e2e2"));
    palette.setColor(QPalette::Base, QColor("#1d1d1d"));
    palette.setColor(QPalette::AlternateBase, QColor("#303030"));
    palette.setColor(QPalette::Text, QColor("#e2e2e2"));
    palette.setColor(QPalette::Button, QColor("#303030"));
    palette.setColor(QPalette::ButtonText, QColor("#e2e2e2"));
    palette.setColor(QPalette::Highlight, QColor("#354c6c"));
    palette.setColor(QPalette::HighlightedText, QColor("#e2e2e2"));
    palette.setColor(QPalette::Light, QColor("#424242"));
    palette.setColor(QPalette::Midlight, QColor("#303030"));
    palette.setColor(QPalette::Mid, QColor("#424242"));
    palette.setColor(QPalette::Dark, QColor("#1d1d1d"));
    palette.setColor(QPalette::Shadow, QColor("#191919"));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#707070"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#707070"));
    app.setPalette(palette);
    QPixmap logo(64, 64);
    logo.fill(Qt::transparent);
    {
        QPainter painter(&logo);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor("#73d6df"), 2));
        painter.setBrush(QColor("#2a7288"));
        painter.drawRoundedRect(QRectF(2, 2, 60, 60), 12, 12);
        painter.setPen(QPen(QColor("#effcff"), 6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPolyline(QPolygonF{{14, 19}, {22, 46}, {32, 27}, {42, 46}, {50, 19}});
    }
    app.setWindowIcon(QIcon(logo));
    logPath = dataRoot + "/Worldbench.log";
    QFile old(logPath);
    if (old.size() > 2 * 1024 * 1024) {
        QFile::remove(logPath + ".previous");
        old.rename(logPath + ".previous");
    }
    qInstallMessageHandler(logMessage);
    qInfo("Worldbench %s starting (D3D11 / Qt 6.8.3)", WORLDBENCH_APP_VERSION);
#ifdef Q_OS_WIN
    qInfo("Startup security: elevated=%d integrity=%lu pid=%lu", startupContext.elevated,
          static_cast<unsigned long>(startupContext.integrity), GetCurrentProcessId());
#endif
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
    QQuickStyle::setStyle("Basic");
    KDDockWidgets::initFrontend(KDDockWidgets::FrontendType::QtQuick);
    qmlRegisterType<Viewport>("Worldbench", 1, 0, "VoxelViewport");
    qmlRegisterUncreatableType<Editor>("Worldbench", 1, 0, "WorldEditor",
                                       "Use the application editor instance");
    Editor editor;
    editor.setTool(-1);
    UpdateService updater;
    QQmlApplicationEngine engine;
    KDDockWidgets::QtQuick::Platform::instance()->setQmlEngine(&engine);
    engine.rootContext()->setContextProperty("editor", &editor);
    engine.rootContext()->setContextProperty("updater", &updater);
    engine.load(QUrl("qrc:/main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    // CI smoke path uses the same renderer and window; it does not replace manual tests.
    const auto args = app.arguments();
    const int openIndex = args.indexOf("--open");
    QString projectPath;
    if (openIndex >= 0 && openIndex + 1 < args.size())
        projectPath = args.value(openIndex + 1);
    else {
        // File associations invoke the executable with the project as a plain
        // positional argument. Keep command-line switches available for tests.
        for (int i = 1; i < args.size(); ++i) {
            const auto candidate = args.value(i);
            if (!candidate.startsWith('-') && candidate.endsWith(".wbench", Qt::CaseInsensitive)) {
                projectPath = candidate;
                break;
            }
        }
    }
    if (!projectPath.isEmpty() && QFileInfo::exists(projectPath))
        editor.open(QUrl::fromLocalFile(projectPath));
    else if (!isolatedAutomation)
        editor.initializeResources(); // The isolated automation fixture uses built-in materials.
    if (args.contains("--frame-world"))
        if (auto *viewport = engine.rootObjects().first()->findChild<Viewport *>("voxelViewport"))
            viewport->frameWorld();
    if (args.contains("--model-preview")) {
        if (auto *viewport = engine.rootObjects().first()->findChild<Viewport *>("voxelViewport"))
            viewport->applyCamera({{"position", QJsonArray{9, 11, 17}}, {"yaw", -27}, {"pitch", -18}});
    }
    if (isolatedAutomation) {
        editor.setMcpEnabled(true);
        QFile configuration(dataRoot + "/mcp.json");
        if (!configuration.open(QIODevice::WriteOnly) ||
            configuration.write(editor.mcpConfiguration().toUtf8()) < 0) return 2;
        configuration.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        configuration.close();
    }
    const int smoke = args.indexOf("--smoke-test");
    if (smoke >= 0) {
        const QString capture = args.value(smoke + 1);
        auto *timer = new QTimer(&app);
        timer->setInterval(500);
        QObject::connect(timer, &QTimer::timeout, &app, [&app, &engine, &editor, capture, timer] {
            if (editor.assetBusy() || editor.meshBusy() ||
                (editor.scene()->chunks.empty() && editor.scene()->farTiles.empty()))
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
