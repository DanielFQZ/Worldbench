#pragma once
#include <QImage>
#include <QJsonObject>
#include <QMap>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>
#include <functional>
#include <memory>
#include <vector>

namespace wb {
struct BlockState {
    QString name;
    QMap<QString, QString> properties;
    bool operator==(const BlockState &) const = default;
    QString label() const;
};
struct ModelFace {
    QVector3D points[4];
    QVector2D uv[4];
    QVector3D normal;
    QVector4D color{1, 1, 1, 1};
    int cull = -1;
};
struct BlockModel {
    std::vector<ModelFace> faces;
    bool full = false, translucent = false, fallback = false;
    bool leafCube = false;
    bool hidesFace(const BlockModel &neighbor, bool sameState) const {
        return (neighbor.full && !neighbor.translucent) || (leafCube && neighbor.leafCube) ||
               (sameState && translucent);
    }
    QString thumbnail, diagnostic;
};
struct CatalogBlock {
    QString name, title;
    QStringList searchTerms;
    QString thumbnailData;
    std::shared_ptr<const BlockModel> preview;
    QMap<QString, QStringList> options;
    BlockState defaults;
};
class AssetLibrary {
  public:
    QString version;
    QStringList sources;
    QMap<QString, CatalogBlock> catalog;
    QImage atlas;
    QMap<QString, QImage> thumbnails;
    QStringList warnings;
    int skippedEntries = 0, unsupportedEntries = 0;
    using Progress = std::function<void(int, const QString &)>;
    static std::shared_ptr<AssetLibrary> load(const QStringList &paths, QString &error,
                                              Progress progress = {});
    std::shared_ptr<const BlockModel> model(const BlockState &state) const;
    bool validate(const BlockState &state, QString &error) const;

  private:
    QMap<QString, QJsonObject> m_models, m_states;
    QMap<QString, QRectF> m_uv;
    QJsonObject inherited(const QString &name, QStringList chain = {}) const;
};
std::shared_ptr<const BlockModel> fallbackModel(int builtin);
QImage whiteAtlas();
} // namespace wb
