#include "Editor.h"
#include "McpServer.h"
#include "Viewport.h"
#include <QBuffer>
#include <QClipboard>
#include <QGuiApplication>
#include <QJsonArray>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QtConcurrent>
#include <cmath>
#include <stdexcept>

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
    return {QVariantMap{{"id", "terrain-base-1"}, {"name", "基础地形层1"}, {"kind", "terrain"}}};
}
QVariantMap Editor::layerDetails() const {
    return {{"name", "基础地形层1"},
            {"size", m_world.size},
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
    QVariantList list;
    auto add = [&](const QString &name, const QString &title, std::shared_ptr<const wb::BlockModel> model,
                   int id) {
        if (!filter.isEmpty() && !name.contains(filter, Qt::CaseInsensitive) &&
            !title.contains(filter, Qt::CaseInsensitive))
            return;
        QString thumbnail;
        if (m_world.assets) {
            auto image = m_world.assets->thumbnails.value(model->thumbnail);
            if (!image.isNull()) {
                QByteArray png;
                QBuffer b(&png);
                b.open(QIODevice::WriteOnly);
                image.save(&b, "PNG");
                thumbnail = "data:image/png;base64," + QString::fromLatin1(png.toBase64());
            }
        }
        const auto &color = wb::info(id < wb::BlockCount ? id : wb::Stone);
        list << QVariantMap{{"name", name},
                            {"title", title},
                            {"thumbnail", thumbnail},
                            {"color", QColor::fromRgbF(color.r, color.g, color.b)},
                            {"fallback", model->fallback},
                            {"diagnostic", model->diagnostic}};
    };
    if (m_world.assets && !m_world.assets->catalog.isEmpty()) {
        for (auto i = m_world.assets->catalog.begin(); i != m_world.assets->catalog.end(); ++i) {
            if (!filter.isEmpty() && !i.key().contains(filter, Qt::CaseInsensitive) &&
                !i->title.contains(filter, Qt::CaseInsensitive))
                continue;
            add(i.key(), i->title, m_world.assets->model(i->defaults), wb::BlockCount);
        }
    } else
        for (int i = 0; i < wb::BlockCount; ++i)
            add(m_world.palette[i].name, QString::fromUtf8(wb::info(i).name), m_world.models[i], i);
    return list;
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
void Editor::loadResources(const QStringList &paths, bool restoring) {
    if (assetBusy() || exportBusy())
        return;
    endStroke();
    m_restoringAssets = restoring;
    if (paths.isEmpty()) {
        m_world.setAssets({});
        m_status = "已清除资源来源 · 使用内置调试材质";
        ++m_epoch;
        ++m_revision;
        ++m_resourceRevision;
        dirtyAll();
        emit catalogChanged();
        emit changed();
        return;
    }
    m_status = "正在解析资源（不执行 JAR）…";
    m_assetsLoading = true;
    m_assetWatcher.setFuture(QtConcurrent::run([paths] {
        AssetResult r;
        r.library = wb::AssetLibrary::load(paths, r.error);
        return r;
    }));
    emit changed();
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
    loadResources(paths);
}
void Editor::removeResource(int index) {
    auto paths = m_world.resourcePaths;
    if (index < 0 || index >= paths.size())
        return;
    if (index == 0)
        paths.clear();
    else
        paths.removeAt(index);
    loadResources(paths);
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
    return {{"revision", double(m_revision)},
            {"size", m_world.size},
            {"min", QJsonArray{m_world.originX, wb::World::MinY, m_world.originZ}},
            {"max", QJsonArray{m_world.originX + m_world.size - 1, wb::World::MaxY - 1,
                               m_world.originZ + m_world.size - 1}},
            {"protected_y", wb::World::MinY},
            {"modified", modified()},
            {"resource_version", m_world.resourceVersion},
            {"palette_states", int(m_world.palette.size())},
            {"override_cells", int(m_world.overrides.size())},
            {"busy", commandBusy()},
            {"mesh_busy", meshBusy()},
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
        std::array<int, 3> p{integer(a[0], m_world.originX, m_world.originX + m_world.size - 1),
                             integer(a[1], wb::World::MinY + (writable ? 1 : 0), wb::World::MaxY - 1),
                             integer(a[2], m_world.originZ, m_world.originZ + m_world.size - 1)};
        return p;
    };
    if (name == "world_info")
        return worldInfo();
    if (name == "catalog") {
        QString query = args["query"].toString();
        int limit = args.contains("limit") ? integer(args["limit"], 1, 200) : 50;
        QJsonArray blocks;
        int total = 0;
        auto append = [&](const wb::BlockState &s, const QString &title,
                          const QMap<QString, QStringList> &options) {
            if (!query.isEmpty() && !s.name.contains(query, Qt::CaseInsensitive) && !title.contains(query))
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
            v->applyCamera(args);
            return {{"camera", v->cameraText()}};
        }
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
    if (name == "undo") {
        undo();
        return worldInfo();
    }
    if (name == "redo") {
        redo();
        return worldInfo();
    }
    if (name == "sculpt") {
        auto p = coord(args["position"], true);
        int tool = integer(args["tool"], 0, 4), radius = integer(args["radius"], 1, 16),
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
        stroke.stamp(hit, tool, radius, strength, wb::Block(material), args["reverse"].toBool());
        auto command = stroke.finish("MCP 雕刻");
        if (!command.cells.empty() || !command.heights.empty()) {
            ++m_revision;
            pushCommand(std::move(command));
            dirtyAll();
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
        QSet<uint32_t> seen;
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
        command.cells.push_back({key, before, after});
    }
    const int count = int(command.cells.size());
    if (count) {
        m_world = std::move(next);
        ++m_revision;
        pushCommand(std::move(command));
        dirtyAll();
    }
    auto result = worldInfo();
    result["changed_cells"] = count;
    return result;
}
