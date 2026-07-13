/*!
 * \file   storageshapegeom.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Per-shape meaning of a storage unit's three raw dimensions.
 *
 *         A storage node's Param 1/2/3 rows carry different quantities depending
 *         on the shape — "Major Axis Length" for a cone, "Base Length" for a
 *         pyramid, nothing at all for a tabular curve. QPropertyModel reflects
 *         its rows from `metaObject()` and can neither rename nor hide them at
 *         runtime, so (exactly as `xsectshapegeom.h` does for link geom1..geom4)
 *         the rows stay generic and this table supplies the shape-specific label
 *         and applicability that PropertiesPanel / the attribute table use to
 *         label, tooltip and grey them.
 *
 *         Ordinals match `SWMMNodePropertyAdapter::StorageShape` and the engine's
 *         `SWMM_StorageShape`.
 */

#ifndef STORAGESHAPEGEOM_H
#define STORAGESHAPEGEOM_H

#include <QCoreApplication>
#include <QString>

namespace openswmmvis {

//! One storage-shape row. A paramNLabel is the empty string when that dimension
//! is unused for the shape (tabular and functional use no raw dimensions at all —
//! they are driven by a curve and by the A/B/C coefficients respectively).
struct StorageShapeRow {
    const char *name;         //!< "PYRAMIDAL", … (engine keyword; PARABOLOID is "PARABOLIC").
    int         engineId;     //!< SWMM_StorageShape numeric.
    const char *param1Label;  //!< "" when unused.
    const char *param2Label;
    const char *param3Label;
};

//! Engine ids, spelled out so call sites don't hard-code magic numbers.
inline constexpr int kStorageTabularId     = 0;
inline constexpr int kStorageFunctionalId  = 1;
inline constexpr int kStorageCylindricalId = 2;
inline constexpr int kStorageConicalId     = 3;
inline constexpr int kStorageParaboloidId  = 4;
inline constexpr int kStoragePyramidalId   = 5;

//! Dimension meanings are the legacy solver's (src/legacy/engine/node.c
//! storage_readParams): for the elliptical shapes p1/p2 are the FULL axes (the
//! engine halves them into semi-axes itself); for the pyramid they are the full
//! base length and width. p3 is a run-over-rise side slope, except on the
//! paraboloid where it is the height at the top axes — and where it may not be 0.
inline constexpr StorageShapeRow kStorageShapes[] = {
    { "TABULAR",     kStorageTabularId,     "",                  "",                  ""                       },
    { "FUNCTIONAL",  kStorageFunctionalId,  "",                  "",                  ""                       },
    { "CYLINDRICAL", kStorageCylindricalId, "Major Axis Length", "Minor Axis Width",  ""                       },
    { "CONICAL",     kStorageConicalId,     "Major Axis Length", "Minor Axis Width",  "Side Slope (run/rise)"  },
    { "PARABOLIC",   kStorageParaboloidId,  "Top Major Axis",    "Top Minor Axis",    "Height at Top Axes"     },
    { "PYRAMIDAL",   kStoragePyramidalId,   "Base Length",       "Base Width",        "Side Slope (run/rise)"  },
};

//! Look up a shape row by engine id; falls back to FUNCTIONAL for an unknown id
//! so callers never get nullptr.
inline const StorageShapeRow *findStorageShapeRow(int engineId)
{
    for (const auto &r : kStorageShapes) {
        if (r.engineId == engineId) return &r;
    }
    return &kStorageShapes[kStorageFunctionalId];
}

//! True iff `shapeId` is one of the four geometric shapes — i.e. the ones driven
//! by raw dimensions rather than by a curve or by the A/B/C power law.
inline bool storageShapeIsGeometric(int shapeId)
{
    return shapeId == kStorageCylindricalId || shapeId == kStorageConicalId
        || shapeId == kStorageParaboloidId  || shapeId == kStoragePyramidalId;
}

//! Shape-specific label for param `ordinal` (1..3), e.g. "Base Length".
//! Empty when the dimension is unused for the shape or the ordinal is out of range.
inline QString storageGeomLabel(int shapeId, int ordinal)
{
    const StorageShapeRow *r = findStorageShapeRow(shapeId);
    const char *lbl = nullptr;
    switch (ordinal) {
    case 1: lbl = r->param1Label; break;
    case 2: lbl = r->param2Label; break;
    case 3: lbl = r->param3Label; break;
    default: return {};
    }
    return (lbl && *lbl)
               ? QCoreApplication::translate("StorageShape", lbl)
               : QString();
}

//! True iff param `ordinal` (1..3) is a real, directly-editable dimension for
//! `shapeId`. A cylinder has no side slope, so ordinal 3 is false there.
inline bool storageGeomApplies(int shapeId, int ordinal)
{
    return !storageGeomLabel(shapeId, ordinal).isEmpty();
}

} // namespace openswmmvis

#endif // STORAGESHAPEGEOM_H
