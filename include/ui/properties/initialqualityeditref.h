/*!
 * \file   initialqualityeditref.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Initial-quality UI round — compound-attribute cell value for the
 * per-element "Initial Quality" row in the Property Browser. Mirrors
 * UserFlagsEditRef: carries what the cell button needs to (a) display
 * a short summary of how many [INITIAL_QUALITY] overrides the element
 * holds and (b) open InitialQualityDialog scoped to that element.
 *
 * Registered as a Qt metatype with a QString converter so the read-only
 * DisplayRole path renders `summary`.
 */

#ifndef INITIALQUALITYEDITREF_H
#define INITIALQUALITYEDITREF_H

#include <QMetaType>
#include <QString>

#include <openswmm/engine/openswmm_callbacks.h>  // SWMM_Engine typedef

/*! Identifies the [INITIAL_QUALITY] overrides of a single node or link. */
struct InitialQualityEditRef
{
    SWMM_Engine engine = nullptr;  ///< Engine handle (borrow, not owned)
    int         isLink = 0;        ///< 0 = node scope, 1 = link scope
    QString     elementName;       ///< Owning element id (case-preserved)
    QString     summary;           ///< Short text shown in the cell

    bool operator==(const InitialQualityEditRef &other) const noexcept
    {
        return engine == other.engine && isLink == other.isLink
               && elementName == other.elementName
               && summary == other.summary;
    }
};

Q_DECLARE_METATYPE(InitialQualityEditRef)

/*! Compute the cell summary for one element: "(none)" or "n set".
 *  Null engine / unknown element reads as no overrides. */
QString initialQualitySummaryFor(SWMM_Engine engine, int isLink,
                                 const QString &elementName);

/*! Install the `InitialQualityEditRef → QString` metatype converter so
 *  the read-only DisplayRole path renders `summary`. Idempotent. */
void registerInitialQualityEditRefConverter();

#endif // INITIALQUALITYEDITREF_H
