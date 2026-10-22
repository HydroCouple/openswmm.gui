// SPDX-License-Identifier: Apache-2.0
//
// Copyright 2026 Caleb Buahin
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/**
 * @file      meshinfil.h
 * @brief     Per-cell 2D infiltration: value types, tag/override resolution and
 *         method↔parameter masks.
 *
 *  Implements `workplans/INTEGRATED2D_GW_GUI_PLAN_2026-08-15.md` §3.2a
 *  (phase GG0). Mirrors the engine contract in
 *  `openswmm.engine/src/engine/2d/infil/Infil2D.hpp` — the two MUST agree on
 *  method tokens, positional parameter order, destination tokens and
 *  resolution precedence, because they read and write the same
 *  `[2D_INFILTRATION*]` sections.
 *
 *  Key invariants:
 *  - **Resolution (engine D-I3):** `per-cell override > tag row > '*' row >
 *    none`. The GUI never flattens tag inheritance into per-cell rows.
 *  - **Units (user decision 2026-08-20):** parameters are in PROJECT units —
 *    the same numbers a user types into `[INFILTRATION]`. The GUI stores and
 *    displays them verbatim and performs no conversion.
 *  - **Destination:** Lost leaves the model and SubcatchAquifer routes to
 *    the containing subcatchment's legacy aquifer. Aquifer2D is retained as
 *    a legacy file token for review/migration; current engine validation
 *    rejects it as obsolete. Integrated aquifer capacity is assigned through
 *    the separate groundwater ownership workflow.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   Apache-2.0
 */

#ifndef OPENSWMMVIS_MESHINFIL_H
#define OPENSWMMVIS_MESHINFIL_H

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>
#include <QtNumeric>

namespace mesh
{

struct MeshResult;

/*! Infiltration method. Values mirror the engine's `InfilModel` plus an
 *  explicit `None`. Stored in files as the tokens returned by
 *  infilMethodToken(). */
enum class InfilMethod : int {
    None = -1,
    Horton = 0,
    ModHorton = 1,
    GreenAmpt = 2,
    ModGreenAmpt = 3,
    CurveNumber = 4,
    Constant = 5
};

/*! Destination of infiltrated water (engine `Infil2DDest`). */
enum class InfilDest : int {
    Lost = 0,            //!< Leaves the model
    SubcatchAquifer = 1, //!< U3: recharges the containing subcatchment's aquifer
    Aquifer2D = 2 //!< Legacy token, rejected by current engine validation; migrate ownership.
};

/*! Widest positional parameter count (Horton: f0 fmin decay dry_time Fmax). */
inline constexpr int kInfilMaxParams = 5;

/*! \brief One infiltration specification: method + positional parameters + destination.
 *
 *  Parameters are POSITIONAL and in PROJECT units, matching legacy
 *  `[INFILTRATION]`:
 *
 *  | method         | p0          | p1   | p2           | p3          | p4   |
 *  |----------------|-------------|------|--------------|-------------|------|
 *  | Horton         | f0          | fmin | decay (1/hr) | dry_time(d) | Fmax |
 *  | ModHorton      | f0          | fmin | decay (1/hr) | dry_time(d) | Fmax |
 *  | GreenAmpt      | S (suction) | Ks   | IMD          | —           | —    |
 *  | ModGreenAmpt   | S (suction) | Ks   | IMD          | —           | —    |
 *  | CurveNumber    | CN          | —    | dry_time(d)  | —           | —    |
 *  | Constant       | rate        | —    | —            | —           | —    |
 *
 *  NaN in a slot means "not set"; the writer emits `-` for unused columns.
 */
struct InfilRow {
    InfilMethod method = InfilMethod::None;
    double p[kInfilMaxParams] = {qQNaN(), qQNaN(), qQNaN(), qQNaN(), qQNaN()};
    InfilDest dest = InfilDest::Lost;

