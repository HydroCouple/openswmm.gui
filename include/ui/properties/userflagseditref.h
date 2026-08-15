/*!
 * \file   userflagseditref.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Phase 4 of docs/USER_FLAGS_UI_PLAN_2026-06-03.md — compound-attribute
 * cell value for the per-object "User Flags" row in the Property
 * Browser. Mirrors NodeCompoundEditRef: carries what the cell button
 * needs to (a) display a short summary of how many flags are set on the
 * object and (b) open UserFlagValuesDialog to edit the assignments.
 *
 * Registered as a Qt metatype with a QString converter so the read-only
 * DisplayRole path renders `summary`.
 */

#ifndef USERFLAGSEDITREF_H
#define USERFLAGSEDITREF_H

#include <QMetaType>
#include <QString>

namespace openswmmvis::ui {
class UserFlagsModel;
}

/*! Identifies the user-flag assignments of a single model object. */
struct UserFlagsEditRef
{
    /*! Shared per-project store (borrow, not owned — lifetime is bound
     *  to the SWMMModelLayer). May be null when the engine is closed or
     *  in test contexts; the button disables gracefully. */
    openswmmvis::ui::UserFlagsModel *model = nullptr;

    QString objectType;   ///< [USER_FLAG_VALUES] token: NODE / LINK / SUBCATCHMENT / GAGE
    QString objectName;   ///< Owning object id (case-preserved)
    QString summary;      ///< Short text shown in the cell

    bool operator==(const UserFlagsEditRef &other) const noexcept
    {
        return model == other.model && objectType == other.objectType
               && objectName == other.objectName && summary == other.summary;
    }
};

Q_DECLARE_METATYPE(UserFlagsEditRef)

/*! Compute the cell summary for one object: "(no flags defined)",
 *  "(none set)", or "n of m set". Null model reads as no flags. */
QString userFlagsSummaryFor(openswmmvis::ui::UserFlagsModel *model,
                            const QString &objectType,
                            const QString &objectName);

/*! Install the `UserFlagsEditRef → QString` metatype converter so the
 *  read-only DisplayRole path renders `summary`. Idempotent. */
void registerUserFlagsEditRefConverter();

#endif // USERFLAGSEDITREF_H
