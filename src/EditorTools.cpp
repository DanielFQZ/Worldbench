#include "Editor.h"
#include "McpServer.h"
#include "Viewport.h"
#include "core/Search.h"
#include <QBuffer>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QtConcurrent>
#include <cmath>
#include <stdexcept>

namespace {
QVariantMap vecMap(const QVector3D &v) {
    return {{"x", v.x()}, {"y", v.y()}, {"z", v.z()}};
}
} // namespace

QString Editor::resourceSummary() const {
    if (assetBusy())
        return "正在解析资源与构建图集…";
    if (!m_world.assets)
        return "内置调试材质 · 尚未载入 Minecraft 资源";
    return QString("%1 · %2 个方块 · %3 个来源\n32 px 贴图预览 / 首帧 / JSON 模型\n跳过 %4 个无效条目 · 忽略 "
                   "%5 个 OptiFine 条目")
        .arg(m_world.assets->version.isEmpty() ? "资源包" : m_world.assets->version)
        .arg(m_world.assets->catalog.size())
        .arg(m_world.resourcePaths.size())
        .arg(m_world.assets->skippedEntries)
        .arg(m_world.assets->unsupportedEntries);
}
QString Editor::resourceDiagnostics() const {
    if (!m_world.assets)
        return {};
    auto lines = m_world.assets->warnings;
    if (m_world.assets->skippedEntries > 80)
        lines << QString("仅显示前 80 条跳过详情，共 %1 条。").arg(m_world.assets->skippedEntries);
    if (m_world.assets->unsupportedEntries)
        lines << "OptiFine CTM / 自定义颜色等专用效果尚未支持，普通纹理和 JSON 模型仍会加载。";
    return lines.isEmpty() ? "未发现需要跳过的资源条目。" : lines.join('\n');
}
QVariantList Editor::sceneNodes() const {
    QVariantList nodes;
    for (auto it = m_world.heights.instances.rbegin(); it != m_world.heights.instances.rend(); ++it)
        nodes << QVariantMap{{"id", it->id},
                             {"name", it->name + (it->enabled ? "" : "（隐藏）")},
                             {"kind", "heightmap"},
                             {"visible", it->enabled}};
    for (auto it = m_world.heights.layers.rbegin(); it != m_world.heights.layers.rend(); ++it)
        nodes << QVariantMap{{"id", it->id},
                             {"name", it->name},
                             {"kind", "terrain"},
                             {"visible", it->enabled},
                             {"active", it->id == m_world.heights.activeLayer}};
    for (auto it = m_world.creation.volumes.rbegin(); it != m_world.creation.volumes.rend(); ++it)
        nodes << QVariantMap{{"id", it->id},
                             {"name", it->name},
                             {"kind", "pcg-volume"},
                             {"visible", it->visible},
                             {"active", it->enabled}};
    for (auto it = m_world.creation.presets.rbegin(); it != m_world.creation.presets.rend(); ++it)
        nodes << QVariantMap{
            {"id", it->id}, {"name", it->name}, {"kind", "asset-instance"}, {"visible", it->enabled}};
    for (auto it = m_world.creation.roadNodes.rbegin(); it != m_world.creation.roadNodes.rend(); ++it)
        nodes << QVariantMap{{"id", it->id},
                             {"name", it->name.isEmpty() ? "道路节点" : it->name},
                             {"kind", "road-node"},
                             {"visible", it->enabled}};
    const auto e = environmentDetails();
    nodes << QVariantMap{
        {"id", "environment"}, {"name", "环境"}, {"kind", "environment"}, {"visible", e["enabled"]}};
    for (auto pair : {std::pair{"sky", "天光 / 天空"},
                      {"sun", "太阳 · 平行光"},
                      {"atmosphere", "大气"},
                      {"water", "水面"}})
        nodes << QVariantMap{{"id", QString("environment-") + pair.first},
                             {"parent", "environment"},
                             {"name", QString("    ") + pair.second},
                             {"kind", "environment"},
                             {"visible", e[pair.first]}};
    return nodes;
}

