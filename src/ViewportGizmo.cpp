#include "Viewport.h"
#include <QGuiApplication>
#include <QLineF>
#include <QPolygonF>
#include <QSettings>
#include <algorithm>
#include <cmath>
namespace {
double distanceToLine(QPointF p, QPointF a, QPointF b) {
    auto d = b - a;
    double l = QPointF::dotProduct(d, d);
    return QLineF(p, a + d * std::clamp(l > 0 ? QPointF::dotProduct(p - a, d) / l : 0., 0., 1.)).length();
}
std::array<QVector3D, 3> gizmoBasis(const wb::TerrainInstance &i, int mode, QVector3D signs) {
    std::array<QVector3D, 3> basis{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
    if (mode == 2) {
        QMatrix4x4 rotation;
        rotation.rotate(i.rotationY, 0, 1, 0);
        rotation.rotate(i.rotationX, 1, 0, 0);
        rotation.rotate(i.rotationZ, 0, 0, 1);
        for (int k = 0; k < 3; ++k)
            basis[k] = rotation.mapVector(basis[k]) * signs[k];
    }
    return basis;
}
const int planeAxes[3][2] = {{0, 1}, {0, 2}, {1, 2}};
QVector3D gizmoAnchor(const wb::TerrainInstance &i) {
    // A stable editing anchor, independent of evaluated terrain, visibility, rotation and scale.
    const auto pivot = i.pivot();
    return {pivot.x(), float(i.y + i.high + 7), pivot.z()};
}
} // namespace
void Viewport::setSnapEnabled(bool enabled) {
    m_snapEnabled = enabled;
    QSettings().setValue("input/transformSnap", enabled);
    emit cameraChanged();
}
QPointF Viewport::projectPoint(QVector3D p, bool *visible) const {
    auto q = viewProjection() * QVector4D(p, 1);
    if (visible)
        *visible = q.w() > .15f && q.z() > -q.w() && q.z() < q.w();
    if (std::abs(q.w()) < .0001f)
        return {-10000, -10000};
    return {(q.x() / q.w() + 1) * width() * .5, (1 - q.y() / q.w()) * height() * .5};
}
void Viewport::setGizmoMode(int mode) {
    if (m_gizmoDragging)
        finishGizmo();
    m_gizmoMode = std::clamp(mode, 0, 2);
    emit cameraChanged();
}
QVector3D Viewport::gizmoScaleSigns(const QString &id) const {
    if (m_editor)
        for (const auto &p : m_editor->world().creation.presets)
            if (p.id == id)
                return {p.scale.x() < 0 ? -1.f : 1.f, 1.f, p.scale.z() < 0 ? -1.f : 1.f};
    return {1, 1, 1};
}
// Adapt object semantics to the one gizmo geometry/input path. The adapter is
// transient; project data, integer structure scale and AABB volumes are unchanged.
bool Viewport::creationGizmo(wb::TerrainInstance &t, QVector3D &anchor, QString &kind) const {
    if (!m_editor)
        return false;
    const auto &state = m_editor->world().creation;
    const auto id = m_editor->selectedNode();
    t = wb::TerrainInstance{};
    t.id = id;
    for (const auto &v : state.volumes)
        if (v.id == id && v.visible) {
            kind = "volume";
            t.name = v.name;
            anchor = (v.min + v.max) * .5f;
        }
    for (const auto &p : state.presets)
        if (p.id == id && p.enabled) {
            kind = "asset";
            t.name = p.name;
            anchor = p.position;
            // Structure yaw uses clockwise integer quarter turns.
            t.rotationY = -int(p.rotation.y());
            t.scaleX = int(std::abs(p.scale.x()) * 100);
            t.scaleY = int(std::abs(p.scale.y()) * 100);
            t.scaleZ = int(std::abs(p.scale.z()) * 100);
        }
    for (const auto &n : state.roadNodes)
        if (n.id == id && n.enabled) {
            kind = "road";
            t.name = n.name;
            anchor = n.position + QVector3D(0, 1, 0);
        }
    if (kind.isEmpty())
        return false;
    t.x = int(std::lround(anchor.x()));
    t.y = int(std::lround(anchor.y()));
    t.z = int(std::lround(anchor.z()));
    return true;
}
QVariantMap Viewport::gizmoOverlay() const {
    QVariantList markers, axes;
    if (!m_editor || width() < 1 || height() < 1)
        return {};
    const auto &world = m_editor->world();
    const wb::TerrainInstance *selected = nullptr;
    QVector3D center;
    for (const auto &i : world.heights.instances) {
        const auto pivot = i.pivot();
        const int x = int(pivot.x()), z = int(pivot.z());
        if (!world.contains(x, 0, z) && !(m_gizmoDragging && i.id == m_gizmoInitial.id))
            continue;
        const auto position = gizmoAnchor(i);
        bool visible;
        auto screen = projectPoint(position, &visible);
        visible = visible && screen.x() > 16 && screen.x() < width() - 16 && screen.y() > 55 &&
                  screen.y() < height() - 65;
        if (visible)
            markers.append(QVariantMap{{"id", i.id},
                                       {"name", i.name},
                                       {"x", screen.x()},
                                       {"y", screen.y()},
                                       {"enabled", i.enabled},
                                       {"selected", i.id == m_editor->selectedNode()}});
        if (i.id == m_editor->selectedNode() && (visible || m_gizmoDragging)) {
            selected = &i;
            center = position;
        }
    }
    // Badges share the same size, hit target and center handle for every object.
    for (auto entry : creationOverlay()) {
        const auto o = entry.toMap();
        markers.append(QVariantMap{{"id", o["id"]},
                                   {"name", o["name"]},
                                   {"x", o["x"]},
                                   {"y", o["y"]},
                                   {"enabled", true},
                                   {"selected", o["selected"]},
                                   {"kind", o["kind"]}});
    }
    wb::TerrainInstance creation;
    QString kind;
    QVector3D anchor;
    if (creationGizmo(creation, anchor, kind)) {
        bool visible;
        projectPoint(anchor, &visible);
        if (visible || m_gizmoDragging) {
            selected = &creation;
            center = anchor;
        }
    }
    QVariantMap result{{"markers", markers}, {"axes", axes}, {"mode", m_gizmoMode}};
    if ((kind == "road" && m_gizmoMode != 0) || (kind == "volume" && m_gizmoMode == 1))
        return result;
    if (!selected || m_editor->tool() != -1)
        return result;
    if (m_gizmoDragging) {
        center = m_gizmoCenter;
        if (m_gizmoMode == 0)
            center += QVector3D(selected->x - m_gizmoInitial.x, selected->y - m_gizmoInitial.y,
                                selected->z - m_gizmoInitial.z);
    }
    // Perspective scale is determined by view depth, not radial distance. No
    // world-size cap: the same handle must remain usable from a 16K overview.
    // Freeze the scale during a gesture so displacement mapping stays stable.
    const double depth = QVector3D::dotProduct(center - m_camera, forward());
    const double length = m_gizmoDragging
                              ? m_gizmoWorldLength
                              : std::max(.001, depth) * 90.0 / std::max(1., height()) * 1.154700538;
    const auto origin = projectPoint(center);
    result["cx"] = origin.x();
    result["cy"] = origin.y();
    result["length"] = length;
    result["active"] = true;
    result["dragging"] = m_gizmoDragging;
    result["readout"] = m_gizmoReadout;
    result["dragX"] = m_gizmoLast.x();
    result["dragY"] = m_gizmoLast.y();
    result["hover"] = m_gizmoHover;
    result["axis"] = m_gizmoAxis;
    result["angle"] = m_gizmoAngle;
    const auto basis = gizmoBasis(*selected, m_gizmoMode, gizmoScaleSigns(selected->id));
    QVariantList planes;
    if (m_gizmoMode != 1) {
        for (int n = 0; n < 3; ++n) {
            QVariantList points;
            for (auto pair : {QPointF(.22, .22), QPointF(.43, .22), QPointF(.43, .43), QPointF(.22, .43)}) {
                const auto p = projectPoint(
                    center +
                    (basis[planeAxes[n][0]] * pair.x() + basis[planeAxes[n][1]] * pair.y()) * length);
                points.append(QVariantMap{{"x", p.x()}, {"y", p.y()}});
            }
            planes.append(QVariantMap{
                {"axis", n + 4}, {"points", points}, {"label", QStringList{"XY", "XZ", "YZ"}[n]}});
        }
    }
    result["planes"] = planes;
    const QString colors[3] = {"#ef6666", "#76d789", "#70a9ff"};
    for (int k = 0; k < 3; ++k) {
        if (kind == "asset" && m_gizmoMode == 1 && k != 1)
            continue;
        QVariantList points;
        if (m_gizmoMode == 1) {
            const auto view = m_camera - center;
            const auto u =
                basis[(k + 1) % 3] * (QVector3D::dotProduct(view, basis[(k + 1) % 3]) >= 0 ? 1 : -1);
            const auto v =
                basis[(k + 2) % 3] * (QVector3D::dotProduct(view, basis[(k + 2) % 3]) >= 0 ? 1 : -1);
            for (int n = 0; n <= 24; ++n) {
                double a = n * 1.5707963267948966 / 24;
                auto p = projectPoint(center + (u * std::cos(a) + v * std::sin(a)) * length);
                points.append(QVariantMap{{"x", p.x()}, {"y", p.y()}});
            }
        } else {
            auto p = projectPoint(center + basis[k] * length);
            points = {QVariantMap{{"x", origin.x()}, {"y", origin.y()}},
                      QVariantMap{{"x", p.x()}, {"y", p.y()}}};
        }
        axes.append(QVariantMap{{"axis", k},
                                {"color", colors[k]},
                                {"points", points},
                                {"selected", m_gizmoDragging ? m_gizmoAxis == k : m_gizmoHover == k}});
    }
    result["axes"] = axes;
    if (m_gizmoDragging && m_gizmoMode == 1) {
        const bool snap = m_snapEnabled || QGuiApplication::keyboardModifiers().testFlag(Qt::ControlModifier);
        const double step = kind == "asset" ? 90 : snap ? 15 : 1;
        const double delta = std::round(m_gizmoAngle / step) * step;
        const auto u = m_rotationStart, v = QVector3D::crossProduct(basis[m_gizmoAxis], u);
        QVariantList sector, ticks;
        sector.append(QVariantMap{{"x", origin.x()}, {"y", origin.y()}});
        const double angle = std::clamp(delta, -360., 360.) * 3.141592653589793 / 180.;
        for (int n = 0; n <= 64; ++n) {
            double a = angle * n / 64.;
            auto p = projectPoint(center + (u * std::cos(a) + v * std::sin(a)) * length);
            sector.append(QVariantMap{{"x", p.x()}, {"y", p.y()}});
        }
        for (int n = 0; n < 24; ++n) {
            double a = n * 6.283185307179586 / 24;
            auto direction = u * std::cos(a) + v * std::sin(a);
            auto p = projectPoint(center + direction * length);
            auto q = projectPoint(center + direction * length * (n % 6 == 0 ? 1.12 : 1.06));
            ticks.append(QVariantMap{{"x", p.x()}, {"y", p.y()}, {"x2", q.x()}, {"y2", q.y()}});
        }
        result["sector"] = sector;
        result["ticks"] = ticks;
    }
    return result;
}
int Viewport::hitGizmo(QPointF p, const QVariantMap &overlay) const {
    if (!overlay["active"].toBool())
        return -1;
    QPointF center(overlay["cx"].toDouble(), overlay["cy"].toDouble());
    if (m_gizmoMode != 1 && QLineF(p, center).length() < 9)
        return 3;
    for (auto item : overlay["planes"].toList()) {
        auto plane = item.toMap();
        QPolygonF polygon;
        for (auto v : plane["points"].toList()) {
            auto a = v.toMap();
            polygon << QPointF(a["x"].toDouble(), a["y"].toDouble());
        }
        if (polygon.containsPoint(p, Qt::OddEvenFill))
            return plane["axis"].toInt();
    }
    int axis = -1;
    double best = 9;
    for (auto item : overlay["axes"].toList()) {
        auto a = item.toMap();
        auto pts = a["points"].toList();
        for (int n = 1; n < pts.size(); ++n) {
            auto x = pts[n - 1].toMap(), y = pts[n].toMap();
            double distance = distanceToLine(p, {x["x"].toDouble(), x["y"].toDouble()},
                                             {y["x"].toDouble(), y["y"].toDouble()});
            if (distance < best) {
                best = distance;
                axis = a["axis"].toInt();
            }
        }
    }
    return axis;
}
bool Viewport::pressGizmo(QPointF p) {
    if (!m_editor || m_editor->assetBusy() || m_editor->terrainEditing())
        return false;
    auto overlay = gizmoOverlay();
    const int axis = hitGizmo(p, overlay);
    for (auto item : overlay["markers"].toList()) {
        auto m = item.toMap();
        // Transform handles take precedence over badges, including overlapping instances.
        if (axis >= 0)
            break;
        if (QLineF(p, {m["x"].toDouble(), m["y"].toDouble()}).length() < 14) {
            m_editor->setSelectedNode(m["id"].toString());
            m_editor->setTool(-1);
            return true;
        }
    }
    if (axis < 0)
        return false;
    m_gizmoKind.clear();
    if (!creationGizmo(m_gizmoInitial, m_gizmoCenter, m_gizmoKind)) {
        for (const auto &i : m_editor->world().heights.instances)
            if (i.id == m_editor->selectedNode())
                m_gizmoInitial = i;
        m_gizmoCenter = gizmoAnchor(m_gizmoInitial);
    }
    if (m_gizmoKind == "volume")
        for (const auto &v : m_editor->world().creation.volumes)
            if (v.id == m_gizmoInitial.id)
                m_gizmoVolumeSize = v.max - v.min;
    m_gizmoAxis = axis;
    m_gizmoStart = m_gizmoLast = p;
    m_gizmoScreenCenter = {overlay["cx"].toDouble(), overlay["cy"].toDouble()};
    m_gizmoWorldLength = overlay["length"].toDouble();
    m_gizmoAngle = 0;
    m_gizmoReadout.clear();
    const auto basis = gizmoBasis(m_gizmoInitial, m_gizmoMode, gizmoScaleSigns(m_gizmoInitial.id));
    if (axis < 3 && m_gizmoMode != 1)
        m_gizmoScreenAxis =
            projectPoint(m_gizmoCenter + basis[axis] * m_gizmoWorldLength) - m_gizmoScreenCenter;
    if (axis >= 4) {
        const int pairs[3][2] = {{0, 1}, {0, 2}, {1, 2}};
        m_gizmoPlaneU = projectPoint(m_gizmoCenter + basis[pairs[axis - 4][0]] * m_gizmoWorldLength) -
                        m_gizmoScreenCenter;
        m_gizmoPlaneV = projectPoint(m_gizmoCenter + basis[pairs[axis - 4][1]] * m_gizmoWorldLength) -
                        m_gizmoScreenCenter;
    }
    if (m_gizmoMode == 1) {
        m_rotationRay = rotationVector(p, m_rotationStart);
        if (!m_rotationRay)
            m_rotationStart = basis[(axis + 1) % 3];
        m_rotationLast = m_rotationStart;
    }
    if (m_gizmoKind.isEmpty())
        m_editor->beginTerrainEdit();
    else
        m_editor->beginCreationTransform();
    m_gizmoDragging = true;
    moveGizmo(p);
    emit cameraChanged();
    return true;
}
bool Viewport::rotationVector(QPointF p, QVector3D &vector) const {
    bool invertible;
    const auto inverse = viewProjection().inverted(&invertible);
    if (!invertible)
        return false;
    const float x = float(p.x() * 2 / width() - 1), y = float(1 - p.y() * 2 / height());
    auto near = inverse * QVector4D(x, y, -1, 1), far = inverse * QVector4D(x, y, 1, 1);
    if (std::abs(near.w()) < 1e-8 || std::abs(far.w()) < 1e-8)
        return false;
    const auto origin = near.toVector3DAffine(), direction = (far.toVector3DAffine() - origin).normalized();
    const auto axis = gizmoBasis(m_gizmoInitial, 1, gizmoScaleSigns(m_gizmoInitial.id))[m_gizmoAxis];
    const double denominator = QVector3D::dotProduct(direction, axis);
    if (std::abs(denominator) < .025)
        return false;
    const double t = QVector3D::dotProduct(m_gizmoCenter - origin, axis) / denominator;
    vector = origin + direction * t - m_gizmoCenter;
    if (t < 0 || vector.lengthSquared() < .001)
        return false;
    vector.normalize();
    return true;
}
void Viewport::moveGizmo(QPointF p) {
    if (!m_gizmoDragging)
        return;
    const auto d = p - m_gizmoStart;
    const auto &i = m_gizmoInitial;
    QVariantMap changes;
    double projected = QPointF::dotProduct(m_gizmoScreenAxis, m_gizmoScreenAxis);
    double t = projected > 25 ? QPointF::dotProduct(d, m_gizmoScreenAxis) / projected : d.x() / 90.;
    const bool snap = m_snapEnabled || QGuiApplication::keyboardModifiers().testFlag(Qt::ControlModifier);
    auto quantize = [snap](double value, int step) {
        return int(std::lround(value / (snap ? step : 1))) * (snap ? step : 1);
    };
    if (m_gizmoMode == 0) {
        if (m_gizmoAxis >= 4) {
            double det = m_gizmoPlaneU.x() * m_gizmoPlaneV.y() - m_gizmoPlaneU.y() * m_gizmoPlaneV.x();
            if (std::abs(det) < 1)
                return;
            double u = (d.x() * m_gizmoPlaneV.y() - d.y() * m_gizmoPlaneV.x()) / det * m_gizmoWorldLength;
            double v = (m_gizmoPlaneU.x() * d.y() - m_gizmoPlaneU.y() * d.x()) / det * m_gizmoWorldLength;
            const int pairs[3][2] = {{0, 1}, {0, 2}, {1, 2}};
            const QString keys[3] = {"x", "y", "z"};
            const int start[3] = {i.x, i.y, i.z};
            for (int n = 0; n < 2; ++n) {
                int axis = pairs[m_gizmoAxis - 4][n], limit = axis == 1 ? 10000 : 2000000;
                changes[keys[axis]] = std::clamp(start[axis] + quantize(n ? v : u, 4), -limit, limit);
            }
        } else if (m_gizmoAxis == 3) {
            const auto r = right();
            const double units = m_gizmoWorldLength / 90.;
            auto forwardFlat = QVector3D(forward().x(), 0, forward().z()).normalized();
            auto move = (r * float(d.x()) - forwardFlat * float(d.y())) * units;
            changes = {{"x", std::clamp(i.x + quantize(move.x(), 4), -2000000, 2000000)},
                       {"z", std::clamp(i.z + quantize(move.z(), 4), -2000000, 2000000)}};
        } else {
            const QString keys[3] = {"x", "y", "z"};
            int starts[3] = {i.x, i.y, i.z};
            int limit = m_gizmoAxis == 1 ? 10000 : 2000000;
            changes[keys[m_gizmoAxis]] =
                std::clamp(starts[m_gizmoAxis] + quantize(t * m_gizmoWorldLength, 4), -limit, limit);
        }
    } else if (m_gizmoMode == 1) {
        const int start[3] = {i.rotationX, i.rotationY, i.rotationZ};
        const QString keys[3] = {"rotationX", "rotationY", "rotationZ"};
        const auto axis = gizmoBasis(i, 1, gizmoScaleSigns(i.id))[m_gizmoAxis];
        if (m_rotationRay) {
            QVector3D current;
            if (!rotationVector(p, current))
                return;
            m_gizmoAngle +=
                std::atan2(QVector3D::dotProduct(axis, QVector3D::crossProduct(m_rotationLast, current)),
                           QVector3D::dotProduct(m_rotationLast, current)) *
                180 / 3.141592653589793;
            m_rotationLast = current;
        } else {
            auto a = m_gizmoLast - m_gizmoScreenCenter, b = p - m_gizmoScreenCenter;
            const double angle = std::atan2(a.x() * b.y() - a.y() * b.x(), QPointF::dotProduct(a, b)) * 180 /
                                 3.141592653589793;
            m_gizmoAngle += angle * (QVector3D::dotProduct(m_camera - m_gizmoCenter, axis) >= 0 ? -1 : 1);
        }
        int value = start[m_gizmoAxis] + quantize(m_gizmoAngle, 15);
        while (value > 180)
            value -= 360;
        while (value < -180)
            value += 360;
        changes[keys[m_gizmoAxis]] = value;
    } else {
        const int start[3] = {i.scaleX, i.scaleY, i.scaleZ};
        const QString keys[3] = {"scaleX", "scaleY", "scaleZ"};
        auto proportional = [&](std::initializer_list<int> axes, double factor) {
            // Snap the shared multiplier, not each component: preserve an existing unequal ratio.
            if (snap)
                factor = std::round(factor * 10) / 10;
            double minimum = 0, maximum = 1600;
            for (int k : axes) {
                minimum = std::max(minimum, 1. / start[k]);
                maximum = std::min(maximum, 1600. / start[k]);
            }
            factor = std::clamp(factor, minimum, maximum);
            for (int k : axes)
                changes[keys[k]] = int(std::lround(start[k] * factor));
        };
        if (m_gizmoAxis == 3)
            proportional({0, 1, 2}, 1 + d.x() / 120);
        else if (m_gizmoAxis >= 4) {
            const auto diagonal = m_gizmoPlaneU + m_gizmoPlaneV;
            const auto length2 = QPointF::dotProduct(diagonal, diagonal);
            const double amount = length2 > 25 ? QPointF::dotProduct(d, diagonal) / length2 : d.x() / 120;
            proportional({planeAxes[m_gizmoAxis - 4][0], planeAxes[m_gizmoAxis - 4][1]}, 1 + amount);
        } else
            changes[keys[m_gizmoAxis]] = std::clamp(quantize(start[m_gizmoAxis] * (1 + t), 10), 1, 1600);
    }
    QVariantMap now;
    if (m_gizmoKind.isEmpty()) {
        m_editor->setTerrainOptions(changes);
        now = m_editor->terrainDetails();
    } else {
        QVariantMap delta;
        const QStringList positionKeys{"x", "y", "z"}, scaleKeys{"scaleX", "scaleY", "scaleZ"};
        const int positions[3] = {i.x, i.y, i.z}, scales[3] = {i.scaleX, i.scaleY, i.scaleZ};
        for (int k = 0; k < 3; ++k) {
            if (changes.contains(positionKeys[k]))
                delta[positionKeys[k]] = changes[positionKeys[k]].toInt() - positions[k];
            if (changes.contains(scaleKeys[k]))
                delta[scaleKeys[k]] = changes[scaleKeys[k]].toDouble() / scales[k];
        }
        if (changes.contains("rotationY"))
            delta["rotationY"] = -(changes["rotationY"].toInt() - i.rotationY);
        m_editor->previewCreationTransform(delta);
        wb::TerrainInstance current;
        QVector3D anchor;
        QString kind;
        creationGizmo(current, anchor, kind);
        now = {{"x", current.x},           {"y", current.y},           {"z", current.z},
               {"scaleX", current.scaleX}, {"scaleY", current.scaleY}, {"scaleZ", current.scaleZ}};
        // Report the accepted dimensions, including clamping or rejected generation.
        if (m_gizmoKind == "volume" && m_gizmoMode == 2)
            for (const auto &v : m_editor->world().creation.volumes)
                if (v.id == i.id)
                    for (int k = 0; k < 3; ++k)
                        now[scaleKeys[k]] =
                            int(std::lround((v.max[k] - v.min[k]) / m_gizmoVolumeSize[k] * 100));
    }
    m_gizmoLast = p;
    QStringList readout;
    const QStringList labels{"X", "Y", "Z"};
    if (m_gizmoMode == 1) {
        int delta =
            m_gizmoKind == "asset" ? -int(std::round(m_gizmoAngle / 90) * 90) : quantize(m_gizmoAngle, 15);
        if (delta)
            readout << QString("%1  %2%3°").arg(labels[m_gizmoAxis], delta > 0 ? "+" : "").arg(delta);
    } else {
        const QStringList keys =
            m_gizmoMode == 0 ? QStringList{"x", "y", "z"} : QStringList{"scaleX", "scaleY", "scaleZ"};
        const int start[3] = {m_gizmoMode == 0 ? i.x : i.scaleX, m_gizmoMode == 0 ? i.y : i.scaleY,
                              m_gizmoMode == 0 ? i.z : i.scaleZ};
        for (int k = 0; k < 3; ++k) {
            const int value = now[keys[k]].toInt(), delta = value - start[k];
            if (delta)
                readout << (m_gizmoMode == 0
                                ? QString("%1  %2%3 格").arg(labels[k], delta > 0 ? "+" : "").arg(delta)
                                : QString("%1  %2%").arg(labels[k]).arg(value));
        }
    }
    m_gizmoReadout = readout.join("   ");
    emit cameraChanged();
}
void Viewport::finishGizmo(bool cancel) {
    if (!m_gizmoDragging)
        return;
    m_gizmoDragging = false;
    m_gizmoReadout.clear();
    m_gizmoHover = -1;
    if (m_gizmoKind.isEmpty())
        m_editor->endTerrainEdit(cancel);
    else
        m_editor->endCreationTransform(cancel);
    m_gizmoKind.clear();
    emit cameraChanged();
}
