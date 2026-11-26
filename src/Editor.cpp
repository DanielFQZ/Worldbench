#include "Editor.h"
#include "Viewport.h"
#include <QKeyEvent>
#include <QKeySequence>
#include "McpServer.h"
#include "core/Project.h"
#include "core/RenderSupport.h"
#include "core/TerrainSurface.h"
#include <QBuffer>
#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSettings>
#include <QThreadPool>
#include <QUuid>
#include <QtConcurrent>
#include <algorithm>

namespace {
QString defaultShortcut(const QString &action) {
    static const QHash<QString, QString> defaults{
        {"save", "Ctrl+S"},          {"open", "Ctrl+O"},  {"new", "Ctrl+N"},
        {"undo", "Ctrl+Z"},          {"redo", "Ctrl+Y"},  {"clearSelection", "Ctrl+D"},
        {"deleteTerrain", "Delete"}, {"focus", "F"},      {"translateGizmo", "W"},
        {"rotateGizmo", "E"},        {"scaleGizmo", "R"}, {"toolCycle", "1"},
        {"toolFlatten", "2"},        {"toolCarve", "3"},  {"toolFill", "4"},
        {"toolPaint", "5"},          {"toolSmooth", "6"}, {"toolSelect", "7"},
        {"toolNavigate", "0"}, {"assetLibrary", "Ctrl+Space"}, {"redoAlternate", "Ctrl+Shift+Z"}, {"cancelSelection", "Esc"},
        {"brushSmaller", "["}, {"brushLarger", "]"}};
    return defaults.value(action);
}
} // namespace

Editor::Editor(QObject *parent)
    : QObject(parent), m_world(wb::World::create()), m_scene(std::make_shared<wb::MeshScene>()) {
    connect(this, &Editor::changed, this, &Editor::placementChanged);
    m_surfaceWorkers.setMaxThreadCount(std::clamp(QThread::idealThreadCount() - 2, 1, 4));
    QSettings settings;
    m_exactRadiusChunks = std::clamp(settings.value("performance/exactRadiusChunks", 4).toInt(), 1, 12);
    initializeTerrainImporter();
    m_regionTimer.setInterval(50);
    connect(&m_regionTimer, &QTimer::timeout, this, &Editor::regionProgressChanged);
    connect(&m_regionWatcher, &QFutureWatcherBase::finished, this, [this] {
        m_regionTimer.stop();
        m_regionLoading = false;
        // Consume the result so the completed future cannot pin a large layer
        // after its undo entry is evicted or another project is opened.
        auto result = m_regionWatcher.future().takeResult();
        emit regionProgressChanged();
        if (result.cancelled || m_regionControl->cancelled.load()) {
            m_regionState = "cancelled";
            m_regionResultRevision = m_revision;
            m_status = "已取消区域高度调整 · 工程与撤销历史未改变";
            emit changed();
            return;
        }
        if (m_regionRevision != m_revision)
            result.error = "计算期间工程状态已变化，区域结果已丢弃，请重新执行";
        if (!result.error.isEmpty()) {
            m_regionState = "failed";
            m_regionError = result.error;
            m_regionResultRevision = m_revision;
            m_status = result.error;
            emit failure(result.error);
            emit changed();
            return;
        }
        const int count = result.changed;
        if (count) {
            m_world.apply(result.command, true);
            ++m_revision;
            if (!dirtyCommand(result.command)) {
                m_regionState = "failed";
                m_regionError = "创作生成失败，区域修改回滚";
                emit changed();
                return;
            }
            pushCommand(std::move(result.command));
        }
        m_status = QString("选区高度调整完成 · %1 列改变 · %2 列触及高度边界").arg(count).arg(result.clipped);
        m_regionState = "completed";
        m_regionResultRevision = m_revision;
        m_regionChanged = count;
        m_regionClipped = result.clipped;
        emit changed();
    });
    m_meshTimer.setSingleShot(true);
    m_surfaceProgressTimer.setInterval(150);
    connect(&m_surfaceProgressTimer, &QTimer::timeout, this, &Editor::changed);
    m_meshTimer.setInterval(8);
    connect(&m_meshTimer, &QTimer::timeout, this, &Editor::startMesh);
    connect(&m_meshWatcher, &QFutureWatcherBase::finished, this, [this] {
        m_meshActive = false;
        m_surfaceProgressTimer.stop();
        const auto result = m_meshWatcher.result();
        if (result && !result->error.isEmpty() && m_jobEpoch == m_epoch &&
            result->sourceRevision == m_revision) {
            m_surfaceFailure = result->error;
            m_dirty.clear();
            m_farDirty = false;
            m_farAllDirty = true;
            emit failure(m_surfaceFailure);
            emit changed();
            return;
        }
        // One worker publishes monotonically newer complete snapshots. While
        // input continues, show completed work and retain every pending dirty
        // region instead of cancelling forever. meshBusy stays true until the
        // latest snapshot converges; MCP screenshots cannot mistake it for final.
        const bool sameRevision = result && result->sourceRevision == m_revision;
        const bool activePreview =
            result && result->provisional && m_interactiveMesh && m_jobGesture == m_gesture;
        const bool correctQuality = result && (m_interactiveMesh ? activePreview : !result->provisional);
        const bool valid = result && result->error.isEmpty() && m_jobEpoch == m_epoch && correctQuality &&
                           (sameRevision || activePreview || !m_rejectStaleMeshes);
        if (valid) {
            m_scene = result;
            if (sameRevision && !m_interactiveMesh)
                m_rejectStaleMeshes = false;
            emit sceneChanged();
        }
        if (!valid) {
            // A newer command may have arrived while this worker was still
            // building. Its dirtyCommand() already recorded the affected
            // exact chunks and far regions, so retain those narrow invalidations
            // instead of promoting a stale result to a full-world rebuild.
            // Epoch changes (open/new project) have no trustworthy dirty set
            // and still require a complete rebuild.
            if (m_jobEpoch == m_epoch) {
                m_dirty.insert(m_jobDirty.begin(), m_jobDirty.end());
                m_farRegions.insert(m_farRegions.end(), m_jobFarRegions.begin(), m_jobFarRegions.end());
                m_farAllDirty = m_farAllDirty || m_jobAllFar;
                m_farDirty = true;
            } else {
                m_dirty = desiredChunks();
                m_farDirty = m_farAllDirty = true;
            }
        }
        m_jobDirty.clear();
        m_jobFarRegions.clear();
        if (!m_dirty.empty() || m_farDirty)
            m_meshTimer.start();
        else if (m_status.startsWith("正在构建") || m_status == "正在准备地形…")
            m_status = "就绪 · 右键 + WASD 漫游，左键雕刻";
        emit changed();
    });
    connect(&m_exportWatcher, &QFutureWatcherBase::finished, this, [this] {
        m_exportActive = false;
        const auto r = m_exportWatcher.result();
        m_status = r.ok ? QString("导出完成 · %1 个区块").arg(r.chunks) : r.error;
        emit changed();
        if (r.ok)
            emit exported(r.path);
        else if (!m_cancelExport.load())
            emit failure(r.error);
    });
    m_mcp = std::make_unique<McpServer>(this);
    connect(&m_assetWatcher, &QFutureWatcherBase::finished, this, [this] {
        m_assetsLoading = false;
        const auto result = m_assetWatcher.result();
        if (!result.library) {
            emit failure(result.error);
            m_status = "资源载入失败，保留原资源";
            reportAssetProgress(m_assetProgress, "失败：" + result.error);
            emit changed();
            emit resourceLoadFinished(false);
            return;
        }
        m_world.assets = result.library;
        m_world.resourcePaths = result.library->sources;
        m_world.resourceVersion = result.library->version;
        m_world.models = result.models;
        restoreBrushProfile();
        if (m_rememberDefault) {
            QSettings().setValue("resources/defaultSources", m_world.resourcePaths);
        }
        if (m_world.resourcePaths == defaultResourceSources()) {
            m_defaultAssets = result.library;
            m_defaultFingerprint = m_loadingFingerprint;
        }
        reportAssetProgress(100, "资源载入完成；预览网格在后台更新");
        ++m_epoch;
        ++m_revision;
        if (!m_restoringAssets)
            ++m_resourceRevision;
        m_scene = std::make_shared<wb::MeshScene>();
        dirtyAll();
        m_status = QString("资源已载入 · %1 个方块 · %2")
                       .arg(result.library->catalog.size())
                       .arg(result.library->version.isEmpty() ? "资源包" : result.library->version);
        if (!m_resourceWarning.isEmpty()) {
            m_status += " · " + m_resourceWarning;
            reportAssetProgress(100, m_resourceWarning);
            m_resourceWarning.clear();
        }
        if (result.library->skippedEntries)
            m_status +=
                QString(" · 跳过 %1 个无效条目（资源菜单查看详情）").arg(result.library->skippedEntries);
        emit catalogChanged();
        emit sceneChanged();
        emit changed();
        emit resourceLoadFinished(true);
    });
    restoreBrushProfile();
    dirtyAll();
}

