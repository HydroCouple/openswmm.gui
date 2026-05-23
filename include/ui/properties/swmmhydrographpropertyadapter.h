/*!
 * \file   swmmhydrographpropertyadapter.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice DA.2 — Property-tree adapter for [HYDROGRAPHS] groups. The
 * engine stores one row per `(group, month, response)`; this adapter
 * surfaces the group identity (name + assigned rain gage + row count).
 * Editing per-month R/T/K parameters lands as the BS HydrographGroupEditor
 * behind an "Edit…" button.
 */

#ifndef SWMMHYDROGRAPHPROPERTYADAPTER_H
#define SWMMHYDROGRAPHPROPERTYADAPTER_H

#include "ui/properties/swmmdataobjectpropertyadapter.h"

class SWMMHydrographPropertyAdapter : public SWMMDataObjectPropertyAdapter
{
    Q_OBJECT
    Q_PROPERTY(QString gageName   READ gageName   NOTIFY changed)
    Q_PROPERTY(int     rowCount   READ rowCount   NOTIFY changed)
    Q_PROPERTY(QString rowSummary READ rowSummary NOTIFY changed)

public:
    using SWMMDataObjectPropertyAdapter::SWMMDataObjectPropertyAdapter;

    /*! Rain gage name assigned to this UH group (or empty if none). */
    [[nodiscard]] QString gageName()   const;
    /*! Number of `(month, response)` parameter rows for this group. */
    [[nodiscard]] int     rowCount()   const;
    /*! Human summary like "12 months × 3 responses". */
    [[nodiscard]] QString rowSummary() const;

    Q_INVOKABLE QString displayLabelFor(const QString &property) const;
};

#endif // SWMMHYDROGRAPHPROPERTYADAPTER_H
