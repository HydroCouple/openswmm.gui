#ifndef LINKOFFSETDISPLAY_H
#define LINKOFFSETDISPLAY_H

/*!
 * \file linkoffsetdisplay.h
 * \brief Mode-aware link offset accessors for the editing surfaces.
 *
 * The engine stores every link offset / crest as a DEPTH above the node
 * invert regardless of LINK_OFFSETS (the parser normalises elevations at open
 * and the .inp writer re-adds the invert on save). Legacy SWMM-GUI shows the
 * user the value as it appears in the file, so in ELEVATION mode the property
 * editor and attribute table must present `depth + invert` and accept an
 * elevation back. These wrappers share the C API signatures so they drop into
 * the same function-pointer slots as the raw `swmm_link_*_offset_*` calls.
 */

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_nodes.h>

#include <cstring>

namespace linkoffsetdisplay {

inline bool elevationMode(SWMM_Engine e)
{
    char buf[32] = {};
    if (!e || swmm_options_get(e, "LINK_OFFSETS", buf, sizeof(buf)) != SWMM_OK)
        return false;
    return std::strncmp(buf, "ELEV", 4) == 0 || std::strncmp(buf, "elev", 4) == 0;
}

// Invert of the node the offset is measured from (0 when unresolved).
inline double endInvert(SWMM_Engine e, int idx, bool upstream)
{
    int n = -1;
    if (upstream) swmm_link_get_from_node(e, idx, &n);
    else          swmm_link_get_to_node(e, idx, &n);
    double inv = 0.0;
    if (n >= 0) swmm_node_get_invert_elev(e, n, &inv);
    return inv;
}

inline int getDisplay(SWMM_Engine e, int idx, double *v, bool upstream,
                      int (*rawGet)(SWMM_Engine, int, double *))
{
    const int rc = rawGet(e, idx, v);
    if (rc == SWMM_OK && v && elevationMode(e))
        *v += endInvert(e, idx, upstream);
    return rc;
}

// Legacy GetOffsetDepth clamps a below-invert elevation to depth 0.
inline int setDisplay(SWMM_Engine e, int idx, double v, bool upstream,
                      int (*rawSet)(SWMM_Engine, int, double))
{
    if (elevationMode(e))
    {
        v -= endInvert(e, idx, upstream);
        if (v < 0.0) v = 0.0;
    }
    return rawSet(e, idx, v);
}

inline int getOffsetUp(SWMM_Engine e, int i, double *v)
{ return getDisplay(e, i, v, true, &swmm_link_get_offset_up); }
inline int setOffsetUp(SWMM_Engine e, int i, double v)
{ return setDisplay(e, i, v, true, &swmm_link_set_offset_up); }

inline int getOffsetDn(SWMM_Engine e, int i, double *v)
{ return getDisplay(e, i, v, false, &swmm_link_get_offset_dn); }
inline int setOffsetDn(SWMM_Engine e, int i, double v)
{ return setDisplay(e, i, v, false, &swmm_link_set_offset_dn); }

// Weir / outlet crest is measured from the upstream node.
inline int getCrestHeight(SWMM_Engine e, int i, double *v)
{ return getDisplay(e, i, v, true, &swmm_link_get_crest_height); }
inline int setCrestHeight(SWMM_Engine e, int i, double v)
{ return setDisplay(e, i, v, true, &swmm_link_set_crest_height); }

} // namespace linkoffsetdisplay

#endif // LINKOFFSETDISPLAY_H
