#include "NbtPayload.h"
#include "World.h"
#include "WorldCreation.h"
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>
namespace wb {
namespace {
struct NodeSpec {
    const char *type;
    const char *name;
    const char *ports;
    QJsonObject defaults;
};
const std::vector<NodeSpec> specs{
    {"constant", "常量", "", {{"value", 1.0}}},
    {"parameter", "暴露参数", "", {{"name", "density"}, {"default", 0.5}}},
    {"input", "子图输入", "", {}},
    {"subgraph", "子图", "value", {{"value", 1}, {"graph", QJsonObject{}}}},
    {"world_x", "世界 X", "", {}},
    {"world_z", "世界 Z", "", {}},
    {"local_x", "局部 X", "", {}},
    {"local_z", "局部 Z", "", {}},
    {"world_height", "最终高度", "", {}},
    {"base_height", "生成前高度", "", {}},
    {"slope", "坡度（度）", "", {}},
    {"normal_y", "法线 Y", "", {}},
    {"mask", "范围 / 贡献蒙版", "", {}},
    {"seed", "实例种子", "", {}},
    {"noise", "分形噪声", "", {{"scale", 32.0}, {"octaves", 3}, {"amplitude", 1.0}, {"offset", 0.0}}},
    {"paint_mask", "手绘圆盘蒙版", "", {{"radius", 8.0}, {"points", QJsonArray{}}}},
    {"add", "相加", "a,b", {{"a", 0}, {"b", 0}}},
    {"multiply", "相乘", "a,b", {{"a", 1}, {"b", 1}}},
    {"min", "较小", "a,b", {}},
    {"max", "较大", "a,b", {}},
    {"mix", "混合", "a,b,t", {{"a", 0}, {"b", 1}, {"t", 0.5}}},
    {"compare", "大于阈值", "value", {{"threshold", 0.5}}},
    {"range", "区间 / 过渡", "value", {{"min", 0}, {"max", 90}, {"feather", 5}}},
    {"remap", "重映射", "value", {{"inMin", 0}, {"inMax", 100}, {"outMin", 0}, {"outMax", 1}}},
    {"curve", "幂曲线", "value", {{"exponent", 2}}},
    {"clamp", "限制", "value", {{"min", 0}, {"max", 1}}},
    {"output_surface",
     "地表输出",
     "value",
     {{"value", 1},
      {"threshold", 0.5},
      {"block", "minecraft:stone"},
      {"under", "minecraft:dirt"},
      {"depth", 3},
      {"alternatives", QJsonArray{}}}},
    {"output_scatter",
     "植被 / 结构输出",
     "value",
     {{"value", 1},
      {"density", 0.5},
      {"spacing", 12},
      {"assetId", ""},
      {"assets", QJsonArray{}},
      {"rotation", true},
      {"minScale", 1},
      {"maxScale", 1},
      {"embed", 0}}}};
const NodeSpec *spec(const QString &type) {
    for (auto &s : specs)
        if (type == s.type)
            return &s;
    return nullptr;
}
bool validId(const QString &s) {
    static QRegularExpression re("^[A-Za-z0-9_.-]{1,96}$");
    return re.match(s).hasMatch();
}
double hash(int64_t x, int64_t z, uint32_t seed) {
    uint64_t n = uint64_t(x) * 0x9e3779b185ebca87ull ^ uint64_t(z) * 0xc2b2ae3d27d4eb4full ^ seed;
    n ^= n >> 30;
    n *= 0xbf58476d1ce4e5b9ull;
    n ^= n >> 27;
    n *= 0x94d049bb133111ebull;
    n ^= n >> 31;
    return (n >> 11) * 0x1.0p-53;
}
double noise(double x, double z, uint32_t seed) {
    auto ix = int64_t(std::floor(x)), iz = int64_t(std::floor(z));
    double u = x - ix, v = z - iz;
    u = u * u * (3 - 2 * u);
    v = v * v * (3 - 2 * v);
    return std::lerp(std::lerp(hash(ix, iz, seed), hash(ix + 1, iz, seed), u),
                     std::lerp(hash(ix, iz + 1, seed), hash(ix + 1, iz + 1, seed), u), v);
}
} // namespace
QJsonArray pcgNodeCatalog() {
    QJsonArray a;
    for (auto &s : specs) {
        QJsonArray ports;
        for (auto &p : QString(s.ports).split(',', Qt::SkipEmptyParts))
            ports.append(p);
        a.append(QJsonObject{{"type", s.type},
                             {"name", s.name},
                             {"inputs", ports},
                             {"output", "value"},
                             {"valueType", "scalar"},
                             {"parameters", s.defaults}});
    }
    return a;
}
static bool compileGraphDepth(const PcgGraph &g, PcgPlan &plan, QString &error, int depth) {
    error.clear();
    plan = {};
    auto fail = [&](QString e) {
        error = e;
        return false;
    };
    if (depth > 8)
        return fail("子图嵌套不能超过8层");
    if (!validId(g.id) || g.version != 1 || g.nodes.size() > 256 || g.connections.size() > 1024)
        return fail("PCG 图版本、ID或预算无效（最多256节点/1024连线）");
    QHash<QString, int> ids;
    std::vector<std::vector<int>> incoming(g.nodes.size());
    for (int i = 0; i < int(g.nodes.size()); ++i) {
        auto &n = g.nodes[i];
        if (!validId(n.id) || ids.contains(n.id) || !spec(n.type))
            return fail("未知节点类型或重复节点ID：" + n.id + " / " + n.type);
        ids[n.id] = i;
        for (auto v : n.parameters)
            if (v.isDouble() && (!std::isfinite(v.toDouble()) || std::abs(v.toDouble()) > 1e9))
                return fail("节点参数不是有限数值");
    }
    QSet<QString> sockets;
    for (int i = 0; i < int(g.connections.size()); ++i) {
        auto &c = g.connections[i];
        if (!ids.contains(c.fromNode) || !ids.contains(c.toNode) || c.fromPort != "value")
            return fail("连线引用不存在的节点或输出端口");
        auto &n = g.nodes[ids[c.toNode]];
        if (!QString(spec(n.type)->ports).split(',').contains(c.toPort) ||
            sockets.contains(c.toNode + ":" + c.toPort))
            return fail("输入端口无效或已有连线");
        if (g.nodes[ids[c.fromNode]].type.startsWith("output_"))
            return fail("输出节点不能作为上游输入");
        sockets.insert(c.toNode + ":" + c.toPort);
        incoming[ids[c.toNode]].push_back(i);
    }
    std::vector<int> mark(g.nodes.size()), mapped(g.nodes.size(), -1);
    std::function<bool(int)> visit = [&](int i) {
        if (mark[i] == 2)
            return true;
        if (mark[i] == 1)
            return fail("PCG 图包含循环");
        mark[i] = 1;
        PcgInstruction ins;
        ins.id = g.nodes[i].id;
        ins.type = g.nodes[i].type;
        ins.parameters = spec(ins.type)->defaults;
        for (auto it = g.nodes[i].parameters.begin(); it != g.nodes[i].parameters.end(); ++it)
            ins.parameters[it.key()] = it.value();
        for (int ci : incoming[i]) {
            auto &c = g.connections[ci];
            int from = ids[c.fromNode];
            if (!visit(from))
                return false;
            int *slot = c.toPort == "a"   ? &ins.a
                        : c.toPort == "b" ? &ins.b
                        : c.toPort == "t" ? &ins.t
                                          : &ins.value;
            *slot = mapped[from];
        }
        if (ins.type == "subgraph" && !ins.parameters["graph"].toObject().isEmpty()) {
            auto json = ins.parameters["graph"].toObject();
            PcgGraph child;
            child.id = json["id"].toString("embedded");
            child.version = json["version"].toInt(1);
            for (auto v : json["nodes"].toArray()) {
                auto n = v.toObject();
                if (n["type"].toString().startsWith("output_"))
                    return fail("子图应输出字段，不能包含地表或散布输出节点");
                child.nodes.push_back({n["id"].toString(), n["type"].toString(), n["parameters"].toObject()});
            }
            for (auto v : json["connections"].toArray()) {
                auto c = v.toObject();
                child.connections.push_back({c["fromNode"].toString(), c["fromPort"].toString(),
                                             c["toNode"].toString(), c["toPort"].toString()});
            }
            auto nested = std::make_shared<PcgPlan>();
            if (!compileGraphDepth(child, *nested, error, depth + 1))
                return false;
            ins.subgraph = nested;
        }
        mapped[i] = int(plan.instructions.size());
        if (ins.type.startsWith("output_"))
            plan.output = mapped[i];
        plan.instructions.push_back(std::move(ins));
        mark[i] = 2;
        return true;
    };
    for (int i = 0; i < int(g.nodes.size()); ++i)
        if (!visit(i))
            return false;
    // Fold constant-only expression chains once, preserving node IDs and ports.
    const QSet<QString> operators{"add",     "multiply", "min",   "max",   "mix",
                                  "compare", "range",    "remap", "curve", "clamp"};
    for (size_t i = 0; i < plan.instructions.size(); ++i) {
        auto &n = plan.instructions[i];
        if (!operators.contains(n.type))
            continue;
        bool constant = true;
        for (int k : {n.a, n.b, n.t, n.value})
            if (k >= 0 && plan.instructions[k].type != "constant")
                constant = false;
        if (constant) {
            PcgPlan single;
            auto copy = n;
            auto replace = [&](int &slot, const char *name) {
                if (slot >= 0) {
                    copy.parameters[name] = plan.instructions[slot].parameters["value"];
                    slot = -1;
                }
            };
            replace(copy.a, "a");
            replace(copy.b, "b");
            replace(copy.t, "t");
            replace(copy.value, "value");
            single.instructions.push_back(copy);
            double value = single.evaluate({});
            n.type = "constant";
            n.parameters = {{"value", value}};
            n.a = n.b = n.t = n.value = -1;
        }
    }
    return true; // Incomplete graphs are editable; unconnected outputs use visible defaults.
}
bool compileGraph(const PcgGraph &g, PcgPlan &p, QString &e) {
    return compileGraphDepth(g, p, e, 0);
}
std::vector<double> PcgPlan::evaluateValues(const PcgSample &s) const {
    std::vector<double> values;
    values.reserve(instructions.size());
    for (auto &n : instructions) {
        auto p = [&](const char *k, double fallback = 0) { return n.parameters[k].toDouble(fallback); };
        auto input = [&](int i, const char *k, double d = 0) { return i < 0 ? p(k, d) : values[i]; };
        double a = input(n.a, "a"), b = input(n.b, "b"), v = input(n.value, "value", 1), r = 0;
        auto t = n.type;
        if (t == "parameter")
            r = s.parameters ? s.parameters->value(n.parameters["name"].toString()).toDouble(p("default", .5))
                             : p("default", .5);
        else if (t == "input")
            r = s.input;
        else if (t == "subgraph") {
            auto sub = s;
            sub.input = v;
            r = n.subgraph ? n.subgraph->evaluate(sub) : v;
        } else if (t == "constant")
            r = p("value", 1);
        else if (t == "world_x")
            r = s.x;
        else if (t == "world_z")
            r = s.z;
        else if (t == "local_x")
            r = s.localX;
        else if (t == "local_z")
            r = s.localZ;
        else if (t == "world_height")
            r = s.height;
        else if (t == "base_height")
            r = s.baseHeight;
        else if (t == "slope")
            r = s.slope;
        else if (t == "normal_y")
            r = std::cos(s.slope * 3.141592653589793 / 180);
        else if (t == "mask")
            r = s.mask;
        else if (t == "seed")
            r = s.seed;
        else if (t == "noise") {
            double weight = 1, total = 0, scale = std::max(.01, p("scale", 32));
            for (int i = 0; i < std::clamp(int(p("octaves", 3)), 1, 8); ++i) {
                r += noise(s.x / scale, s.z / scale, s.seed + uint32_t(i) * 7919) * weight;
                total += weight;
                weight *= .5;
                scale *= .5;
            }
            r = r / total * p("amplitude", 1) + p("offset");
        } else if (t == "paint_mask") {
            double rad = std::max(.01, p("radius", 8));
            for (auto point : n.parameters["points"].toArray()) {
                auto q = point.toArray();
                if (q.size() == 2)
                    r = std::max(
                        r, std::clamp(1 - std::hypot(s.x - q[0].toDouble(), s.z - q[1].toDouble()) / rad, 0.,
                                      1.));
            }
        } else if (t == "add")
            r = a + b;
        else if (t == "multiply")
            r = a * b;
        else if (t == "min")
            r = std::min(a, b);
        else if (t == "max")
            r = std::max(a, b);
        else if (t == "mix")
            r = std::lerp(a, b, std::clamp(input(n.t, "t", .5), 0., 1.));
        else if (t == "compare")
            r = v >= p("threshold", .5);
        else if (t == "range") {
            double feather = std::max(0., p("feather", 5));
            r = v < p("min") || v > p("max", 90) ? 0
                : feather > 0 ? std::clamp(std::min(v - p("min"), p("max", 90) - v) / feather, 0., 1.)
                              : 1;
        } else if (t == "remap") {
            double d = p("inMax", 100) - p("inMin");
            r = std::lerp(p("outMin"), p("outMax", 1), std::abs(d) > 1e-12 ? (v - p("inMin")) / d : 0);
        } else if (t == "curve")
            r = std::pow(std::clamp(v, 0., 1.), std::clamp(p("exponent", 2), .01, 100.));
        else if (t == "clamp")
            r = std::clamp(v, p("min"), std::max(p("min"), p("max", 1)));
        else if (t.startsWith("output_"))
            r = v;
        values.push_back(std::isfinite(r) ? r : 0);
    }
    return values;
}
double PcgPlan::evaluate(const PcgSample &s) const {
    auto values = evaluateValues(s);
    return output >= 0 ? values[output] : values.empty() ? 0 : values.back();
}
bool parseCreationBlock(const QString &text, BlockState &state, QString &error) {
    static QRegularExpression re("^([a-z0-9_.-]+:[a-z0-9_./-]+)(?:\\[([a-z0-9_=,]+)\\])?$");
    auto m = re.match(text);
    if (!m.hasMatch()) {
        error = "非法方块状态：" + text;
        return false;
    }
    state = {m.captured(1), {}};
    for (auto item : m.captured(2).split(',', Qt::SkipEmptyParts)) {
        auto pair = item.split('=');
        if (pair.size() != 2 || state.properties.contains(pair[0])) {
            error = "重复或非法方块属性";
            return false;
        }
        state.properties[pair[0]] = pair[1];
    }
    return true;
}
bool validateCreation(const CreationState &s, QString &error) {
    error.clear();
    QSet<QString> ids, graphs, assets, roads;
    auto fail = [&](QString e) {
        error = e;
        return false;
    };
    auto unique = [&](const auto &list) {
        if (list.size() > 4096)
            return false;
        for (auto &v : list) {
            if (!validId(v.id) || ids.contains(v.id))
                return false;
            ids.insert(v.id);
        }
        return true;
    };
    QSet<QString> paths;
    for (const auto &path : s.folders) {
        if (path.trimmed().isEmpty() || path.size() > 256 || paths.contains(path.toCaseFolded()) ||
            s.folders.size() > 4096)
            return fail("资产文件夹名称无效或重复");
        paths.insert(path.toCaseFolded());
    }
    for (const auto &path : s.assetFolders)
        if (!s.folders.contains(path))
            return fail("资产引用了不存在的文件夹");
    if (!unique(s.graphs) || !unique(s.volumes) || !unique(s.assets) || !unique(s.presets) ||
        !unique(s.roadNodes) || !unique(s.roadEdges) || !unique(s.caveMarkers) || !unique(s.pcgAssets))
        return fail("创作对象ID必须唯一，单类最多4096项");
    for (const auto &asset : s.pcgAssets) {
        CreationState local;
        local.graphs = {asset.graph};
        local.volumes = {asset.volume};
        local.assets = asset.assets;
        local.caveMarkers = asset.markers;
        if (!validateCreation(local, error))
            return false;
    }
    for (auto &g : s.graphs) {
        if (!g.validate(error))
            return false;
        graphs.insert(g.id);
    }
    size_t cells = 0, nbtBytes = 0;
    auto validTransform = [](QVector3D rotation, QVector3D scale) {
        return rotation.x() == 0 && rotation.z() == 0 && std::isfinite(rotation.y()) &&
               std::abs(rotation.y()) <= 360000 && std::fmod(rotation.y(), 90) == 0 && scale.y() > 0 &&
               std::all_of(&scale[0], &scale[0] + 3, [](float s) {
                   return std::isfinite(s) && s != 0 && std::abs(s) <= 8 && std::round(s) == s;
               });
    };
    for (auto &a : s.assets) {
        assets.insert(a.id);
        if (!a.category.isEmpty() && a.category != "plant" && a.category != "building" &&
            a.category != "cave")
            return fail("结构资产分类无效");
        if (!validTransform(a.defaultRotation, a.defaultScale) || a.groundBlend < 0 || a.groundBlend > 2)
            return fail("资产默认变换或地基模式无效");
        for (int axis = 0; axis < 3; ++axis)
            if (!std::isfinite(a.pivot[axis]) || std::abs(a.pivot[axis]) > 4096 ||
                std::round(a.pivot[axis]) != a.pivot[axis])
                return fail("资产枢轴必须为±4096范围内的整数坐标");
        if (a.entities.size() > 16384)
            return fail("单资产最多16384实体");
        try {
            for (auto &e : a.entities) {
                for (int axis = 0; axis < 3; ++axis)
                    if (!std::isfinite(e.position[axis]) || std::abs(e.position[axis]) > 4096)
                        return fail("资产实体坐标无效");
                nbtBytes += e.nbt.size();
                patchNbtPayload(e.nbt, {});
            }
        } catch (const std::exception &e) {
            return fail(QString::fromUtf8(e.what()));
        }
        if (a.width < 1 || a.height < 1 || a.depth < 1 || a.width > 512 || a.height > 384 || a.depth > 512 ||
            int64_t(a.width) * a.height * a.depth > 16000000)
            return fail("资产包围盒超出预算（最大512×384×512，1600万体素）");
        cells += a.blocks.size();
        if (cells > 2000000)
            return fail("工程结构数据超过200万方块预算");
        if (!QStringList{"place", "carve", "ignore", "cave"}.contains(a.placement))
            return fail("未知资产写入类别");
        QSet<quint64> seen;
        for (auto &b : a.blocks) {
            if (b.x < 0 || b.y < 0 || b.z < 0 || b.x >= a.width || b.y >= a.height || b.z >= a.depth)
                return fail("结构方块超出包围盒");
            quint64 k = (quint64(b.y) * a.depth + b.z) * a.width + b.x;
            if (seen.contains(k))
                return fail("结构含重复坐标");
            seen.insert(k);
            BlockState block;
            if (!parseCreationBlock(b.state, block, error))
                return false;
            if (!b.nbt.isEmpty()) {
                nbtBytes += b.nbt.size();
                try {
                    patchNbtPayload(b.nbt, {});
                } catch (const std::exception &e) {
                    return fail(QString::fromUtf8(e.what()));
                }
            }
        }
        if (nbtBytes > 32 * 1024 * 1024)
            return fail("工程实体NBT超过32MiB预算");
    }
    auto finite = [](const QVector3D &v) {
        return std::isfinite(v.x()) && std::isfinite(v.y()) && std::isfinite(v.z()) && v.length() < 1e7;
    };
    for (auto &v : s.volumes) {
        for (auto val : v.overrides)
            if (!val.isDouble() || !std::isfinite(val.toDouble()) || std::abs(val.toDouble()) > 1e9)
                return fail("实例参数覆盖必须为有限标量");
        if (!graphs.contains(v.graphId) || !finite(v.min) || !finite(v.max) || v.min.x() >= v.max.x() ||
            v.min.y() >= v.max.y() || v.min.z() >= v.max.z() || !std::isfinite(v.transition) ||
            v.transition < 0)
            return fail("PCG范围或图引用无效");
    }
    for (auto &p : s.presets)
        if (!assets.contains(p.assetId) || !finite(p.position) || !finite(p.rotation) || !finite(p.scale) ||
            p.scale.x() == 0 || p.scale.y() == 0 || p.scale.z() == 0 ||
            std::round(p.scale.x()) != p.scale.x() || std::round(p.scale.y()) != p.scale.y() ||
            std::round(p.scale.z()) != p.scale.z() || p.scale.y() < 0 || std::abs(p.scale.x()) > 8 ||
            std::abs(p.scale.y()) > 8 || std::abs(p.scale.z()) > 8 || p.rotation.x() != 0 ||
            p.rotation.z() != 0 || std::fmod(p.rotation.y(), 90) != 0)
            return fail("结构需90°步进的Y旋转，比例范围非零±8；引用或坐标无效");
    for (auto &n : s.roadNodes) {
        roads.insert(n.id);
        if (!finite(n.position))
            return fail("道路坐标无效");
    }
    for (auto &e : s.roadEdges) {
        BlockState block;
        if (!roads.contains(e.from) || !roads.contains(e.to) || e.from == e.to || e.width < 1 ||
            e.width > 64 || !std::isfinite(e.maxSlope) || e.maxSlope < 1 || e.maxSlope > 80 ||
            e.clearance < 2 || e.clearance > 32 || (e.bridge && e.tunnel) ||
            !parseCreationBlock(e.surface, block, error))
            return fail("道路连接、模式、宽度或材质无效");
        auto a =
            std::find_if(s.roadNodes.begin(), s.roadNodes.end(), [&](auto &n) { return n.id == e.from; });
        auto b = std::find_if(s.roadNodes.begin(), s.roadNodes.end(), [&](auto &n) { return n.id == e.to; });
        if ((a->position - b->position).length() > 32768)
            return fail("单段道路不能超过32768格，请添加中间节点");
    }
    for (auto &m : s.caveMarkers) {
        if (!assets.contains(m.assetId) || !finite(m.position) || !finite(m.end) ||
            !QStringList{"portal", "exit", "seal", "interior"}.contains(m.kind))
            return fail("洞穴标记或引用无效");
    }
    for (auto &g : s.graphs)
        for (auto &n : g.nodes) {
            if (n.type == "output_scatter") {
                QString id = n.parameters["assetId"].toString();
                if (!id.isEmpty() && !assets.contains(id))
                    return fail("散布节点引用缺失资产：" + id);
                for (auto a : n.parameters["assets"].toArray())
                    if (!assets.contains(a.toObject()["id"].toString()))
                        return fail("散布权重列表含缺失资产");
            }
            if (n.type == "output_surface")
                for (auto k : {"block", "under"})
                    if (n.parameters.contains(k)) {
                        BlockState state;
                        if (!parseCreationBlock(n.parameters[k].toString(), state, error))
                            return false;
                    }
        }
    return true;
}
} // namespace wb