    /**
     * @brief Test whether this row explicitly selects no infiltration method.
     * @return True only for InfilMethod::None, false for every other enum value including
     * unknown values. Does not validate parameters/destination or resolve inheritance; constant
     * read-only query.
     */
    bool isNone() const { return method == InfilMethod::None; }
    /**
     * @brief Compare method, destination and every positional parameter, treating unset NaNs as
     * equal.
     * @param[in] o Borrowed row, unchanged.
     * @return True only when methods/destinations match and each of five slots compares exactly
     * or both values are NaN. Even unused slots participate, infinities use ordinary equality
     * and no numeric tolerance or method-specific validation occurs. No mutation, allocation or
     * unit conversion.
     */
    bool operator==(const InfilRow &o) const;
    /**
     * @brief Negate all-slot infiltration-row equality with its NaN-as-unset convention.
     * @param[in] o Borrowed other row, unchanged.
     * @return False for equal method/destination/all parameters, true otherwise; no tolerance or
     * validation. Read only, with no allocation or ownership transfer.
     */
    bool operator!=(const InfilRow &o) const { return !(*this == o); }
};

/*! \brief One `[2D_INFILTRATION_DEFAULTS]` row. `tag == "*"` is the mesh-wide
 *         fallback and may appear anywhere in the section. */
struct InfilDefaultRow {
    QString tag;
    InfilRow row;
};

/*! \brief `[2D_INFILTRATION_OPTIONS]`. */
struct InfilOptions {
    /*! Evaluation cadence in seconds. <= 0 means "engine default"
     *  (the project WET_STEP). */
    double infilStep = 0.0;
};

/*! Where a cell's resolved parameters came from — drives the muted/italic
 *  "inherited" rendering in the attribute table and property panels, and is
 *  what undo must restore (GUI plan §3.5(3)). */
enum class InfilProvenance : int {
    None = 0,    //!< No model resolves for this cell
    Star = 1,    //!< From the '*' default row
    Tag = 2,     //!< From a tag default row
    Override = 3 //!< From a per-cell override
};

/*! \brief Result of resolving one triangle's infiltration. */
struct ResolvedInfil {
    InfilRow row;
    InfilProvenance provenance = InfilProvenance::None;
    QString sourceTag; //!< Tag that supplied it (Tag/Star only)

