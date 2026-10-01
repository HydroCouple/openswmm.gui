#ifndef OPENSWMMVIS_IO_MESH2DRESULTVARIABLE_H
#define OPENSWMMVIS_IO_MESH2DRESULTVARIABLE_H

#include <QString>
#include <QUrl>
#include <cstdint>

namespace openswmmvis::io {

// File/source-local semantic identity. The owning results layer supplies the
// run/source identity; species indexes are deliberately never persisted here.
struct Mesh2DResultVariable {
    enum class Domain { Surface, Groundwater };
    enum class Zone { None, Saturated, Unsaturated, Sigma };
    enum class Temporal { Reported, Held, Static, Envelope };

    QString dataset;
    QString species;
    QString label;
    QString units; // empty when the file does not establish the actual units
    Domain domain = Domain::Surface;
    Zone zone = Zone::None;
    Temporal temporal = Temporal::Reported;
    int layer = -1; // explicit sigma-layer coordinate, not a species row
    int frameCount = 0; // 0 for time-independent static/envelope variables
    bool unitsKnown = false;

    QString key() const {
        if (dataset.isEmpty()) return {};
        QString result = (domain == Domain::Groundwater ? QStringLiteral("groundwater:")
                                                        : QStringLiteral("surface:"))
            + QString::fromLatin1(QUrl::toPercentEncoding(dataset));
        if (!species.isEmpty())
            result += QStringLiteral(":species:") + QString::fromLatin1(QUrl::toPercentEncoding(species));
        if (layer >= 0) result += QStringLiteral(":layer:") + QString::number(layer);
        return result;
    }
};

// Keep a physical zero distinct from unavailable output and waterless zones.
// Raw waterless/not-applicable values are retained; Missing values become NaN.
enum class Mesh2DValueStatus : std::uint8_t { Valid, Missing, Waterless, NotApplicable };

} // namespace openswmmvis::io
#endif
