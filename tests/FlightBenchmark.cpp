#include "Editor.h"
#include "Viewport.h"
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <iostream>
#include <kddockwidgets/Config.h>
#include <kddockwidgets/qtquick/Platform.h>

// Reproducible navigation workload; reports frame intervals, GUI stalls and
// exact-area catch-up separately. Run before/after with the same project copy.
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("Worldbench flight benchmark");
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    QQuickStyle::setStyle("Basic");
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
    KDDockWidgets::initFrontend(KDDockWidgets::FrontendType::QtQuick);
    qmlRegisterType<Viewport>("Worldbench", 1, 0, "VoxelViewport");
    qmlRegisterUncreatableType<Editor>("Worldbench", 1, 0, "WorldEditor", "Application instance only");
    if (argc < 3)
        return 2;
    Editor editor;
    editor.setTool(-1);
    QQmlApplicationEngine engine;
    KDDockWidgets::QtQuick::Platform::instance()->setQmlEngine(&engine);
    engine.rootContext()->setContextProperty("editor", &editor);
    engine.load(QUrl("qrc:/main.qml"));
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    if (!window || !QTest::qWaitForWindowExposed(window))
        return 3;
    if (app.arguments().contains("--maximized"))
        window->showMaximized();
    else
        window->resize(1440, 900);
    auto *v = window->findChild<Viewport *>("voxelViewport");
    if (app.arguments().contains("--scale75"))
        v->setRenderScale(75);
    auto settle = [&] {
        return QTest::qWaitFor([&] { return !editor.meshBusy() && !editor.assetBusy(); }, 180000);
    };
    if (!settle() || !editor.open(QUrl::fromLocalFile(app.arguments()[1])) || !settle())
        return 4;
    const auto &w = editor.world();
    const int x0 = w.originX + w.width / 2 - 400, z0 = w.originZ + w.depth / 2;
    auto camera = [&](int x) {
        v->applyCamera({{"position", QJsonArray{x, w.height(x, z0) + 35, z0}}, {"yaw", 90}, {"pitch", -20}});
    };
    camera(x0);
    if (!settle())
        return 5;
    QJsonArray frames, calls, builds, arrivals, gpu, submit;
    QElapsedTimer clock;
    clock.start();
    qint64 lastFrame = 0;
    bool measure = true;
    QObject::connect(window, &QQuickWindow::frameSwapped, &app, [&] {
        const auto now = clock.nsecsElapsed();
        if (measure && lastFrame)
            frames.append(double(now - lastFrame) / 1e6);
        lastFrame = now;
    });
    QObject::connect(&editor, &Editor::sceneChanged, &app, [&] {
        if (measure)
            builds.append(QJsonObject{{"ms", double(editor.scene()->buildMilliseconds)},
                                      {"far_ms", double(editor.scene()->farMilliseconds)},
                                      {"chunks", editor.scene()->updatedChunks},
                                      {"tiles", editor.scene()->updatedFarTiles}});
    });
    // Fixed path and update cadence, independent of machine navigation speed.
    QEventLoop navigation;
    QTimer cameraTimer;
    cameraTimer.setTimerType(Qt::PreciseTimer);
    int step = 0;
    QObject::connect(&cameraTimer, &QTimer::timeout, &navigation, [&] {
        QElapsedTimer call;
        call.start();
        camera(x0 + step * 2);
        calls.append(double(call.nsecsElapsed()) / 1e6);
        gpu.append(v->renderMetrics()->gpuMicros.load() / 1000.);
        submit.append(v->renderMetrics()->submitMicros.load() / 1000.);
        if (++step == 240) {
            cameraTimer.stop();
            navigation.quit();
        }
    });
    cameraTimer.start(16);
    navigation.exec();
    QElapsedTimer tail;
    tail.start();
    if (!settle())
        return 6;
    const auto catchup = tail.elapsed();
    measure = false;
    // Cold entry from high altitude at three distinct positions.
    for (int i = 0; i < 3; ++i) {
        int x = x0 + i * 160;
        v->applyCamera({{"position", QJsonArray{x, 800, z0}}, {"yaw", 90}, {"pitch", -30}});
        if (!settle())
            return 7;
        tail.restart();
        camera(x);
        if (!settle() || !editor.previewContains(x, z0))
            return 8;
        arrivals.append(double(tail.elapsed()));
    }
    auto stats = [](const QJsonArray &array) {
        std::vector<double> values;
        for (auto a : array)
            values.push_back(a.toDouble());
        std::sort(values.begin(), values.end());
        if (values.empty())
            return QJsonObject{};
        return QJsonObject{{"count", int(values.size())},
                           {"p50", values[values.size() / 2]},
                           {"p95", values[size_t((values.size() - 1) * .95)]},
                           {"max", values.back()}};
    };
    QJsonObject report{
        {"method", "240 camera updates at 16ms timer cadence; native event loop"},
        {"width", w.width},
        {"depth", w.depth},
        {"instances", int(w.heights.instances.size())},
        {"frame_ms", stats(frames)},
        {"camera_call_ms", stats(calls)},
        {"builds", builds},
        {"catchup_ms", double(catchup)},
        {"cold_near_ms", arrivals},
        {"frame_samples", frames},
        {"logical_window", QString::number(window->width()) + "x" + QString::number(window->height())},
        {"render_pixels", QString::number(v->renderMetrics()->pixelWidth.load()) + "x" +
                              QString::number(v->renderMetrics()->pixelHeight.load())},
        {"gpu_ms", stats(gpu)},
        {"cpu_submit_ms", stats(submit)},
        {"dpr", window->devicePixelRatio()},
        {"environment_enabled", w.environment.enabled},
        {"exact_radius", editor.exactRadiusChunks()}};
    QFile output(app.arguments()[2]);
    if (!output.open(QIODevice::WriteOnly))
        return 9;
    output.write(QJsonDocument(report).toJson());
    std::cout << QJsonDocument(QJsonObject{{"frame_ms", stats(frames)},
                                           {"camera_call_ms", stats(calls)},
                                           {"catchup_ms", double(catchup)},
                                           {"cold_near_ms", arrivals}})
                     .toJson()
                     .constData();
    return 0;
}
