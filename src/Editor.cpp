#include "Editor.h"
#include "McpServer.h"
#include "core/Project.h"
#include <QBuffer>
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QRegularExpression>
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
            emit changed();
            return;
        }
        m_world.setAssets(result.library);
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
    });
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
    m_tool = std::clamp(v, -1, 4);
    emit changed();
}
void Editor::setRadius(int v) {
    m_radius = std::clamp(v, 1, 16);
    emit changed();
}
void Editor::setStrength(int v) {
    m_strength = std::clamp(v, 1, 4);
    emit changed();
}
void Editor::setMaterial(int v) {
    m_material = std::clamp(v, 0, int(m_world.palette.size()) - 1);
    emit changed();
}
QString Editor::projectName() const {
    return m_path.isEmpty() ? "未命名工程" : QFileInfo(m_path).completeBaseName();
}
QString Editor::sceneStats() const {
    return QString("%1 × %1 方块   ·   %2 区块   ·   %3 万三角形")
        .arg(m_world.size)
        .arg(m_scene->chunks.size())
        .arg(m_scene->vertices / 30000.0, 0, 'f', 1);
}
void Editor::beginStroke() {
    if (!m_stroke) {
        m_stroke = std::make_unique<wb::Stroke>(m_world);
        m_strokeChanged = false;
    }
}
void Editor::stamp(const wb::Hit &hit, bool reverse) {
    if (!hit.valid || m_tool < 0 || assetBusy())
        return;
    beginStroke();
    if (!m_stroke->stamp(hit, m_tool, m_radius, m_strength, wb::Block(m_material), reverse))
        return;
    m_strokeChanged = true;
    ++m_revision;
    const int n = m_world.size / 16;
    for (int z = wb::floorDiv(hit.z - m_radius - 2 - m_world.originZ, 16);
         z <= wb::floorDiv(hit.z + m_radius + 2 - m_world.originZ, 16); ++z)
        for (int x = wb::floorDiv(hit.x - m_radius - 2 - m_world.originX, 16);
             x <= wb::floorDiv(hit.x + m_radius + 2 - m_world.originX, 16); ++x)
            if (x >= 0 && z >= 0 && x < n && z < n)
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
    endStroke();
    if (!canUndo())
        return;
    const auto &e = m_history[--m_cursor];
    m_world.apply(e.command, false);
    m_state = e.before;
    ++m_revision;
    dirtyAll();
    m_status = "已撤销一个笔划";
    emit changed();
}
void Editor::redo() {
    endStroke();
    if (!canRedo())
        return;
    const auto &e = m_history[m_cursor++];
    m_world.apply(e.command, true);
    m_state = e.after;
    ++m_revision;
    dirtyAll();
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
    if (exportBusy() || assetBusy())
        return;
    endStroke();
    m_world = wb::World::create();
    m_material = wb::Stone;
    emit catalogChanged();
    m_path.clear();
    resetHistory();
    m_scene = std::make_shared<wb::MeshScene>();
    dirtyAll();
    m_status = "新建 128 × 128 演示地形";
    emit sceneChanged();
    emit worldReset();
    emit changed();
}
bool Editor::save(const QUrl &url) {
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
    if (!m_world.resourcePaths.isEmpty())
        loadResources(m_world.resourcePaths, true);
    return true;
}
void Editor::dirtyAll() {
    const int n = m_world.size / 16;
    m_dirty.clear();
    for (int i = 0; i < n * n; ++i)
        m_dirty.insert(i);
    m_meshTimer.start();
    emit changed();
}
void Editor::startMesh() {
    if (m_meshActive || m_dirty.empty())
        return;
    auto dirty = std::move(m_dirty);
    m_dirty.clear();
    auto snapshot = m_world;
    auto old = m_scene;
    const auto serial = ++m_meshSerial;
    m_jobEpoch = m_epoch;
    m_meshActive = true;
    m_meshWatcher.setFuture(
        QtConcurrent::run([snapshot = std::move(snapshot), dirty = std::move(dirty), old, serial] {
            auto next = std::make_shared<wb::MeshScene>(*old);
            const int n = snapshot.size / 16;
            next->chunks.resize(n * n);
            next->revision = serial;
            next->assets = snapshot.assets;
            for (int i : dirty)
                next->chunks[i] = wb::buildChunkMesh(snapshot, snapshot.originX / 16 + i % n,
                                                     snapshot.originZ / 16 + i / n, serial);
            next->vertices = 0;
            for (const auto &c : next->chunks)
                if (c)
                    next->vertices += c->opaque.size() + c->transparent.size();
            return next;
        }));
    emit changed();
}
void Editor::exportWorld(const QUrl &parent, const QString &name) {
    if (exportBusy() || assetBusy())
        return;
    endStroke();
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
    m_exportWatcher.setFuture(QtConcurrent::run([this, snapshot = std::move(snapshot), path] {
        return wb::exportMinecraft(snapshot, path, &m_cancelExport, [this](int done, int total) {
            QMetaObject::invokeMethod(
                this,
                [this, done, total] {
                    m_exportProgress = done * 100 / total;
                    emit changed();
                },
                Qt::QueuedConnection);
        });
    }));
    emit changed();
}
void Editor::cancelExport() {
    m_cancelExport = true;
    m_status = "正在取消导出…";
    emit changed();
}
