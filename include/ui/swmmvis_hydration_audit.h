/*!
 * \file   swmmvis_hydration_audit.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice CX (Phase CX.3) — INP-as-source-of-truth audit list.
 *
 * Mirrors the §M.2 control table in docs/GUI_IMPLEMENTATION_PLAN.md.
 * The Phase CX.1 test (tests/gui/test_options_hydration_contract.cpp)
 * iterates this array and asserts that, for each entry, the active
 * project's value is what the engine reports under @ref optionsKey
 * at all three §M.1 trigger points: project open, tab activation,
 * external mutation.
 *
 * Adding a new status-bar widget that reflects an [OPTIONS] key?
 * Add a row here, point @ref optionsKey at the engine key, and the
 * regression test will pick it up automatically.
 */
#ifndef SWMMVIS_HYDRATION_AUDIT_H
#define SWMMVIS_HYDRATION_AUDIT_H

#include <array>

namespace openswmmvis {

/*! \brief One row of the §M.2 status-bar audit table. */
struct HydrationAuditEntry
{
    const char *widgetName;   ///< Human-readable widget label (matches §M.2).
    const char *optionsKey;   ///< Engine [OPTIONS] key read via swmm_options_get.
};

/*! \brief The closed §M.2 status-bar list. Update when a new INP-driven
 *         widget lands; the Phase CX.1 regression test asserts every entry
 *         hydrates correctly at all three §M.1 triggers. */
inline constexpr std::array<HydrationAuditEntry, 2> kStatusBarHydrationAudit = {{
    { "Flow Units combo",     "FLOW_UNITS"   },
    { "Offset Mode checkbox", "LINK_OFFSETS" },
}};

// CRS button and Map Scale combo are tracked in §M.2 but are not driven by an
// [OPTIONS] key — CRS proxies the layer SRS, Map Scale proxies the canvas
// extent. They are exercised by test_layerreprojection / canvas tests rather
// than by the OPTIONS-keyed harness here.

} // namespace openswmmvis

#endif // SWMMVIS_HYDRATION_AUDIT_H
