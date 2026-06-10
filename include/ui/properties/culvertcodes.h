/*!
 * \file   culvertcodes.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * docs/ATTRIBUTE_EDITOR_WIRING_PLAN_2026-06-04.md Phase 0 — single
 * source of truth for the FHWA HDS-5 culvert inlet-geometry codes
 * (1..57; 0 = no culvert / no inlet control). Transcribed from the
 * legacy selector tree in `SWMM-GUI/Epaswmm5/Dculvert.dfm` so the
 * Property Browser combobox, the Attribute Table enum column, and any
 * future UI all show the same labels.
 *
 * Labels are deliberately ASCII-only: a previous revision routed
 * UTF-8 literals through `QString::fromLatin1`, baking mojibake into
 * the culvert combobox (see plan §2, encoding hygiene rule).
 */

#ifndef CULVERTCODES_H
#define CULVERTCODES_H

#include <QString>
#include <vector>

/*! One HDS-5 culvert code entry. `group` is the shape/material
 *  heading the legacy tree filed the code under. */
struct CulvertCodeInfo
{
    int         code;    ///< 1..57 (HDS-5 chart code); 0 entry not stored here
    const char *group;   ///< Shape / material group (ASCII)
    const char *label;   ///< Inlet-edge description (ASCII)
};

/*! All 57 codes in ascending order (code 0 "(none)" is NOT included —
 *  callers prepend it so they control its wording). */
[[nodiscard]] const std::vector<CulvertCodeInfo> &culvertCodes();

/*! Short display label for a code, e.g.
 *  "3) Circular Concrete: Groove end projecting". Returns "(none)"
 *  for code <= 0 and "Code N" for an unknown positive code. */
[[nodiscard]] QString culvertCodeLabel(int code);

#endif // CULVERTCODES_H