    /**
     * @brief Check the resolved row's provenance rather than whether a kernel is enabled.
     * @return True only for per-cell Override provenance, including an explicit None override.
     * Read-only query; does not resolve the mesh or inspect method parameters.
     */
    bool isOverride() const { return provenance == InfilProvenance::Override; }
    /**
     * @brief Check whether a resolved row came from a tag or wildcard default.
     * @return True for Tag or Star provenance, including inherited None rows; false for
     * None/Override. Constant read-only query, independent of method validity or whether a
     * solver kernel runs.
     */
    bool isInherited() const
    {
        return provenance == InfilProvenance::Tag || provenance == InfilProvenance::Star;
    }
};

// ---------------------------------------------------------------------------
// Resolution
// ---------------------------------------------------------------------------

/**
 * @brief Resolve an owned infiltration specification while preserving its authoring provenance.
 * @param[in] mesh Borrowed stable cell/default/override arrays, unchanged.
 * @param[in] tri Zero-based cell index, bounds checked before looking for an override.
 * @return Owned row with Override then first matching nonempty cell-tag default then first
 * wildcard precedence, or default None/no provenance for invalid indices or no matching row. An
 * explicit None stops inheritance and retains its source provenance. Parameters remain in
 * project units, sourceTag is populated only for tag/wildcard rows. No physical validation,
 * override materialization, unit conversion or engine mutation; borrowed references are not
 * retained.
 *
 * @details Resolve one triangle's infiltration per engine D-I3 precedence: `override > tag row >
 * '*' row > none`.
 *
 * Reads `MeshResult::infilOverrides`, `MeshResult::infilDefaults` and the triangle's
 * `MeshTriangle::tag`. Never mutates the mesh.
 */
ResolvedInfil resolveInfil(const MeshResult &mesh, int tri);

/**
 * @brief Copy the first wildcard infiltration default, if present.
 * @param[in] mesh Borrowed stable defaults array, unchanged.
 * @return Owned first row whose case-sensitive tag is *, or a default None row when absent. Does
 * not validate method/parameters or distinguish absent wildcard from an explicit None wildcard.
 * Linear search, no mesh mutation or unit conversion.
 *
 * @details Convenience: the '*' row from \p mesh, or a None row when absent.
 */
InfilRow starDefault(const MeshResult &mesh);

/**
 * @brief Find the first exact case-sensitive default tag match.
 * @param[in] mesh Borrowed stable default rows, unchanged.
 * @param[in] tag Borrowed tag, compared without trimming or normalization; empty and * are
 * allowed.
 * @return Zero-based first matching index or -1 when absent. Duplicate tags are not rejected.
 * Linear read-only scan, no geometry or inheritance resolution.
 *
 * @details Index of \p tag in `MeshResult::infilDefaults`, or -1.
 */
int indexOfDefault(const MeshResult &mesh, const QString &tag);

// ---------------------------------------------------------------------------
// Method ↔ parameter masks
// ---------------------------------------------------------------------------

/**
 * @brief Check whether a model uses a zero-based positional parameter slot.
 * @param[in] method Model enum, unknown/None values use no slots.
 * @param[in] slot Position 0..4; any outside position returns false.
 * @return True for all five Horton slots, first three Green-Ampt slots, Curve Number slots 0 and
 * 2, or Constant slot 0; false otherwise. No parameter values read, validation or unit
 * conversion.
 *
 * @details True when \p method uses positional slot \p slot.
 *
 * Drives the attribute table's per-row column masking: a cell whose method does not use a
 * parameter renders "—" and refuses edits, exactly like
 * `MeshAttributeTableModel::rowIsBoundaryEdge()` does for BC columns on interior edges.
 */
bool infilUsesParam(InfilMethod method, int slot);

/**
 * @brief Report serialized parameter-column width rather than number of active values.
 * @param[in] method Infiltration method, unknown/None values yield zero.
 * @return Five for Horton variants, three for Green-Ampt and Curve Number, one for Constant,
 * zero otherwise. Curve Number's middle placeholder is included although infilUsesParam()
 * rejects it. Read-only query.
 *
 * @details Number of serialized positional columns for \p method, including unused placeholders.
 */
int infilParamCount(InfilMethod method);

/**
 * @brief Translate the model-specific label for an active positional parameter.
 * @param[in] method Model enum, unchanged.
 * @param[in] slot Zero-based slot, valid only when infilUsesParam(method,slot) is true.
 * @return Owned MeshInfil-context label, empty for unused/out-of-range/unknown combinations.
 * Labels distinguish method semantics, not values; no model mutation or unit conversion.
 *
 * @details Display label for positional slot \p slot under \p method, e.g. ("Horton", 0) → "Max
 * Rate (f0)". Empty when unused.
 */
QString infilParamLabel(InfilMethod method, int slot);

/**
 * @brief Map an active model slot to its case-sensitive shared registry key.
 * @param[in] method Model enum, unchanged.
 * @param[in] slot Zero-based positional slot, 0..4; unused/out-of-range combinations return
 * empty.
 * @return Owned byte key such as infil.f0 or infil.dryTime, empty if unused/unknown. Drying time
 * maps from Horton slot 3 and Curve Number slot 2. No registry mutation or numeric parameter
 * access.
 *
 * @details Registry key for positional slot \p slot under \p method, e.g. `"infil.f0"`. Empty
 * when unused.
 *
 * This is the bridge to `mesh::cellParamSpecs()`: the attribute table registers one column per
 * NAMED key (the union across methods) and masks per row via infilUsesParam().
 */
QByteArray infilParamKey(InfilMethod method, int slot);

/**
 * @brief Resolve a named parameter key to its position under one model.
 * @param[in] method Model enum used to interpret slot semantics, unchanged.
 * @param[in] key Borrowed case-sensitive registry key; empty/unknown keys do not match.
 * @return Slot 0..4 when active for this method, -1 otherwise. No inheritance resolution,
 * parameter validation or mutation; constant-size scan of the five slots.
 *
 * @details Inverse of infilParamKey(): maps a registry key to the positional slot it occupies
 * under \p method, or -1 when \p method does not use it.
 */
int infilSlotForKey(InfilMethod method, const QByteArray &key);

/**
 * @brief Return the immutable union of named infiltration parameter keys in registration order.
 * @return Borrowed process-lifetime vector, ten distinct case-sensitive keys; do not delete or
 * mutate. Lazy static initialization may allocate once. Keys shared across methods appear once,
 * so callers must use method-specific slot lookup instead of interpreting list positions as
 * parameter slots.
 *
 * @details Every named parameter key, in registration order. The attribute table and the assign
 * dialog iterate this.
 */
const QVector<QByteArray> &infilParamKeys();

// ---------------------------------------------------------------------------
// Tokens (must match the engine exactly)
// ---------------------------------------------------------------------------

/**
 * @brief Encode an infiltration method using the stable uppercase file vocabulary.
 * @param[in] m Method enum, unchanged.
 * @return Owned HORTON, MODIFIED_HORTON, GREEN_AMPT, MODIFIED_GREEN_AMPT, CURVE_NUMBER or
 * CONSTANT; None and unknown enum values both return NONE. No validation, locale translation or
 * mutation.
 */
QString infilMethodToken(InfilMethod m); //!< "HORTON", "CURVE_NUMBER", "NONE", …
/**
 * @brief Parse a trimmed case-insensitive infiltration method token.
 * @param[in] token Borrowed stable text, unchanged; complete token must match the file
 * vocabulary.
 * @param[out] ok Optional borrowed recognition flag, true for a known token including NONE,
 * false otherwise.
 * @return Recognized method or None on unknown/empty input. Use ok to distinguish explicit NONE
 * from failure. No parameter validation, unit conversion, ownership retention or model mutation.
 */
InfilMethod infilMethodFromToken(const QString &token, bool *ok = nullptr);
/**
 * @brief Translate a display label for an infiltration method.
 * @param[in] m Method enum, unchanged; unknown values use the None label.
 * @return Owned MeshInfil-context label using current application translators, with no model
 * mutation.
 */
QString infilMethodLabel(InfilMethod m); //!< Translated UI label
/**
 * @brief Build translated method labels in consecutive enum order from None through Constant.
 * @return Owned seven-label list; index 0 corresponds to enum -1, so enum value equals label
 * index minus 1. Uses current translators on each call, allocates value storage and changes no
 * model state.
 */
QStringList infilMethodLabels(); //!< For EnumComboDelegate, in enum order

/**
 * @brief Encode a destination enum as its retained uppercase file token.
 * @param[in] d Destination enum, unchanged.
 * @return Owned SUBCATCH_AQUIFER or AQUIFER_2D for those values, LOST for Lost or unknown
 * values. Encoding AQUIFER_2D preserves legacy authoring data but does not imply current engine
 * support; current validation rejects it as obsolete. No model mutation or availability check.
 */
QString infilDestToken(InfilDest d); //!< "LOST", …
/**
 * @brief Parse a retained destination token independently of current engine support.
 * @param[in] token Borrowed complete case-insensitive token, surrounding whitespace ignored.
 * @param[out] ok Optional borrowed flag, true for LOST/SUBCATCH_AQUIFER/AQUIFER_2D, false
 * otherwise.
 * @return Recognized destination or Lost for unknown/empty input. AQUIFER_2D is recognized for
 * legacy review/migration despite current validation rejection. No model existence or routing
 * checks.
 */
InfilDest infilDestFromToken(const QString &token, bool *ok = nullptr);
/**
 * @brief Translate the display label of a retained destination value.
 * @param[in] d Destination enum, unchanged; unknown values use Lost.
 * @return Owned MeshInfil-context label. A 2D Aquifer label names a legacy enum, not a support
 * guarantee; no model validation or routing mutation occurs.
 */
QString infilDestLabel(InfilDest d);
/**
 * @brief Build translated destination labels in enum order Lost, SubcatchAquifer, Aquifer2D.
 * @return Owned three-label list indexed by enum integer, including the retained legacy
 * Aquifer2D option. Does not filter unsupported choices or inspect model ownership; no model
 * mutation.
 */
QStringList infilDestLabels();

/**
 * @brief Translate destination guidance without resolving any open model.
 * @param[in] d Destination enum; unknown values use the Lost guidance.
 * @return Owned explanation: lost outflow, legacy subcatchment recharge, or obsolete Aquifer2D
 * migration via groundwater ownership review. This is authoring guidance, not a runtime
 * support/coverage result; no model handle, geometry lookup or state mutation is involved.
 *
 * @details A one-sentence tooltip for destination \p d: where the water goes and what the choice
 * needs.
 *
 * The hint describes authoring guidance only; it does not inspect a model. Aquifer2D is retained
 * for legacy-file migration and its hint directs users to the groundwater ownership review
 * workflow.
 */
QString infilDestHint(InfilDest d);

// ---------------------------------------------------------------------------
// Classified-lookup tables (GUI plan §3.4(a))
// ---------------------------------------------------------------------------

/*! \brief One row of a classified lookup: a key (or key pair, for the
 *         Curve-Number landuse×HSG case) mapping to a full InfilRow. */
struct InfilLookupEntry {
    QString key1;
    QString key2; //!< Empty for single-key lookups
    InfilRow row;
};

/*! \brief An editable, reusable classified lookup table. Saved/loaded as CSV
 *         so an agency's standard table travels between projects. */
struct InfilLookupTable {
    QString name;
    bool twoKey = false;
    QString key1Label;
    QString key2Label;
    QVector<InfilLookupEntry> entries;
    InfilRow fallback; //!< Applied to unmatched keys

