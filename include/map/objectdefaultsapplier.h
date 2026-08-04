#ifndef OBJECTDEFAULTSAPPLIER_H
#define OBJECTDEFAULTSAPPLIER_H

/*!
 * \file  objectdefaultsapplier.h
 * \brief Applies PreferencesManager::ObjectDefaults to a freshly created
 *        engine object. Called by the Add*Commands (draw tools) and the GIS
 *        feature importer — the two creation choke points — BEFORE any
 *        geometry-derived overrides (auto-length, auto-area, terrain
 *        inverts) so the more specific value wins.
 *
 *        Deliberately NOT called from SWMMModelLayer::applyXxxAdd: those are
 *        the replay primitives for undo-of-delete and must stay
 *        defaults-free so restored objects keep their captured properties.
 *
 *        Plan: workplans/OBJECT_CREATION_DEFAULTS_PLAN_2026-08-03.md
 */

typedef void *SWMM_Engine;

namespace ObjectDefaultsApplier
{
    //! Junction / outfall / storage / divider defaults per nodeType
    //! (SWMM_NodeType codes 0-3).
    void applyNodeDefaults(SWMM_Engine engine, int idx, int nodeType);

    //! Conduit / pump / orifice / weir / outlet defaults per linkType
    //! (SWMM_LinkType codes 0-4). \a skipLength suppresses the conduit
    //! length default when auto-length has computed the real value.
    void applyLinkDefaults(SWMM_Engine engine, int idx, int linkType,
                           bool skipLength);

    //! Subcatchment defaults. \a skipArea suppresses the area default when
    //! auto-area has computed the real value. Infiltration parameters are
    //! written only for the family matching the subcatchment's engine-side
    //! infiltration model.
    void applySubcatchDefaults(SWMM_Engine engine, int idx, bool skipArea);

    //! Rain gage defaults (format, interval, snow catch factor).
    void applyGageDefaults(SWMM_Engine engine, int idx);
}

#endif // OBJECTDEFAULTSAPPLIER_H
