/*!
 * \file   xsectshapegeom.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Shared cross-section shape / geometry metadata — the single source of
 * truth for which of geom1..geom4 a SWMM_XSectShape actually uses and
 * what each one means. Consumed by the complex editor
 * (LinkCompoundEditDialog), the per-object Property Browser
 * (SWMMLinkPropertyAdapter) and the Attribute Table
 * (SWMMAttributeTableModel) so the three surfaces stay in lock-step.
 *
 * Header-only (inline) so every translation unit shares one definition
 * without a separate .cpp; the table is tiny.
 */

#ifndef XSECTSHAPEGEOM_H
#define XSECTSHAPEGEOM_H

#include <QString>

namespace openswmmvis {

//! One SWMM_XSectShape row (matches openswmm_links.h SWMM_XSECT_* enum).
//! Order is the engine's integer value; legacy SWMM-GUI Dxsect.pas uses
//! the same names. A geomNLabel is the empty string when that geom is
//! unused for the shape.
struct XsectShapeRow {
    const char *name;        //!< "CIRCULAR", "RECT_CLOSED", …
    int         engineId;    //!< SWMM_XSECT_* numeric.
    const char *geom1Label;  //!< "Diameter", "Max Depth", … ("" if unused).
    const char *geom2Label;
    const char *geom3Label;
    const char *geom4Label;
};

//! IRREGULAR / STREET engine ids — for these, geom1 is an index into the
//! transect / street list (not a length-like dimension), so it is managed
//! only by the complex dialog's name picker, never as a raw inline number.
inline constexpr int kXsectIrregularId = 19;
inline constexpr int kXsectStreetId    = 24;

inline constexpr XsectShapeRow kXsectShapes[] = {
    { "CIRCULAR",         0, "Diameter",  "",                "",               ""  },
    { "FILLED_CIRCULAR",  1, "Diameter",  "Filled Depth",    "",               ""  },
    { "RECT_CLOSED",      2, "Max Depth", "Width",           "",               ""  },
    { "RECT_OPEN",        3, "Max Depth", "Width",           "",               ""  },
    { "TRAPEZOIDAL",      4, "Max Depth", "Bottom Width",    "Left Slope",     "Right Slope" },
    { "TRIANGULAR",       5, "Max Depth", "Top Width",       "",               ""  },
    { "PARABOLIC",        6, "Max Depth", "Top Width",       "",               ""  },
    { "POWER",            7, "Max Depth", "Top Width",       "Exponent",       ""  },
    { "RECT_TRIANGULAR",  8, "Max Depth", "Top Width",       "Triangle Height","" },
    { "RECT_ROUND",       9, "Max Depth", "Top Width",       "Bottom Radius",  ""  },
    { "MOD_BASKETHANDLE",10, "Max Depth", "Bottom Width",    "Top Radius",     ""  },
    { "HORIZ_ELLIPSE",   11, "Max Height","Max Width",       "",               ""  },
    { "VERT_ELLIPSE",    12, "Max Height","Max Width",       "",               ""  },
    { "ARCH",            13, "Max Height","Max Width",       "",               ""  },
    { "EGGSHAPED",       14, "Max Depth", "",                "",               ""  },
    { "HORSESHOE",       15, "Max Depth", "",                "",               ""  },
    { "GOTHIC",          16, "Max Depth", "",                "",               ""  },
    { "CATENARY",        17, "Max Depth", "",                "",               ""  },
    { "SEMIELLIPTICAL",  18, "Max Depth", "",                "",               ""  },
    { "IRREGULAR",       19, "Transect (index)", "",         "",               ""  },
    { "STREET",          24, "Street (index)",   "",         "",               ""  },
};

//! Look up a shape row by engine id; falls back to the first row
//! (CIRCULAR) for an unknown id so callers never get nullptr.
inline const XsectShapeRow *findXsectShapeRow(int engineId)
{
    for (const auto &r : kXsectShapes) {
        if (r.engineId == engineId) return &r;
    }
    return &kXsectShapes[0];
}

//! Shape-specific label for geom `ordinal` (1..4), e.g. "Diameter".
//! Empty string when the geom is unused for the shape or ordinal is out
//! of range.
inline QString xsectGeomLabel(int shapeId, int ordinal)
{
    const XsectShapeRow *r = findXsectShapeRow(shapeId);
    const char *lbl = nullptr;
    switch (ordinal) {
    case 1: lbl = r->geom1Label; break;
    case 2: lbl = r->geom2Label; break;
    case 3: lbl = r->geom3Label; break;
    case 4: lbl = r->geom4Label; break;
    default: return {};
    }
    return (lbl && *lbl) ? QString::fromLatin1(lbl) : QString();
}

//! True iff geom `ordinal` (1..4) is a real, directly-editable dimension
//! for `shapeId`. IRREGULAR / STREET return false for every ordinal
//! (geom1 there is a transect / street index, set only via the dialog).
inline bool xsectGeomApplies(int shapeId, int ordinal)
{
    if (shapeId == kXsectIrregularId || shapeId == kXsectStreetId)
        return false;
    return !xsectGeomLabel(shapeId, ordinal).isEmpty();
}

} // namespace openswmmvis

#endif // XSECTSHAPEGEOM_H
