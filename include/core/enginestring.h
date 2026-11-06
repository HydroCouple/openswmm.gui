#pragma once
#include <QByteArray>
#include <QString>
#include <cstring>

namespace openswmmvis {
// Older engine getters return success even when the buffer was too short.
// A full buffer is ambiguous, so retry until the complete terminator fits.
template <class Getter> QString readEngineString(Getter get, const QString& fallback = {})
{
    for (int size = 256; size <= 64 * 1024 * 1024; size *= 2) {
        QByteArray bytes(size, '\0');
        if (get(bytes.data(), size) != 0)
            return fallback;
        if (std::strlen(bytes.constData()) < static_cast<std::size_t>(size - 1))
            return QString::fromUtf8(bytes.constData());
    }
    return fallback;
}
}
