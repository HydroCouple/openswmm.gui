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

#include <openswmm/engine/openswmm_links.h>

namespace openswmmvis {

//! One SWMM_XSectShape row.
//!
//! `engineId` MUST be spelled as the engine's own SWMM_XSECT_* constant, never
//! as a bare integer: this table is fed straight to swmm_link_set_xsect(), so a
//! literal that drifts from the engine's numbering silently writes the WRONG
//! cross-section into the model. (Before 6.0 exactly that happened — the table
//! carried the pre-renumbering literals, so picking e.g. EGGSHAPED stored a
//! baskethandle and IRREGULAR stored a vertical ellipse.)
//!
//! Row order is the legacy SWMM-GUI (Dxsect.pas) presentation order, which the
//! shape picker shows verbatim; it is deliberately NOT the engine's numeric
//! order. A geomNLabel is the empty string when that geom is unused.
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
inline constexpr int kXsectIrregularId = SWMM_XSECT_IRREGULAR;
inline constexpr int kXsectStreetId    = SWMM_XSECT_STREET;
//! CUSTOM is the third tabulated shape: geom1 IS a real dimension (max depth)
//! but geom2 is an index into the shape-curve list, so only geom2 is excluded
//! from inline editing — unlike IRREGULAR / STREET, where geom1 itself is the
//! index and nothing is inline-editable.
inline constexpr int kXsectCustomId    = SWMM_XSECT_CUSTOM;

inline constexpr XsectShapeRow kXsectShapes[] = {
    { "CIRCULAR",        SWMM_XSECT_CIRCULAR,       "Diameter",  "",             "",               ""  },
    { "FILLED_CIRCULAR", SWMM_XSECT_FILLED_CIRCULAR,"Diameter",  "Filled Depth", "",               ""  },
    { "RECT_CLOSED",     SWMM_XSECT_RECT_CLOSED,    "Max Depth", "Width",        "",               ""  },
    { "RECT_OPEN",       SWMM_XSECT_RECT_OPEN,      "Max Depth", "Width",        "",               ""  },
    { "TRAPEZOIDAL",     SWMM_XSECT_TRAPEZOIDAL,    "Max Depth", "Bottom Width", "Left Slope",     "Right Slope" },
    { "TRIANGULAR",      SWMM_XSECT_TRIANGULAR,     "Max Depth", "Top Width",    "",               ""  },
    { "PARABOLIC",       SWMM_XSECT_PARABOLIC,      "Max Depth", "Top Width",    "",               ""  },
    { "POWER",           SWMM_XSECT_POWER,          "Max Depth", "Top Width",    "Exponent",       ""  },
    { "RECT_TRIANGULAR", SWMM_XSECT_RECT_TRIANG,    "Max Depth", "Top Width",    "Triangle Height","" },
    { "RECT_ROUND",      SWMM_XSECT_RECT_ROUND,     "Max Depth", "Top Width",    "Bottom Radius",  ""  },
    { "MOD_BASKETHANDLE",SWMM_XSECT_MOD_BASKET,     "Max Depth", "Bottom Width", "Top Radius",     ""  },
    { "HORIZ_ELLIPSE",   SWMM_XSECT_HORIZ_ELLIPSE,  "Max Height","Max Width",    "",               ""  },
    { "VERT_ELLIPSE",    SWMM_XSECT_VERT_ELLIPSE,   "Max Height","Max Width",    "",               ""  },
    { "ARCH",            SWMM_XSECT_ARCH,           "Max Height","Max Width",    "",               ""  },
    { "EGGSHAPED",       SWMM_XSECT_EGGSHAPED,      "Max Depth", "",             "",               ""  },
    { "HORSESHOE",       SWMM_XSECT_HORSESHOE,      "Max Depth", "",             "",               ""  },
    { "GOTHIC",          SWMM_XSECT_GOTHIC,         "Max Depth", "",             "",               ""  },
    { "CATENARY",        SWMM_XSECT_CATENARY,       "Max Depth", "",             "",               ""  },
    { "SEMIELLIPTICAL",  SWMM_XSECT_SEMIELLIPTICAL, "Max Depth", "",             "",               ""  },
    // Slice SP.3 — the five shapes the engine has always supported but the
    // picker never surfaced (they had no SVG artwork; the section preview now
    // draws every shape procedurally, so the gap is closed).
    { "BASKETHANDLE",    SWMM_XSECT_BASKETHANDLE,   "Max Depth", "",             "",               ""  },
    { "SEMICIRCULAR",    SWMM_XSECT_SEMICIRCULAR,   "Max Depth", "",             "",               ""  },
    // FORCE_MAIN is geometrically a circular pipe; geom2 selects the friction
    // law's coefficient (Hazen-Williams C or Darcy-Weisbach roughness height),
    // NOT a dimension — hence the explicit label.
    { "FORCE_MAIN",      SWMM_XSECT_FORCE_MAIN,     "Diameter",  "Roughness (C or e)", "",         ""  },
    { "IRREGULAR",       SWMM_XSECT_IRREGULAR,      "Transect (index)", "",      "",               ""  },
    { "CUSTOM",          SWMM_XSECT_CUSTOM,         "Max Depth", "Shape Curve (index)", "",        ""  },
    { "STREET",          SWMM_XSECT_STREET,         "Street (index)",   "",      "",               ""  },
    // DUMMY carries no geometry at all: it exists so a link can be routed
    // without conveyance. No geom is editable.
    { "DUMMY",           SWMM_XSECT_DUMMY,          "",          "",             "",               ""  },
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

//! Shape name for `engineId` ("CIRCULAR", …), or an empty QString when the id
//! is not one the GUI surfaces. Since Slice SP.3 the table covers all 26
//! engine shapes, so an empty return now means the id is genuinely unknown
//! (e.g. a model written by a newer engine). Unlike findXsectShapeRow() this
//! does NOT fall back to CIRCULAR — display paths must be able to render
//! "UNKNOWN" instead of confidently naming the wrong shape.
inline QString xsectShapeName(int engineId)
{
    for (const auto &r : kXsectShapes) {
        if (r.engineId == engineId) return QString::fromLatin1(r.name);
    }
    return {};
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
//! (geom1 there is a transect / street index, set only via the dialog);
//! CUSTOM excludes only geom2 (its shape-curve index), keeping geom1
//! (max depth) inline-editable.
//!
//! This answers "does this geom MEAN anything for the shape" — it drives
//! labels and tooltips. It is deliberately NOT the editability test: the
//! stored geom values exist regardless of shape, and blanking them made
//! the inline cells look broken. Use xsectGeomIsPickerIndex() to decide
//! what an inline editor may write.
inline bool xsectGeomApplies(int shapeId, int ordinal)
{
    if (shapeId == kXsectIrregularId || shapeId == kXsectStreetId)
        return false;
    if (shapeId == kXsectCustomId && ordinal == 2)
        return false;
    return !xsectGeomLabel(shapeId, ordinal).isEmpty();
}

//! True iff geom `ordinal` holds a PICKER-OWNED INDEX rather than a
//! dimension: IRREGULAR / STREET geom1 (an index into the transect / street
//! list) and CUSTOM geom2 (an index into the shape-curve list).
//!
//! These are the only slots an inline numeric editor must refuse. Typing a
//! raw number into them re-points the section at an arbitrary transect /
//! curve — silent model corruption — so they stay read-only and are set
//! through the complex dialog's name picker. Every other geom, including
//! ones the current shape doesn't use, is a plain stored number and is
//! freely editable: a user switching CIRCULAR → RECT_CLOSED expects the
//! width they typed to still be there.
inline bool xsectGeomIsPickerIndex(int shapeId, int ordinal)
{
    if (ordinal == 1
        && (shapeId == kXsectIrregularId || shapeId == kXsectStreetId))
        return true;
    return shapeId == kXsectCustomId && ordinal == 2;
}

} // namespace openswmmvis

#endif // XSECTSHAPEGEOM_H