void Editor::setExactRadiusChunks(int value) {
    value = std::clamp(value, 1, 12);
    if (value == m_exactRadiusChunks)
        return;
    m_exactRadiusChunks = value;
    QSettings().setValue("performance/exactRadiusChunks", value);
    // Camera coverage changes do not invalidate terrain heights or material pages.
    m_farDirty = true;
    if (!m_meshTimer.isActive())
        m_meshTimer.start();
    emit changed();
}

void Editor::setExactPreviewEnabled(bool value) {
    if (m_exactPreviewEnabled == value)
        return;
    m_exactPreviewEnabled = value;
    // Camera coverage changes do not invalidate terrain heights or material pages.
    m_farDirty = true;
    if (!m_meshTimer.isActive())
        m_meshTimer.start();
    emit changed();
}

namespace {
// Only an isolated translation may retain the validated procedural prefix.
bool independentMove(const wb::World &world, const wb::CreationState &before,
                     const wb::CreationState &after) {
    if (before.presets.size() != after.presets.size() || !world.generated ||
        world.generated->placementPages.size() > 64)
        return false;
    size_t bakedCells = 0;
    for (const auto &asset : before.presets) {
        if (!asset.enabled)
            continue;
        const auto stamp = world.generated->stamps.value(asset.id);
        if (!stamp)
            return false;
        bakedCells += stamp->voxels.size();
        if (bakedCells > 32768)
            return false;
    }
    auto unchanged = after;
    unchanged.presets = before.presets;
    if (unchanged != before)
        return false;
    const wb::AssetPreset *moved = nullptr, *previous = nullptr;
    for (size_t i = 0; i < after.presets.size(); ++i) {
        const auto &a = before.presets[i], &b = after.presets[i];
        if (a == b)
            continue;
        auto restored = b;
        restored.position = a.position;
        if (moved || !b.enabled || restored != a || !std::isfinite(b.position.lengthSquared()) ||
            b.position.lengthSquared() >= 1e14f)
            return false;
        previous = &a;
        moved = &b;
    }
    return moved && wb::canReuseScatter(world, *previous, *moved);
}
} // namespace
bool Editor::commitCreation(wb::CreationState next, const QString &label,
                            const std::vector<wb::TerrainInstance> *terrains) {
    auto terrainNames = terrains ? *terrains : m_world.heights.instances;
    QString nameError;
    if (!prepareLibraryNames(next, terrainNames, false, nameError) ||
        (terrains && !wb::validateTerrain(terrainNames, nameError))) {
        emit failure(nameError);
        return false;
    }
    const bool terrainChanged = terrains && terrainNames != m_world.heights.instances;
    if (next == m_world.creation && !terrainChanged)
        return true;
    if (regionBusy() || assetBusy()) {
        emit failure("请等待当前任务完成");
        return false;
    }
    endStroke();
    wb::Command command;
    command.label = label;
    command.creationBefore = std::make_shared<const wb::CreationState>(m_world.creation);
    command.creationAfter = std::make_shared<const wb::CreationState>(std::move(next));
    auto candidate = m_world;
    candidate.creation = *command.creationAfter;
    if (terrainChanged) {
        command.terrainBefore =
            std::make_shared<const std::vector<wb::TerrainInstance>>(candidate.heights.instances);
        command.terrainAfter = std::make_shared<const std::vector<wb::TerrainInstance>>(terrainNames);
        candidate.heights.instances = std::move(terrainNames);
    }
    const auto previousGenerated = m_world.generated;
    m_creationPending = command.creationAfter;
    QString error;
    auto metadataOnly = m_world.creation;
    metadataOnly.pcgAssets = candidate.creation.pcgAssets;
    metadataOnly.folders = candidate.creation.folders;
    metadataOnly.assetFolders = candidate.creation.assetFolders;
    auto materialVolumes = [](const wb::CreationState &state) {
        std::vector<wb::PcgVolume> result;
        for (const auto &v : state.volumes) {
            const auto *g = wb::libraryGraph(state, v.graphId);
            if (g && std::any_of(g->nodes.begin(), g->nodes.end(),
                                 [](const auto &n) { return n.type == "output_surface"; }))
                result.push_back(v);
        }
        return result;
    };
    const auto &before = m_world.creation;
    const auto &after = candidate.creation;
    const bool reuseMaterials = !terrainChanged && before.graphs == after.graphs &&
                                before.roadNodes == after.roadNodes && before.roadEdges == after.roadEdges &&
                                materialVolumes(before) == materialVolumes(after);
    QElapsedTimer generationTimer;
    generationTimer.start();
    const bool isolated = !terrainChanged && independentMove(m_world, before, after);
    const bool prepared = isolated ? candidate.rebuildCreation(error, {}, true, true)
                          : !terrainChanged && metadataOnly == candidate.creation
                              ? wb::validateCreation(candidate.creation, error)
                              : prepareCreation(candidate, error, true, reuseMaterials);
    m_placementGenerationMs = generationTimer.nsecsElapsed() / 1e6;
    if (!prepared) {
        m_creationPending.reset();
        emit failure(error);
        emit changed();
        return false;
    }
    m_creationPending.reset();
    m_world = std::move(candidate);
    ++m_revision;
    if (terrainChanged)
        dirtyAll();
    else
        dirtyCreation(previousGenerated);
    pushCommand(std::move(command), false);
    m_status = label + " · Ctrl+Z 撤销";
    if (isolated)
        emit placementChanged();
    else
        emit changed();
    return true;
}

QString Editor::shortcut(const QString &action) const {
    auto fallback = defaultShortcut(action);
    // Defaults were never persisted. Follow UE for uncustomized bindings,
    // but never steal Ctrl+Space from a user-defined action on upgrade.
    if (action == "assetLibrary" && !QSettings().contains("shortcuts/assetLibrary")) {
        QSettings settings;
        settings.beginGroup("shortcuts");
        QStringList occupied;
        for (const auto &key : settings.childKeys())
            occupied << QKeySequence::fromString(settings.value(key).toString(),
                                                QKeySequence::PortableText).toString(QKeySequence::PortableText);
        if (occupied.contains(fallback))
            fallback = occupied.contains("Shift+Space") ? QString{} : QStringLiteral("Shift+Space");
    }
    if (fallback.isEmpty())
        return {};
    return QKeySequence::fromString(QSettings().value("shortcuts/" + action, fallback).toString(),
                                    QKeySequence::PortableText).toString(QKeySequence::PortableText);
}

void Editor::setShortcut(const QString &action, const QString &sequence) {
    if (defaultShortcut(action).isEmpty())
        return;
    const auto value = sequence.trimmed();
    QSettings().setValue("shortcuts/" + action, QKeySequence::fromString(value, QKeySequence::PortableText).toString(QKeySequence::PortableText));
    emit shortcutsChanged();
    emit changed();
}

