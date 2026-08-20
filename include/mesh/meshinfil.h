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

/*! \file meshinfil.h
 *  \brief Per-cell 2D infiltration: value types, tag/override resolution and
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
 *  - **Destination (engine D-I4):** only `Lost` is accepted by the engine in
 *    this release; the others exist so the grammar is stable and are shown
 *    disabled in the UI.
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

namespace mesh {

struct MeshResult;

/*! Infiltration method. Values mirror the engine's `InfilModel` plus an
 *  explicit `None`. Stored in files as the tokens returned by
 *  infilMethodToken(). */
enum class InfilMethod : int {
    None          = -1,
    Horton        = 0,
    ModHorton     = 1,
    GreenAmpt     = 2,
    ModGreenAmpt  = 3,
    CurveNumber   = 4,
    Constant      = 5
};

/*! Destination of infiltrated water (engine `Infil2DDest`). */
enum class InfilDest : int {
    Lost            = 0,   //!< Only value the engine accepts in this release
    SubcatchAquifer = 1,   //!< Reserved — shown disabled
    Aquifer2D       = 2    //!< Reserved — shown disabled
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
struct InfilRow
{
    InfilMethod method = InfilMethod::None;
    double      p[kInfilMaxParams] = {qQNaN(), qQNaN(), qQNaN(), qQNaN(), qQNaN()};
    InfilDest   dest   = InfilDest::Lost;

    bool isNone() const { return method == InfilMethod::None; }
    bool operator==(const InfilRow &o) const;
    bool operator!=(const InfilRow &o) const { return !(*this == o); }
};

/*! \brief One `[2D_INFILTRATION_DEFAULTS]` row. `tag == "*"` is the mesh-wide
 *         fallback and may appear anywhere in the section. */
struct InfilDefaultRow
{
    QString  tag;
    InfilRow row;
};

/*! \brief `[2D_INFILTRATION_OPTIONS]`. */
struct InfilOptions
{
    /*! Evaluation cadence in seconds. <= 0 means "engine default"
     *  (the project WET_STEP). */
    double infilStep = 0.0;
};

/*! Where a cell's resolved parameters came from — drives the muted/italic
 *  "inherited" rendering in the attribute table and property panels, and is
 *  what undo must restore (GUI plan §3.5(3)). */
enum class InfilProvenance : int {
    None     = 0,   //!< No model resolves for this cell
    Star     = 1,   //!< From the '*' default row
    Tag      = 2,   //!< From a tag default row
    Override = 3    //!< From a per-cell override
};

/*! \brief Result of resolving one triangle's infiltration. */
struct ResolvedInfil
{
    InfilRow        row;
    InfilProvenance provenance = InfilProvenance::None;
    QString         sourceTag;              //!< Tag that supplied it (Tag/Star only)

    bool isOverride() const { return provenance == InfilProvenance::Override; }
    bool isInherited() const
    { return provenance == InfilProvenance::Tag || provenance == InfilProvenance::Star; }
};

// ---------------------------------------------------------------------------
// Resolution
// ---------------------------------------------------------------------------

/*! \brief Resolve one triangle's infiltration per engine D-I3 precedence:
 *         `override > tag row > '*' row > none`.
 *
 *  Reads `MeshResult::infilOverrides`, `MeshResult::infilDefaults` and the
 *  triangle's `MeshTriangle::tag`. Never mutates the mesh. */
ResolvedInfil resolveInfil(const MeshResult &mesh, int tri);

/*! \brief Convenience: the '*' row from \p mesh, or a None row when absent. */
InfilRow starDefault(const MeshResult &mesh);

/*! \brief Index of \p tag in `MeshResult::infilDefaults`, or -1. */
int indexOfDefault(const MeshResult &mesh, const QString &tag);

// ---------------------------------------------------------------------------
// Method ↔ parameter masks
// ---------------------------------------------------------------------------

/*! \brief True when \p method uses positional slot \p slot.
 *
 *  Drives the attribute table's per-row column masking: a cell whose method
 *  does not use a parameter renders "—" and refuses edits, exactly like
 *  `MeshAttributeTableModel::rowIsBoundaryEdge()` does for BC columns on
 *  interior edges. */
bool infilUsesParam(InfilMethod method, int slot);

/*! \brief Number of meaningful positional parameters for \p method. */
int infilParamCount(InfilMethod method);

/*! \brief Display label for positional slot \p slot under \p method,
 *         e.g. ("Horton", 0) → "Max Rate (f0)". Empty when unused. */
QString infilParamLabel(InfilMethod method, int slot);

/*! \brief Registry key for positional slot \p slot under \p method, e.g.
 *         `"infil.f0"`. Empty when unused.
 *
 *  This is the bridge to `mesh::cellParamSpecs()`: the attribute table
 *  registers one column per NAMED key (the union across methods) and masks
 *  per row via infilUsesParam(). */
QByteArray infilParamKey(InfilMethod method, int slot);

/*! \brief Inverse of infilParamKey(): maps a registry key to the positional
 *         slot it occupies under \p method, or -1 when \p method does not use it. */
int infilSlotForKey(InfilMethod method, const QByteArray &key);

/*! \brief Every named parameter key, in registration order. The attribute
 *         table and the assign dialog iterate this. */
const QVector<QByteArray> &infilParamKeys();

// ---------------------------------------------------------------------------
// Tokens (must match the engine exactly)
// ---------------------------------------------------------------------------

QString     infilMethodToken(InfilMethod m);      //!< "HORTON", "CURVE_NUMBER", "NONE", …
InfilMethod infilMethodFromToken(const QString &token, bool *ok = nullptr);
QString     infilMethodLabel(InfilMethod m);      //!< Translated UI label
QStringList infilMethodLabels();                  //!< For EnumComboDelegate, in enum order

QString   infilDestToken(InfilDest d);            //!< "LOST", …
InfilDest infilDestFromToken(const QString &token, bool *ok = nullptr);
QString   infilDestLabel(InfilDest d);
QStringList infilDestLabels();

/*! \brief True when the engine accepts \p d in this release (engine D-I4).
 *         The UI shows the others disabled with an explanatory tooltip. */
bool infilDestSupported(InfilDest d);

// ---------------------------------------------------------------------------
// Classified-lookup tables (GUI plan §3.4(a))
// ---------------------------------------------------------------------------

/*! \brief One row of a classified lookup: a key (or key pair, for the
 *         Curve-Number landuse×HSG case) mapping to a full InfilRow. */
struct InfilLookupEntry
{
    QString  key1;
    QString  key2;    //!< Empty for single-key lookups
    InfilRow row;
};

/*! \brief An editable, reusable classified lookup table. Saved/loaded as CSV
 *         so an agency's standard table travels between projects. */
struct InfilLookupTable
{
    QString                   name;
    bool                      twoKey = false;
    QString                   key1Label;
    QString                   key2Label;
    QVector<InfilLookupEntry> entries;
    InfilRow                  fallback;   //!< Applied to unmatched keys

    /*! Lookup with fallback. \p matched receives whether a real entry hit. */
    InfilRow lookup(const QString &k1, const QString &k2, bool *matched = nullptr) const;
};

/*! \brief CSV round-trip. Column order:
 *  `key1[,key2],method,p0,p1,p2,p3,p4,dest`, `#` comments, header row optional. */
bool saveLookupTableCsv(const InfilLookupTable &table, const QString &path, QString *err);
bool loadLookupTableCsv(InfilLookupTable *table, const QString &path, QString *err);

} // namespace mesh

#endif // OPENSWMMVIS_MESHINFIL_H
