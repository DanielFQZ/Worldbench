#include "McpServer.h"
#include "Editor.h"
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QUuid>
#include <stdexcept>

namespace {
QJsonArray toolList() {
    const QJsonObject integer{{"type", "integer"}}, str{{"type", "string"}}, boolean{{"type", "boolean"}};
    const QJsonObject position{{"type", "array"}, {"items", integer}, {"minItems", 3}, {"maxItems", 3}};
    const QJsonObject revision{{"type", "integer"},
                               {"minimum", 1},
                               {"description", "Current world_info.revision; stale writes are rejected. Read "
                                               "again before deciding whether to retry."}};
    QJsonArray tools;
    auto add = [&](QString name, QString description, QJsonObject properties, QJsonArray required,
                   bool readOnly) {
        tools << QJsonObject{{"name", name},
                             {"description", description},
                             {"inputSchema", QJsonObject{{"type", "object"},
                                                         {"properties", properties},
                                                         {"required", required},
                                                         {"additionalProperties", false}}},
                             {"annotations", QJsonObject{{"readOnlyHint", readOnly},
                                                         {"destructiveHint", !readOnly},
                                                         {"openWorldHint", false}}}};
    };
    add("world_info",
        "Inspect editable bounds, protected bedrock, busy state, scene nodes and monotonic revision. "
        "Coordinates are Minecraft XYZ; Y is up.",
        {}, {}, true);
    add("creation",
        "Inspect or edit the shared PCG/asset/road/cave creation state. "
        "action=inspect/new_graph/new_volume/store_pcg_asset/import_asset/place_asset/road_node/edit/file/"
        "cave_analysis. "
        "Mutations require expected_revision. edit operation=add/patch/remove/graph/replace; data uses the "
        "inspect schema. file operation=save_graph/load_graph/save_asset/load_asset. All edits are one undo "
        "command and use the same revision as GUI.",
        {{"action", str},
         {"name", str},
         {"graph_id", str},
         {"surface_material", boolean},
         {"path", str},
         {"asset_id", str},
         {"kind", str},
         {"x", integer},
         {"y", integer},
         {"z", integer},
         {"id", str},
         {"operation", str},
         {"data", QJsonObject{{"type", "object"}}},
         {"expected_revision", revision}},
        {"action"}, false);
    add("catalog",
        "Search loaded version's block names, visual properties and suggested states. JAR assets do not "
        "define all gameplay defaults.",
        {{"query", str}, {"limit", QJsonObject{{"type", "integer"}, {"minimum", 1}, {"maximum", 200}}}}, {},
        true);
    add("read_region",
        "Read up to 16384 blocks in inclusive bounds, with a state palette. Flat array ordering is y,z,x (x "
        "fastest).",
        {{"min", position}, {"max", position}}, {"min", "max"}, true);
    const QJsonObject entry{{"type", "object"},
                            {"properties", QJsonObject{{"position", position}, {"block", str}}},
                            {"required", QJsonArray{"position", "block"}},
                            {"additionalProperties", false}};
    add("set_blocks",
        "Atomically set 1..65536 unique cells. One undo step. block syntax: "
        "minecraft:oak_door[half=lower,facing=north]. Place paired blocks and connections explicitly; no "
        "game physics.",
        {{"blocks", QJsonObject{{"type", "array"}, {"items", entry}, {"minItems", 1}, {"maxItems", 65536}}},
         {"expected_revision", revision}},
        {"blocks", "expected_revision"}, false);
    add("fill_box",
        "Fill an inclusive cuboid of up to 65536 cells as one undo step. hollow=true writes only boundary "
        "cells and preserves the interior.",
        {{"min", position},
         {"max", position},
         {"block", str},
         {"hollow", boolean},
         {"expected_revision", revision}},
        {"min", "max", "block", "expected_revision"}, false);
    add("sculpt",
        "Apply one terrain brush stamp. Tools: 0 raise/lower, 1 flatten, 2 carve, 3 fill, 4 paint, 5 smooth. "
        "radius "
        "1..16, strength 1..4. material_id is a read_region palette index (default stone). reverse flips the "
        "tool. Flatten preserves cavities and caps the surface; fill_holes optionally fills air to "
        "fill_depth below it.",
        {{"position", position},
         {"tool", QJsonObject{{"type", "integer"}, {"minimum", 0}, {"maximum", 5}}},
         {"radius", QJsonObject{{"type", "integer"}, {"minimum", 1}, {"maximum", 16}}},
         {"strength", QJsonObject{{"type", "integer"}, {"minimum", 1}, {"maximum", 4}}},
         {"material_id", integer},
         {"reverse", boolean},
         {"fill_holes", boolean},
         {"decay", QJsonObject{{"type", "number"}, {"minimum", 0}, {"maximum", 1}}},
         {"fill_depth", QJsonObject{{"type", "integer"}, {"minimum", 1}, {"maximum", 383}}},
         {"shape", QJsonObject{{"type", "integer"}, {"minimum", 0}, {"maximum", 1}}},
         {"falloff", QJsonObject{{"type", "integer"}, {"minimum", 0}, {"maximum", 2}}},
         {"flatten_mode", QJsonObject{{"type", "integer"}, {"minimum", 0}, {"maximum", 2}}},
         {"expected_revision", revision}},
        {"position", "tool", "radius", "strength", "expected_revision"}, false);
    add("terrain_instance",
        "Atomically adjust an existing heightmap instance. Inspect world_info.terrain_instances first. "
        "changes may contain x,y,z,rotationX/Y/Z (-180..180),scaleX/Y/Z (1..1600 "
        "percent),width,depth,low,high,blend (0 replace,1 add,2 max,3 min,4 smart maximum),opacity (0..100),"
        "feather (0..50, modes 0-3),blendSmoothness (0..128 vertical blocks),blendEdge (0..1024 local "
        "surface blocks, clamped to half extent). Smart mode uses absolute height, adds at most "
        "blendSmoothness/4 near crossings and fades inside edges; both zero means hard max. "
        "enabled,name. Embedded image data is unchanged. One shared undo step.",
        {{"id", str}, {"changes", QJsonObject{{"type", "object"}}}, {"expected_revision", revision}},
        {"id", "changes", "expected_revision"}, false);
    add("sculpt_layer",
        "Manage additive sculpt layers. action: add/select/visible/rename/remove. Existing layers need id. "
        "visible needs enabled, rename needs name. Base cannot be removed. Inspect world_info.sculpt_layers. "
        "Sculpt changes relative height on active layer.",
        {{"action", str}, {"id", str}, {"name", str}, {"enabled", boolean}, {"expected_revision", revision}},
        {"action", "expected_revision"}, false);
    add("environment",
        "Change viewport environment only (water, waterWaves, waterReflections, shadows, clouds: bool; "
        "waveStrength/waveDensity/waveSpeed/waterFoamStrength/cloudDensity/cloudSpeed: "
        "integer 0..100; "
        "waterFogDensity: 0..200 (percent extinction per metre), waterFogStart: 0..128 metres; "
        "waterFogColor/waterSurfaceColor/waterReflectionColor/waterFoamColor: #RRGGBB; waterFoamWidth: "
        "integer 1..32); shared undo and project persistence. Changes: "
        "enabled,sky,sun,atmosphere "
        "(bool); skyIntensity 0..200, sunIntensity 0..300 percent; azimuth -180..180, elevation 0..90 "
        "degrees; "
        "haze 0..100, exposure -30..30 tenths EV; skyColor,horizonColor,groundColor,sunColor #RRGGBB; "
        "skyTexturePath is an optional local 2:1 equirectangular HDR/EXR/PNG/JPG/TGA/DDS/KTX2 (HDR is "
        "decoded with RGBE tone mapping). Clouds use a low-cost spherical noise layer; "
        "volumetric clouds and GI are not included.",
        {{"changes", QJsonObject{{"type", "object"}}}, {"expected_revision", revision}},
        {"changes", "expected_revision"}, false);
    add("undo", "Undo the latest shared GUI/MCP command. Check the revision first.",
        {{"expected_revision", revision}}, {"expected_revision"}, false);
    const QJsonObject column{{"type", "array"}, {"items", integer}, {"minItems", 2}, {"maxItems", 2}};
    add("height_region",
        "Start asynchronous height offset on the active sculpt layer. Inclusive min/max [x,z], up to "
        "1048576 columns and a 256 MiB workspace estimate. Offset -381..381 excluding zero. Returns "
        "job_id immediately; poll/cancel with region_job. Atomic commit with one undo; leaves selection "
        "unchanged.",
        {{"min", column}, {"max", column}, {"offset", integer}, {"expected_revision", revision}},
        {"min", "max", "offset", "expected_revision"}, false);
    add("region_job",
        "Inspect or cancel the latest height-region job by exact job_id, even while busy. "
        "action=status/cancel. "
        "Cancel is a request, not confirmation: poll until cancelled/completed/failed. Completed jobs cannot "
        "be cancelled. IDs expire on new/open project or a newer job; stale IDs never cancel newer work.",
        {{"job_id", str},
         {"action", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"status", "cancel"}}}}},
        {"job_id", "action"}, false);
    add("redo", "Redo the latest undone shared command.", {{"expected_revision", revision}},
        {"expected_revision"}, false);
    add("camera",
        "Inspect or set preview camera. position is [x,y,z], yaw/pitch in degrees. reset=true frames the "
        "world. The default renderer combines error-bounded terrain with ready local voxel chunks. "
        "world_info.preview reports scope/error/readiness. Does not alter world "
        "data.",
        {{"position", QJsonObject{{"type", "array"},
                                  {"items", QJsonObject{{"type", "number"}}},
                                  {"minItems", 3},
                                  {"maxItems", 3}}},
         {"yaw", QJsonObject{{"type", "number"}}},
         {"pitch", QJsonObject{{"type", "number"}}},
         {"reset", boolean}},
        {}, false);
    add("screenshot",
        "Capture the editor window as PNG for visual feedback. Wait until world_info.mesh_busy=false. No "
        "files are written.",
        {}, {}, true);
    return tools;
}
} // namespace
McpServer::McpServer(Editor *editor) : QObject(editor), m_editor(editor) {
    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        while (auto *socket = m_server.nextPendingConnection()) {
            m_clients.insert(socket);
            socket->setReadBufferSize(8 * 1024 * 1024 + 1);
            auto buffer = std::make_shared<QByteArray>();
            auto initialized = std::make_shared<int>(0);
            connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
                m_clients.remove(socket);
                socket->deleteLater();
            });
            connect(socket, &QLocalSocket::readyRead, this, [this, socket, buffer, initialized] {
                buffer->append(socket->readAll());
                if (buffer->size() > 8 * 1024 * 1024) {
                    socket->abort();
                    return;
                }
                int newline;
                while ((newline = buffer->indexOf('\n')) >= 0) {
                    auto line = buffer->left(newline);
                    buffer->remove(0, newline + 1);
                    auto envelope = QJsonDocument::fromJson(line).object();
                    if (envelope["token"].toString() != m_token) {
                        socket->abort();
                        return;
                    }
                    const QJsonObject request = envelope["request"].toObject();
                    auto id = request["id"];
                    bool hasId = request.contains("id");
                    QString method = request["method"].toString();
                    QJsonObject response{{"jsonrpc", "2.0"}, {"id", id}}, result;
                    auto protocolError = [&](int code, QString message) {
                        response["error"] = QJsonObject{{"code", code}, {"message", message}};
                    };
                    if (request["jsonrpc"].toString() != "2.0" || method.isEmpty())
                        protocolError(-32600, "Invalid Request");
                    else if (method == "initialize") {
                        *initialized = 1;
                        result = {
                            {"protocolVersion", "2025-06-18"},
                            {"capabilities", QJsonObject{{"tools", QJsonObject{{"listChanged", false}}}}},
                            {"serverInfo", QJsonObject{{"name", "Worldbench"},
                                                       {"version", QCoreApplication::applicationVersion()}}},
                            {"instructions",
                             "Local editor session. Read world_info and catalog before editing. Writes "
                             "require expected_revision and are undoable. The user can edit concurrently. "
                             "Assets are not game physics: place door halves and fence connection states "
                             "explicitly. Do not save/export unless the user handles it in the UI."}};
                    } else if (method == "notifications/initialized" && *initialized == 1) {
                        *initialized = 2;
                    } else if (method == "ping")
                        result = {};
                    else if (method.startsWith("notifications/")) {
                    } else if (*initialized != 2)
                        protocolError(-32002, "Initialize the MCP session first");
                    else if (method == "tools/list")
                        result = {{"tools", toolList()}};
                    else if (method == "tools/call") {
                        const auto params = request["params"].toObject();
                        const auto name = params["name"].toString();
                        try {
                            QJsonObject output = m_editor->executeTool(name, params["arguments"].toObject());
                            QJsonArray content;
                            if (output.contains("png"))
                                content << QJsonObject{
                                    {"type", "image"}, {"mimeType", "image/png"}, {"data", output["png"]}};
                            else
                                content
                                    << QJsonObject{{"type", "text"},
                                                   {"text", QString::fromUtf8(QJsonDocument(output).toJson(
                                                                QJsonDocument::Compact))}};
                            result = {{"content", content}, {"isError", false}};
                            m_activity =
                                "已执行 " + name + " · revision " + QString::number(m_editor->revision());
                        } catch (const std::exception &e) {
                            result = {
                                {"content", QJsonArray{QJsonObject{{"type", "text"},
                                                                   {"text", QString::fromUtf8(e.what())}}}},
                                {"isError", true}};
                            m_activity = "已拒绝 " + name + "：" + QString::fromUtf8(e.what());
                        }
                        emit m_editor->changed();
                    } else
                        protocolError(-32601, "Method not found");
                    if (hasId) {
                        if (!response.contains("error"))
                            response["result"] = result;
                        socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact) + '\n');
                    }
                }
            });
        }
    });
}
void McpServer::setEnabled(bool enable) {
    if (enable == enabled())
        return;
    if (!enable) {
        m_server.close();
        const auto clients = m_clients;
        for (auto *c : clients)
            c->abort();
        m_token.clear();
        m_activity = "MCP 已关闭，客户端已断开";
        return;
    }
    m_name = "Worldbench-" + QUuid::createUuid().toString(QUuid::Id128);
    m_token = QUuid::createUuid().toString(QUuid::Id128) + QUuid::createUuid().toString(QUuid::Id128);
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server.listen(m_name)) {
        emit m_editor->failure(m_server.errorString());
        return;
    }
    m_activity = "MCP 已开启 · 本地当前用户 · 等待客户端";
}
McpServer::~McpServer() {
    m_server.disconnect(this);
    const auto clients = m_clients;
    for (auto *client : clients) {
        client->disconnect(this);
        client->abort();
        delete client;
    }
    m_clients.clear();
    m_server.close();
}
QString McpServer::configuration() const {
    if (!enabled())
        return "请先启用 MCP，再复制客户端配置。";
    QJsonObject server{
        {"command", QDir::toNativeSeparators(QCoreApplication::applicationDirPath() + "/WorldbenchMcp.exe")},
        {"args", QJsonArray{"--pipe", m_name, "--token", m_token}}};
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"mcpServers", QJsonObject{{"worldbench", server}}}})
                                 .toJson(QJsonDocument::Indented));
}
