/*!
 * \file   dataobjectref.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice DA.4.3 — Property-cell value carrying a reference to one named
 * data object (curve / time series / pattern / unit hydrograph). The
 * matching custom editor (`DataObjectPickerEditor`) renders a combobox
 * filtered by `kind` plus a "…" button that launches an inline name
 * prompt + `SWMMModelLayer::createDataObject` for TS / UH categories
 * (gap categories surface the future-slice tooltip per Slice
 * BM.0-Add-New, 2026-05-24).
 *
 * The value type intentionally carries no setter callback — writes flow
 * through the owning adapter's WRITE slot (e.g. `setOutfallTidalCurveRef`),
 * which is what calls the engine. This keeps the MVC contract clean
 * ([[feedback_mvc_synchronized_uis]]): the engine is the single source
 * of truth; the ref is just a coordinate.
 */

#ifndef DATAOBJECTREF_H
#define DATAOBJECTREF_H

#include <QMetaType>
#include <QString>

#include <openswmm/engine/openswmm_callbacks.h>

class SWMMModelLayer;

/*! Identifies one data object referenced from a property cell. */
struct DataObjectRef
{
    /*! Which family of data object this cell points at. Drives both the
     *  combo population filter and the editor dialog that "…" launches.
     *  Extend as new picker callsites land. */
    enum Kind {
        TidalCurve     = 0,  ///< Curves filtered to type == TIDAL (engine code 6)
        AnyCurve       = 1,  ///< Any non-timeseries table (engine table type 1..11)
        TimeSeries     = 2,  ///< Tables of type TIMESERIES (engine code 0)
        Pattern        = 3,  ///< Time patterns (filtered by `typeLock` when >= 0)
        UnitHydrograph = 4,  ///< RDII unit-hydrograph groups
        Pollutant      = 5,  ///< [POLLUTANTS] entries — for co-pollutant picker
        RainGage       = 6,  ///< Rain gages — for hydrograph gage picker; no
                             ///  comprehensive editor yet, so the "…" button
                             ///  is a no-op (combo selection only).
    };

    SWMM_Engine     engine      = nullptr;  ///< Engine handle (borrow)
    SWMMModelLayer *layer       = nullptr;  ///< For "…" → create-new flow
    Kind            kind        = TimeSeries;
    int             typeLock    = -1;       ///< Pattern: 0=MONTHLY 1=DAILY 2=HOURLY 3=WEEKEND; -1 = any
    QString         currentName;             ///< Currently-assigned object id; empty = unassigned

    bool operator==(const DataObjectRef &other) const noexcept
    {
        return engine == other.engine && layer == other.layer
               && kind == other.kind && typeLock == other.typeLock
               && currentName == other.currentName;
    }
};

Q_DECLARE_METATYPE(DataObjectRef)

/*! Install the `DataObjectRef → QString` converter so a non-edit cell
 *  renders `currentName` (or "(unassigned)" when empty). Idempotent. */
void registerDataObjectRefConverter();

#endif // DATAOBJECTREF_H
