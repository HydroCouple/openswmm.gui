#ifndef PROFILE_SECTION_PRESENTATION_H
#define PROFILE_SECTION_PRESENTATION_H
#include "plot/profilesectionseries.h"
#include <QObject>

namespace ProfileSection {
// Static fields and extrema intentionally have no effective report time.
// A read/source error must never be presented as a valid static quantity.
inline QString effectiveTimeLabel(const SampledSeries &series)
{
    using Temporal = openswmmvis::io::Mesh2DResultVariable::Temporal;
    if (!series.error.isEmpty()) return QObject::tr("Unavailable");
    if (series.descriptor.temporal == Temporal::Static) return QObject::tr("Static");
    if (series.descriptor.temporal == Temporal::Envelope) return QObject::tr("Whole-run maximum");
    return series.effectiveTime.isValid() ? series.effectiveTime.toString(Qt::ISODate)
                                         : QObject::tr("No report available");
}
inline QString accessibleSeriesSummary(const SampledSeries &series)
{
    const QString label = series.definition.label.isEmpty() ? series.descriptor.label : series.definition.label;
    const QString units = series.unitsKnown ? series.units : QObject::tr("units unknown");
    QString text = QStringLiteral("%1 [%2] — %3").arg(label, units, effectiveTimeLabel(series));
    if (!series.error.isEmpty()) text += QStringLiteral(": ") + series.error;
    return text;
}
}
#endif
