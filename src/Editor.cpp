#include "Editor.h"
#include "McpServer.h"
#include "core/Project.h"
#include <QBuffer>
#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSettings>
#include <QtConcurrent>
#include <algorithm>

Editor::Editor(QObject *parent)
    : QObject(parent), m_world(wb::World::create()), m_scene(std::make_shared<wb::MeshScene>()) {
    m_meshTimer.setSingleShot(true);
    m_meshTimer.setInterval(25);
    connect(&m_meshTimer, &QTimer::timeout, this, &Editor::startMesh);
    connect(&m_meshWatcher, &QFutureWatcherBase::finished, this, [this] {
        m_meshActive = false;
        if (m_jobEpoch == m_epoch) {
            m_scene = m_meshWatcher.result();
            emit sceneChanged();
        }
        if (!m_dirty.empty())
            m_meshTimer.start();
        else if (m_status.startsWith("正在构建"))
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
Editor::~Editor() {
    m_cancelExport = true;
    m_assetWatcher.waitForFinished();
    m_meshWatcher.waitForFinished();
    m_exportWatcher.waitForFinished();
}
void Editor::setTool(int v) {
    endStroke();
    saveBrushProfile();
    m_tool = std::clamp(v, -1, 6);
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
    return QString("%1 × %2 方块 · 预览 %3 / %4 区块 · %5 万三角形")
        .arg(m_world.width)
        .arg(m_world.depth)
        .arg(m_scene->chunks.size())
        .arg(m_world.chunksX() * m_world.chunksZ())
        .arg(m_scene->vertices / 30000.0, 0, 'f', 1);
}
void Editor::beginStroke() {
    if (!m_stroke) {
        m_stroke = std::make_unique<wb::Stroke>(m_world);
        m_strokeChanged = false;
    }
}
void Editor::stamp(const wb::Hit &hit, bool reverse) {
    if (!hit.valid || m_tool < 0 || m_tool > 5 || assetBusy())
        return;
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
    pushCommand(std::move(c));
}
void Editor::pushCommand(wb::Command c) {
    if (!c.cells.empty() || !c.heights.empty()) {
        m_history.erase(m_history.begin() + m_cursor, m_history.end());
        const auto next = m_nextState++;
        m_history.push_back({std::move(c), m_state, next});
        m_state = next;
        // Bound history by both stroke count and delta payload size.
        auto bytes = [&] {
            size_t b = 0;
            for (const auto &e : m_history)
                b += e.command.cells.size() * sizeof(wb::CellChange) +
                     e.command.heights.size() * sizeof(wb::HeightChange);
            return b;
        };
        while (m_history.size() > 64 || (m_history.size() > 1 && bytes() > 32 * 1024 * 1024))
            m_history.erase(m_history.begin());
        m_cursor = int(m_history.size());
        m_status = "笔划已应用 · Ctrl+Z 撤销";
    }
    emit changed();
}
void Editor::undo() {
    if (assetBusy())
        return;
    endStroke();
    if (!canUndo())
        return;
    const auto &e = m_history[--m_cursor];
    m_world.apply(e.command, false);
    m_state = e.before;
    ++m_revision;
    dirtyCommand(e.command);
    m_status = "已撤销一个笔划";
    emit changed();
}
void Editor::redo() {
    if (assetBusy())
        return;
    endStroke();
    if (!canRedo())
        return;
    const auto &e = m_history[m_cursor++];
    m_world.apply(e.command, true);
    m_state = e.after;
    ++m_revision;
    dirtyCommand(e.command);
    m_status = "已重做一个笔划";
    emit changed();
}
void Editor::resetHistory() {
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
    endStroke();
    QString path = url.isEmpty() ? m_path : url.toLocalFile();
    if (path.isEmpty())
        return false;
    if (!path.endsWith(".wbench", Qt::CaseInsensitive))
        path += ".wbench";
    QString error;
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
    endStroke();
    wb::World next;
    QString error;
    if (!wb::loadProject(url.toLocalFile(), next, error)) {
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
    const auto sources = m_world.resourcePaths.isEmpty() ? defaultResourceSources() : m_world.resourcePaths;
    if (!sources.isEmpty())
        loadResources(sources, true);
    return true;
}
void Editor::dirtyCommand(const wb::Command &command) {
    auto chunks = wb::affectedChunks(m_world, command);
    m_dirty.insert(chunks.begin(), chunks.end());
    if (!m_dirty.empty())
        m_meshTimer.start();
}
QString Editor::meshUpdateSummary() const {
    return QString("最近更新 %1 / %2 区块 · %3 ms")
        .arg(m_scene->updatedChunks)
        .arg(m_scene->chunks.size())
        .arg(m_scene->buildMilliseconds);
}
std::set<int> Editor::desiredChunks() const {
    std::set<int> result;
    const int nx = m_world.chunksX(), nz = m_world.chunksZ();
    const bool local = m_world.width > 256 || m_world.depth > 256;
    const int cx = std::clamp((m_previewX - m_world.originX) / 16, 0, nx - 1),
              cz = std::clamp((m_previewZ - m_world.originZ) / 16, 0, nz - 1);
    for (int z = local ? std::max(0, cz - 4) : 0; z < (local ? std::min(nz, cz + 5) : nz); ++z)
        for (int x = local ? std::max(0, cx - 4) : 0; x < (local ? std::min(nx, cx + 5) : nx); ++x)
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
    ++m_epoch;
    m_dirty = std::move(after);
    m_meshTimer.start();
    emit changed();
}
void Editor::dirtyAll() {
    m_dirty = desiredChunks();
    m_meshTimer.start();
    emit changed();
}
void Editor::startMesh() {
    if (m_meshActive || m_dirty.empty())
        return;
    const auto wanted = desiredChunks();
    auto dirty = std::move(m_dirty);
    m_dirty.clear();
    auto snapshot = m_world;
    auto old = m_scene;
    const auto serial = ++m_meshSerial;
    m_jobEpoch = m_epoch;
    m_meshActive = true;
    m_meshWatcher.setFuture(
        QtConcurrent::run([snapshot = std::move(snapshot), dirty = std::move(dirty), old, serial, wanted] {
            QElapsedTimer elapsed;
            elapsed.start();
            auto next = std::make_shared<wb::MeshScene>();
            next->revision = serial;
            next->assets = snapshot.assets;
            std::map<int, wb::MeshPtr> cached;
            for (size_t i = 0; i < old->chunkIds.size(); ++i)
                cached[old->chunkIds[i]] = old->chunks[i];
            int rebuilt = 0;
            for (int id : wanted) {
                auto it = cached.find(id);
                auto mesh = it == cached.end() ? nullptr : it->second;
                if (!mesh || dirty.contains(id)) {
                    mesh = wb::buildChunkMesh(snapshot, snapshot.originX / 16 + id % snapshot.chunksX(),
                                              snapshot.originZ / 16 + id / snapshot.chunksX(), serial);
                    ++rebuilt;
                }
                next->chunks.push_back(mesh);
                next->chunkIds.push_back(id);
            }
            next->updatedChunks = rebuilt;
            next->buildMilliseconds = elapsed.elapsed();
            next->vertices = 0;
            for (const auto &c : next->chunks)
                if (c)
                    next->vertices += c->opaque.size() + c->transparent.size();
            return next;
        }));
    emit changed();
}
void Editor::exportWorld(const QUrl &parent, const QString &name, bool selectedArea) {
    if (exportBusy() || assetBusy())
        return;
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
