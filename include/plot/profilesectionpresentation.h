/**
 * @file      profilesectionpresentation.h
 * @brief     Presentation of profile section fields, extrema, and report times.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef PROFILE_SECTION_PRESENTATION_H
#define PROFILE_SECTION_PRESENTATION_H
#include "plot/profilesectionseries.h"
#include <QObject>

namespace ProfileSection
{
// Static fields and extrema intentionally have no effective report time.
// A read/source error must never be presented as a valid static quantity.
/**
 * @brief Describe a series's error state and temporal provenance for a profile legend.
 * @param[in] series Sampled series metadata; no sample/source reads occur.
 * @return Translated Unavailable on error, Static/Whole-run maximum for independent fields, otherwise effectiveTime in ISO format or No report available. Error state takes priority over temporal kind.
 */
inline QString effectiveTimeLabel(const SampledSeries &series)
{
    using Temporal = openswmmvis::io::Mesh2DResultVariable::Temporal;
    if (!series.error.isEmpty())
        return QObject::tr("Unavailable");
    if (series.descriptor.temporal == Temporal::Static)
        return QObject::tr("Static");
    if (series.descriptor.temporal == Temporal::Envelope)
        return QObject::tr("Whole-run maximum");
    return series.effectiveTime.isValid() ? series.effectiveTime.toString(Qt::ISODate)
                                          : QObject::tr("No report available");
}
/**
 * @brief Compose an accessible label with units, effective-time meaning and any error.
 * @param[in] series Sampled result metadata. Definition label takes priority over descriptor label.
 * @return Independent display text using units unknown when unitsKnown is false and appending error text verbatim when present.
 */
inline QString accessibleSeriesSummary(const SampledSeries &series)
{
    const QString label =
        series.definition.label.isEmpty() ? series.descriptor.label : series.definition.label;
    const QString units = series.unitsKnown ? series.units : QObject::tr("units unknown");
    QString text = QStringLiteral("%1 [%2] — %3").arg(label, units, effectiveTimeLabel(series));
    if (!series.error.isEmpty())
        text += QStringLiteral(": ") + series.error;
    return text;
}
}
#endif
