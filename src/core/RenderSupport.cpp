#include "RenderSupport.h"
#include <QMatrix4x4>
#include <algorithm>
#include <array>

namespace wb {
QVector4D surfaceColor(const World &w, Block block) {
    const auto &model = w.models.at(block);
    QVector4D color(.5f, .5f, .5f, 1);
    const ModelFace *face = nullptr;
    for (const auto &f : model->faces)
        if (!face || f.normal.y() > face->normal.y())
            face = &f;
    if (face) {
        color = face->color;
        if (w.assets) {
            const auto &atlas = w.assets->atlas;
            QVector4D sum;
            float weight = 0;
            // The loaded atlas uses 32px tiles. Sample its complete face,
            // rather than 16 points which can alias patterned resource packs.
            const int samples = 32;
            for (int z = 0; z < samples; ++z)
                for (int x = 0; x < samples; ++x) {
                    auto uv = face->uv[0] + (face->uv[1] - face->uv[0]) * ((x + .5f) / samples) +
                              (face->uv[3] - face->uv[0]) * ((z + .5f) / samples);
                    auto p =
                        atlas.pixelColor(std::clamp(int(uv.x() * atlas.width()), 0, atlas.width() - 1),
                                         std::clamp(int(uv.y() * atlas.height()), 0, atlas.height() - 1));
                    sum += QVector4D(p.redF(), p.greenF(), p.blueF(), 1) * p.alphaF();
                    weight += p.alphaF();
                }
            if (weight > 0)
                color *= sum / weight;
        }
    }
    return color;
}
SurfaceMaterials surfaceMaterials(const World &w) {
    SurfaceMaterials result;
    for (Block block = 0; block < w.models.size(); ++block)
        result.colors.push_back(surfaceColor(w, block));
    return result;
}
bool visibleMesh(const ChunkMesh &m, const QMatrix4x4 &matrix) {
    // Canonical OpenGL clip space, before the backend correction matrix.
    for (int axis = 0; axis < 3; ++axis)
        for (int sign : {-1, 1}) {
            QVector4D plane = matrix.row(3) + matrix.row(axis) * float(sign);
            QVector3D p(plane.x() >= 0 ? m.upper.x() : m.lower.x(),
                        plane.y() >= 0 ? m.upper.y() : m.lower.y(),
                        plane.z() >= 0 ? m.upper.z() : m.lower.z());
            if (QVector3D::dotProduct(plane.toVector3D(), p) + plane.w() < 0)
                return false;
        }
    return true;
}
} // namespace wb