QVariantMap Editor::layerDetails() const {
    int n = m_world.heights.layerIndex(m_selectedNode);
    if (n < 0)
        n = m_world.heights.layerIndex(m_world.heights.activeLayer);
    const auto &l = m_world.heights.layers[n];
    return {
        {"name", l.name},
        {"id", l.id},
        {"enabled", l.enabled},
        {"removable", n > 0},
        {"activeName", m_world.heights.layers[m_world.heights.layerIndex(m_world.heights.activeLayer)].name},
        {"editedColumns", int(l.deltas->size())},
        {"size", m_world.width},
        {"width", m_world.width},
        {"depth", m_world.depth},
        {"originX", m_world.originX},
        {"originZ", m_world.originZ},
        {"minY", wb::World::MinY},
        {"maxY", wb::World::MaxY - 1},
        {"seaLevel", m_world.seaLevel},
        {"seed", double(m_world.seed)},
        {"overrideCells", int(m_world.overrides.size())}};
}
QString Editor::materialLabel() const {
    return m_world.palette[m_material].label();
}
QVariantList Editor::materialProperties() const {
    QVariantList list;
    if (!m_world.assets)
        return list;
    const auto &s = m_world.palette[m_material];
    auto options = m_world.assets->catalog.value(s.name).options;
    for (auto p = options.begin(); p != options.end(); ++p)
        list << QVariantMap{{"key", p.key()}, {"values", p.value()}, {"value", s.properties.value(p.key())}};
    return list;
}
QVariantList Editor::blockCatalog(const QString &filter) const {
    std::vector<std::pair<int, QVariantMap>> ranked;
    auto add = [&](const QString &name, const QString &title, const QStringList &keys,
                   std::shared_ptr<const wb::BlockModel> model, const QString &thumbnail, int id) {
        int score = wb::searchScore(keys, filter);
        if (score < 0)
            return;
        const auto &color = wb::info(id < wb::BlockCount ? id : wb::Stone);
        ranked.push_back({score, QVariantMap{{"name", name},
                                             {"title", title},
                                             {"thumbnail", thumbnail},
                                             {"color", QColor::fromRgbF(color.r, color.g, color.b)},
                                             {"fallback", model->fallback},
                                             {"diagnostic", model->diagnostic}}});
    };
    if (m_world.assets && !m_world.assets->catalog.isEmpty()) {
        for (auto i = m_world.assets->catalog.cbegin(); i != m_world.assets->catalog.cend(); ++i)
            add(i.key(), i->title, i->searchTerms, i->preview, i->thumbnailData, wb::BlockCount);
    } else
        for (int i = 0; i < wb::BlockCount; ++i) {
            auto name = m_world.palette[i].name, title = QString::fromUtf8(wb::info(i).name);
            QString thumbnail;
            if (m_world.assets) {
                const auto img = m_world.assets->thumbnails.value(m_world.models[i]->thumbnail);
                if (!img.isNull()) {
                    QByteArray png;
                    QBuffer b(&png);
                    b.open(QIODevice::WriteOnly);
                    img.save(&b, "PNG");
                    thumbnail = "data:image/png;base64," + QString::fromLatin1(png.toBase64());
                }
            }
            add(name, title, wb::searchKeys(name, title), m_world.models[i], thumbnail, i);
        }
    std::stable_sort(ranked.begin(), ranked.end(),
                     [](const auto &a, const auto &b) { return a.first < b.first; });
    QVariantList list;
    for (const auto &entry : ranked)
        list << entry.second;
    return list;
}
QVariantMap Editor::brushOptions() const {
    const auto &o = m_brushOptions;
    return {{"decay", o.decay},
            {"shape", o.shape},
            {"falloff", o.falloff},
            {"flattenMode", o.flattenMode},
            {"fillHoles", o.fillHoles},
            {"fillDepth", o.fillDepth},
            {"filterEnabled", o.filterEnabled},
            {"minHeight", o.minHeight},
            {"maxHeight", o.maxHeight},
            {"minSlope", o.minSlope},
            {"maxSlope", o.maxSlope},
            {"materialFilter", o.materialFilter},
            {"filterBlock", o.filterBlock}};
}
void Editor::setBrushOption(const QString &key, const QVariant &value) {
    endStroke();
    auto &o = m_brushOptions;
    if (key == "decay")
        o.decay = std::clamp(value.toDouble(), 0.0, 1.0);
    else if (key == "shape")
        o.shape = std::clamp(value.toInt(), 0, 1);
    else if (key == "falloff")
        o.falloff = std::clamp(value.toInt(), 0, 2);
    else if (key == "flattenMode")
        o.flattenMode = std::clamp(value.toInt(), 0, 2);
    else if (key == "fillHoles")
        o.fillHoles = value.toBool();
    else if (key == "fillDepth")
        o.fillDepth = std::clamp(value.toInt(), 1, 383);
    else if (key == "filterEnabled")
        o.filterEnabled = value.toBool();
    else if (key == "minHeight") {
        o.minHeight = std::clamp(value.toInt(), -63, 318);
        o.maxHeight = std::max(o.maxHeight, o.minHeight);
    } else if (key == "maxHeight") {
        o.maxHeight = std::clamp(value.toInt(), -63, 318);
        o.minHeight = std::min(o.minHeight, o.maxHeight);
    } else if (key == "minSlope") {
        o.minSlope = std::clamp(value.toInt(), 0, 90);
        o.maxSlope = std::max(o.maxSlope, o.minSlope);
    } else if (key == "maxSlope") {
        o.maxSlope = std::clamp(value.toInt(), 0, 90);
        o.minSlope = std::min(o.minSlope, o.maxSlope);
    } else if (key == "materialFilter")
        o.materialFilter = std::clamp(value.toInt(), 0, 2);
    else if (key == "filterBlock")
        o.filterBlock = value.toString();
    saveBrushProfile();
    emit changed();
}
QStringList Editor::defaultResourceSources() const {
    return QSettings().value("resources/defaultSources").toStringList();
}
void Editor::initializeResources() {
    const auto paths = defaultResourceSources();
    if (paths.isEmpty())
        emit resourceSetupRequested();
    else
        loadResources(paths, true);
}
void Editor::clearDefaultResources() {
    QSettings().remove("resources/defaultSources");
    m_defaultAssets.reset();
    m_defaultFingerprint.clear();
    emit changed();
}
QUrl Editor::dialogFolder(const QString &key) const {
    QString path = QSettings().value("dialogs/" + key).toString();
    if (!QDir(path).exists() || path.isEmpty())
        path = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return QUrl::fromLocalFile(path);
}
void Editor::rememberDialogFolder(const QString &key, const QUrl &url, bool directory) {
    if (!url.isLocalFile())
        return;
    const auto path = directory ? url.toLocalFile() : QFileInfo(url.toLocalFile()).absolutePath();
    if (QDir(path).exists())
        QSettings().setValue("dialogs/" + key, path);
}
QString Editor::resourceFingerprint(const QStringList &paths) {
    QStringList parts;
    for (const auto &path : paths) {
        QFileInfo file(path);
        if (!file.exists() || !file.isFile())
            return {};
        parts << file.absoluteFilePath() << QString::number(file.size())
              << QString::number(file.lastModified().toMSecsSinceEpoch());
    }
    return parts.join('|');
}
void Editor::reportAssetProgress(int progress, const QString &message) {
    m_assetProgress = std::max(m_assetProgress, progress);
    m_assetStage = message;
    m_assetLog << QTime::currentTime().toString("HH:mm:ss") + "  " + message;
    while (m_assetLog.size() > 160)
        m_assetLog.removeFirst();
    emit assetProgressChanged();
}
void Editor::selectBlock(const QString &name) {
    if (assetBusy())
        return;
    endStroke();
    for (int i = 0; i < wb::BlockCount; ++i)
        if (m_world.palette[i].name == name) {
            setMaterial(i);
            return;
        }
    if (m_world.assets && m_world.assets->catalog.contains(name)) {
        try {
            setMaterial(int(m_world.intern(m_world.assets->catalog[name].defaults)));
        } catch (const std::exception &e) {
            emit failure(e.what());
        }
    }
}
void Editor::setBlockProperty(const QString &key, const QString &value) {
    if (!m_world.assets || assetBusy())
        return;
    endStroke();
    auto s = m_world.palette[m_material];
    s.properties[key] = value;
    QString error;
    if (!m_world.assets->validate(s, error)) {
        emit failure(error);
        return;
    }
    try {
        setMaterial(int(m_world.intern(s)));
    } catch (const std::exception &e) {
        emit failure(e.what());
    }
}
void Editor::loadResources(const QStringList &paths, bool restoring, bool rememberDefault) {
    if (assetBusy() || exportBusy())
        return;
    endStroke();
    m_restoringAssets = restoring;
    m_rememberDefault = rememberDefault;
    if (paths.isEmpty()) {
        m_world.setAssets({});
        m_status = "已清除资源来源 · 使用内置调试材质";
        if (rememberDefault)
            clearDefaultResources();
        ++m_epoch;
        ++m_revision;
        ++m_resourceRevision;
        dirtyAll();
        emit catalogChanged();
        emit changed();
        return;
    }
    m_loadingFingerprint = resourceFingerprint(paths);
    auto cache = (!m_loadingFingerprint.isEmpty() && m_defaultFingerprint == m_loadingFingerprint)
                     ? m_defaultAssets
                     : nullptr;
    m_assetProgress = 0;
    m_assetLog.clear();
    m_assetsLoading = true;
    m_status = "正在加载资源…";
    reportAssetProgress(0, cache ? "复用本次运行中已解析的默认资源" : "读取资源来源，后台解析中");
    emit changed();
    emit resourceLoadStarted();
    const auto palette = m_world.palette;
    m_assetWatcher.setFuture(QtConcurrent::run([this, paths, palette, cache] {
        auto report = [this](int progress, const QString &message) {
            QMetaObject::invokeMethod(
                this, [this, progress, message] { reportAssetProgress(progress, message); },
                Qt::QueuedConnection);
        };
        AssetResult r;
        r.library = cache ? cache : wb::AssetLibrary::load(paths, r.error, [&](int p, const QString &m) {
            report(p * 90 / 100, m);
        });
        if (r.library) {
            report(92, "准备当前工程使用的方块模型");
            for (size_t i = 0; i < palette.size(); ++i) {
                r.models.push_back(r.library->model(palette[i]));
                if (i % 128 == 0)
                    report(92 + int(7 * i / palette.size()),
                           QString("工程模型 %1 / %2").arg(i + 1).arg(palette.size()));
            }
        }
        return r;
    }));
}
void Editor::importResource(const QUrl &url, bool overlay) {
    if (!url.isLocalFile()) {
        emit failure("请选择本地 JAR/ZIP 文件");
        return;
    }
    QStringList paths = overlay ? m_world.resourcePaths : QStringList{};
    if (paths.contains(url.toLocalFile())) {
        emit failure("该来源已经加载");
        return;
    }
    paths << url.toLocalFile();
    loadResources(paths, false, true);
}
void Editor::removeResource(int index) {
    auto paths = m_world.resourcePaths;
    if (index < 0 || index >= paths.size())
        return;
    if (index == 0)
        paths.clear();
    else
        paths.removeAt(index);
    loadResources(paths, false, true);
}
bool Editor::mcpEnabled() const {
    return m_mcp && m_mcp->enabled();
}
void Editor::setMcpEnabled(bool enabled) {
    if (m_mcp)
        m_mcp->setEnabled(enabled);
    emit changed();
}
QString Editor::mcpConfiguration() const {
    return m_mcp ? m_mcp->configuration() : QString();
}
QString Editor::mcpActivity() const {
    return m_mcp ? m_mcp->activity() : QString();
}
void Editor::copyMcpConfiguration() {
    QGuiApplication::clipboard()->setText(mcpConfiguration());
}
QJsonObject Editor::worldInfo() const {
    QJsonArray terrain;
    for (const auto &i : m_world.heights.instances)
        terrain.append(QJsonObject{{"id", i.id},
                                   {"name", i.name},
                                   {"x", i.x},
                                   {"z", i.z},
                                   {"y", i.y},
                                   {"rotationX", i.rotationX},
                                   {"rotationY", i.rotationY},
                                   {"rotationZ", i.rotationZ},
                                   {"scaleX", i.scaleX},
                                   {"scaleY", i.scaleY},
                                   {"scaleZ", i.scaleZ},
                                   {"width", i.width},
                                   {"depth", i.depth},
                                   {"low", i.low},
                                   {"high", i.high},
                                   {"blend", i.blend},
                                   {"opacity", i.opacity},
                                   {"feather", i.feather},
                                   {"blendSmoothness", i.blendSmoothness},
                                   {"blendEdge", i.blendEdge},
                                   {"enabled", i.enabled},
                                   {"source", i.sourceName},
                                   {"mask", i.maskName}});
    QJsonArray sculptLayers;
    for (const auto &l : m_world.heights.layers)
        sculptLayers.append(QJsonObject{
            {"id", l.id}, {"name", l.name}, {"enabled", l.enabled}, {"columns", int(l.deltas->size())}});
    const auto creation = QJsonObject::fromVariantMap(creationState());
    QJsonObject rendering;
    if (auto *viewport = qobject_cast<Viewport *>(m_cameraController.data())) {
        auto m = viewport->renderMetrics();
        rendering = {{"cpu_submit_ms", m->submitMicros.load() / 1000.},
                     {"gpu_ms", m->gpuMicros.load() / 1000.},
                     {"pixel_width", m->pixelWidth.load()},
                     {"pixel_height", m->pixelHeight.load()},
                     {"render_scale", viewport->renderScale()},
                     {"shadow_quality", viewport->shadowQuality()},
                     {"object_triangles", m->objectTriangles.load()},
                     {"far_batches", m->objectBatches.load()},
                     {"object_lods", m->objectLods.load()}};
    }
    return {
        {"revision", double(m_revision)},
        {"rendering", rendering},
        {"sculpt_layers", sculptLayers},
        {"active_sculpt_layer", m_world.heights.activeLayer},
        {"terrain_instances", terrain},
        {"creation", creation},
        {"size", m_world.width},
        {"width", m_world.width},
        {"depth", m_world.depth},
        {"min", QJsonArray{m_world.originX, wb::World::MinY, m_world.originZ}},
        {"max", QJsonArray{m_world.originX + m_world.width - 1, wb::World::MaxY - 1,
                           m_world.originZ + m_world.depth - 1}},
        {"protected_y", wb::World::MinY},
        {"modified", modified()},
        {"resource_version", m_world.resourceVersion},
        {"palette_states", int(m_world.palette.size())},
        {"override_cells", int(m_world.overrides.size())},
        {"busy", commandBusy()},
        {"region_job", regionJobInfo()},
        {"mesh_busy", meshBusy()},
        {"environment", wb::saveEnvironment(m_world.environment)},
        {"preview",
         QJsonObject{{"exact_chunks", int(m_scene->chunks.size())},
                     {"renderer", "adaptive_terrain_with_local_voxels"},
                     {"surface_failure", m_surfaceFailure},
                     {"surface_scope", "formal error-bounded heightfield and sea plane plus local voxels; "
                                       "sparse interaction preview has no certified height bound; "
                                       "distant cave/model and independent water geometry pending"},
                     {"provisional", m_scene->provisional},
                     {"surface_error_bound", m_scene->provisional ? QJsonValue(QJsonValue::Null)
                                                                  : QJsonValue(m_scene->surfaceError)},
                     {"surface_samples", double(m_scene->sampleCount)},
                     {"updated_surface_samples", double(m_scene->updatedSamples)},
                     {"updated_surface_tiles", m_scene->updatedFarTiles},
                     {"surface_color_bytes", double(m_scene->colorBytes)},
                     {"surface_color_source",
                      "resolved top-face atlas average per column; no detailed texture mapping"},
                     {"mesh_bytes", double(m_scene->bufferBytes)},
                     {"source_revision", double(m_scene->sourceRevision)},
                     {"far_tiles", int(m_scene->farTiles.size())},
                     {"vertices", double(m_scene->vertices)},
                     {"water_vertices", double(m_scene->waterVertices)},
                     {"build_ms", double(m_scene->buildMilliseconds)},
                     {"far_ms", double(m_scene->farMilliseconds)}}},
        {"can_undo", canUndo()},
        {"can_redo", canRedo()},
        {"scene_nodes", QJsonArray::fromVariantList(sceneNodes())}};
}
QJsonObject Editor::executeTool(const QString &name, const QJsonObject &args) {
    auto integer = [](const QJsonValue &v, int lo, int hi) -> int {
        if (!v.isDouble() || !std::isfinite(v.toDouble()) || v.toDouble() != std::floor(v.toDouble()) ||
            v.toDouble() < lo || v.toDouble() > hi)
            throw std::runtime_error("整数参数缺失、类型错误或越界");
        return int(v.toDouble());
    };
    auto coord = [&](const QJsonValue &v, bool writable) {
        auto a = v.toArray();
        if (a.size() != 3)
            throw std::runtime_error("坐标必须为 [x,y,z]");
        std::array<int, 3> p{integer(a[0], m_world.originX, m_world.originX + m_world.width - 1),
                             integer(a[1], wb::World::MinY + (writable ? 1 : 0), wb::World::MaxY - 1),
                             integer(a[2], m_world.originZ, m_world.originZ + m_world.depth - 1)};
        return p;
    };
    if (name == "world_info")
        return worldInfo();
    if (name == "creation" && args["action"].toString() == "cancel") {
        cancelCreation();
        return {{"cancel_requested", creationBusy()}};
    }
    if (name == "creation" && args.value("action").toString("inspect") == "inspect")
        return {{"revision", double(m_revision)},
                {"creation", QJsonObject::fromVariantMap(creationState())},
                {"node_types", wb::pcgNodeCatalog()}};
    if (name == "region_job") {
        const auto action = args["action"].toString();
        if (m_regionId.isEmpty() || args["job_id"].toString() != m_regionId)
            throw std::runtime_error("区域任务不存在或已被新任务替换");
        if (action == "cancel")
            cancelRegion();
        else if (action != "status")
            throw std::runtime_error("action 必须为 status 或 cancel");
        return regionJobInfo();
    }
    if (name == "catalog") {
        QString query = args["query"].toString();
        int limit = args.contains("limit") ? integer(args["limit"], 1, 200) : 50;
        QJsonArray blocks;
        int total = 0;
        auto append = [&](const wb::BlockState &s, const QString &title,
                          const QMap<QString, QStringList> &options) {
            if (wb::searchScore(wb::searchKeys(s.name, title), query) < 0)
                return;
            ++total;
            if (blocks.size() >= limit)
                return;
            QJsonObject props;
            for (auto p = options.begin(); p != options.end(); ++p)
                props[p.key()] = QJsonArray::fromStringList(p.value());
            blocks << QJsonObject{{"block", s.label()}, {"title", title}, {"properties", props}};
        };
        if (m_world.assets && !m_world.assets->catalog.isEmpty())
            for (auto i = m_world.assets->catalog.begin(); i != m_world.assets->catalog.end(); ++i)
                append(i->defaults, i->title, i->options);
        else
            for (int i = 0; i < wb::BlockCount; ++i)
                append(m_world.palette[i], QString::fromUtf8(wb::info(i).name), {});
        return {{"blocks", blocks}, {"matches", total}, {"truncated", total > limit}};
    }
    if (name == "read_region") {
        auto a = coord(args["min"], false), b = coord(args["max"], false);
        int64_t count = 1;
        for (int i = 0; i < 3; ++i) {
            if (a[i] > b[i])
                throw std::runtime_error("min 必须小于等于 max");
            count *= b[i] - a[i] + 1;
        }
        if (count > 16384)
            throw std::runtime_error("读取区域最多 16384 方块");
        QJsonArray cells, palette;
        for (const auto &p : m_world.palette)
            palette << p.label();
        for (int y = a[1]; y <= b[1]; ++y)
            for (int z = a[2]; z <= b[2]; ++z)
                for (int x = a[0]; x <= b[0]; ++x)
                    cells << int(m_world.block(x, y, z));
        return {{"revision", double(m_revision)},
                {"palette", palette},
                {"order", "y,z,x; x fastest"},
                {"blocks", cells}};
    }
    if (name == "camera" || name == "screenshot") {
        Viewport *v = nullptr;
        for (auto *window : QGuiApplication::topLevelWindows())
            if (auto *quick = qobject_cast<QQuickWindow *>(window))
                if (auto *found = quick->findChild<Viewport *>("voxelViewport")) {
                    v = found;
                    break;
                }
        if (!v)
            throw std::runtime_error("视口不可用");
        if (name == "camera") {
            if (!args.isEmpty() && (commandBusy() || v->rawActive()))
                throw std::runtime_error("用户正在操作视口，请稍后重试");
            for (auto it = args.begin(); it != args.end(); ++it)
                if (it.key() != "position" && it.key() != "yaw" && it.key() != "pitch" && it.key() != "reset")
                    throw std::runtime_error(("Unknown camera parameter: " + it.key()).toStdString());
            if (args.contains("reset") && !args["reset"].isBool())
                throw std::runtime_error("reset must be boolean");
            v->applyCamera(args);
            return {{"camera", v->cameraText()}};
        }
        if (!m_surfaceFailure.isEmpty())
            throw std::runtime_error(m_surfaceFailure.toStdString());
        if (meshBusy())
            throw std::runtime_error("网格仍在更新，请等待 world_info.mesh_busy=false 再截图");
        auto img = v->window()->grabWindow();
        if (img.isNull())
            throw std::runtime_error("窗口截图不可用");
        img = img.scaled(1280, 900, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QByteArray data;
        QBuffer buffer(&data);
        buffer.open(QIODevice::WriteOnly);
        img.save(&buffer, "PNG");
        return {{"png", QString::fromLatin1(data.toBase64())}};
    }
    if (commandBusy())
        throw std::runtime_error("用户正在编辑或加载资源，请稍后重试");
    if (!args["expected_revision"].isDouble() || args["expected_revision"].toDouble() != double(m_revision))
        throw std::runtime_error("revision 已过期；请先读取 world_info，不得盲目重试写入");
    if (name == "height_region") {
        const auto a = args["min"].toArray(), b = args["max"].toArray();
        if (a.size() != 2 || b.size() != 2)
            throw std::runtime_error("坐标必须为 [x,z]");
        const int x = integer(a[0], m_world.originX, m_world.originX + m_world.width - 1);
        const int z = integer(a[1], m_world.originZ, m_world.originZ + m_world.depth - 1);
        const int mx = integer(b[0], x, m_world.originX + m_world.width - 1);
        const int mz = integer(b[1], z, m_world.originZ + m_world.depth - 1);
        const int offset = integer(args["offset"], -381, 381);
        QString error;
        if (!startHeightRegion(QRect(QPoint(x, z), QPoint(mx, mz)), offset, error))
            throw std::runtime_error(error.toStdString());
        return regionJobInfo();
    }
    if (name == "environment") {
        if (!args["changes"].isObject())
            throw std::runtime_error("changes must be an object");
        wb::Environment next;
        QString error;
        if (!wb::patchEnvironment(m_world.environment, args["changes"].toObject(), next, error))
            throw std::runtime_error(error.toStdString());
        setEnvironment(args["changes"].toObject().toVariantMap());
        return worldInfo();
    }
    if (name == "creation") {
        QObject feedback;
        QString creationError;
        connect(this, &Editor::failure, &feedback, [&](const QString &message) { creationError = message; });
        const auto action = args["action"].toString();
        bool ok = false;
        if (action == "edit")
            ok = creationEdit(args["operation"].toString(), args["data"].toObject().toVariantMap());
        else if (action == "new_graph")
            ok = createPcgGraph(args["name"].toString(), args["surface_material"].toBool());
        else if (action == "store_pcg_asset")
            ok = storePcgAsset(args["id"].toString());
        else if (action == "new_volume")
            ok = addPcgVolume(args["graph_id"].toString());
        else if (action == "import_asset")
            ok = importStructureAsset(QUrl::fromLocalFile(args["path"].toString()));
        else if (action == "place_asset")
            ok = placeAssetInstance(args["asset_id"].toString(),
                                    integer(args["x"], m_world.originX, m_world.originX + m_world.width - 1),
                                    integer(args["y"], -63, 319),
                                    integer(args["z"], m_world.originZ, m_world.originZ + m_world.depth - 1));
        else if (action == "road_node")
            ok = addRoadNode(integer(args["x"], m_world.originX, m_world.originX + m_world.width - 1),
                             integer(args["y"], -63, 319),
                             integer(args["z"], m_world.originZ, m_world.originZ + m_world.depth - 1));
        else if (action == "file")
            ok = creationFile(args["operation"].toString(), args["id"].toString(),
                              QUrl::fromLocalFile(args["path"].toString()));
        else if (action == "cave_analysis")
            return QJsonObject::fromVariantMap(caveAnalysis(args["asset_id"].toString()));
        else
            throw std::runtime_error("未知创作操作");
        if (!ok)
            throw std::runtime_error(
                (creationError.isEmpty()
                     ? QString("创作操作失败；检查对象引用、参数、资源与生成预算。世界未提交。")
                     : creationError)
                    .toStdString());
        return {{"revision", double(m_revision)}, {"creation", QJsonObject::fromVariantMap(creationState())}};
    }
    if (name == "undo") {
        undo();
        return worldInfo();
    }
    if (name == "sculpt_layer") {
        const auto action = args["action"].toString(), id = args["id"].toString();
        const int n = m_world.heights.layerIndex(id);
        if (action == "add") {
            if (m_world.heights.layers.size() >= 32)
                throw std::runtime_error("最多32个雕刻层");
            addSculptLayer();
        } else {
            if (n < 0)
                throw std::runtime_error("雕刻层不存在");
            if (action == "select")
                setActiveSculptLayer(id);
            else if (action == "visible") {
                if (!args["enabled"].isBool())
                    throw std::runtime_error("enabled必须是布尔值");
                setNodeVisible(id, args["enabled"].toBool());
            } else if (action == "remove") {
                if (n == 0)
                    throw std::runtime_error("基础层不能移除");
                setSelectedNode(id);
                removeSculptLayer();
            } else if (action == "rename") {
                auto name = args["name"].toString().trimmed();
                if (name.isEmpty() || name.size() > 128)
                    throw std::runtime_error("层名无效");
                setSelectedNode(id);
                renameSculptLayer(name);
            } else
                throw std::runtime_error("未知雕刻层操作");
        }
        return worldInfo();
    }
    if (name == "terrain_instance") {
        auto next = m_world.heights.instances;
        auto it = std::find_if(next.begin(), next.end(),
                               [&](const auto &i) { return i.id == args["id"].toString(); });
        if (it == next.end() || !args["changes"].isObject() || args["changes"].toObject().isEmpty())
            throw std::runtime_error("请选择有效实例，并提供非空 changes 对象");
        const auto changes = args["changes"].toObject();
        for (auto p = changes.begin(); p != changes.end(); ++p) {
            const auto key = p.key();
            if (key == "name") {
                if (!p.value().isString())
                    throw std::runtime_error("name 必须为字符串");
                it->name = p.value().toString().trimmed();
            } else if (key == "enabled") {
                if (!p.value().isBool())
                    throw std::runtime_error("enabled 必须为布尔值");
                it->enabled = p.value().toBool();
            } else {
                const int n = integer(p.value(), -2000000, 2000000);
                if (key == "x")
                    it->x = n;
                else if (key == "z")
                    it->z = n;
                else if (key == "y")
                    it->y = n;
                else if (key == "rotationX")
                    it->rotationX = n;
                else if (key == "rotationY")
                    it->rotationY = n;
                else if (key == "rotationZ")
                    it->rotationZ = n;
                else if (key == "scaleX")
                    it->scaleX = n;
                else if (key == "scaleY")
                    it->scaleY = n;
                else if (key == "scaleZ")
                    it->scaleZ = n;
                else if (key == "width")
                    it->width = n;
                else if (key == "depth")
                    it->depth = n;
                else if (key == "low")
                    it->low = n;
                else if (key == "high")
                    it->high = n;
                else if (key == "blend")
                    it->blend = n;
                else if (key == "opacity")
                    it->opacity = n;
                else if (key == "feather")
                    it->feather = n;
                else if (key == "blendSmoothness")
                    it->blendSmoothness = n;
                else if (key == "blendEdge")
                    it->blendEdge = n;
                else
                    throw std::runtime_error("未知地貌属性");
            }
        }
        QString error;
        if (!wb::validateTerrain(next, error))
            throw std::runtime_error(error.toStdString());
        commitTerrain(std::move(next), "MCP 调整地貌实例");
        return worldInfo();
    }
    if (name == "redo") {
        redo();
        return worldInfo();
    }
    if (name == "sculpt") {
        if (!m_world.heights.layers[m_world.heights.layerIndex(m_world.heights.activeLayer)].enabled)
            throw std::runtime_error("当前雕刻层隐藏，请先启用或选择其他层");
        auto p = coord(args["position"], true);
        int tool = integer(args["tool"], 0, 5), radius = integer(args["radius"], 1, 16),
            strength = integer(args["strength"], 1, 4);
        int material = args.contains("material_id")
                           ? integer(args["material_id"], 0, int(m_world.palette.size()) - 1)
                           : int(wb::Stone);
        wb::Stroke stroke(m_world);
        wb::Hit hit{true,
                    p[0],
                    p[1],
                    p[2],
                    QVector3D(float(p[0]), float(p[1]), float(p[2])),
                    {0, 1, 0},
                    m_world.block(p[0], p[1], p[2])};
        wb::BrushOptions options;
        if (args.contains("fill_holes")) {
            if (!args["fill_holes"].isBool())
                throw std::runtime_error("fill_holes 必须是布尔值");
            options.fillHoles = args["fill_holes"].toBool();
        }
        if (args.contains("fill_depth"))
            options.fillDepth = integer(args["fill_depth"], 1, 383);
        if (args.contains("decay")) {
            if (!args["decay"].isDouble() || !std::isfinite(args["decay"].toDouble()) ||
                args["decay"].toDouble() < 0 || args["decay"].toDouble() > 1)
                throw std::runtime_error("decay 必须为 0…1");
            options.decay = args["decay"].toDouble();
        }
        if (args.contains("shape"))
            options.shape = integer(args["shape"], 0, 1);
        if (args.contains("falloff"))
            options.falloff = integer(args["falloff"], 0, 2);
        if (args.contains("flatten_mode"))
            options.flattenMode = integer(args["flatten_mode"], 0, 2);
        stroke.stamp(hit, tool, radius, strength, wb::Block(material), args["reverse"].toBool(), options);
        auto command = stroke.finish("MCP 雕刻");
        if (!command.cells.empty() || !command.heights.empty()) {
            ++m_revision;
            if (!dirtyCommand(command))
                throw std::runtime_error("创作生成失败，修改已回滚");
            pushCommand(std::move(command));
        }
        return worldInfo();
    }
    auto parseBlock = [&](const QJsonValue &value) {
        if (!value.isString())
            throw std::runtime_error("block 必须为命名空间方块状态字符串");
        const QString text = value.toString();
        static const QRegularExpression rx("^([a-z0-9_.-]+:[a-z0-9_./-]+)(?:\\[([a-z0-9_=,]+)\\])?$");
        auto match = rx.match(text);
        if (!match.hasMatch())
            throw std::runtime_error("无效方块状态格式");
        wb::BlockState s{match.captured(1), {}};
        bool known = false;
        for (const auto &p : m_world.palette)
            if (p.name == s.name) {
                s = p;
                known = true;
                break;
            }
        if (m_world.assets && m_world.assets->catalog.contains(s.name)) {
            s = m_world.assets->catalog[s.name].defaults;
            known = true;
        }
        if (!known)
            throw std::runtime_error("未知方块；先导入对应版本 JAR 并查询 catalog");
        QSet<QString> keys;
        for (const auto &part : match.captured(2).split(',', Qt::SkipEmptyParts)) {
            auto pair = part.split('=');
            if (pair.size() != 2 || keys.contains(pair[0]))
                throw std::runtime_error("无效或重复方块属性");
            keys << pair[0];
            s.properties[pair[0]] = pair[1];
        }
        if (m_world.assets && m_world.assets->catalog.contains(s.name)) {
            QString error;
            if (!m_world.assets->validate(s, error))
                throw std::runtime_error(error.toStdString());
        } else {
            bool valid = false;
            for (const auto &p : m_world.palette)
                if (p == s)
                    valid = true;
            if (!valid)
                throw std::runtime_error("无资源时只接受内置状态");
        }
        return s;
    };
    struct Edit {
        std::array<int, 3> p;
        wb::BlockState state;
    };
    std::vector<Edit> edits;
    if (name == "set_blocks") {
        const auto entries = args["blocks"].toArray();
        if (entries.isEmpty() || entries.size() > 65536)
            throw std::runtime_error("blocks 必须包含 1–65536 项");
        QSet<uint64_t> seen;
        for (const auto &entry : entries) {
            auto o = entry.toObject();
            auto p = coord(o["position"], true);
            auto key = m_world.key(p[0], p[1], p[2]);
            if (seen.contains(key))
                throw std::runtime_error("批次包含重复坐标");
            seen << key;
            edits.push_back({p, parseBlock(o["block"])});
        }
    } else if (name == "fill_box") {
        auto a = coord(args["min"], true), b = coord(args["max"], true);
        int64_t count = 1;
        for (int i = 0; i < 3; ++i) {
            if (a[i] > b[i])
                throw std::runtime_error("min 必须小于等于 max");
            count *= b[i] - a[i] + 1;
        }
        if (count > 65536)
            throw std::runtime_error("单次填充最多 65536 方块");
        auto state = parseBlock(args["block"]);
        bool hollow = args["hollow"].toBool();
        for (int y = a[1]; y <= b[1]; ++y)
            for (int z = a[2]; z <= b[2]; ++z)
                for (int x = a[0]; x <= b[0]; ++x)
                    if (!hollow || x == a[0] || x == b[0] || y == a[1] || y == b[1] || z == a[2] || z == b[2])
                        edits.push_back({{x, y, z}, state});
    } else
        throw std::runtime_error("未知工具");
    // Validate and stage against a snapshot before changing the authoritative world.
    auto next = m_world;
    wb::Command command;
    command.label = "MCP " + name;
    for (const auto &edit : edits) {
        auto p = edit.p;
        auto block = next.intern(edit.state);
        auto key = next.key(p[0], p[1], p[2]);
        if (next.block(p[0], p[1], p[2]) == block)
            continue;
        auto old = next.overrides.find(key);
        int before = old == next.overrides.end() ? -1 : int(old->second);
        next.setBlock(p[0], p[1], p[2], block);
        auto current = next.overrides.find(key);
        int after = current == next.overrides.end() ? -1 : int(current->second);
        command.cells.push_back({key, before, after, m_world.terrainOverrideKeys.contains(key), false});
    }
    const int count = int(command.cells.size());
    if (count) {
        m_world = std::move(next);
        ++m_revision;
        if (!dirtyCommand(command))
            throw std::runtime_error("创作生成失败，修改已回滚");
        pushCommand(std::move(command));
    }
    auto result = worldInfo();
    result["changed_cells"] = count;
    return result;
}

void Editor::saveBrushProfile() {
    if (m_tool < 0 || m_tool > 5)
        return;
    QSettings settings;
    settings.beginGroup(QString("brush/%1").arg(m_tool));
    settings.setValue("radius", m_radius);
    settings.setValue("strength", m_strength);
    settings.setValue("options", brushOptions());
    const auto &state = m_world.palette[m_material];
    settings.setValue("block", state.name);
    QVariantMap properties;
    for (auto it = state.properties.begin(); it != state.properties.end(); ++it)
        properties[it.key()] = it.value();
    settings.setValue("properties", properties);
}
void Editor::restoreBrushProfile() {
    if (m_tool < 0 || m_tool > 5)
        return;
    QSettings settings;
    settings.beginGroup(QString("brush/%1").arg(m_tool));
    m_radius = std::clamp(settings.value("radius", 4).toInt(), 1, 16);
    m_strength = std::clamp(settings.value("strength", 4).toInt(), 1, 4);
    const auto saved = settings.value("options").toMap();
    auto bounded = [&](const char *key, int fallback, int lo, int hi) {
        return std::clamp(saved.value(key, fallback).toInt(), lo, hi);
    };
    auto &o = m_brushOptions;
    o = wb::BrushOptions{};
    o.decay = std::clamp(saved.value("decay", .5).toDouble(), 0.0, 1.0);
    o.shape = bounded("shape", 0, 0, 1);
    o.falloff = bounded("falloff", 0, 0, 2);
    o.flattenMode = bounded("flattenMode", 0, 0, 2);
    o.fillHoles = saved.value("fillHoles", false).toBool();
    o.fillDepth = bounded("fillDepth", 8, 1, 383);
    o.filterEnabled = saved.value("filterEnabled", false).toBool();
    o.materialFilter = bounded("materialFilter", 0, 0, 2);
    o.minHeight = bounded("minHeight", -63, -63, 318);
    o.maxHeight = std::max(o.minHeight, bounded("maxHeight", 318, -63, 318));
    o.minSlope = bounded("minSlope", 0, 0, 90);
    o.maxSlope = std::max(o.minSlope, bounded("maxSlope", 90, 0, 90));
    o.filterBlock = saved.value("filterBlock", "minecraft:stone").toString();
    wb::BlockState state{settings.value("block", "minecraft:stone").toString(), {}};
    const auto properties = settings.value("properties").toMap();
    for (auto it = properties.begin(); it != properties.end(); ++it)
        state.properties[it.key()] = it.value().toString();
    m_material = wb::Stone;
    for (size_t i = 0; i < m_world.palette.size(); ++i)
        if (m_world.palette[i] == state) {
            m_material = int(i);
            return;
        }
    QString error;
    if (m_world.assets && m_world.assets->validate(state, error))
        m_material = int(m_world.intern(state));
}
QVariantMap Editor::selection() const {
    return {{"valid", m_selection.isValid()}, {"minX", m_selection.left()},   {"minZ", m_selection.top()},
            {"maxX", m_selection.right()},    {"maxZ", m_selection.bottom()}, {"width", m_selection.width()},
            {"depth", m_selection.height()}};
}
void Editor::setSelection(int x0, int z0, int x1, int z1) {
    endStroke();
    m_selection =
        QRect(QPoint(std::min(x0, x1), std::min(z0, z1)), QPoint(std::max(x0, x1), std::max(z0, z1)))
            .intersected(QRect(m_world.originX, m_world.originZ, m_world.width, m_world.depth));
    emit changed();
}
void Editor::clearSelection() {
    endStroke();
    m_selection = {};
    m_selectionOnly = false;
    emit changed();
}
