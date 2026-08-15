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

#include "ui/properties/dataobjectref.h"
#include "ui/properties/swmmdataobjectpropertyadapter.h"

class SWMMHydrographPropertyAdapter : public SWMMDataObjectPropertyAdapter
{
    Q_OBJECT
    // Slice BM.0-Browse-Edit (2026-05-25) — typed as DataObjectRef
    // (kind=RainGage) so the cell hosts the picker editor. Engine setter
    // is swmm_hydrograph_set_gage.
    Q_PROPERTY(DataObjectRef gageName
               READ gageNameRef WRITE setGageNameRef NOTIFY changed)
    Q_PROPERTY(int     rowCount   READ rowCount   NOTIFY changed)
    Q_PROPERTY(QString rowSummary READ rowSummary NOTIFY changed)

public:
    using SWMMDataObjectPropertyAdapter::SWMMDataObjectPropertyAdapter;

    /*! Rain gage name assigned to this UH group (or empty if none). */
    [[nodiscard]] QString gageName()   const;
    /*! Slice BM.0-Browse-Edit — DataObjectRef wrapper of `gageName()`. */
    [[nodiscard]] DataObjectRef gageNameRef() const;
    /*! Number of `(month, response)` parameter rows for this group. */
    [[nodiscard]] int     rowCount()   const;
    /*! Human summary like "12 months × 3 responses". */
    [[nodiscard]] QString rowSummary() const;

    Q_INVOKABLE QString displayLabelFor(const QString &property) const;

public slots:
    /*! Slice BM.0-Browse-Edit — writes the selected rain gage via
     *  `swmm_hydrograph_set_gage`. Empty `ref.currentName` clears the
     *  assignment (engine accepts a NULL gage name). */
    void setGageNameRef(const DataObjectRef &ref);
};

#endif // SWMMHYDROGRAPHPROPERTYADAPTER_H
