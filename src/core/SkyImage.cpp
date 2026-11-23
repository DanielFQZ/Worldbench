#include "SkyImage.h"
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QtZlib/zlib.h>
#define TINYEXR_USE_MINIZ 0
#define TINYEXR_USE_THREAD 0
#define TINYEXR_IMPLEMENTATION
#include "../third_party/tinyexr.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 16384
#include "../third_party/stb_image.h"
#include <cmath>
namespace wb {
QImage loadSkyImage(const QString &path, QString &error) {
    error.clear();
    if (path.isEmpty())
        return {};
    QFile f(path);
    if (f.size() > 256 * 1024 * 1024 || !f.open(QIODevice::ReadOnly)) {
        error = "天空图不可读或超过256MiB";
        return {};
    }
    const auto bytes = f.readAll();
    const auto *data = reinterpret_cast<const unsigned char *>(bytes.constData());
    float *pixels = nullptr;
    int w = 0, h = 0, channels = 0;
    const bool exr = QFileInfo(path).suffix().compare("exr", Qt::CaseInsensitive) == 0;
    if (exr) {
        EXRVersion version;
        EXRHeader header;
        InitEXRHeader(&header);
        const char *msg = nullptr;
        if (ParseEXRVersionFromMemory(&version, data, bytes.size()) != 0 || version.multipart ||
            version.non_image || ParseEXRHeaderFromMemory(&header, &version, data, bytes.size(), &msg) != 0) {
            error = msg ? QString::fromUtf8(msg) : "不支持的EXR（需单部分RGB/RGBA，非Deep）";
            if (msg)
                FreeEXRErrorMessage(msg);
            FreeEXRHeader(&header);
            return {};
        }
        w = header.data_window.max_x - header.data_window.min_x + 1;
        h = header.data_window.max_y - header.data_window.min_y + 1;
        FreeEXRHeader(&header);
        if (w < 1 || h < 1 || w > 16384 || h > 8192 || qint64(w) * h > 32 * 1024 * 1024) {
            error = "天空图尺寸超出32M像素预算";
            return {};
        }
        if (LoadEXRFromMemory(&pixels, &w, &h, data, bytes.size(), &msg) != 0) {
            error = msg ? QString::fromUtf8(msg) : "EXR解码失败";
            if (msg)
                FreeEXRErrorMessage(msg);
            return {};
        }
    } else {
        if (stbi_info_from_memory(data, int(bytes.size()), &w, &h, &channels) && w > 0 && h > 0 &&
            qint64(w) * h <= 32 * 1024 * 1024)
            pixels = stbi_loadf_from_memory(data, int(bytes.size()), &w, &h, &channels, 4);
    }
    if (pixels) {
        if (w != 2 * h) {
            free(pixels);
            error = "天空图需要2:1经纬度投影";
            return {};
        }
        QImage image(w, h, QImage::Format_RGBA32FPx4);
        auto *dst = reinterpret_cast<float *>(image.bits());
        for (qint64 i = 0; i < qint64(w) * h * 4; ++i)
            dst[i] =
                i % 4 == 3 ? 1.f : (std::isfinite(pixels[i]) ? std::clamp(pixels[i], 0.f, 65504.f) : 0.f);
        free(pixels);
        return image;
    }
    QImageReader reader(path);
    const auto size = reader.size();
    if (!size.isValid() || qint64(size.width()) * size.height() > 32 * 1024 * 1024 ||
        size.width() != size.height() * 2) {
        error = "不支持的天空格式/尺寸；支持HDR、EXR、PNG、JPG、TGA、BMP";
        return {};
    }
    auto image = reader.read().convertToFormat(QImage::Format_RGBA32FPx4);
    if (image.isNull())
        error = reader.errorString();
    return image;
}
} // namespace wb
