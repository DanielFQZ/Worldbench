#include "Assets.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <cmath>

namespace wb {
namespace {
// Bump when atlas packing, parsing, bundled translations, or model semantics change.
constexpr int CacheSchema = 1;
constexpr qint64 MaxMetadata = 32 * 1024 * 1024;
constexpr qint64 MaxAtlas = 4096ll * 4096 * 4;
constexpr qint64 HeaderBytes = 8 + 12 + 32;
QByteArray signature(const QStringList &paths) {
    if (paths.isEmpty() || paths.size() > 8)
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArray::number(CacheSchema));
    for (const auto &path : paths) {
        QFileInfo info(path);
        QFile file(path);
        if (!info.isFile() || info.size() > 512ll * 1024 * 1024 || !file.open(QIODevice::ReadOnly))
            return {};
        // Content hash also catches replacements preserving both timestamp and size.
        QCryptographicHash content(QCryptographicHash::Sha256);
        if (!content.addData(&file))
            return {};
        const QJsonArray identity{info.canonicalFilePath(), double(info.size()),
                                 double(info.lastModified().toMSecsSinceEpoch()),
                                 QString::fromLatin1(content.result().toHex())};
        hash.addData(QJsonDocument(identity).toJson(QJsonDocument::Compact));
    }
    return hash.result().toHex();
}
QJsonObject objects(const QMap<QString, QJsonObject> &map) {
    QJsonObject result;
    for (auto i = map.cbegin(); i != map.cend(); ++i)
        result[i.key()] = i.value();
    return result;
}
QJsonObject strings(const QMap<QString, QString> &map) {
    QJsonObject result;
    for (auto i = map.cbegin(); i != map.cend(); ++i)
        result[i.key()] = i.value();
    return result;
}
void pruneCache(const QString &directory, const QString &current) {
    const auto files = QDir(directory).entryInfoList({"*.wbassets"}, QDir::Files, QDir::Time);
    qint64 bytes = QFileInfo(current).size();
    int retained = 1;
    for (const auto &file : files) {
        if (file.absoluteFilePath() == current || file.completeBaseName().size() != 64 ||
            QByteArray::fromHex(file.completeBaseName().toLatin1()).toHex() != file.completeBaseName().toLatin1())
            continue;
        if (retained >= 4 || bytes + file.size() > 512ll * 1024 * 1024)
            QFile::remove(file.absoluteFilePath());
        else {
            bytes += file.size();
            ++retained;
        }
    }
}
} // namespace

bool AssetLibrary::writeCache(const QString &file, const QByteArray &key) const {
    QJsonObject uv, entries;
    for (auto i = m_uv.cbegin(); i != m_uv.cend(); ++i)
        uv[i.key()] = QJsonArray{i->x(), i->y(), i->width(), i->height()};
    for (auto i = catalog.cbegin(); i != catalog.cend(); ++i) {
        QJsonObject options;
        for (auto p = i->options.cbegin(); p != i->options.cend(); ++p)
            options[p.key()] = QJsonArray::fromStringList(p.value());
        entries[i.key()] = QJsonObject{{"title", i->title}, {"search", QJsonArray::fromStringList(i->searchTerms)},
                                      {"thumbnail", i->thumbnailData}, {"options", options},
                                      {"defaults", strings(i->defaults.properties)}};
    }
    const auto metadata = QJsonDocument(QJsonObject{
        {"schema", CacheSchema}, {"key", QString::fromLatin1(key)}, {"version", version},
        {"models", objects(m_models)}, {"states", objects(m_states)}, {"uv", uv}, {"catalog", entries},
        {"warnings", QJsonArray::fromStringList(warnings)}, {"skipped", skippedEntries},
        {"unsupported", unsupportedEntries}}).toJson(QJsonDocument::Compact);
    const auto pixels = atlas.convertToFormat(QImage::Format_RGBA8888);
    if (metadata.size() > MaxMetadata || pixels.isNull() || pixels.sizeInBytes() > MaxAtlas)
        return false;
    const QByteArrayView raw(reinterpret_cast<const char *>(pixels.constBits()), pixels.sizeInBytes());
    QCryptographicHash digest(QCryptographicHash::Sha256);
    digest.addData(metadata);
    digest.addData(raw);
    QSaveFile output(file);
    if (!output.open(QIODevice::WriteOnly))
        return false;
    QDataStream stream(&output);
    stream.writeRawData("WBASSET1", 8);
    stream << quint32(metadata.size()) << quint32(pixels.width()) << quint32(pixels.height());
    stream.writeRawData(digest.result().constData(), 32);
    if (output.write(metadata) != metadata.size() || output.write(raw.data(), raw.size()) != raw.size())
        return false;
    return stream.status() == QDataStream::Ok && output.commit();
}

