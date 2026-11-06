#ifndef LINKOFFSETDISPLAY_H
#define LINKOFFSETDISPLAY_H

/**
 * @file      linkoffsetdisplay.h
 * @brief     Mode-aware link offset accessors for the editing surfaces.
 *
 * The engine stores every link offset / crest as a DEPTH above the node
 * invert regardless of LINK_OFFSETS (the parser normalises elevations at open
 * and the .inp writer re-adds the invert on save). Legacy SWMM-GUI shows the
 * user the value as it appears in the file, so in ELEVATION mode the property
 * editor and attribute table must present `depth + invert` and accept an
 * elevation back. These wrappers share the C API signatures so they drop into
 * the same function-pointer slots as the raw `swmm_link_*_offset_*` calls.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_nodes.h>

#include <cstring>

namespace linkoffsetdisplay
{

/**
 * @brief Inspect the engine LINK_OFFSETS token using the wrapper's supported prefix spellings.
 * @param[in] e Borrowed engine; null/getter failure yields false.
 * @return True for token prefix ELEV or elev; mixed-case prefixes are not normalized.
 */
inline bool elevationMode(SWMM_Engine e)
{
    char buf[32] = {};
    if (!e || swmm_options_get(e, "LINK_OFFSETS", buf, sizeof(buf)) != SWMM_OK)
        return false;
    return std::strncmp(buf, "ELEV", 4) == 0 || std::strncmp(buf, "elev", 4) == 0;
}

// Invert of the node the offset is measured from (0 when unresolved).
/**
 * @brief Read the invert at one link endpoint, with zero fallback on unresolved/read failure.
 * @param[in] e Borrowed engine.
 * @param[in] idx Zero-based link index.
 * @param[in] upstream True selects from-node, false to-node.
 * @return Invert in engine user length units, or zero; failure is not separately reported.
 */
inline double endInvert(SWMM_Engine e, int idx, bool upstream)
{
    int n = -1;
    if (upstream)
        swmm_link_get_from_node(e, idx, &n);
    else
        swmm_link_get_to_node(e, idx, &n);
    double inv = 0.0;
    if (n >= 0)
        swmm_node_get_invert_elev(e, n, &inv);
    return inv;
}

/**
 * @brief Read raw link depth and, in elevation mode, add its endpoint invert.
 * @param[in] e Borrowed engine.
 * @param[in] idx Zero-based link index.
 * @param[out] v Output forwarded to rawGet; converted only after successful raw read when non-null.
 * @param[in] upstream Endpoint used for invert conversion.
 * @param[in] rawGet Required raw getter function pointer.
 * @return Raw getter status unchanged; invert lookup failures use zero and do not alter the status.
 */
inline int getDisplay(SWMM_Engine e, int idx, double *v, bool upstream,
                      int (*rawGet)(SWMM_Engine, int, double *))
{
    const int rc = rawGet(e, idx, v);
    if (rc == SWMM_OK && v && elevationMode(e))
        *v += endInvert(e, idx, upstream);
    return rc;
}

// Legacy GetOffsetDepth clamps a below-invert elevation to depth 0.
/**
 * @brief Convert a displayed elevation to a nonnegative raw depth when elevation mode is active.
 * @param[in] e Borrowed engine.
 * @param[in] idx Zero-based link index.
 * @param[in] v User length value: elevation in elevation mode, depth otherwise.
 * @param[in] upstream Endpoint whose invert is subtracted.
 * @param[in] rawSet Required raw setter function pointer.
 * @return Raw setter status; conversion performs no independent finite check and uses zero invert fallback.
 */
inline int setDisplay(SWMM_Engine e, int idx, double v, bool upstream,
                      int (*rawSet)(SWMM_Engine, int, double))
{
    if (elevationMode(e)) {
        v -= endInvert(e, idx, upstream);
        if (v < 0.0)
            v = 0.0;
    }
    return rawSet(e, idx, v);
}

/**
 * @brief Read the upstream offset using LINK_OFFSETS display semantics.
 * @param[in] e Borrowed engine.
 * @param[in] i Zero-based link index.
 * @param[out] v Output in user length units: elevation when configured, otherwise raw depth; pointer validity follows the engine getter.
 * @return Underlying engine getter/setter status; endpoint-invert lookup failure is represented by zero rather than a separate error.
 */
inline int getOffsetUp(SWMM_Engine e, int i, double *v)
{
    return getDisplay(e, i, v, true, &swmm_link_get_offset_up);
}
/**
 * @brief Write the upstream offset using LINK_OFFSETS display semantics.
 * @param[in] e Borrowed engine.
 * @param[in] i Zero-based link index.
 * @param[in] v Value in user length units: elevation converts to depth with a zero floor, otherwise passed through as raw depth.
 * @return Underlying engine getter/setter status; endpoint-invert lookup failure is represented by zero rather than a separate error.
 */
inline int setOffsetUp(SWMM_Engine e, int i, double v)
{
    return setDisplay(e, i, v, true, &swmm_link_set_offset_up);
}

/**
 * @brief Read the downstream offset using LINK_OFFSETS display semantics.
 * @param[in] e Borrowed engine.
 * @param[in] i Zero-based link index.
 * @param[out] v Output in user length units: elevation when configured, otherwise raw depth; pointer validity follows the engine getter.
 * @return Underlying engine getter/setter status; endpoint-invert lookup failure is represented by zero rather than a separate error.
 */
inline int getOffsetDn(SWMM_Engine e, int i, double *v)
{
    return getDisplay(e, i, v, false, &swmm_link_get_offset_dn);
}
/**
 * @brief Write the downstream offset using LINK_OFFSETS display semantics.
 * @param[in] e Borrowed engine.
 * @param[in] i Zero-based link index.
 * @param[in] v Value in user length units: elevation converts to depth with a zero floor, otherwise passed through as raw depth.
 * @return Underlying engine getter/setter status; endpoint-invert lookup failure is represented by zero rather than a separate error.
 */
inline int setOffsetDn(SWMM_Engine e, int i, double v)
{
    return setDisplay(e, i, v, false, &swmm_link_set_offset_dn);
}

// Weir / outlet crest is measured from the upstream node.
/**
 * @brief Read the upstream-referenced weir/outlet crest using LINK_OFFSETS display semantics.
 * @param[in] e Borrowed engine.
 * @param[in] i Zero-based link index.
 * @param[out] v Output in user length units: elevation when configured, otherwise raw depth; pointer validity follows the engine getter.
 * @return Underlying engine getter/setter status; endpoint-invert lookup failure is represented by zero rather than a separate error.
 */
inline int getCrestHeight(SWMM_Engine e, int i, double *v)
{
    return getDisplay(e, i, v, true, &swmm_link_get_crest_height);
}
/**
 * @brief Write the upstream-referenced weir/outlet crest using LINK_OFFSETS display semantics.
 * @param[in] e Borrowed engine.
 * @param[in] i Zero-based link index.
 * @param[in] v Value in user length units: elevation converts to depth with a zero floor, otherwise passed through as raw depth.
 * @return Underlying engine getter/setter status; endpoint-invert lookup failure is represented by zero rather than a separate error.
 */
inline int setCrestHeight(SWMM_Engine e, int i, double v)
{
    return setDisplay(e, i, v, true, &swmm_link_set_crest_height);
}

} // namespace linkoffsetdisplay

#endif // LINKOFFSETDISPLAY_H
