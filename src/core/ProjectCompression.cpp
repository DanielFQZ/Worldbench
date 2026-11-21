#include "ProjectFormat.h"
#include <QtEndian>
#include <QtZlib/zlib.h>

namespace wb {
bool unpackProjectBlock(const QByteArray &packed, qsizetype limit, QByteArray &raw) {
    raw.clear();
    if (packed.size() < 5)
        return false;
    const auto size = qFromBigEndian<uint32_t>(packed.constData());
    if (!size || size > uint64_t(limit))
        return false;
    QByteArray output(size, Qt::Uninitialized);
    uLongf available = size;
    uLong consumed = uLong(packed.size() - 4);
    const int status = uncompress2(reinterpret_cast<Bytef *>(output.data()), &available,
                                   reinterpret_cast<const Bytef *>(packed.constData() + 4), &consumed);
    if (status != Z_OK || available != size || consumed != uLong(packed.size() - 4))
        return false;
    raw = std::move(output);
    return true;
}
} // namespace wb