std::shared_ptr<AssetLibrary> AssetLibrary::readCache(const QString &file, const QByteArray &key) {
    QFile input(file);
    if (!input.open(QIODevice::ReadOnly) || input.size() < HeaderBytes ||
        input.size() > HeaderBytes + MaxMetadata + MaxAtlas)
        return {};
    QDataStream stream(&input);
    char magic[8], checksum[32];
    quint32 size = 0, width = 0, height = 0;
    stream.readRawData(magic, 8);
    stream >> size >> width >> height;
    stream.readRawData(checksum, 32);
    if (stream.status() != QDataStream::Ok || QByteArrayView(magic, 8) != QByteArrayView("WBASSET1", 8) ||
        size == 0 || size > MaxMetadata || width < 32 || width > 4096 || width != height ||
        (width & (width - 1)) || input.size() != HeaderBytes + size + qint64(width) * height * 4)
        return {};
    const auto metadata = input.read(size);
    const auto raw = input.readAll();
    QCryptographicHash digest(QCryptographicHash::Sha256);
    digest.addData(metadata);
    digest.addData(raw);
    if (digest.result() != QByteArray(checksum, 32))
        return {};
    const auto doc = QJsonDocument::fromJson(metadata);
    const auto root = doc.object();
    if (!doc.isObject() || root["schema"].toInt() != CacheSchema || root["key"].toString().toLatin1() != key)
        return {};
    for (const auto *name : {"models", "states", "uv", "catalog"})
        if (!root[name].isObject() || root[name].toObject().size() > 100000)
            return {};
    auto result = std::make_shared<AssetLibrary>();
    result->atlas = QImage(reinterpret_cast<const uchar *>(raw.constData()), width, height,
                          QImage::Format_RGBA8888).copy();
    if (result->atlas.isNull())
        return {};
    result->version = root["version"].toString();
    result->skippedEntries = root["skipped"].toInt();
    result->unsupportedEntries = root["unsupported"].toInt();
    for (const auto &warning : root["warnings"].toArray())
        result->warnings << warning.toString();
    const auto models = root["models"].toObject(), states = root["states"].toObject();
    for (auto i = models.begin(); i != models.end(); ++i)
        result->m_models[i.key()] = i.value().toObject();
    for (auto i = states.begin(); i != states.end(); ++i)
        result->m_states[i.key()] = i.value().toObject();
    const auto uv = root["uv"].toObject();
    if (uv.size() > 6000)
        return {};
    for (auto i = uv.begin(); i != uv.end(); ++i) {
        const auto a = i.value().toArray();
        if (a.size() != 4)
            return {};
        QRectF rect(a[0].toDouble(-1), a[1].toDouble(-1), a[2].toDouble(-1), a[3].toDouble(-1));
        const int x = qRound(rect.x() * width - .01), y = qRound(rect.y() * height - .01);
        if (x < 0 || y < 0 || x + 32 > int(width) || y + 32 > int(height) ||
            std::abs(rect.width() * width - 31.98) > .001 ||
            std::abs(rect.height() * height - 31.98) > .001)
            return {};
        result->m_uv[i.key()] = rect;
        result->thumbnails[i.key()] = result->atlas.copy(x, y, 32, 32);
    }
    const auto entries = root["catalog"].toObject();
    for (auto i = entries.begin(); i != entries.end(); ++i) {
        if (!i.value().isObject())
            return {};
        auto &entry = result->catalog[i.key()];
        entry.name = entry.defaults.name = i.key();
        const auto value = i.value().toObject();
        entry.title = value["title"].toString();
        entry.thumbnailData = value["thumbnail"].toString();
        for (const auto &term : value["search"].toArray())
            entry.searchTerms << term.toString();
        const auto options = value["options"].toObject(), defaults = value["defaults"].toObject();
        for (auto p = options.begin(); p != options.end(); ++p)
            for (const auto &v : p.value().toArray())
                entry.options[p.key()] << v.toString();
        for (auto p = defaults.begin(); p != defaults.end(); ++p)
            entry.defaults.properties[p.key()] = p.value().toString();
    }
    for (auto i = result->catalog.begin(); i != result->catalog.end(); ++i)
        i->preview = result->model(i->defaults);
    result->restoredFromDiskCache = true;
    return result;
}

std::shared_ptr<AssetLibrary> AssetLibrary::loadCached(const QStringList &paths, QString &error,
                                                     Progress progress, QString directory) {
    error.clear();
    auto report = [&](int percent, const QString &message) {
        if (progress)
            progress(percent, message);
    };
    report(0, "校验资源来源与本地缓存");
    if (directory.isEmpty()) {
        directory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        if (directory.isEmpty())
            return load(paths, error, progress);
        directory += "/minecraft-resources";
    }
    const auto key = signature(paths);
    const auto file = QDir(directory).absoluteFilePath(QString::fromLatin1(key) + ".wbassets");
    if (!key.isEmpty())
        if (auto cached = readCache(file, key)) {
            cached->sources = paths;
            report(100, "已恢复本地资源缓存；无需重新解压 JAR/ZIP");
            return cached;
        }
    report(1, "缓存缺失或失效，重新解析资源");
    auto result = load(paths, error, [&](int p, const QString &m) { report(1 + p * 94 / 100, m); });
    if (result && !key.isEmpty() && key == signature(paths)) {
        report(96, "保存本地资源缓存");
        if (QDir().mkpath(directory) && result->writeCache(file, key))
            pruneCache(directory, file);
        else
            report(98, "缓存无法写入；本次资源仍可正常使用");
    }
    if (result)
        report(100, "资源已就绪");
    return result;
}
} // namespace wb
