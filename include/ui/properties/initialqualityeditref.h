/**
 * @file      initialqualityeditref.h
 * @brief     Initial-quality UI round — compound-attribute cell value for the per-element "Initial
 *            Quality" row in the Property Browser.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Initial-quality UI round — compound-attribute cell value for the
 * per-element "Initial Quality" row in the Property Browser. Mirrors
 * UserFlagsEditRef: carries what the cell button needs to (a) display
 * a short summary of how many [INITIAL_QUALITY] overrides the element
 * holds and (b) open InitialQualityDialog scoped to that element.
 *
 * Registered as a Qt metatype with a QString converter so the read-only
 * DisplayRole path renders `summary`.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef INITIALQUALITYEDITREF_H
#define INITIALQUALITYEDITREF_H

#include <QMetaType>
#include <QString>

#include <openswmm/engine/openswmm_callbacks.h> // SWMM_Engine typedef

/*! Identifies the [INITIAL_QUALITY] overrides of a single node or link. */
struct InitialQualityEditRef {
    SWMM_Engine engine = nullptr; ///< Engine handle (borrow, not owned)
    int isLink = 0;               ///< 0 = node scope, 1 = link scope
    QString elementName;          ///< Owning element id (case-preserved)
    QString summary;              ///< Short text shown in the cell
    bool wroteChanges = false;    ///< Editor has applied changes, even if it was cancelled

    /**
     * @brief Compare reference identity and every cached display/value field.
     * @param[in] other Reference to compare without dereferencing borrowed engine/model pointers.
     * @return True only when all stored fields match; equality does not verify current engine state.
     */
    bool operator==(const InitialQualityEditRef &other) const noexcept
    {
        return engine == other.engine && isLink == other.isLink &&
               elementName == other.elementName && summary == other.summary &&
               wroteChanges == other.wroteChanges;
    }
};

Q_DECLARE_METATYPE(InitialQualityEditRef)

/*! Compute the cell summary for one element: "(none)" or "n set".
 *  Null engine / unknown element reads as no overrides.
 * @param[in] engine Borrowed engine; nullptr returns the no-overrides label.
 * @param[in] isLink Zero selects node records, nonzero selects link records.
 * @param[in] elementName Exact element ID used to resolve the scoped index.
 * @return Count summary for explicit initial-quality overrides; unknown elements/read failures appear as no overrides.
 */
QString initialQualitySummaryFor(SWMM_Engine engine, int isLink, const QString &elementName);

/*! Install the `InitialQualityEditRef → QString` metatype converter so
 *  the read-only DisplayRole path renders `summary`. Idempotent.
 * @details Installs the Qt value-to-QString converter once using a local registration flag. Call during GUI startup before concurrent use; no engine ownership is acquired.
 */
void registerInitialQualityEditRefConverter();

#endif // INITIALQUALITYEDITREF_H
