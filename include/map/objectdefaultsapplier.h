#ifndef OBJECTDEFAULTSAPPLIER_H
#define OBJECTDEFAULTSAPPLIER_H

/**
 * @file      objectdefaultsapplier.h
 * @brief     Applies PreferencesManager::ObjectDefaults to a freshly created
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
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

typedef void *SWMM_Engine;

namespace ObjectDefaultsApplier
{
//! Junction / outfall / storage / divider defaults per nodeType
//! (SWMM_NodeType codes 0-3).
/**
 * @brief Apply saved node-subtype properties to an existing newly created node.
 * @param[in] engine Borrowed live editable engine; null ignored.
 * @param[in] idx Zero-based existing node index; negative ignored.
 * @param[in] nodeType SWMM node type: junction 0, outfall 1, storage 2, divider 3; unknown performs
 *   no writes.
 * @note GUI-thread only; borrows engine and uses defaults selected by GLOBAL UnitSystem SI/US mode,
 *   which caller must synchronize with target engine. Null engine or negative index is a no-op;
 *   otherwise caller supplies a valid existing row in an editable BUILDING/OPENED engine.
 * @note Setter statuses are ignored, so failed writes can leave partial defaults without a reported
 *   error or rollback. No undo entry, model/cache refresh, Qt notification or preference
 *   persistence is performed. Apply before geometry-derived overrides and never to
 *   replay/restoration primitives.
 * @note Junction writes maximum/initial/surcharge depths and ponded area. Outfall writes type/flap
 *   gate. Storage writes maximum depth, functional area coefficients and constant seepage
 *   (inches/hour or mm/hour, replacing Green-Ampt seep parameters). Divider writes type only.
 *   Depths use project feet/metres and ponded area square feet/metres; no invert/coordinates/curve
 *   assignment is supplied.
 */
void applyNodeDefaults(SWMM_Engine engine, int idx, int nodeType);

//! Conduit / pump / orifice / weir / outlet defaults per linkType
//! (SWMM_LinkType codes 0-4). \a skipLength suppresses the conduit
//! length default when auto-length has computed the real value.
/**
 * @brief Apply saved link-subtype properties before geometry-specific creation overrides.
 * @param[in] engine Borrowed live editable engine; null ignored.
 * @param[in] idx Zero-based existing link index; negative ignored.
 * @param[in] linkType Conduit 0, pump 1, orifice 2, weir 3, outlet 4; unknown performs no writes.
 * @param[in] skipLength Suppress conduit length setter only; all other selected properties still
 *   written.
 * @note GUI-thread only; borrows engine and uses defaults selected by GLOBAL UnitSystem SI/US mode,
 *   which caller must synchronize with target engine. Null engine or negative index is a no-op;
 *   otherwise caller supplies a valid existing row in an editable BUILDING/OPENED engine.
 * @note Setter statuses are ignored, so failed writes can leave partial defaults without a reported
 *   error or rollback. No undo entry, model/cache refresh, Qt notification or preference
 *   persistence is performed. Apply before geometry-derived overrides and never to
 *   replay/restoration primitives.
 * @note Conduit writes shape/geometries, Manning roughness, optional length, barrels, inlet/outlet
 *   losses, average loss zero and flap gate. Pump writes initial state and start/shutoff depths
 *   without a curve. Orifice forces circular geometry and writes type, diameter, discharge
 *   coefficient, flap gate and saved open/close value directly to the API rate (fraction of full
 *   opening per second, zero instantaneous).
 * @note Weir forces rectangular-open geometry with saved height/length, and sets type, discharge
 *   coefficient, end contractions and flap gate. Outlet writes rating type/coefficient/exponent and
 *   flap gate without assigning a tabular curve. Geometric lengths/depths use project feet/metres;
 *   coefficients retain each engine subtype convention. No validation/conversion is added around
 *   saved values.
 */
void applyLinkDefaults(SWMM_Engine engine, int idx, int linkType, bool skipLength);

