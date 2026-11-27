#pragma once
#include <QQmlComponent>
#include <QTimer>
#include <QPointer>
#include <memory>

// Included after UiTests' check helper. Every MCP call uses the shipped stdio
// bridge; direct Editor calls are only fixture setup and GUI lifecycle actions.
class StabilityBridge {
    QProcess process;
    QByteArray pending;
    int serial = 0;
  public:
    explicit StabilityBridge(Editor &editor) {
        auto args = QJsonDocument::fromJson(editor.mcpConfiguration().toUtf8()).object()["mcpServers"]
                        .toObject()["worldbench"].toObject()["args"].toArray();
        QStringList arguments;
        for (auto value : args) arguments << value.toString();
        process.start(QCoreApplication::applicationDirPath() + "/WorldbenchMcp.exe", arguments);
        check(process.waitForStarted(5000), "stability stdio bridge starts");
        check(!request("initialize", {}).contains("error"), "stability MCP initializes");
        send({{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}});
    }
    ~StabilityBridge() { process.kill(); process.waitForFinished(3000); }
    void kill() { process.kill(); process.waitForFinished(3000); }
    void send(const QJsonObject &object) {
        process.write(QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n');
        check(process.waitForBytesWritten(5000), "stdio request written");
    }
    int start(const QString &method, const QJsonObject &params) {
        const int id = ++serial;
        send({{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}});
        return id;
    }
    QJsonObject response(int id) {
        QJsonObject response;
        check(QTest::qWaitFor([&] {
            pending += process.readAllStandardOutput();
            while (pending.contains('\n')) {
                const auto at = pending.indexOf('\n');
                auto message = QJsonDocument::fromJson(pending.left(at)).object();
                pending.remove(0, at + 1);
                if (message["id"].toInt() == id) { response = message; return true; }
            }
            return false;
        }, 30000), "stdio response arrives");
        return response;
    }
    QJsonObject request(const QString &method, const QJsonObject &params) { return response(start(method, params)); }
    QJsonObject call(const QString &name, QJsonObject args = {}, bool expectError = false) {
        auto envelope = request("tools/call", {{"name", name}, {"arguments", args}});
        check(!envelope.contains("error"), "MCP valid JSON-RPC envelope");
        const auto result = envelope["result"].toObject();
        if (result["isError"].toBool() != expectError)
            std::cerr << QJsonDocument(result).toJson().toStdString();
        check(result["isError"].toBool() == expectError, "MCP expected success/error contract");
        return QJsonDocument::fromJson(result["content"].toArray().first().toObject()["text"].toString().toUtf8()).object();
    }
};

static void runStabilityChecks(Editor &editor, QQmlApplicationEngine &engine, QQuickWindow *window) {
    QTemporaryDir files;
    auto settle = [&] { check(QTest::qWaitFor([&] { return !editor.assetBusy() && !editor.meshBusy(); }, 30000), "stability scene settles"); };
    settle();
    check(editor.createWorld(32, 1, 10, 0, -60, true), "small stability world");
    const auto project = QUrl::fromLocalFile(files.filePath("shared.wbench"));
    check(editor.save(project), "first editor saves fixture");
    {
        Editor second;
        check(second.open(project) && second.projectReadOnly(), "second editor opens read-only");
        check(!second.save(project), "read-only editor refuses overwrite");
        check(second.save(QUrl::fromLocalFile(files.filePath("copy.wbench"))) && !second.projectReadOnly(),
              "read-only editor can save independent copy");
        auto external = editor.world(); external.setBlock(0, 12, 0, wb::Glass);
        QString error;
        check(wb::saveProject(project.toLocalFile(), external, error), "external writer fixture");
        check(!editor.save(project) && editor.modified() == false, "stale disk overwrite refused");
        check(editor.open(project) && editor.world().block(0, 12, 0) == wb::Glass, "explicit reload accepts external edit");
    }
    check(editor.createPcgGraph("稳定性图", true) && editor.addPcgVolume(), "create evaluated graph");
    auto graph = editor.world().creation.graphs.front().toJson();
    const auto graphId = graph["id"].toString();
    auto nodes = graph["nodes"].toArray();
    auto node = nodes[0].toObject(), parameters = node["parameters"].toObject();
    parameters["uiX"] = 123; node["parameters"] = parameters; nodes[0] = node; graph["nodes"] = nodes;
    const auto generated = editor.world().generated;
    check(editor.creationEdit("graph", graph.toVariantMap()) && editor.world().generated == generated,
          "layout-only edit retains generated content");
    editor.undo(); check(editor.world().generated == generated, "layout undo does not generate");
    editor.redo(); check(editor.world().generated == generated, "layout redo does not generate");

    wb::StructureAsset asset;
    asset.id = "stability-asset"; asset.name = "稳定性结构";
    asset.width = asset.depth = 8; asset.height = 8;
    asset.blocks.push_back({0, 0, 0, "minecraft:stone"});
    check(editor.creationEdit("add", {{"kind", "assets"}, {"changes", asset.toJson().toVariantMap()}}), "asset gesture fixture");
    editor.setMcpEnabled(true);
    {
        StabilityBridge bridge(editor);
        auto tools = bridge.request("tools/list", {})["result"].toObject()["tools"].toArray();
        check(tools.size() >= 12, "actual MCP tool catalog available");
        auto info = bridge.call("world_info");
        check(info["revision"].toDouble() == double(editor.revision()), "MCP reads authoritative revision");
        const auto revision = editor.revision();
        auto bad = asset.toJson(); auto badBlocks = bad["blocks"].toArray();
        auto block = badBlocks[0].toObject(); block["x"] = "invalid"; badBlocks[0] = block; bad["blocks"] = badBlocks;
        bridge.call("creation", {{"action", "edit"}, {"operation", "patch"}, {"expected_revision", double(revision)},
                    {"data", QJsonObject{{"kind", "assets"}, {"id", asset.id}, {"changes", QJsonObject{{"blocks", badBlocks}}}}}}, true);
        check(editor.revision() == revision && editor.world().creation.assets.front().blocks[0].x == 0,
              "invalid MCP coordinates leave state and revision untouched");
        check(editor.beginCreationParameterEdit(), "start numeric gesture transaction");
        bridge.call("undo", {{"expected_revision", double(revision)}}, true);
        for (int x : {1, 2, 3})
            check(editor.previewCreationParameter("patch", {{"kind", "assets"}, {"id", asset.id},
                          {"changes", QVariantMap{{"pivot", QVariantList{x, 0, 0}}}}}), "stage numeric preview");
        check(editor.revision() == revision && editor.world().creation.assets.front().pivot.x() == 0,
              "numeric previews do not write authoritative state");
        editor.endCreationParameterEdit();
        check(editor.revision() == revision + 1 && editor.world().creation.assets.front().pivot.x() == 3,
              "numeric release commits once");
        bridge.call("undo", {{"expected_revision", double(editor.revision())}});
        check(editor.world().creation.assets.front().pivot.x() == 0, "one MCP undo removes complete GUI gesture");
        bridge.call("redo", {{"expected_revision", double(editor.revision())}});
        check(editor.world().creation.assets.front().pivot.x() == 3, "MCP redo restores complete GUI gesture");
        bridge.call("undo", {{"expected_revision", double(revision)}}, true);
        bridge.call("creation", {{"action", "cave_analysis"}, {"asset_id", asset.id}, {"expected_revision", double(editor.revision())}});
    }

    // Real QML controls and native Qt mouse/key events: one release, one undo;
    // Escape does not insert a restoring command or consume the redo branch.
    auto mouseGesture = [&](const QString &url, const QString &idProperty, const QString &id,
                            const QString &controlName, const QString &selected) {
        QQmlComponent component(&engine, QUrl(url));
        auto *object = component.create(engine.rootContext());
        check(object, "stability editor component loads");
        object->setParent(window);
        auto *item = qobject_cast<QQuickItem *>(object);
        check(item, "stability component is visual");
        item->setParentItem(window->contentItem()); item->setZ(10000);
        item->setWidth(window->width()); item->setHeight(window->height());
        item->setProperty(idProperty.toUtf8().constData(), id);
        if (!selected.isEmpty()) item->setProperty("selected", selected);
        window->requestActivate();
        check(QTest::qWaitForWindowActive(window), "gesture window active");
        QTest::qWait(100);
        std::function<QQuickItem *(QQuickItem *)> find = [&](QQuickItem *parent) -> QQuickItem * {
            if (parent->objectName() == controlName) return parent;
            for (auto *child : parent->childItems()) if (auto *result = find(child)) return result;
            return nullptr;
        };
        auto *control = find(item);
        check(control && control->isVisible(), "numeric gesture control visible");
        const auto original = editor.world().creation;
        auto drag = [&](bool cancel) {
            const auto point = control->mapToScene({control->width() / 2, control->height() / 2}).toPoint();
            QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, point);
            QTest::mouseMove(window, point + QPoint(12, 0), 20);
            QTest::mouseMove(window, point + QPoint(24, 0), 20);
            check(editor.commandBusy() && editor.world().creation == original, "QML drag stages without generation");
            if (cancel) QTest::keyClick(window, Qt::Key_Escape);
            QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, point + QPoint(24, 0));
            QTest::qWait(40);
        };
        drag(false);
        check(editor.world().creation != original && !editor.commandBusy(), "QML release commits gesture");
        editor.undo(); check(editor.world().creation == original, "one undo restores pre-drag state");
        check(editor.canRedo(), "redo branch exists before cancelled gesture");
        drag(true);
        check(editor.world().creation == original && editor.canRedo() && !editor.commandBusy(),
              "Escape cancels without new history");
        item->setVisible(false);
        object->deleteLater();
        QTest::qWait(100);
    };
    mouseGesture("qrc:/AssetEditor.qml", "assetId", asset.id, "assetPivot0", {});
    const auto graphNow = editor.world().creation.graphs.front();
    const auto numeric = std::find_if(graphNow.nodes.begin(), graphNow.nodes.end(), [](const auto &n) { return n.parameters["value"].isDouble(); });
    check(numeric != graphNow.nodes.end(), "graph has numeric input");
    mouseGesture("qrc:/PcgGraphEditor.qml", "graphId", graphId, "pcgParameter_value", numeric->id);

    int analysisStarts = 0; bool wasBusy = false;
    auto observation = QObject::connect(&editor, &Editor::changed, &editor, [&] {
        const bool busy = editor.creationBusy(); if (busy && !wasBusy) ++analysisStarts; wasBusy = busy;
    });
    editor.assetSlice(asset.id, 0, true);
    const int cachedStarts = analysisStarts;
    editor.assetSlice(asset.id, 1, true); editor.assetSlice(asset.id, 2, true);
    check(analysisStarts == cachedStarts, "changing slice reuses cached analysis");
    check(editor.addCaveMarker(asset.id, "interior", 1, 1, 1), "modify cave marker");
    const int beforeAnalysis = analysisStarts;
    editor.assetSlice(asset.id, 0, true);
    check(analysisStarts == beforeAnalysis + 1, "marker edit invalidates analysis cache");
    QObject::disconnect(observation);

    // Cancellation is sent by another real stdio client while analysis runs.
    auto large = asset;
    large.id = "large-analysis"; large.name = "大体积分析";
    large.width = large.depth = 512; large.height = 61;
    check(editor.creationEdit("add", {{"kind", "assets"}, {"changes", large.toJson().toVariantMap()}}), "large analysis fixture");
    {
        StabilityBridge reader(editor), canceller(editor);
        bool sent = false;
        auto cancellation = QObject::connect(&editor, &Editor::changed, &editor, [&] {
            if (editor.creationBusy() && !sent) {
                sent = true;
                QTimer::singleShot(0, &editor, [&] { canceller.call("creation", {{"action", "cancel"}}); });
            }
        });
        const auto before = editor.revision();
        auto result = reader.call("creation", {{"action", "cave_analysis"}, {"asset_id", large.id},
                                                {"expected_revision", double(before)}});
        QObject::disconnect(cancellation);
        check(sent && result["cancelled"].toBool() && editor.revision() == before,
              "real MCP cancels cave analysis without changing the world");
        editor.assetSlice(large.id, 1, true);
        result = reader.call("creation", {{"action", "cave_analysis"}, {"asset_id", large.id},
                                           {"expected_revision", double(before)}});
        check(!result["cancelled"].toBool() && editor.revision() == before, "explicit analysis retries after cancellation");
    }

    // Reproduce both transport shutdown paths during an actual generation.
    for (bool disable : {false, true}) {
        settle();
        check(editor.createWorld(512, 1, 10, 0, -60, true) && editor.createPcgGraph("断连测试", true) && editor.addPcgVolume(),
              "generation disconnect fixture");
        editor.setMcpEnabled(true);
        StabilityBridge bridge(editor);
        bool acted = false;
        auto connection = QObject::connect(&editor, &Editor::changed, &editor, [&] {
            if (editor.creationBusy() && !acted) {
                acted = true;
                QTimer::singleShot(0, &editor, [&] { if (disable) editor.setMcpEnabled(false); else bridge.kill(); });
            }
        });
        auto volume = editor.world().creation.volumes.front();
        const auto before = editor.revision();
        const QJsonObject changes{{"min", QJsonArray{-250, -64, -250}}, {"max", QJsonArray{250, 320, 250}}};
        const QJsonObject data{{"kind", "volumes"}, {"id", volume.id}, {"changes", changes}};
        const QJsonObject args{{"action", "edit"}, {"operation", "patch"},
                               {"expected_revision", double(before)}, {"data", data}};
        bridge.start("tools/call", {{"name", "creation"}, {"arguments", args}});
        check(QTest::qWaitFor([&] { return acted && !editor.creationBusy(); }, 30000), "editor survives MCP disconnect during generation");
        QObject::disconnect(connection);
        check(editor.revision() == before + 1, "disconnected successful generation commits exactly once");
        editor.setMcpEnabled(true);
        StabilityBridge reconnected(editor);
        check(reconnected.call("world_info")["revision"].toDouble() == double(editor.revision()), "fresh MCP client reads recovered session");
    }
    editor.setMcpEnabled(false);
    std::cout << "Stability: project conflicts, input rejection, numeric gestures, cached analysis and real stdio reconnect passed\n";
}