void Editor::resetShortcuts() {
    QSettings settings;
    for (const auto &action :
         {QStringLiteral("save"), QStringLiteral("open"), QStringLiteral("new"), QStringLiteral("undo"),
          QStringLiteral("redo"), QStringLiteral("clearSelection"), QStringLiteral("deleteTerrain"),
          QStringLiteral("focus"), QStringLiteral("translateGizmo"), QStringLiteral("rotateGizmo"),
          QStringLiteral("scaleGizmo"), QStringLiteral("toolCycle"), QStringLiteral("toolFlatten"),
          QStringLiteral("toolCarve"), QStringLiteral("toolFill"), QStringLiteral("toolPaint"),
          QStringLiteral("toolSmooth"), QStringLiteral("toolSelect"), QStringLiteral("toolNavigate"), QStringLiteral("assetLibrary"), QStringLiteral("redoAlternate"),
          QStringLiteral("cancelSelection"), QStringLiteral("brushSmaller"), QStringLiteral("brushLarger")})
        settings.remove("shortcuts/" + action);
    emit shortcutsChanged();
    emit changed();
}
void Editor::setShortcutRecording(bool recording) {
    if (m_shortcutRecording == recording) return;
    m_shortcutRecording = recording;
    if (recording) qApp->installEventFilter(this);
    else qApp->removeEventFilter(this);
    emit shortcutRecordingChanged();
}
bool Editor::eventFilter(QObject *object, QEvent *event) {
    if (m_shortcutRecording) {
        if (event->type() == QEvent::ShortcutOverride) { event->accept(); return true; }
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
            auto *key = static_cast<QKeyEvent *>(event);
            if (event->type() == QEvent::KeyPress && !key->isAutoRepeat() &&
                key->key() != Qt::Key_unknown && key->key() != Qt::Key_Shift &&
                key->key() != Qt::Key_Control && key->key() != Qt::Key_Alt &&
                key->key() != Qt::Key_Meta && key->key() != Qt::Key_AltGr) {
                emit shortcutCaptured(QKeySequence(key->keyCombination()).toString(QKeySequence::PortableText));
            }
            event->accept(); return true;
        }
    }
    return QObject::eventFilter(object, event);
}
Editor::~Editor() {
    if (m_shortcutRecording) qApp->removeEventFilter(this);
    if (m_meshCancel)
        m_meshCancel->store(true);
    if (m_regionControl)
        m_regionControl->cancelled = true;
    m_regionWatcher.waitForFinished();
    m_heightmapWatcher.waitForFinished();
    m_cancelExport = true;
    m_assetWatcher.waitForFinished();
    m_meshWatcher.waitForFinished();
    m_exportWatcher.waitForFinished();
}
bool Editor::acceptsProjectDrop(const QList<QUrl> &urls) const {
    return urls.size() == 1 && urls.front().isLocalFile() && QFileInfo(urls.front().toLocalFile()).isFile() &&
           QFileInfo(urls.front().toLocalFile()).suffix().compare("wbench", Qt::CaseInsensitive) == 0;
}
bool Editor::adjustSelectedHeight(int offset) {
    QString error;
    const bool ok = startHeightRegion(m_selection, offset, error);
    if (!ok && !error.isEmpty())
        emit failure(error);
    return ok;
}
bool Editor::startHeightRegion(const QRect &area, int offset, QString &error) {
    if (commandBusy() || exportBusy()) {
        error = "当前操作尚未结束";
        return false;
    }
    if (!wb::validateHeightRegion(m_world.heights, area, offset, error))
        return false;
    auto snapshot = m_world.heights;
    m_regionLoading = true;
    m_regionRevision = m_revision;
    m_regionResultRevision = m_revision;
    m_regionId = QUuid::createUuid().toString(QUuid::Id128);
    m_regionState = "running";
    m_regionError.clear();
    m_regionChanged = m_regionClipped = 0;
    m_regionControl = std::make_shared<RegionControl>();
    m_status = "正在分块计算选区高度 · 可继续观察场景或取消";
    m_regionTimer.start();
    m_regionWatcher.setFuture(
        QtConcurrent::run([snapshot = std::move(snapshot), area, offset, control = m_regionControl]() {
            return wb::prepareHeightRegion(snapshot, area, offset, [control](int done, int total) {
                control->total = total;
                control->done = done;
                return !control->cancelled.load();
            });
        }));
    emit regionProgressChanged();
    emit changed();
    return true;
}
int Editor::regionProgress() const {
    if (!m_regionControl)
        return 0;
    return int(std::min<int64_t>(m_regionLoading ? 99 : 100, int64_t(m_regionControl->done.load()) * 100 /
                                                                 std::max(1, m_regionControl->total.load())));
}
QString Editor::regionStatus() const {
    if (!m_regionControl)
        return {};
    if (m_regionControl->cancelled.load())
        return "正在取消区域操作…";
    return QString("分块计算 %1% · %2 / %3 项")
        .arg(regionProgress())
        .arg(m_regionControl->done.load())
        .arg(m_regionControl->total.load());
}
void Editor::cancelRegion() {
    if (m_regionLoading && m_regionControl) {
        m_regionControl->cancelled = true;
        m_regionState = "cancelling";
        emit regionProgressChanged();
    }
}
QJsonObject Editor::regionJobInfo() const {
    return {{"job_id", m_regionId},
            {"state", m_regionState},
            {"progress", regionProgress()},
            {"source_revision", double(m_regionRevision)},
            {"result_revision", double(m_regionResultRevision)},
            {"revision", double(m_revision)},
            {"changed_columns", m_regionChanged},
            {"clipped_columns", m_regionClipped},
            {"error", m_regionError}};
}
void Editor::setTool(int v) {
    endStroke();
    saveBrushProfile();
    m_tool = m_inspectionOnly ? -1 : std::clamp(v, -1, 6);
    restoreBrushProfile();
    emit changed();
}
void Editor::setRadius(int v) {
    m_radius = std::clamp(v, 1, 16);
    saveBrushProfile();
    emit changed();
}
void Editor::setStrength(int v) {
    m_strength = std::clamp(v, 1, 4);
    saveBrushProfile();
    emit changed();
}
void Editor::setMaterial(int v) {
    m_material = std::clamp(v, 0, int(m_world.palette.size()) - 1);
    saveBrushProfile();
    emit changed();
}
QString Editor::projectName() const {
    return m_path.isEmpty() ? "未命名工程" : QFileInfo(m_path).completeBaseName();
}
QString Editor::sceneStats() const {
    return QString("%1 × %2 方块 · 精确 %3 区块 / %5 远景块 · %4 万三角形")
        .arg(m_world.width)
        .arg(m_world.depth)
        .arg(m_scene->chunks.size())
        .arg(m_scene->vertices / 30000.0, 0, 'f', 1)
        .arg(m_scene->farTiles.size());
}
void Editor::beginStroke() {
    if (!m_stroke && !assetBusy()) {
        m_stroke = std::make_unique<wb::Stroke>(m_world);
        m_strokeChanged = false;
    }
}
void Editor::stamp(const wb::Hit &hit, bool reverse) {
    if (!hit.valid || m_tool < 0 || m_tool > 5 || assetBusy())
        return;
    // Height brushes edit authoritative columns at any distance. Residency
    // follows camera navigation, never the brush: keep the far surface stable.
    beginStroke();
    auto options = m_brushOptions;
    options.selectionEnabled = m_selectionOnly && m_selection.isValid();
    options.selectionMinX = m_selection.left();
    options.selectionMaxX = m_selection.right();
    options.selectionMinZ = m_selection.top();
    options.selectionMaxZ = m_selection.bottom();
    if (!m_stroke->stamp(hit, m_tool, m_radius, m_strength, wb::Block(m_material), reverse, options))
        return;
    m_strokeChanged = true;
    m_farDirty = true;
    m_farRegions.emplace_back(hit.x - m_radius - 2, hit.z - m_radius - 2, m_radius * 2 + 5, m_radius * 2 + 5);
    ++m_revision;
    const int n = m_world.chunksX();
    for (int z = wb::floorDiv(hit.z - m_radius - 2 - m_world.originZ, 16);
         z <= wb::floorDiv(hit.z + m_radius + 2 - m_world.originZ, 16); ++z)
        for (int x = wb::floorDiv(hit.x - m_radius - 2 - m_world.originX, 16);
             x <= wb::floorDiv(hit.x + m_radius + 2 - m_world.originX, 16); ++x)
            if (x >= 0 && z >= 0 && x < n && z < m_world.chunksZ())
                m_dirty.insert(z * n + x);
    if (!m_meshTimer.isActive())
        m_meshTimer.start();
    emit changed();
}
void Editor::endStroke() {
    if (!m_stroke)
        return;
    auto c = m_stroke->finish("雕刻笔划");
    m_stroke.reset();
    m_strokeChanged = false;
    if ((!c.cells.empty() || !c.heights.empty()) &&
        (!m_world.creation.volumes.empty() || !m_world.creation.presets.empty() ||
         !m_world.creation.roadEdges.empty()) &&
        !dirtyCommand(c))
        return;
    pushCommand(std::move(c));
}
void Editor::pushCommand(wb::Command c, bool notify) {
    if (!c.cells.empty() || !c.heights.empty() || c.terrainAfter || c.layersAfter || c.themeAfter ||
        c.environmentAfter || c.deltaAfter || c.creationAfter) {
        m_history.erase(m_history.begin() + m_cursor, m_history.end());
        const auto next = m_nextState++;
        m_history.push_back({std::move(c), m_state, next});
        m_state = next;
        // Bound history by both stroke count and delta payload size.
        auto bytes = [&] {
            size_t b = 0;
            std::set<const wb::HeightmapData *> images;
            std::set<const void *> layerMaps, structureData;
            for (const auto &a : m_world.creation.assets)
                structureData.insert(a.blocks.identity());
            // Only charge history for retained data beyond the live world.
            for (const auto &layer : m_world.heights.layers)
                layer.deltas->uniqueBytes(layerMaps);
            for (const auto &instance : m_world.heights.instances)
                for (const auto &image : {instance.image, instance.mask})
                    if (image)
                        images.insert(image.get());
            for (const auto &e : m_history) {
                for (const auto &state : {e.command.creationBefore, e.command.creationAfter})
                    if (state) {
                        b += state->graphs.size() * 1024 + state->volumes.size() * 256 +
                             state->presets.size() * 256;
                        for (const auto &a : state->assets)
                            if (structureData.insert(a.blocks.identity()).second)
                                b += a.blocks.size() * 80;
                    }
                for (const auto &theme : {e.command.themeBefore, e.command.themeAfter})
                    if (theme)
                        b += theme->bands.size() * 256;
                b += e.command.cells.size() * sizeof(wb::CellChange) +
                     e.command.heights.size() * sizeof(wb::HeightChange);
                for (const auto &map : {e.command.deltaBefore, e.command.deltaAfter})
                    if (map)
                        b += map->uniqueBytes(layerMaps);
                for (const auto &list : {e.command.layersBefore, e.command.layersAfter})
                    if (list)
                        for (const auto &l : *list)
                            b += l.deltas->uniqueBytes(layerMaps);
                for (const auto &list : {e.command.terrainBefore, e.command.terrainAfter})
                    if (list)
                        for (const auto &instance : *list)
                            for (const auto &image : {instance.image, instance.mask})
                                if (image && images.insert(image.get()).second)
                                    b += image->values.size() * 2 +
                                         image->bounds.size() * sizeof(wb::HeightmapData::Bounds);
            }
            return b;
        };
        while (m_history.size() > 64 || (m_history.size() > 1 && bytes() > 32 * 1024 * 1024))
            m_history.erase(m_history.begin());
        m_cursor = int(m_history.size());
        m_status = "笔划已应用 · Ctrl+Z 撤销";
    }
    if (notify)
        emit changed();
}
void Editor::undo() {
    if (assetBusy())
        return;
    endCreationTransform();
    endEnvironmentEdit();
    endTerrainEdit();
    endStroke();
    if (!canUndo())
        return;
    const auto &e = m_history[--m_cursor];
    m_world.apply(e.command, false);
    m_state = e.before;
    ++m_revision;
    if (!dirtyCommand(e.command, false)) {
        ++m_cursor;
        m_state = e.after;
        return;
    }
    m_status = "已撤销一个笔划";
    emit changed();
}
void Editor::redo() {
    if (assetBusy())
        return;
    endCreationTransform();
    endEnvironmentEdit();
    endTerrainEdit();
    endStroke();
    if (!canRedo())
        return;
    const auto &e = m_history[m_cursor++];
    m_world.apply(e.command, true);
    m_state = e.after;
    ++m_revision;
    if (!dirtyCommand(e.command, true)) {
        --m_cursor;
        m_state = e.before;
        return;
    }
    m_status = "已重做一个笔划";
    emit changed();
}
void Editor::resetHistory() {
    m_thumbnailCache.clear();
    m_regionControl.reset();
    m_regionId.clear();
    m_regionState = "idle";
    m_regionError.clear();
    m_regionChanged = m_regionClipped = 0;
    m_regionRevision = m_regionResultRevision = 0;
    m_selectedNode = m_world.heights.activeLayer;
    m_terrainStart.reset();
    m_environmentStart.reset();
    m_stroke.reset();
    m_strokeChanged = false;
    m_history.clear();
    m_cursor = 0;
    m_state = m_nextState++;
    m_savedState = m_state;
    ++m_epoch;
    ++m_revision;
    m_resourceRevision = m_savedResourceRevision = 0;
}
void Editor::newWorld() {
    createWorld(128, 2026, 24, 12, 12, false);
}
bool Editor::createWorld(int width, int seed, int baseHeight, int variation, int seaLevel, bool flat,
                         int depth) {
    if (exportBusy() || assetBusy())
        return false;
    wb::World next;
    try {
        next = wb::World::create(wb::WorldOptions{width, seed, seaLevel, baseHeight, variation, flat, depth});
    } catch (const std::exception &e) {
        emit failure(QString::fromUtf8(e.what()));
        return false;
    }
    endStroke();
    m_world = std::move(next);
    m_previewX = m_world.originX + m_world.width / 2;
    m_previewZ = m_world.originZ + m_world.depth / 2;
    m_selection = {};
    m_selectionOnly = false;
    m_material = wb::Stone;
    m_path.clear();
    resetHistory();
    m_scene = std::make_shared<wb::MeshScene>();
    dirtyAll();
    m_status =
        QString("新建 %1 × %2 %3地形").arg(m_world.width).arg(m_world.depth).arg(flat ? "平坦" : "丘陵");
    emit catalogChanged();
    emit sceneChanged();
    emit worldReset();
    emit changed();
    if (!defaultResourceSources().isEmpty())
        loadResources(defaultResourceSources(), true);
    return true;
}
bool Editor::save(const QUrl &url) {
    if (assetBusy())
        return false;
    endCreationTransform();
    endEnvironmentEdit();
    endTerrainEdit();
    endStroke();
    QString path = url.isEmpty() ? m_path : url.toLocalFile();
    if (path.isEmpty())
        return false;
    if (!path.endsWith(".wbench", Qt::CaseInsensitive))
        path += ".wbench";
    QString error;
    if (auto *viewport = qobject_cast<Viewport *>(m_cameraController.data()))
        m_world.savedCamera = viewport->cameraState();
    if (!wb::saveProject(path, m_world, error)) {
        emit failure(error);
        return false;
    }
    m_path = path;
    m_savedState = m_state;
    m_savedResourceRevision = m_resourceRevision;
    m_status = "已保存：" + path;
    emit changed();
    return true;
}
bool Editor::open(const QUrl &url) {
    if (exportBusy() || assetBusy())
        return false;
    endCreationTransform();
    endEnvironmentEdit();
    endTerrainEdit();
    endStroke();
    wb::World next;
    QString error;
    if (!wb::loadProject(url.toLocalFile(), next, error)) {
        emit failure(error);
        return false;
    }
    if (!prepareLibraryNames(next.creation, next.heights.instances, true, error)) {
        emit failure(error);
        return false;
    }
    m_world = std::move(next);
    m_previewX = m_world.originX + m_world.width / 2;
    m_previewZ = m_world.originZ + m_world.depth / 2;
    m_selection = {};
    m_selectionOnly = false;
    m_material = wb::Stone;
    emit catalogChanged();
    m_path = url.toLocalFile();
    resetHistory();
    m_scene = std::make_shared<wb::MeshScene>();
    dirtyAll();
    m_status = "已打开工程";
    emit sceneChanged();
    emit worldReset();
    emit changed();
    m_resourceWarning.clear();
    const auto requested = m_world.resourcePaths.isEmpty() ? defaultResourceSources() : m_world.resourcePaths;
    QStringList sources;
    QStringList missing;
    for (const auto &source : requested) {
        if (QFileInfo(source).isFile())
            sources << source;
        else
            missing << source;
    }
    // Projects keep their original resource paths, but a moved or uninstalled
    // JAR must not prevent the world itself from opening. Use the remembered
    // default when it is available, then leave a diagnostic in the status bar.
    if (sources.isEmpty() && !m_world.resourcePaths.isEmpty()) {
        QStringList defaults;
        for (const auto &source : defaultResourceSources())
            if (QFileInfo(source).isFile())
                defaults << source;
        if (!defaults.isEmpty()) {
            sources = defaults;
            missing << "工程记录的资源来源";
        }
    }
    if (!missing.isEmpty())
        m_resourceWarning = QString("跳过 %1 个不可用资源来源，工程仍已打开").arg(missing.size());
    if (!sources.isEmpty())
        loadResources(sources, true);
    else if (!missing.isEmpty()) {
        m_status = "已打开工程 · " + m_resourceWarning + "；使用内置材质";
        reportAssetProgress(100, m_resourceWarning);
        emit changed();
    }
    return true;
}
void Editor::invalidateTerrainBounds(QRectF bounds) {
    bounds.adjust(-1, -1, 1, 1);
    m_farRegions.push_back(bounds.toAlignedRect());
    for (int id : desiredChunks()) {
        QRectF chunk(m_world.originX + id % m_world.chunksX() * 16,
                     m_world.originZ + id / m_world.chunksX() * 16, 16, 16);
        if (chunk.intersects(bounds))
            m_dirty.insert(id);
    }
}
bool Editor::dirtyCommand(const wb::Command &command, bool forward) {
    if (command.creationAfter || (!m_world.creation.volumes.empty() || !m_world.creation.presets.empty() ||
                                  !m_world.creation.roadEdges.empty()) &&
                                     !command.environmentAfter) {
        QString error;
        auto candidate = m_world;
        m_world.apply(command, !forward);
        const auto committedRevision = m_revision;
        --m_revision;
        const bool isolated = command.creationBefore && !command.terrainAfter &&
                              independentMove(m_world, m_world.creation, candidate.creation);
        const auto previousGenerated = m_world.generated;
        if (!(isolated ? candidate.rebuildCreation(error, {}, true, true)
                       : prepareCreation(candidate, error, false))) {
            dirtyAll();
            emit failure(error + "；本次修改已回滚");
            return false;
        }
        m_world = std::move(candidate);
        m_revision = committedRevision;
        if (isolated)
            dirtyCreation(previousGenerated);
        else
            dirtyAll();
        return true;
    }
    if (command.deltaAfter) {
        // Avoid scanning a million deltas on the UI thread at commit/undo.
        // Only resident exact chunks and intersecting far tiles are invalidated.
        const auto area = command.heightArea.adjusted(-1, -1, 1, 1);
        m_farDirty = true;
        m_farRegions.push_back(area);
        for (int id : desiredChunks()) {
            const QRect chunk(m_world.originX + id % m_world.chunksX() * 16,
                              m_world.originZ + id / m_world.chunksX() * 16, 16, 16);
            if (chunk.intersects(area))
                m_dirty.insert(id);
        }
        m_meshTimer.start();
        return true;
    }
    if (command.environmentAfter)
        return true;
    m_farDirty = true;
    if (command.themeAfter)
        m_farAllDirty = true;
    else if (!command.terrainAfter && !command.layersAfter)
        for (int id : wb::affectedChunks(m_world, command))
            m_farRegions.emplace_back(m_world.originX + id % m_world.chunksX() * 16,
                                      m_world.originZ + id / m_world.chunksX() * 16, 16, 16);
    if (!m_meshTimer.isActive())
        m_meshTimer.start();
    if (command.themeAfter) {
        dirtyAll();
        return true;
    }
    if (command.terrainAfter || command.layersAfter) {
        if (selectedTerrainIndex() < 0 && m_world.heights.layerIndex(m_selectedNode) < 0 &&
            !m_selectedNode.startsWith("environment"))
            m_selectedNode = "terrain-base-1";
        if (command.terrainBefore && command.terrainAfter) {
            // Index changes also matter because replace/min/max composition is ordered.
            const auto &a = *command.terrainBefore, &b = *command.terrainAfter;
            for (size_t n = 0; n < std::max(a.size(), b.size()); ++n) {
                if (n < a.size() && n < b.size() && a[n] == b[n])
                    continue;
                if (n < a.size() && a[n].enabled)
                    invalidateTerrainBounds(a[n].bounds());
                if (n < b.size() && b[n].enabled)
                    invalidateTerrainBounds(b[n].bounds());
            }
        }
        if (command.layersBefore && command.layersAfter) {
            for (int direction : {0, 1}) {
                const auto &from = direction ? *command.layersAfter : *command.layersBefore;
                const auto &to = direction ? *command.layersBefore : *command.layersAfter;
                for (const auto &l : from) {
                    const auto other =
                        std::find_if(to.begin(), to.end(), [&](const auto &v) { return v.id == l.id; });
                    if (!l.enabled ||
                        (other != to.end() && other->enabled == l.enabled && *other->deltas == *l.deltas))
                        continue;
                    QRect area;
                    for (auto [index, value] : *l.deltas) {
                        const QRect column(index % m_world.width + m_world.originX,
                                           index / m_world.width + m_world.originZ, 1, 1);
                        area = area.isNull() ? column : area.united(column);
                    }
                    if (!area.isEmpty())
                        invalidateTerrainBounds(area);
                }
            }
        }
        if (!m_dirty.empty() && !m_meshTimer.isActive())
            m_meshTimer.start();
        // commitTerrain publishes once after updating the status. Avoid two
        // complete QML detail/gizmo refreshes for every active drag sample.
        if (!m_terrainStart)
            emit changed();
        return true;
    }
    auto chunks = wb::affectedChunks(m_world, command);
    m_dirty.insert(chunks.begin(), chunks.end());
    if (!m_dirty.empty())
        m_meshTimer.start();
    return true;
}
QString Editor::meshUpdateSummary() const {
    return QString("构网 %1 ms（远景 %2 ms）· 更新 %3 精确 / %4 地块 · 网格 %5 MiB · 颜色 %6 MiB")
        .arg(m_scene->buildMilliseconds)
        .arg(m_scene->farMilliseconds)
        .arg(m_scene->updatedChunks)
        .arg(m_scene->updatedFarTiles)
        .arg(m_scene->bufferBytes / 1048576., 0, 'f', 1)
        .arg(m_scene->colorBytes / 1048576., 0, 'f', 1);
}
QString Editor::terrainUpdateStatus() const {
    if (m_creationStart)
        return "创作变换预览 · 松手后校验地基 / 地表规则与散布";
    if (!m_surfaceFailure.isEmpty())
        return "地形更新失败 · " + m_surfaceFailure;
    if (m_interactiveMesh)
        return m_scene && m_scene->provisional ? "交互预览网格 · 完整快照实时更新，松开后恢复高精度地形"
                                               : "交互编辑中 · 等待首个完整预览快照";
    if (m_scene && m_scene->provisional)
        return "地形网格待收敛";
    if (meshBusy())
        return QString("后台更新地形 %1 / %2 · 当前画面待收敛")
            .arg(m_surfaceCompleted ? m_surfaceCompleted->load() : 0)
            .arg(m_surfaceTotal);
    return QString("地表参考面误差上界 %1 格 · 近处显示真实方块 / 模型")
        .arg(m_scene->surfaceError, 0, 'f', 2);
}
std::set<int> Editor::desiredChunks() const {
    std::set<int> result;
    if (m_inspectionOnly || !m_exactPreviewEnabled)
        return result;
    const int nx = m_world.chunksX(), nz = m_world.chunksZ();
    const bool local = m_world.width > 256 || m_world.depth > 256;
    const int cx = std::clamp((m_previewX - m_world.originX) / 16, 0, nx - 1),
              cz = std::clamp((m_previewZ - m_world.originZ) / 16, 0, nz - 1);
    const int radius = std::clamp(m_exactRadiusChunks, 1, 12);
    for (int z = local ? std::max(0, cz - radius) : 0; z < (local ? std::min(nz, cz + radius + 1) : nz); ++z)
        for (int x = local ? std::max(0, cx - radius) : 0; x < (local ? std::min(nx, cx + radius + 1) : nx);
             ++x)
            result.insert(z * nx + x);
    return result;
}
bool Editor::previewContains(int x, int z) const {
    if (!m_world.contains(x, 0, z))
        return false;
    const int id = (z - m_world.originZ) / 16 * m_world.chunksX() + (x - m_world.originX) / 16;
    return std::find(m_scene->chunkIds.begin(), m_scene->chunkIds.end(), id) != m_scene->chunkIds.end();
}
void Editor::setPreviewCenter(int x, int z) {
    const auto before = desiredChunks();
    m_previewX = std::clamp(x, m_world.originX, m_world.originX + m_world.width - 1);
    m_previewZ = std::clamp(z, m_world.originZ, m_world.originZ + m_world.depth - 1);
    auto after = desiredChunks();
    if (before == after)
        return;
    // Retain the whole surface cache; only moved cutout edges and missing exact chunks rebuild.
    m_farDirty = true;
    m_meshTimer.start();
    emit changed();
}
void Editor::dirtyAll() {
    if (m_meshCancel)
        m_meshCancel->store(true);
    m_farDirty = m_farAllDirty = true;
    m_dirty = desiredChunks();
    if (!m_meshTimer.isActive())
        m_meshTimer.start();
    emit changed();
}
namespace {
uint64_t placementMeshBytes(const wb::ChunkMesh &m) {
    return (m.opaque.size() + m.transparent.size() + m.water.size()) * sizeof(wb::Vertex) +
           m.opaqueIndices.size() * sizeof(uint32_t) + m.surfaceColorBytes() + m.clipCacheBytes();
}
// Rigid placements preserve terrain heights. Refresh only contact geometry, cutouts and color texels.
bool refreshPlacementScene(wb::MeshScene &scene, const wb::World &w, const std::set<int> &chunks,
                           const std::set<int> &columns, uint64_t serial) {
    if (!scene.surfaceMaterials || columns.size() > 4096 || chunks.size() > 32)
        return false;
    const uint64_t budget = 512ull * 1024 * 1024;
    auto replace = [&](wb::MeshPtr &slot, wb::MeshPtr next, bool surface) {
        auto count = [&](const wb::ChunkMesh &m) {
            return (surface ? m.opaqueIndices.size() : m.opaque.size()) + m.transparent.size() +
                   m.water.size();
        };
        if (slot) {
            scene.bufferBytes -= placementMeshBytes(*slot) - slot->surfaceColorBytes();
            scene.vertices -= count(*slot);
            scene.waterVertices -= slot->water.size();
            scene.colorBytes -= slot->surfaceColorBytes();
        }
        slot = next;
        if (next) {
            scene.bufferBytes += placementMeshBytes(*next) - next->surfaceColorBytes();
            scene.vertices += count(*next);
            scene.waterVertices += next->water.size();
            scene.colorBytes += next->surfaceColorBytes();
        }
    };
    for (auto &tile : scene.farTiles) {
        const auto area = tile.area.adjusted(-1, -1, 2, 2);
        if (std::none_of(columns.begin(), columns.end(), [&](int c) {
                return area.contains(w.originX + c % w.width, w.originZ + c / w.width);
            }))
            continue;
        replace(tile.mesh, wb::refreshPlacementSurface(w, tile, *scene.surfaceMaterials, columns, serial),
                true);
        ++scene.updatedFarTiles;
    }
    for (int id : chunks) {
        int cx = id % w.chunksX() + w.originX / 16, cz = id / w.chunksX() + w.originZ / 16;
        bool exact = scene.exactArea.contains(cx * 16, cz * 16);
        const int slotId = exact ? id : -id - 1;
        auto it = std::find(scene.chunkIds.begin(), scene.chunkIds.end(), slotId);
        const bool required = exact;
        if (!required && it == scene.chunkIds.end())
            continue;
        const auto mesh =
            required ? wb::buildChunkMesh(w, cx, cz, serial, !exact, false, true) : wb::MeshPtr{};
        if (it != scene.chunkIds.end()) {
            const size_t index = it - scene.chunkIds.begin();
            replace(scene.chunks[index], mesh, false);
            if (!mesh) {
                scene.chunks.erase(scene.chunks.begin() + index);
                scene.chunkIds.erase(it);
            }
        } else {
            scene.chunkIds.push_back(slotId);
            scene.chunks.emplace_back();
            replace(scene.chunks.back(), mesh, false);
        }
        if (exact)
            ++scene.updatedChunks;
    }
    return scene.bufferBytes + scene.colorBytes <= budget;
}
} // namespace
void Editor::dirtyCreation(const std::shared_ptr<const wb::GeneratedCreation> &before) {
    std::set<int> changed, columns;
    bool waterChanged = false;
    auto add = [&](uint64_t key) {
        int x, y, z;
        m_world.decode(key, x, y, z);
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                const int cx = (x - m_world.originX) / 16 + dx, cz = (z - m_world.originZ) / 16 + dz;
                if (cx >= 0 && cz >= 0 && cx < m_world.chunksX() && cz < m_world.chunksZ())
                    changed.insert(cz * m_world.chunksX() + cx);
            }
    };
    std::unordered_map<int, int> surfaceHeights;
    const auto &after = m_world.generated;
    const bool local = before && after && before->procedural && before->procedural == after->procedural;
    std::set<int> pages;
    if (local) {
        pages = before->placementPages;
        pages.insert(after->placementPages.begin(), after->placementPages.end());
    }
    auto compare = [&](const auto &a, const auto &b) {
        if (!a)
            return;
        auto checkCell = [&](uint64_t key, wb::Block value) {
            if (b && b->cells.contains(key) && b->cells.at(key) == value &&
                a->objectCells.contains(key) == b->objectCells.contains(key) &&
                a->caveAir.contains(key) == b->caveAir.contains(key))
                return;
            if (a->objectCells.contains(key)) {
                int x, y, z;
                m_world.decode(key, x, y, z);
                // Ordinary above-ground contact faces are retained by the terrain mesh.
                auto [top, added] = surfaceHeights.try_emplace(m_world.column(x, z));
                if (added)
                    top->second = std::max(m_world.height(x, z), m_world.seaLevel);
                if (y > top->second && y != m_world.seaLevel + 1)
                    return;
            }
            if (local) {
                int x, y, z;
                m_world.decode(key, x, y, z);
                const int c = m_world.column(x, z);
                const int h = m_world.heights[c];
                const auto road = after->heights.find(c);
                if (y == h || (road != after->heights.end() && y == road->second))
                    columns.insert(c);
                if (y == m_world.seaLevel || y == m_world.seaLevel + 1) {
                    auto wet = [&](const wb::GeneratedCreation *layer) {
                        const auto above = m_world.blockAtHeight(x, m_world.seaLevel + 1, z, h, layer);
                        return m_world.blockAtHeight(x, m_world.seaLevel, z, h, layer) == wb::Water &&
                               above != wb::Water && (above == wb::Air || m_world.models[above]->translucent);
                    };
                    waterChanged |= wet(after.get()) != wet(before.get());
                }
                for (int dz = -1; dz <= 1; ++dz)
                    for (int dx = -1; dx <= 1; ++dx) {
                        int nx = x + dx, nz = z + dz;
                        if (m_world.contains(nx, y, nz))
                            changed.insert((nz - m_world.originZ) / 16 * m_world.chunksX() +
                                           (nx - m_world.originX) / 16);
                    }
                return;
            }
            add(key);
        };
        if (local) {
            for (int id : pages)
                if (const auto *page = a->cells.page(id))
                    for (const auto &[key, value] : *page)
                        checkCell(key, value);
            return;
        }
        for (const auto &[key, value] : a->cells)
            checkCell(key, value);
        for (auto [column, h] : a->heights)
            if (!b || !b->heights.contains(column) || b->heights.at(column) != h)
                add(m_world.key(m_world.originX + column % m_world.width, 0,
                                m_world.originZ + column / m_world.width));
    };
    compare(before, m_world.generated);
    compare(m_world.generated, before);
    if (local)
        std::set_symmetric_difference(before->geometryChunks.begin(), before->geometryChunks.end(),
                                      after->geometryChunks.begin(), after->geometryChunks.end(),
                                      std::inserter(changed, changed.end()));
    // Preserve GPU buffers when a rigid move retains the same exposed local voxels.
    QElapsedTimer publishTimer;
    publishTimer.start();
    auto publishLocal = [&]() -> bool {
        if (local && m_scene && m_scene->generated == before && !m_scene->provisional && !meshBusy()) {
            auto oldState = m_scene->creation;
            int index = -1;
            for (size_t i = 0; i < oldState.presets.size(); ++i)
                if (oldState.presets[i].position != m_world.creation.presets[i].position) {
                    index = int(i);
                    break;
                }
            if (index >= 0) {
                const auto &moved = m_world.creation.presets[index];
                const auto delta = moved.position - oldState.presets[index].position;
                bool rebuildMoved = false;
                auto compatible = [&](const auto &a, const auto &b, QVector3D offset) {
                    for (int id : pages)
                        if (const auto *page = a->objectCells.page(id))
                            for (const auto &[key, owner] : *page) {
                                if (m_world.explicitOverride(key))
                                    continue;
                                int x, y, z;
                                m_world.decode(key, x, y, z);
                                const auto &object = a->objects[owner];
                                if (object.id == moved.id) {
                                    x += qRound(offset.x());
                                    y += qRound(offset.y());
                                    z += qRound(offset.z());
                                }
                                if (!m_world.contains(x, y, z))
                                    return false;
                                const auto dest = m_world.key(x, y, z);
                                const auto *nextOwner = b->objectCells.get(dest);
                                if (!nextOwner || m_world.explicitOverride(dest) ||
                                    b->objects[*nextOwner].id != object.id ||
                                    b->cells.at(dest) != a->cells.at(key)) {
                                    if (object.id != moved.id)
                                        return false;
                                    rebuildMoved = true;
                                }
                            }
                    return true;
                };
                if (delta == QVector3D(qRound(delta.x()), qRound(delta.y()), qRound(delta.z())) &&
                    compatible(before, after, delta) && compatible(after, before, -delta)) {
                    auto scene = std::make_shared<wb::MeshScene>(*m_scene);
                    if (rebuildMoved) {
                        auto object = std::find_if(scene->objects.begin(), scene->objects.end(),
                                                   [&](const auto &o) { return o.id == moved.id; });
                        // Keep cluster member indices stable. New or fully occluded objects use the regular
                        // scene builder.
                        if (object == scene->objects.end() || object->shape->size() > 8192)
                            return false;
                        auto replacements =
                            wb::buildCreationMeshes(m_world, m_meshSerial + 1, nullptr, nullptr, moved.id);
                        if (replacements.size() != 1)
                            return false;
                        auto account = [&](const auto &o, int sign) {
                            scene->vertices += sign * (o.mesh->opaque.size() + o.mesh->transparent.size() +
                                                       o.mesh->water.size());
                            scene->waterVertices += sign * o.mesh->water.size();
                            scene->bufferBytes += sign * placementMeshBytes(*o.mesh);
                            for (const auto &lod : o.lods)
                                if (lod != o.mesh)
                                    scene->bufferBytes += sign * placementMeshBytes(*lod);
                        };
                        account(*object, -1);
                        *object = std::move(replacements.front());
                        account(*object, 1);
                        uint64_t objectBytes = 0;
                        for (const auto &obj : scene->objects) {
                            objectBytes += placementMeshBytes(*obj.mesh);
                            for (const auto &lod : obj.lods)
                                if (lod != obj.mesh)
                                    objectBytes += placementMeshBytes(*lod);
                        }
                        for (const auto &cluster : scene->clusters)
                            objectBytes += placementMeshBytes(*cluster.mesh);
                        if (objectBytes > 256ull * 1024 * 1024)
                            return false;
                    }
                    scene->generated = after;
                    scene->creation = m_world.creation;
                    scene->sourceRevision = m_revision;
                    scene->updatedChunks = scene->updatedFarTiles = 0;
                    scene->updatedSamples = 0;
                    const auto serial = ++m_meshSerial;
                    if (!waterChanged && refreshPlacementScene(*scene, m_world, changed, columns, serial)) {
                        scene->revision = serial;
                        scene->buildMilliseconds = publishTimer.elapsed();
                        scene->farMilliseconds = 0;
                        m_scene = std::move(scene);
                        emit sceneChanged();
                        return true;
                    }
                }
            }
        }
        return false;
    };
    try {
        if (publishLocal())
            return;
    } catch (const std::exception &) {
        // Retry through the budgeted asynchronous scene builder, which keeps the previous display on error.
    }
    for (int id : changed) {
        m_farRegions.emplace_back(m_world.originX + id % m_world.chunksX() * 16,
                                  m_world.originZ + id / m_world.chunksX() * 16, 16, 16);
        m_dirty.insert(id);
    }
    m_farDirty = true;
    m_rejectStaleMeshes = true;
    m_meshTimer.start();
}
void Editor::startMesh() {
    if (m_meshActive || (m_dirty.empty() && !m_farDirty))
        return;
    // During transforms the continuous surface covers the whole viewport.
    // Avoid rebuilding expensive block/model meshes on every pointer update.
    const auto wanted = m_interactiveMesh ? std::set<int>{} : desiredChunks();
    auto dirty = std::move(m_dirty);
    m_dirty.clear();
    const bool allFar = m_farAllDirty;
    auto regions = std::move(m_farRegions);
    m_farRegions.clear();
    m_farDirty = m_farAllDirty = false;
    auto snapshot = m_world;
    if (m_creationStart)
        snapshot.creation = *m_creationStart;
    auto old = m_scene;
    const auto serial = ++m_meshSerial;
    const bool interactive = m_interactiveMesh;
    const bool inspection = m_inspectionOnly;
    m_jobEpoch = m_epoch;
    m_jobGesture = m_gesture;
    m_jobDirty = dirty;
    m_jobFarRegions = regions;
    m_jobAllFar = allFar;
    m_meshActive = true;
    m_meshCancel = std::make_shared<std::atomic_bool>(false);
    m_surfaceFailure.clear();
    m_surfaceCompleted = std::make_shared<std::atomic_int>(0);
    // Larger maps use wider patches to bound shared-edge geometry, without
    // skipping any source heights or relaxing the reference-surface error.
    const int patchSize = std::max(m_world.width, m_world.depth) > 4096 ? 256 : 128;
    m_surfaceTotal = int(wb::planSurfaceTiles(m_world, patchSize).size());
    m_surfaceProgressTimer.start();
    const auto completed = m_surfaceCompleted;
    const auto sourceRevision = m_revision;
    const auto cancel = m_meshCancel;
    auto *surfaceWorkers = &m_surfaceWorkers;
    m_meshWatcher.setFuture(QtConcurrent::run([snapshot = std::move(snapshot), old, serial, sourceRevision,
                                               allFar, wanted, dirty = std::move(dirty),
                                               regions = std::move(regions), cancel, completed, patchSize,
                                               interactive, inspection, surfaceWorkers]() {
        QElapsedTimer timer;
        timer.start();
        auto next = std::make_shared<wb::MeshScene>();
        next->revision = serial;
        next->sourceRevision = sourceRevision;
        next->provisional = interactive;
        next->assets = snapshot.assets;
        next->generated = snapshot.generated;
        next->creation = snapshot.creation;
        next->worldArea = QRect(snapshot.originX, snapshot.originZ, snapshot.width, snapshot.depth);
        next->seaSurface = float(snapshot.seaLevel + 1);
        try {
            if (inspection && !snapshot.heights.instances.empty()) {
                auto mesh = wb::buildTerrainAssetPreview(snapshot.heights.instances.front());
                next->chunks = {mesh};
                next->chunkIds = {0};
                next->vertices = mesh->opaque.size();
                next->bufferBytes = next->vertices * sizeof(wb::Vertex) + mesh->opaqueIndices.size() * 4;
                next->buildMilliseconds = timer.elapsed();
                return next;
            }
            std::map<int, wb::MeshPtr> exactCache;
            if (old->assets == snapshot.assets)
                for (size_t i = 0; i < old->chunkIds.size(); ++i)
                    exactCache[old->chunkIds[i]] = old->chunks[i];
            std::vector<int> exactJobs;
            for (int id : wanted) {
                const QRect chunk(snapshot.originX + id % snapshot.chunksX() * 16,
                                  snapshot.originZ + id / snapshot.chunksX() * 16, 16, 16);
                next->exactArea = next->exactArea.isEmpty() ? chunk : next->exactArea.united(chunk);
                if (allFar || dirty.contains(id) || !exactCache.contains(id))
                    exactJobs.push_back(id);
            }
            const auto exactMeshes = QtConcurrent::blockingMapped<std::vector<wb::MeshPtr>>(
                surfaceWorkers, exactJobs, [&](int id) {
                    if (cancel->load())
                        return wb::MeshPtr{};
                    return wb::buildChunkMesh(snapshot, snapshot.originX / 16 + id % snapshot.chunksX(),
                                              snapshot.originZ / 16 + id / snapshot.chunksX(), serial, false,
                                              false, !inspection);
                });
            if (cancel->load())
                return std::shared_ptr<wb::MeshScene>{};
            for (size_t i = 0; i < exactJobs.size(); ++i)
                exactCache[exactJobs[i]] = exactMeshes[i];
            for (int id : wanted) {
                next->chunkIds.push_back(id);
                next->chunks.push_back(exactCache.at(id));
            }
            if (snapshot.generated && !interactive && inspection) {
                std::set<int> patchIds;
                for (const auto &[id, cells] : snapshot.generated->cells.pages())
                    patchIds.insert(id);
                for (int id : patchIds)
                    if (!wanted.contains(id)) {
                        const int cacheId = -id - 1;
                        auto found = exactCache.find(cacheId);
                        auto mesh = (!allFar && !dirty.contains(id) && found != exactCache.end() &&
                                     (old->generated == snapshot.generated ||
                                      (old->generated && old->generated->procedural &&
                                       old->generated->procedural == snapshot.generated->procedural)))
                                        ? found->second
                                        : wb::buildChunkMesh(snapshot,
                                                             snapshot.originX / 16 + id % snapshot.chunksX(),
                                                             snapshot.originZ / 16 + id / snapshot.chunksX(),
                                                             serial, true, inspection, !inspection);
                        next->chunkIds.push_back(cacheId);
                        next->chunks.push_back(mesh);
                    }
            }
            next->updatedChunks = int(exactJobs.size());
            if (inspection) {
                for (auto &mesh : next->chunks) {
                    next->vertices += mesh->opaque.size() + mesh->transparent.size();
                    next->bufferBytes +=
                        (mesh->opaque.size() + mesh->transparent.size()) * sizeof(wb::Vertex);
                }
                next->buildMilliseconds = timer.elapsed();
                return next;
            }
            if (old->generated == snapshot.generated && old->assets == snapshot.assets &&
                (interactive || (!allFar && old->sourceRevision == sourceRevision))) {
                next->objects = old->objects;
                next->clusters = old->clusters;
                next->editedBounds = old->editedBounds;
            } else {
                next->objects = wb::buildCreationMeshes(snapshot, serial, &next->clusters, old.get());
                for (auto [key, block] : snapshot.overrides) {
                    if (snapshot.isAir(block))
                        continue;
                    int x, y, z;
                    snapshot.decode(key, x, y, z);
                    auto &sections = next->editedBounds[{wb::floorDiv(x, 16), wb::floorDiv(z, 16)}];
                    QVector3D lo(x, y, z), hi(x + 1, y + 1, z + 1);
                    auto [it, added] = sections.try_emplace(wb::floorDiv(y, 16), lo, hi);
                    if (!added)
                        for (int axis = 0; axis < 3; ++axis) {
                            it->second.first[axis] = std::min(it->second.first[axis], lo[axis]);
                            it->second.second[axis] = std::max(it->second.second[axis], hi[axis]);
                        }
                }
            }
            QElapsedTimer surfaceTimer;
            surfaceTimer.start();
            next->farTiles = wb::planSurfaceTiles(snapshot, patchSize);
            using Key = std::pair<int, int>;
            std::map<Key, wb::MeshPtr> cache;
            if (!allFar && old->assets == snapshot.assets)
                for (const auto &tile : old->farTiles)
                    cache[{tile.area.x(), tile.area.y()}] = tile.mesh;
            std::vector<int> jobs;
            for (int i = 0; i < int(next->farTiles.size()); ++i) {
                auto &tile = next->farTiles[i];
                auto found = cache.find({tile.area.x(), tile.area.y()});
                const QRect cutout =
                    tile.area.adjusted(0, 0, 1, 1).intersects(next->exactArea) ? next->exactArea : QRect{};
                const bool changed = std::any_of(regions.begin(), regions.end(), [&](const QRect &r) {
                    return tile.area.adjusted(-2, -2, 3, 3).intersects(r);
                });
                if (found != cache.end() && !changed && found->second->surfaceCutout == cutout &&
                    (interactive || !found->second->provisional)) {
                    tile.mesh = found->second;
                    ++(*completed);
                } else
                    jobs.push_back(i);
            }
            next->surfaceMaterials =
                old->assets == snapshot.assets && old->surfaceMaterials &&
                        old->surfaceMaterials->colors.size() == snapshot.models.size()
                    ? old->surfaceMaterials
                    : std::make_shared<const wb::SurfaceMaterials>(wb::surfaceMaterials(snapshot));
            const auto &inputs = *next->surfaceMaterials;
            const int previewStride = jobs.size() >= 64 ? 16 : 8;
            std::atomic_uint64_t bytes{0};
            std::atomic_bool overBudget{false};
            constexpr uint64_t limit = 512ull * 1024 * 1024;
            auto cost = [](const wb::ChunkMesh &m) {
                return uint64_t(
                    (m.opaque.size() + m.transparent.size() + m.water.size()) * sizeof(wb::Vertex) +
                    m.opaqueIndices.size() * sizeof(uint32_t) + m.surfaceColorBytes() + m.clipCacheBytes());
            };
            for (const auto &c : next->clusters)
                bytes += cost(*c.mesh);
            for (const auto &m : next->chunks)
                bytes += cost(*m);
            for (const auto &object : next->objects) {
                bytes += cost(*object.mesh);
                for (const auto &lod : object.lods)
                    if (lod != object.mesh)
                        bytes += cost(*lod);
            }
            for (const auto &tile : next->farTiles)
                if (tile.mesh)
                    bytes += cost(*tile.mesh);
            const auto meshes =
                QtConcurrent::blockingMapped<std::vector<wb::MeshPtr>>(surfaceWorkers, jobs, [&](int i) {
                    if (cancel->load())
                        return wb::MeshPtr{};
                    const auto &tile = next->farTiles[i];
                    const QRect cutout = tile.area.adjusted(0, 0, 1, 1).intersects(next->exactArea)
                                             ? next->exactArea
                                             : QRect{};
                    auto mesh = interactive ? wb::buildInteractiveSurfaceMesh(snapshot, tile, inputs, serial,
                                                                              cancel.get(), previewStride)
                                            : wb::buildSurfaceMesh(snapshot, tile, inputs, serial, 1.f,
                                                                   cancel.get(), cutout);
                    if (mesh) {
                        if (bytes.fetch_add(cost(*mesh)) + cost(*mesh) > limit) {
                            overBudget = true;
                            cancel->store(true);
                            return wb::MeshPtr{};
                        }
                        ++(*completed);
                    }
                    return mesh;
                });
            if (overBudget.load() || bytes.load() > limit) {
                next->error = "地形网格与颜色页超过512 MiB工作预算；保留原画面，未降低地形精度";
                return next;
            }
            if (cancel->load())
                return std::shared_ptr<wb::MeshScene>{};
            for (size_t i = 0; i < jobs.size(); ++i) {
                next->farTiles[jobs[i]].mesh = meshes[i];
                next->updatedSamples += meshes[i]->sampleCount;
            }
            auto account = [&](const wb::ChunkMesh &m, bool surface) {
                next->vertices += (surface ? m.opaqueIndices.size() : m.opaque.size()) +
                                  m.transparent.size() + m.water.size();
                next->waterVertices += m.water.size();
                next->bufferBytes +=
                    (m.opaque.size() + m.transparent.size() + m.water.size()) * sizeof(wb::Vertex) +
                    m.opaqueIndices.size() * sizeof(uint32_t) + m.clipCacheBytes();
                next->colorBytes += m.surfaceColorBytes();
            };
            for (const auto &c : next->clusters)
                next->bufferBytes += cost(*c.mesh);
            for (const auto &mesh : next->chunks)
                account(*mesh, false);
            for (const auto &object : next->objects) {
                account(*object.mesh, false);
                for (const auto &lod : object.lods)
                    if (lod != object.mesh)
                        next->bufferBytes += cost(*lod);
            }
            for (const auto &tile : next->farTiles) {
                account(*tile.mesh, true);
                next->sampleCount += tile.mesh->sampleCount;
                next->surfaceError = std::max(next->surfaceError, tile.mesh->surfaceError);
            }
            next->updatedFarTiles = int(jobs.size());
            next->farMilliseconds = surfaceTimer.elapsed();
            next->buildMilliseconds = timer.elapsed();
            return next;
        } catch (const std::exception &e) {
            next->error = "地形构建失败：" + QString::fromUtf8(e.what());
            return next;
        } catch (...) {
            next->error = "地形构建失败，未替换原画面或修改工程";
            return next;
        }
    }));
    emit changed();
}
void Editor::exportWorld(const QUrl &parent, const QString &name, bool selectedArea) {
    if (exportBusy() || assetBusy())
        return;
    endCreationTransform();
    endEnvironmentEdit();
    endTerrainEdit();
    endStroke();
    if (selectedArea && !m_selection.isValid()) {
        emit failure("请先创建选区");
        return;
    }
    const QRect area = selectedArea ? m_selection : QRect{};
    const QString clean = name.trimmed();
    static const QRegularExpression valid("^[A-Za-z0-9_-]{1,64}$");
    if (!parent.isLocalFile() || !QDir(parent.toLocalFile()).exists() || !valid.match(clean).hasMatch()) {
        emit failure("请选择已存在的父目录；存档名称仅使用 1–64 位英文字母、数字、下划线或短横线。");
        return;
    }
    if (!m_world.resourceVersion.isEmpty() && m_world.resourceVersion != "1.20.1") {
        emit failure("当前基础资源版本为 " + m_world.resourceVersion +
                     "；本版仅允许 1.20.1 导出。请加载 1.20.1 客户端资源。");
        return;
    }
    for (const auto &[key, block] : m_world.overrides)
        if (block >= wb::BlockCount) {
            QString error;
            if (!m_world.assets || m_world.assets->version != "1.20.1" ||
                !m_world.assets->validate(m_world.palette[block], error)) {
                emit failure("动态方块导出需要对应的 1.20.1 JAR 目录：" + m_world.palette[block].name + " " +
                             error);
                return;
            }
        }
    m_cancelExport = false;
    m_exportProgress = 0;
    m_status = "正在烘焙 Minecraft 1.20.1 存档（导出开始时的快照）…";
    auto snapshot = m_world;
    const auto path = QDir(parent.toLocalFile()).filePath(clean);
    m_exportActive = true;
    m_exportWatcher.setFuture(QtConcurrent::run([this, snapshot = std::move(snapshot), path, area] {
        return wb::exportMinecraft(
            snapshot, path, &m_cancelExport,
            [this](int done, int total) {
                QMetaObject::invokeMethod(
                    this,
                    [this, done, total] {
                        m_exportProgress = done * 100 / total;
                        emit changed();
                    },
                    Qt::QueuedConnection);
            },
            area);
    }));
    emit changed();
}
void Editor::cancelExport() {
    m_cancelExport = true;
    m_status = "正在取消导出…";
    emit changed();
}