//! Subcatchment defaults. \a skipArea suppresses the area default when
//! auto-area has computed the real value. Infiltration parameters are
//! written only for the family matching the PROJECT INFILTRATION option.
/**
 * @brief Initialize surface and project-selected infiltration properties of a new subcatchment.
 * @param[in] engine Borrowed live editable engine; null ignored.
 * @param[in] idx Zero-based existing subcatchment index; negative ignored.
 * @param[in] skipArea Leave existing area untouched, typically after geometry-derived area
 *   computation.
 * @note GUI-thread only; borrows engine and uses defaults selected by GLOBAL UnitSystem SI/US mode,
 *   which caller must synchronize with target engine. Null engine or negative index is a no-op;
 *   otherwise caller supplies a valid existing row in an editable BUILDING/OPENED engine.
 * @note Setter statuses are ignored, so failed writes can leave partial defaults without a reported
 *   error or rollback. No undo entry, model/cache refresh, Qt notification or preference
 *   persistence is performed. Apply before geometry-derived overrides and never to
 *   replay/restoration primitives.
 * @note Writes optional area (acres/hectares), width (feet/metres), slope and
 *   impervious/zero-storage percentages, Manning surface coefficients and depression depths
 *   (inches/mm). Values are passed through without local validation.
 * @note Reads PROJECT INFILTRATION option, trimmed/uppercased; getter failure defaults Horton.
 *   CURVE selects curve number/drying days; GREEN selects suction inches/mm, conductivity
 *   inches/hour or mm/hour and deficit fraction; otherwise Horton rates inches/hour or mm/hour,
 *   decay per hour and drying days. MOD prefix restores modified Horton/Green-Ampt model after
 *   family setters. Does not default Horton maximum infiltration volume or bind a rain gage/outlet.
 */
void applySubcatchDefaults(SWMM_Engine engine, int idx, bool skipArea);

//! Rain gage defaults (format, interval, snow catch factor).
/**
 * @brief Initialize rainfall interpretation, reporting interval and snowfall catch factor.
 * @param[in] engine Borrowed live editable engine; null ignored.
 * @param[in] idx Zero-based existing gage index; negative ignored.
 * @note GUI-thread only; borrows engine and uses defaults selected by GLOBAL UnitSystem SI/US mode,
 *   which caller must synchronize with target engine. Null engine or negative index is a no-op;
 *   otherwise caller supplies a valid existing row in an editable BUILDING/OPENED engine.
 * @note Setter statuses are ignored, so failed writes can leave partial defaults without a reported
 *   error or rollback. No undo entry, model/cache refresh, Qt notification or preference
 *   persistence is performed. Apply before geometry-derived overrides and never to
 *   replay/restoration primitives.
 * @note Exact saved VOLUME/CUMULATIVE map to codes 1/2; all other strings to INTENSITY 0. Saved
 *   interval minutes are multiplied by 60 for engine seconds. Snow catch factor is dimensionless.
 *   No rainfall file/time-series source, coordinates or unit selector is assigned.
 */
void applyGageDefaults(SWMM_Engine engine, int idx);

//! While at least one BatchScope is alive, the defaults are read from
//! PreferencesManager once per global SI/US mode and reused. Each read is ~50 QSettings lookups,
//! which dominated batch creation (one per generated rain gage). Opened by
//! BulkEditCommand; changes to preferences are not observed for a mode already cached.
class BatchScope
{
public:
    /**
     * @brief Enter a nested GUI-thread defaults-cache lifetime.
     * @note Increments shared process-local depth; preferences are loaded lazily on first
     *   application per global SI/US mode. Does not pin an engine or begin an engine transaction.
     *   All scopes/callers must serialize on GUI thread; no locking is provided.
     */
    BatchScope();
    /**
     * @brief Leave the defaults-cache lifetime and invalidate both mode caches when outermost scope
     *   ends.
     * @note Does not flush preferences, commit/rollback engine edits or emit model signals. Nested
     *   destruction keeps cached defaults available until depth reaches zero.
     */
    ~BatchScope();
    /**
     * @brief Prohibit copying the scope, preserving balanced shared cache-depth accounting.
     */
    BatchScope(const BatchScope &) = delete;
    /**
     * @brief Prohibit assignment of cache-depth lifetime ownership.
     */
    BatchScope &operator=(const BatchScope &) = delete;
};
}

#endif // OBJECTDEFAULTSAPPLIER_H