    /**
     * @brief Copy the first classified entry matching one or two case-insensitive keys.
     * @param[in] k1 Borrowed first key, compared without trimming, unchanged.
     * @param[in] k2 Borrowed second key, used only when twoKey is true; unchanged.
     * @param[out] matched Optional borrowed flag, true for an entry hit and false for fallback.
     * @return Owned matching InfilRow or fallback, retaining project-unit values exactly.
     * Entries are searched in order; duplicates select first, and literal * has no special
     * matching behavior here. No validation, interpolation, unit conversion, source mutation or
     * pointer retention.
     *
     * @details Lookup with fallback. \p matched receives whether a real entry hit.
     */
    InfilRow lookup(const QString &k1, const QString &k2, bool *matched = nullptr) const;
};

/**
 * @brief Serialize classified infiltration entries and optional fallback to a QSaveFile CSV
 * replacement.
 * @param[in] table Borrowed table, unchanged; names/keys/metadata must not contain
 * commas/newlines that need CSV quoting, because this simple format performs no escaping.
 * Parameters retain project units.
 * @param[in] path Borrowed output pathname, parent directory must already be writable;
 * unchanged.
 * @param[out] err Optional borrowed error string, overwritten on open/commit failure and left
 * untouched on success. No source pointer retained.
 * @return True when commit succeeds, false on open/commit failure. Writes UTF-8,
 * 12-significant-digit numeric fields and '-' for NaN, with all five slots retained even if
 * unused. Non-None fallback writes as a * row; None fallback is omitted. No independent
 * write-count/stream-status validation or physical parameter validation is performed. Existing
 * destination replacement follows QSaveFile semantics.
 *
 * @details CSV round-trip. Column order: `key1[,key2],method,p0,p1,p2,p3,p4,dest`, `#` comments,
 * header row optional.
 */
bool saveLookupTableCsv(const InfilLookupTable &table, const QString &path, QString *err);
/**
 * @brief Load the permissive unquoted classified-lookup CSV format into a fresh table value.
 * @param[out] table Required borrowed destination; null returns false. Untouched when opening
 * fails; reset after successful open and then populated incrementally, including a basename
 * fallback name.
 * @param[in] path Borrowed UTF-8 input pathname, unchanged.
 * @param[out] err Optional borrowed string, overwritten only on open failure; not cleared on
 * success or null-destination refusal.
 * @return False only for null destination/open failure, true after parsing even empty/malformed
 * content. Unknown-method/header rows are skipped; absent/unparseable parameters stay NaN,
 * unknown destination falls back Lost, * key1 sets fallback and last fallback wins. KEY2
 * metadata or nine-or-more columns selects two-key parsing; no CSV quoting, physical validation
 * or later read-error check is performed. Allocates owned entries, retains project-unit values
 * and does not mutate any mesh or engine.
 */
bool loadLookupTableCsv(InfilLookupTable *table, const QString &path, QString *err);

} // namespace mesh

#endif // OPENSWMMVIS_MESHINFIL_H
