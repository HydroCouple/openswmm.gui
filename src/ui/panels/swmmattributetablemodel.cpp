/*!
 * \file   swmmattributetablemodel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/panels/swmmattributetablemodel.h"

#include "core/unitsystem.h"
#include "ui/models/userflagsmodel.h"
#include "ui/properties/culvertcodes.h"      // ATTRIBUTE_EDITOR_WIRING Phase 0
#include "ui/properties/dataobjectref.h"     // pump-curve picker cell
#include "ui/properties/rainintervalref.h"   // DA.2 parity — H:MM interval helpers
#include "ui/properties/linkcompoundeditref.h"
#include "ui/properties/nodecompoundeditref.h"
#include "ui/properties/subcatchcompoundeditref.h"  // Phase 3 compound cells
#include "ui/properties/userflagseditref.h"  // per-object User Flags cell
#include "ui/properties/storageshapegeom.h"  // storage-shape dimension applicability
#include "ui/properties/xsectshapegeom.h"    // inline xsect-geom applicability

#include <QUndoCommand>
#include <QUndoStack>

#include <cmath>

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_gages.h>
#include <openswmm/engine/openswmm_infrastructure.h>
#include <openswmm/engine/openswmm_inflows.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_model.h>    // gage data-file path registry
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_pollutants.h>
#include <openswmm/engine/openswmm_quality.h>
#include <openswmm/engine/openswmm_spatial.h>
#include <openswmm/engine/openswmm_subcatchments.h>
#include <openswmm/engine/openswmm_tables.h>   // pump-curve picker cell

namespace {

using openswmmvis::ColumnSpec;
using openswmmvis::EditorKind;
using openswmmvis::UnitKind;

// What kind of engine entity backs a setter — used to pick the
// right `swmm_*_index` for the name → idx lookup at commit time.
enum class EntityKind { Node, Link, Subcatch, Gage };

int indexForName(SWMM_Engine engine, EntityKind kind, const char *name) {
    switch (kind) {
    case EntityKind::Node:     return swmm_node_index(engine, name);
    case EntityKind::Link:     return swmm_link_index(engine, name);
    case EntityKind::Subcatch: return swmm_subcatch_index(engine, name);
    case EntityKind::Gage:     return swmm_gage_index(engine, name);
    }
    return -1;
}

// Phase 3 of docs/USER_FLAGS_UI_PLAN_2026-06-03.md — column-key prefix
// and category → [USER_FLAG_VALUES] ObjectType token mapping for the
// per-flag columns. Categories without a token (none today) get no
// flag columns.
const QString kUserFlagKeyPrefix = QStringLiteral("userflag:");

QString userFlagObjectType(SWMMModelLayer::Category cat) {
    switch (cat) {
    case SWMMModelLayer::CatJunctions:
    case SWMMModelLayer::CatOutfalls:
    case SWMMModelLayer::CatStorage:
    case SWMMModelLayer::CatDividers:
        return QStringLiteral("NODE");
    case SWMMModelLayer::CatConduits:
    case SWMMModelLayer::CatPumps:
    case SWMMModelLayer::CatOrifices:
    case SWMMModelLayer::CatWeirs:
    case SWMMModelLayer::CatOutlets:
        return QStringLiteral("LINK");
    case SWMMModelLayer::CatSubcatchments:
        return QStringLiteral("SUBCATCHMENT");
    case SWMMModelLayer::CatRainGages:
        return QStringLiteral("GAGE");
    default:
        return QString();
    }
}

// Helper — read-only column.
ColumnSpec ro(const QString &key, const QString &label) {
    ColumnSpec c;
    c.key = key; c.label = label;
    return c;
}

// Helper — editable name column (col 0 in every schema).
ColumnSpec nameCol() {
    ColumnSpec c;
    c.key    = QStringLiteral("Name");
    c.label  = QStringLiteral("Name");
    c.editor = EditorKind::Text;
    c.setter = QStringLiteral("rename");
    return c;
}

// DB.5 — free-form `[TAGS]` label, editable text. Single helper so every
// node/link/subcatch category gets the same column shape.
ColumnSpec tagCol(const QString &setterTag) {
    ColumnSpec c;
    c.key    = QStringLiteral("Tag");
    c.label  = QStringLiteral("Tag");
    c.editor = EditorKind::Text;
    c.setter = setterTag;
    return c;
}

// ATTRIBUTE_EDITOR_WIRING Phase 1 — integer editable column
// (IntegerDelegate / QSpinBox). First user: conduit Barrels.
ColumnSpec intCol(const QString &key, const QString &label,
                   const QString &setter, int minVal, int maxVal) {
    ColumnSpec c;
    c.key = key; c.label = label;
    c.editor = EditorKind::Integer;
    c.setter = setter;
    c.minValue = minVal; c.maxValue = maxVal;
    return c;
}

// Helper — numeric editable column.
ColumnSpec num(const QString &key, const QString &label,
                const QString &setter,
                double minVal = -std::numeric_limits<double>::infinity(),
                double maxVal =  std::numeric_limits<double>::infinity(),
                int decimals = 4,
                UnitKind unit = UnitKind::None) {
    ColumnSpec c;
    c.key = key; c.label = label;
    c.editor = EditorKind::Numeric;
    c.setter = setter;
    c.minValue = minVal; c.maxValue = maxVal; c.decimals = decimals;
    c.unit = unit;
    return c;
}

// Round-4 follow-up 2026-05-12 — resolve the semantic unit kind to
// the display string for the active flow-units system.  Returns an
// empty string for UnitKind::None so headers/tooltips drop the
// suffix entirely.  Falls back to US-customary when no project is
// bound (UnitSystem::instance()->isSI() returns false in that case).
QString unitLabel(UnitKind kind)
{
    auto *us = UnitSystem::instance();
    const bool si = us && us->isSI();
    switch (kind) {
    case UnitKind::None:         return {};
    case UnitKind::Length:       return us ? us->lengthLabel() : QStringLiteral("ft");
    case UnitKind::Area:         return si ? QStringLiteral("m²") : QStringLiteral("ft²");
    case UnitKind::SubcatchArea: return si ? QStringLiteral("ha") : QStringLiteral("ac");
    case UnitKind::Volume:       return us ? us->volumeLabel()   : QStringLiteral("ft³");
    case UnitKind::Velocity:     return us ? us->velocityLabel() : QStringLiteral("ft/s");
    case UnitKind::FlowRate:     return us ? us->flowUnitLabel() : QStringLiteral("CFS");
    case UnitKind::Depression:   return si ? QStringLiteral("mm") : QStringLiteral("in");
    case UnitKind::Percent:      return QStringLiteral("%");
    case UnitKind::Rate:         return si ? QStringLiteral("mm/hr") : QStringLiteral("in/hr");
    }
    return {};
}

// Helper — enum editable column.  Sub-type / configuration enums
// (OutfallType, FlapGate, PumpInitState, CulvertCode) — NOT the
// object-type columns (Node type / Link type stay read-only per
// user rule 2026-05-11).
ColumnSpec enumCol(const QString &key, const QString &label,
                    const QString &setter,
                    const QVariantList &values) {
    ColumnSpec c;
    c.key = key; c.label = label;
    c.editor = EditorKind::Enum;
    c.setter = setter;
    c.enumValues = values;
    return c;
}

// Forward declarations for the enum pair-list builders defined
// further below.  `schemaForCategory` calls them, so they need
// to be visible at parse time.
QVariantList outfallTypeValues();
QVariantList yesNoValues();
QVariantList offOnValues();
QVariantList culvertCodeValues();
QVariantList dividerTypeValues();
// Slice AG.4 — storage geometry shape enum.
QVariantList storageShapeValues();
QVariantList infilModelValues();
// ATTRIBUTE_EDITOR_WIRING Phase 1 — link sub-type enums.
QVariantList orificeTypeValues();
QVariantList weirTypeValues();
QVariantList outletRatingTypeValues();
// ATTRIBUTE_EDITOR_WIRING parity pass — rain gage enums.
QVariantList gageRainTypeValues();
QVariantList gageDataSourceValues();
QVariantList gageRainUnitsValues();

// Compound-attribute column (Inflows / DWF / RDII / Treatment). The
// cell holds a NodeCompoundEditRef built live in data(); the delegate
// (CompoundEditDelegate) instantiates a NodeCompoundEditButton that
// reuses the same NodeCompoundEditDialog the Property Browser opens.
// The `setter` tag is a sentinel — actual writes happen inside the
// dialog via swmm_rdii_add / swmm_treatment_set / etc., so the table
// model's commitValueDirect path just refreshes the row when the
// delegate fires setData (so other columns reflecting the same engine
// state stay current).
//
// §S.SC.1.c (2026-05-25) — the same Compound editor kind also drives
// link-side compound cells. The setter tag's prefix
// ("node_*" vs "link_*") is what `data()` keys off to decide whether
// to build a `NodeCompoundEditRef` (→ NodeCompoundEditDialog) or a
// `LinkCompoundEditRef` (→ LinkCompoundEditDialog). The delegate
// (`CompoundEditDelegate`) dispatches on the cell-value type, so both
// kinds round-trip through the same delegate without a second
// EditorKind enumerator.
ColumnSpec compoundCol(const QString &key, const QString &label,
                        const QString &setterTag) {
    ColumnSpec c;
    c.key    = key;
    c.label  = label;
    c.editor = EditorKind::Compound;
    c.setter = setterTag;
    return c;
}

// §S.SC.1.c — Short summary string for a link's [XSECTIONS] state,
// used as the read-only label on the XSection compound cell so the
// user sees "CIRCULAR (3.0 ft)" / "IRREGULAR (Creek-A)" without
// opening the dialog. Mirrors LinkCompoundEditDialog::computeXsectSummary
// but pure (no dialog member access) so the table can call it cell-
// by-cell.
QString xsectSummaryFor(SWMM_Engine eng, int linkIdx)
{
    if (!eng || linkIdx < 0) return QString();
    int shape = 0;
    double g1 = 0, g2 = 0, g3 = 0, g4 = 0;
    if (swmm_link_get_xsect(eng, linkIdx, &shape, &g1, &g2, &g3, &g4) != SWMM_OK)
        return QString();

    static const char *shapeNames[] = {
        "CIRCULAR", "FILLED_CIRCULAR", "RECT_CLOSED", "RECT_OPEN",
        "TRAPEZOIDAL", "TRIANGULAR", "PARABOLIC", "POWER",
        "RECT_TRIANGULAR", "RECT_ROUND", "MOD_BASKETHANDLE",
        "HORIZ_ELLIPSE", "VERT_ELLIPSE", "ARCH", "EGGSHAPED",
        "HORSESHOE", "GOTHIC", "CATENARY", "SEMIELLIPTICAL", "IRREGULAR",
    };
    const QString shapeStr = (shape >= 0 && shape < 20)
        ? QString::fromLatin1(shapeNames[shape])
        : QStringLiteral("UNKNOWN");

    // IRREGULAR: geom1 is the transect index — resolve to name so the
    // cell isn't cryptic.
    if (shape == /*IRREGULAR*/ 19) {
        const int tIdx = static_cast<int>(std::lround(g1));
        QString txName;
        if (tIdx >= 0 && tIdx < swmm_transect_count(eng)) {
            if (const char *id = swmm_transect_id(eng, tIdx))
                txName = QString::fromUtf8(id);
        }
        return QStringLiteral("%1 (%2)").arg(shapeStr,
                                              txName.isEmpty()
                                                  ? QStringLiteral("—") : txName);
    }
    if (g2 > 0.0)
        return QStringLiteral("%1 (%2 × %3)")
                   .arg(shapeStr).arg(g1, 0, 'g', 4).arg(g2, 0, 'g', 4);
    return QStringLiteral("%1 (%2)").arg(shapeStr).arg(g1, 0, 'g', 4);
}

// Slice DB — coordinate columns, now editable. Setters route through
// `SWMMModelLayer::applyNodeMove` (special-cased in `commitValueDirect`)
// because swmm_spatial_set_node_coord is a two-arg setter that doesn't
// fit the single-double SetterEntry shape, AND because applyNodeMove is
// what refreshes the cached scene coords + attached-link bboxes so the
// canvas tracks the edit without waiting for a geometry rebuild.
ColumnSpec nodeCoordX() {
    return num(QStringLiteral("X"), QStringLiteral("X Coordinate"),
               QStringLiteral("node_coord_x"),
               -1e12, 1e12, 4, UnitKind::None);
}
ColumnSpec nodeCoordY() {
    return num(QStringLiteral("Y"), QStringLiteral("Y Coordinate"),
               QStringLiteral("node_coord_y"),
               -1e12, 1e12, 4, UnitKind::None);
}

// Slice DB — read-only computed + statistics summary columns shared by
// all four node categories. Values come from `identifyByName()` which
// queries the engine getters; pre-run, the stat_* values read back as
// zero. Editor stays `ReadOnly` so the cells display but don't accept
// input.
QList<ColumnSpec> nodeStatBlock() {
    return {
        // Input-time computed.
        ro(QStringLiteral("Crown elev"),  QStringLiteral("Crown Elev.")),
        ro(QStringLiteral("Full volume"), QStringLiteral("Full Volume")),
        ro(QStringLiteral("Degree"),      QStringLiteral("Connected Links")),
        // Post-run summary statistics.
        ro(QStringLiteral("Max depth (stat)"),  QStringLiteral("Max Depth (Sim.)")),
        ro(QStringLiteral("Max overflow"),      QStringLiteral("Max Overflow")),
        ro(QStringLiteral("Vol flooded"),       QStringLiteral("Vol. Flooded")),
        ro(QStringLiteral("Time flooded (hr)"), QStringLiteral("Time Flooded (hr)")),
    };
}

// Column schema per category.  Z.5.1 made `Name` always column 0;
// the rest are sourced from `identifyByName()` for display and
// (when editable) from a setter dispatch table for commit.
QList<ColumnSpec> schemaForCategory(SWMMModelLayer::Category cat)
{
    switch (cat) {
    case SWMMModelLayer::CatJunctions: {
        QList<ColumnSpec> cols = {
            nameCol(),
            ro("Node type",  "Node Type"),
            tagCol("node_tag"),
            nodeCoordX(),
            nodeCoordY(),
            num("Invert elev",     "Invert Elevation",   "node_invert_elev",
                                                          -1e9, 1e9, 4, UnitKind::Length),
            num("Max depth",       "Maximum Depth",      "node_max_depth",
                                                          0.0, 1e6, 4, UnitKind::Length),
            num("Init depth",      "Initial Depth",      "node_initial_depth",
                                                          0.0, 1e6, 4, UnitKind::Length),
            num("Surcharge depth", "Surcharge Depth",    "node_surcharge_depth",
                                                          0.0, 1e6, 4, UnitKind::Length),
            num("Ponded area",     "Ponded Area",        "node_ponded_area",
                                                          0.0, 1e9, 2, UnitKind::Area),
        };
        cols.append(nodeStatBlock());
        // Compound-attribute columns — each cell shows a "summary —
        // Edit…" button that opens NodeCompoundEditDialog at the right
        // page. Same widget the Property Browser uses, so editing from
        // either view round-trips identically.
        cols.append(compoundCol("Inflows",   "External Inflows",
                                  "node_inflows_ref"));
        cols.append(compoundCol("DWF",       "Dry Weather Flow",
                                  "node_dwf_ref"));
        cols.append(compoundCol("RDII",      "RDII",
                                  "node_rdii_ref"));
        cols.append(compoundCol("Treatment", "Pollutant Treatment",
                                  "node_treatment_ref"));
        return cols;
    }
    case SWMMModelLayer::CatOutfalls: {
        // SWMM5 [OUTFALLS]: Name | Elev | Type | Gated | (StageData when FIXED).
        // No MaxDepth / InitDepth / SurDepth / Aponded at a boundary node.
        // ATTRIBUTE_EDITOR_WIRING parity pass (2026-06-04) — stage-data
        // rows (fixed stage / tidal curve / stage time series) + the
        // four node compound cells, Property Browser parity (Slice
        // DA.4.3 rows on SWMMOutfallPropertyAdapter).
        QList<ColumnSpec> cols = {
            nameCol(),
            ro("Node type",  "Node Type"),
            tagCol("node_tag"),
            nodeCoordX(),
            nodeCoordY(),
            num("Invert elev", "Invert Elevation",   "node_invert_elev",
                                                      -1e9, 1e9, 4, UnitKind::Length),
            enumCol("Outfall type", "Boundary Type",
                                                "node_outfall_type",
                                                outfallTypeValues()),
            num("Stage elev",  "Fixed Stage Elevation", "node_outfall_stage",
                                                      -1e9, 1e9, 4, UnitKind::Length),
            compoundCol("Tidal curve", "Tidal Curve", "node_outfall_tidal_ref"),
            compoundCol("Stage series", "Stage Time Series",
                                                "node_outfall_timeseries_ref"),
            enumCol("Flap gate",   "Flap Gate",
                                                "node_outfall_flap_gate",
                                                yesNoValues()),
        };
        cols.append(nodeStatBlock());
        cols.append(compoundCol("Inflows",   "External Inflows",  "node_inflows_ref"));
        cols.append(compoundCol("DWF",       "Dry Weather Flow",  "node_dwf_ref"));
        cols.append(compoundCol("RDII",      "RDII",              "node_rdii_ref"));
        cols.append(compoundCol("Treatment", "Pollutant Treatment",
                                  "node_treatment_ref"));
        return cols;
    }
    case SWMMModelLayer::CatStorage: {
        QList<ColumnSpec> cols = {
            nameCol(),
            ro("Node type",  "Node Type"),
            tagCol("node_tag"),
            nodeCoordX(),
            nodeCoordY(),
            num("Invert elev",     "Invert Elevation",   "node_invert_elev",
                                                          -1e9, 1e9, 4, UnitKind::Length),
            num("Max depth",       "Maximum Depth",      "node_max_depth",
                                                          0.0, 1e6, 4, UnitKind::Length),
            num("Init depth",      "Initial Depth",      "node_initial_depth",
                                                          0.0, 1e6, 4, UnitKind::Length),
            num("Surcharge depth", "Surcharge Depth",    "node_surcharge_depth",
                                                          0.0, 1e6, 4, UnitKind::Length),
            num("Seep rate",       "Seepage Rate",       "node_storage_seep_rate",
                                                          0.0, 1e6, 4, UnitKind::Rate),
        };
        // Slice AG.4 — storage geometry, Property Browser parity: shape
        // selector, tabular depth–area curve picker, the three functional
        // power-law coefficients (Area = A·Depth^B + C), and the three raw
        // dimensions used by the geometric shapes (CYLINDRICAL / CONICAL /
        // PARABOLIC / PYRAMIDAL).
        // Picking a curve switches the node to TABULAR; clearing it (or setting
        // Shape=FUNCTIONAL) reverts to the coefficient form; setting a geometric
        // shape switches to the dimension form. Cells that don't apply to the
        // live shape are blanked + made read-only in data()/flags().
        cols.append(enumCol("Storage shape", "Storage Shape",
                             "node_storage_shape", storageShapeValues()));
        cols.append(compoundCol("Storage curve", "Storage Curve",
                                 "node_storage_curve_ref"));
        cols.append(num("Func coeff A", "Functional Coefficient (A)",
                        "node_storage_coeff_a", 0.0, 1e9, 4, UnitKind::None));
        cols.append(num("Func exp B",   "Functional Exponent (B)",
                        "node_storage_exp_b",   0.0, 1e9, 4, UnitKind::None));
        cols.append(num("Func const C", "Functional Constant (C)",
                        "node_storage_const_c", 0.0, 1e9, 4, UnitKind::Area));
        // Generic headers — the meaning is shape-dependent, so the per-shape name
        // ("Base Length", "Side Slope", …) is supplied as the cell tooltip.
        cols.append(num("Shape param 1", "Shape Parameter 1",
                        "node_storage_param1", 0.0, 1e9, 4, UnitKind::Length));
        cols.append(num("Shape param 2", "Shape Parameter 2",
                        "node_storage_param2", 0.0, 1e9, 4, UnitKind::Length));
        cols.append(num("Shape param 3", "Shape Parameter 3",
                        "node_storage_param3", 0.0, 1e9, 4, UnitKind::None));
        cols.append(nodeStatBlock());
        // ATTRIBUTE_EDITOR_WIRING parity pass (2026-06-04) — browser
        // shows the four compound cells on every node kind.
        cols.append(compoundCol("Inflows",   "External Inflows",  "node_inflows_ref"));
        cols.append(compoundCol("DWF",       "Dry Weather Flow",  "node_dwf_ref"));
        cols.append(compoundCol("RDII",      "RDII",              "node_rdii_ref"));
        cols.append(compoundCol("Treatment", "Pollutant Treatment",
                                  "node_treatment_ref"));
        return cols;
    }
    case SWMMModelLayer::CatDividers: {
        QList<ColumnSpec> cols = {
            nameCol(),
            ro("Node type",  "Node Type"),
            tagCol("node_tag"),
            nodeCoordX(),
            nodeCoordY(),
            num("Invert elev",     "Invert Elevation",   "node_invert_elev",
                                                          -1e9, 1e9, 4, UnitKind::Length),
            num("Max depth",       "Maximum Depth",      "node_max_depth",
                                                          0.0, 1e6, 4, UnitKind::Length),
            num("Init depth",      "Initial Depth",      "node_initial_depth",
                                                          0.0, 1e6, 4, UnitKind::Length),
            num("Surcharge depth", "Surcharge Depth",    "node_surcharge_depth",
                                                          0.0, 1e6, 4, UnitKind::Length),
            enumCol("Divider type", "Divider Type",
                                                "node_divider_type",
                                                dividerTypeValues()),
            // ATTRIBUTE_EDITOR_WIRING parity pass (2026-06-04) —
            // browser's SWMMDividerPropertyAdapter exposes ponded area.
            num("Ponded area",     "Ponded Area",        "node_ponded_area",
                                                          0.0, 1e9, 2, UnitKind::Area),
        };
        cols.append(nodeStatBlock());
        cols.append(compoundCol("Inflows",   "External Inflows",  "node_inflows_ref"));
        cols.append(compoundCol("DWF",       "Dry Weather Flow",  "node_dwf_ref"));
        cols.append(compoundCol("RDII",      "RDII",              "node_rdii_ref"));
        cols.append(compoundCol("Treatment", "Pollutant Treatment",
                                  "node_treatment_ref"));
        return cols;
    }
    case SWMMModelLayer::CatConduits:
        // ATTRIBUTE_EDITOR_WIRING Phase 1 — full ConduitProps parity
        // (SWMM-GUI/Epaswmm5/objprops.txt rows 4, 11-16, 23): Tag,
        // Init/Max Flow, Entry/Exit/Avg Loss, Seepage Rate, Barrels.
        return {
            nameCol(),
            ro("Link type",    "Link Type"),
            ro("Vertex count", "Vertex Count"),
            ro("From node",    "From Node"),
            ro("To node",      "To Node"),
            tagCol("link_tag"),
            num("Length",      "Length",        "link_length",
                                                  0.0, 1e9, 2, UnitKind::Length),
            num("Roughness",   "Manning's n",   "link_roughness",
                                                  1e-6, 1.0, 4),
            num("Offset up",   "Upstream Offset",   "link_offset_up",
                                                  -1e6, 1e6, 4, UnitKind::Length),
            num("Offset dn",   "Downstream Offset", "link_offset_dn",
                                                  -1e6, 1e6, 4, UnitKind::Length),
            num("Init flow",   "Initial Flow",  "link_initial_flow",
                                                  0.0, 1e9, 4, UnitKind::FlowRate),
            num("Max flow",    "Maximum Flow",  "link_max_flow",
                                                  0.0, 1e9, 4, UnitKind::FlowRate),
            num("Loss inlet",  "Entry Loss Coefficient", "link_loss_inlet",
                                                  0.0, 100.0, 4),
            num("Loss outlet", "Exit Loss Coefficient",  "link_loss_outlet",
                                                  0.0, 100.0, 4),
            num("Loss avg",    "Avg. Loss Coefficient",  "link_loss_avg",
                                                  0.0, 100.0, 4),
            num("Seep rate",   "Seepage Rate",  "link_seep_rate",
                                                  0.0, 1e6, 4, UnitKind::Rate),
            // §S.SC.1.c — XSection compound cell. Same dialog the
            // Property Browser opens; lets the user pick a shape +
            // geoms (or a transect, via the new picker) from the
            // attribute table inline.
            compoundCol("XSection", "Cross Section", "link_xsect_ref"),
            // Direct inline geom1..geom4 alongside the XSection editor.
            // Generic labels (meaning varies per shape, surfaced as the
            // cell tooltip); flags() greys the ones that don't apply.
            num("Geom 1", "Geom 1", "link_xsect_geom1", 0.0, 1e9, 4, UnitKind::None),
            num("Geom 2", "Geom 2", "link_xsect_geom2", 0.0, 1e9, 4, UnitKind::None),
            num("Geom 3", "Geom 3", "link_xsect_geom3", 0.0, 1e9, 4, UnitKind::None),
            num("Geom 4", "Geom 4", "link_xsect_geom4", 0.0, 1e9, 4, UnitKind::None),
            intCol("Barrels",       "Barrels",       "link_barrels", 1, 1000),
            enumCol("Flap gate",    "Flap Gate",     "link_flap_gate",     yesNoValues()),
            enumCol("Culvert code", "Culvert Code",  "link_culvert_code",  culvertCodeValues()),
            // ATTRIBUTE_EDITOR_WIRING parity pass (2026-06-04) —
            // browser parity; placeholder page until BO 6.5.8 deepens.
            compoundCol("Inlet usage", "Inlet Usage", "link_inlet_usage_ref"),
        };
    case SWMMModelLayer::CatPumps:
        // SWMM5 [PUMPS]: Name | FromNode | ToNode | PumpCurve | Status | Startup | Shutoff.
        // ATTRIBUTE_EDITOR_WIRING Phase 1 — Tag + Startup/Shutoff rows
        // (PumpProps rows 4, 7-8 in objprops.txt; BN-LINK-05 engine
        // accessors). PumpCurve stays Property-Browser-only (DataObjectRef
        // picker); the table has no picker delegate yet.
        return {
            nameCol(),
            ro("Link type",    "Link Type"),
            ro("Vertex count", "Vertex Count"),
            ro("From node",    "From Node"),
            ro("To node",      "To Node"),
            tagCol("link_tag"),
            // Picker cell (DataObjectRef) — same editor the Property
            // Browser row hands out; legacy PumpProps[5] position.
            compoundCol("Pump curve", "Pump Curve", "link_pump_curve_ref"),
            enumCol("Initial state", "Initial State",
                                                "link_pump_init_state",
                                                offOnValues()),
            num("Startup depth", "Startup Depth", "link_pump_startup_depth",
                                                  0.0, 1e6, 4, UnitKind::Length),
            num("Shutoff depth", "Shutoff Depth", "link_pump_shutoff_depth",
                                                  0.0, 1e6, 4, UnitKind::Length),
        };
    case SWMMModelLayer::CatWeirs:
        // SWMM5 [WEIRS]: Name | FromNode | ToNode | Type | CrestHt | Cd |
        // Gated | EndCon | EndCoeff.  No offsets — weirs sit at the
        // crest elevation directly.
        // ATTRIBUTE_EDITOR_WIRING Phase 1 — Tag + Type rows (WeirProps
        // rows 4-5 in objprops.txt; BN-LINK-03 engine accessors).
        return {
            nameCol(),
            ro("Link type",    "Link Type"),
            ro("Vertex count", "Vertex Count"),
            ro("From node",    "From Node"),
            ro("To node",      "To Node"),
            tagCol("link_tag"),
            enumCol("Weir type", "Type", "link_weir_type", weirTypeValues()),
            // ATTRIBUTE_EDITOR_WIRING parity pass (2026-06-04) — the
            // browser's weir adapter exposes both offsets.
            num("Offset up",   "Upstream Offset",   "link_offset_up",
                                                  -1e6, 1e6, 4, UnitKind::Length),
            num("Offset dn",   "Downstream Offset", "link_offset_dn",
                                                  -1e6, 1e6, 4, UnitKind::Length),
            num("Crest height", "Crest Height",   "link_crest_height",
                                                  0.0, 1e6, 4, UnitKind::Length),
            num("Discharge coeff", "Discharge Coefficient",
                                                  "link_discharge_coeff",
                                                  0.0, 100.0, 4),
            num("End contractions", "End Contractions",
                                                  "link_end_contractions",
                                                  0.0, 1e3, 0),
            // §S.SC.1.c — XSection compound cell. The dialog filters
            // the shape palette to the legal weir shapes
            // (RECT_OPEN / TRAPEZOIDAL / TRIANGULAR) automatically.
            compoundCol("XSection", "Cross Section", "link_xsect_ref"),
            // Direct inline geom1..geom4 (flags() greys inapplicable geoms).
            num("Geom 1", "Geom 1", "link_xsect_geom1", 0.0, 1e9, 4, UnitKind::None),
            num("Geom 2", "Geom 2", "link_xsect_geom2", 0.0, 1e9, 4, UnitKind::None),
            num("Geom 3", "Geom 3", "link_xsect_geom3", 0.0, 1e9, 4, UnitKind::None),
            num("Geom 4", "Geom 4", "link_xsect_geom4", 0.0, 1e9, 4, UnitKind::None),
            enumCol("Flap gate", "Flap Gate",     "link_flap_gate", yesNoValues()),
        };
    case SWMMModelLayer::CatOrifices:
        // SWMM5 [ORIFICES]: Name | FromNode | ToNode | Type | Offset |
        // Cd | Gated.  One offset (no downstream offset).
        // ATTRIBUTE_EDITOR_WIRING Phase 1 — Tag + Type + Open/Close
        // Rate rows (OrificeProps rows 4-5, 12 in objprops.txt;
        // BN-LINK-02/-06 engine accessors). The engine stores a rate
        // in 1/s — legacy displays hours; conversion UX is deferred
        // alongside the Property Browser's identical raw-rate row.
        return {
            nameCol(),
            ro("Link type",    "Link Type"),
            ro("Vertex count", "Vertex Count"),
            ro("From node",    "From Node"),
            ro("To node",      "To Node"),
            tagCol("link_tag"),
            enumCol("Orifice type", "Type", "link_orifice_type",
                                                  orificeTypeValues()),
            num("Offset up",   "Offset",   "link_offset_up",
                                                  -1e6, 1e6, 4, UnitKind::Length),
            num("Discharge coeff", "Discharge Coefficient",
                                                  "link_discharge_coeff",
                                                  0.0, 100.0, 4),
            // §S.SC.1.c — XSection compound cell, restricted to
            // CIRCULAR / RECT_CLOSED for orifices per the dialog's
            // shape allow-list.
            compoundCol("XSection", "Cross Section", "link_xsect_ref"),
            // Direct inline geom1..geom4 (orifices use CIRCULAR / RECT_CLOSED,
            // so only Geom 1/2 ever apply; the rest grey out).
            num("Geom 1", "Geom 1", "link_xsect_geom1", 0.0, 1e9, 4, UnitKind::None),
            num("Geom 2", "Geom 2", "link_xsect_geom2", 0.0, 1e9, 4, UnitKind::None),
            num("Geom 3", "Geom 3", "link_xsect_geom3", 0.0, 1e9, 4, UnitKind::None),
            num("Geom 4", "Geom 4", "link_xsect_geom4", 0.0, 1e9, 4, UnitKind::None),
            enumCol("Flap gate", "Flap Gate",       "link_flap_gate", yesNoValues()),
            num("Open/close rate", "Open/Close Rate",
                                                  "link_orifice_open_close_rate",
                                                  0.0, 1e6, 4),
        };
    case SWMMModelLayer::CatOutlets:
        // SWMM5 [OUTLETS]: Name | FromNode | ToNode | Offset | Type |
        // Coeff | Expon.
        // ATTRIBUTE_EDITOR_WIRING Phase 1 — Tag + Rating Curve type +
        // Coefficient + Exponent rows (OutletProps rows 4, 7, 9-10 in
        // objprops.txt; BN-LINK-04 engine accessors). Coefficient
        // reuses the shared discharge-coeff scalar (same engine cd
        // field — see SWMMOutletPropertyAdapter). The tabular curve
        // picker stays Property-Browser-only (DataObjectRef row).
        return {
            nameCol(),
            ro("Link type",    "Link Type"),
            ro("Vertex count", "Vertex Count"),
            ro("From node",    "From Node"),
            ro("To node",      "To Node"),
            tagCol("link_tag"),
            num("Offset up",   "Offset",   "link_offset_up",
                                                  -1e6, 1e6, 4, UnitKind::Length),
            enumCol("Rating type", "Rating Curve", "link_outlet_rating_type",
                                                  outletRatingTypeValues()),
            num("Coefficient", "Coefficient",  "link_discharge_coeff",
                                                  0.0, 1e6, 4),
            num("Exponent",    "Exponent",     "link_outlet_expon",
                                                  0.0, 100.0, 4),
            // ATTRIBUTE_EDITOR_WIRING parity pass (2026-06-04) —
            // tabular rating-curve picker. Same setter tag as the pump
            // curve cell: the engine shares the curve-index slot
            // between pumps and tabular outlets (see
            // SWMMOutletPropertyAdapter::outletCurve).
            compoundCol("Outlet curve", "Tabular Curve", "link_pump_curve_ref"),
            enumCol("Flap gate", "Flap Gate",       "link_flap_gate", yesNoValues()),
        };
    case SWMMModelLayer::CatSubcatchments:
        return {
            nameCol(),
            ro("Polygon vertices", "Vertex Count"),
            num("Area",      "Area",                  "subcatch_area",
                                                       0.0, 1e9, 4, UnitKind::SubcatchArea),
            num("Width",     "Width",                 "subcatch_width",
                                                       0.0, 1e9, 4, UnitKind::Length),
            num("Slope",     "Slope",                 "subcatch_slope",
                                                       0.0, 100.0, 4, UnitKind::Percent),
            num("Imperv %",  "Impervious",            "subcatch_imperv_pct",
                                                       0.0, 100.0, 2, UnitKind::Percent),
            num("N-Imperv",  "Manning's n (Imperv.)", "subcatch_n_imperv",
                                                       0.0, 1.0, 4),
            num("N-Perv",    "Manning's n (Perv.)",   "subcatch_n_perv",
                                                       0.0, 1.0, 4),
            num("Ds-Imperv", "Depression Storage (Imperv.)",
                                                      "subcatch_ds_imperv",
                                                       0.0, 1e3, 4, UnitKind::Depression),
            num("Ds-Perv",   "Depression Storage (Perv.)",
                                                      "subcatch_ds_perv",
                                                       0.0, 1e3, 4, UnitKind::Depression),
            // Precipitation scaling — multiplies the gage-derived precip for
            // this subcatchment only. Min is just above zero, not zero: the
            // engine rejects a factor <= 0, so allowing 0 in the spin box would
            // produce an edit that silently fails to commit.
            num("Rainfall scale", "Rainfall Scale Factor",
                                                      "subcatch_rain_scale_factor",
                                                       1e-6, 1e3, 4),
            num("Snow scale",     "Snow Scale Factor",
                                                      "subcatch_snow_scale_factor",
                                                       1e-6, 1e3, 4),
            // Phase 3 — Rain gage + outlet pickers (DataObjectRef cells),
            // infiltration model enum, and the per-model parameter columns.
            // Parameter columns are always present (no per-row greying in the
            // table); they read/write the active model's shared param store.
            compoundCol("Rain gage", "Rain Gage",  "subcatch_rain_gage_ref"),
            compoundCol("Outlet",    "Outlet",     "subcatch_outlet_ref"),
            enumCol("Infil. model",  "Infiltration Model",
                                                   "subcatch_infil_model",
                                                   infilModelValues()),
            num("Max. rate",  "Max. Infil. Rate",  "subcatch_horton_f0",     0.0, 1e3, 4),
            num("Min. rate",  "Min. Infil. Rate",  "subcatch_horton_fmin",   0.0, 1e3, 4),
            num("Decay",      "Decay Constant",    "subcatch_horton_decay",  0.0, 100.0, 4),
            num("Dry time",   "Drying Time",       "subcatch_horton_drytime",0.0, 100.0, 4),
            num("Suction",    "Suction Head",      "subcatch_ga_suction",    0.0, 1e3, 4),
            num("Conduct.",   "Conductivity",      "subcatch_ga_conduct",    0.0, 1e3, 4),
            num("Init.def.",  "Initial Deficit",   "subcatch_ga_deficit",    0.0, 1.0, 4),
            num("Curve no.",  "Curve Number",      "subcatch_cn",            0.0, 100.0, 2),
            // Compound cells (open SubcatchCompoundEditDialog tabs).
            compoundCol("Land uses",   "Land Use Coverage", "subcatch_landuse_ref"),
            compoundCol("Groundwater", "Groundwater",       "subcatch_groundwater_ref"),
            compoundCol("LID usage",   "LID Usage",         "subcatch_lid_ref"),
        };
    case SWMMModelLayer::CatRainGages: {
        // ATTRIBUTE_EDITOR_WIRING parity pass (2026-06-04) — rain
        // format / data source / file path rows, Property Browser
        // parity (SWMMRainGagePropertyAdapter). Labels match the
        // adapter's displayLabelFor. The read-only currentRainfall /
        // resolvedFilePath browser rows are sim-time / derived values
        // and stay browser-only.
        ColumnSpec fileCol;
        fileCol.key    = QStringLiteral("Rain file");
        fileCol.label  = QStringLiteral("Rain File (path)");
        fileCol.editor = EditorKind::Text;
        fileCol.setter = QStringLiteral("gage_file_path");
        ColumnSpec stationCol;
        stationCol.key    = QStringLiteral("Station ID");
        stationCol.label  = QStringLiteral("Station ID");
        stationCol.editor = EditorKind::Text;
        stationCol.setter = QStringLiteral("gage_station_id");
        // Recording interval — legacy H:MM editable combo.
        ColumnSpec intervalCol;
        intervalCol.key    = QStringLiteral("Recording interval");
        intervalCol.label  = QStringLiteral("Recording Interval");
        intervalCol.editor = EditorKind::Interval;
        intervalCol.setter = QStringLiteral("gage_interval");
        // DA.2 parity — full legacy [RAINGAGES] field set: rain format,
        // recording interval, snow-catch factor, data source, then the
        // source-specific rows (series picker vs. file path / station / units).
        return {
            nameCol(),
            ro("X",    "X Coordinate"),
            ro("Y",    "Y Coordinate"),
            enumCol("Rain type",   "Rain Type",   "gage_rain_type",
                                                  gageRainTypeValues()),
            intervalCol,
            num("Snow catch factor",  "Snow Catch Factor (SCF)", "gage_snow_factor",
                                                  0.0, 1e3, 4),
            // Rainfall scale factor — the optional trailing token of
            // [RAINGAGES]. Distinct from the SCF above: this scales rainfall,
            // SCF corrects snow catch. Engine rejects <= 0, so min is 1e-6.
            num("Rainfall scale factor", "Rainfall Scale Factor", "gage_scale_factor",
                                                  1e-6, 1e3, 4),
            enumCol("Data source", "Data Source", "gage_data_source",
                                                  gageDataSourceValues()),
            compoundCol("Series name", "Series Name", "gage_series_ref"),
            fileCol,
            stationCol,
            enumCol("Rain units",  "Rain Units",  "gage_rain_units",
                                                  gageRainUnitsValues()),
        };
    }
    default:
        return { nameCol() };
    }
}

// ATTRIBUTE_EDITOR_WIRING Phase 1 — loss-coefficient adapters. The
// engine reads/writes the (inlet, outlet, avg) triple atomically
// (`swmm_link_get/set_loss_coeff`), but the table surfaces three
// independent columns. These wrappers match the single-double
// SetterEntry shape: each setter re-reads the other two coefficients
// then writes the full triple — same contract the Property Browser's
// setLossInlet/Outlet/Avg slots honour (§S.1 Q-S4 decision).
int lossGetInlet(SWMM_Engine e, int idx, double *v) {
    double in = 0, out = 0, avg = 0;
    const int rc = swmm_link_get_loss_coeff(e, idx, &in, &out, &avg);
    *v = in;  return rc;
}
int lossGetOutlet(SWMM_Engine e, int idx, double *v) {
    double in = 0, out = 0, avg = 0;
    const int rc = swmm_link_get_loss_coeff(e, idx, &in, &out, &avg);
    *v = out; return rc;
}
int lossGetAvg(SWMM_Engine e, int idx, double *v) {
    double in = 0, out = 0, avg = 0;
    const int rc = swmm_link_get_loss_coeff(e, idx, &in, &out, &avg);
    *v = avg; return rc;
}
int lossSetInlet(SWMM_Engine e, int idx, double v) {
    double in = 0, out = 0, avg = 0;
    const int rc = swmm_link_get_loss_coeff(e, idx, &in, &out, &avg);
    if (rc != SWMM_OK) return rc;
    return swmm_link_set_loss_coeff(e, idx, v, out, avg);
}
int lossSetOutlet(SWMM_Engine e, int idx, double v) {
    double in = 0, out = 0, avg = 0;
    const int rc = swmm_link_get_loss_coeff(e, idx, &in, &out, &avg);
    if (rc != SWMM_OK) return rc;
    return swmm_link_set_loss_coeff(e, idx, in, v, avg);
}
int lossSetAvg(SWMM_Engine e, int idx, double v) {
    double in = 0, out = 0, avg = 0;
    const int rc = swmm_link_get_loss_coeff(e, idx, &in, &out, &avg);
    if (rc != SWMM_OK) return rc;
    return swmm_link_set_loss_coeff(e, idx, in, out, v);
}

// Inline cross-section geometry — geom1..geom4 surfaced as four
// independent Numeric columns alongside the XSection compound cell. The
// engine writes the whole (shape, g1..g4) tuple, so each setter
// read-modify-writes (mirrors lossSet*). A write to a geom that doesn't
// apply to the current shape is rejected (the cell is also made
// non-editable by flags()), so a stray write can't corrupt the section.
int xsectGeomGet(SWMM_Engine e, int idx, int ordinal, double *v) {
    int shape = 0; double g[4] = {0, 0, 0, 0};
    const int rc = swmm_link_get_xsect(e, idx, &shape, &g[0], &g[1], &g[2], &g[3]);
    *v = (rc == SWMM_OK && ordinal >= 1 && ordinal <= 4) ? g[ordinal - 1] : 0.0;
    return rc;
}
int xsectGeomSet(SWMM_Engine e, int idx, int ordinal, double v) {
    int shape = 0; double g[4] = {0, 0, 0, 0};
    const int rc = swmm_link_get_xsect(e, idx, &shape, &g[0], &g[1], &g[2], &g[3]);
    if (rc != SWMM_OK) return rc;
    if (!openswmmvis::xsectGeomApplies(shape, ordinal)) return SWMM_ERR_BADINDEX;
    g[ordinal - 1] = v;
    return swmm_link_set_xsect(e, idx, shape, g[0], g[1], g[2], g[3]);
}
int xsectGeom1Get(SWMM_Engine e, int i, double *v) { return xsectGeomGet(e, i, 1, v); }
int xsectGeom2Get(SWMM_Engine e, int i, double *v) { return xsectGeomGet(e, i, 2, v); }
int xsectGeom3Get(SWMM_Engine e, int i, double *v) { return xsectGeomGet(e, i, 3, v); }
int xsectGeom4Get(SWMM_Engine e, int i, double *v) { return xsectGeomGet(e, i, 4, v); }
int xsectGeom1Set(SWMM_Engine e, int i, double v) { return xsectGeomSet(e, i, 1, v); }
int xsectGeom2Set(SWMM_Engine e, int i, double v) { return xsectGeomSet(e, i, 2, v); }
int xsectGeom3Set(SWMM_Engine e, int i, double v) { return xsectGeomSet(e, i, 3, v); }
int xsectGeom4Set(SWMM_Engine e, int i, double v) { return xsectGeomSet(e, i, 4, v); }

// geom ordinal (1..4) for an inline xsect-geom setter tag, else 0.
int xsectGeomOrdinalForTag(const QString &tag) {
    if (tag == QStringLiteral("link_xsect_geom1")) return 1;
    if (tag == QStringLiteral("link_xsect_geom2")) return 2;
    if (tag == QStringLiteral("link_xsect_geom3")) return 3;
    if (tag == QStringLiteral("link_xsect_geom4")) return 4;
    return 0;
}
// Current SWMM_XSECT_* shape id for the named link, or -1 on lookup
// failure. Used by data()/flags() to grey out inapplicable geom cells.
int linkShapeForName(SWMMModelLayer *layer, const QString &name) {
    if (!layer || !layer->engine() || name.isEmpty()) return -1;
    const int li = swmm_link_index(layer->engine(), name.toUtf8().constData());
    if (li < 0) return -1;
    int shape = 0; double g1 = 0, g2 = 0, g3 = 0, g4 = 0;
    if (swmm_link_get_xsect(layer->engine(), li, &shape, &g1, &g2, &g3, &g4) != SWMM_OK)
        return -1;
    return shape;
}

// Storage-shape counterparts of the two helpers above. The three dimension columns
// carry a generic header ("Shape param N") because their meaning is shape-dependent,
// so data()/flags() use these to supply the per-shape tooltip and to blank + lock the
// cells that the live shape doesn't use (a cylinder has no side slope).
int storageParamOrdinalForTag(const QString &tag) {
    if (tag == QStringLiteral("node_storage_param1")) return 1;
    if (tag == QStringLiteral("node_storage_param2")) return 2;
    if (tag == QStringLiteral("node_storage_param3")) return 3;
    return 0;
}
// Current SWMM_StorageShape for the named node, or -1 on lookup failure.
int storageShapeForName(SWMMModelLayer *layer, const QString &name) {
    if (!layer || !layer->engine() || name.isEmpty()) return -1;
    const int ni = swmm_node_index(layer->engine(), name.toUtf8().constData());
    if (ni < 0) return -1;
    int shape = 0;
    if (swmm_node_get_storage_shape(layer->engine(), ni, &shape) != SWMM_OK)
        return -1;
    return shape;
}

// Phase 3 — subcatchment infiltration. Model code via the int path; the
// per-model parameter columns read/write the engine's shared infil_p1..p4
// store through the typed getters/setters. The double setters preserve the
// model code (the engine's set_infil_horton/_green_ampt stamp the canonical
// code, which would demote a Mod-Horton/Mod-GA subcatchment) — mirrors
// SWMMSubcatchPropertyAdapter's read-modify-write slots.
int infilModelGetI(SWMM_Engine e, int idx, int *v) {
    return swmm_subcatch_get_infil_model(e, idx, v);
}
int infilModelSetI(SWMM_Engine e, int idx, int v) {
    return swmm_subcatch_set_infil_model(e, idx, v);
}
int hortonF0Get(SWMM_Engine e, int i, double *v) {
    double f0=0,fmin=0,d=0,dt=0; int rc=swmm_subcatch_get_infil_horton(e,i,&f0,&fmin,&d,&dt); *v=f0; return rc; }
int hortonFminGet(SWMM_Engine e, int i, double *v) {
    double f0=0,fmin=0,d=0,dt=0; int rc=swmm_subcatch_get_infil_horton(e,i,&f0,&fmin,&d,&dt); *v=fmin; return rc; }
int hortonDecayGet(SWMM_Engine e, int i, double *v) {
    double f0=0,fmin=0,d=0,dt=0; int rc=swmm_subcatch_get_infil_horton(e,i,&f0,&fmin,&d,&dt); *v=d; return rc; }
int hortonDryGet(SWMM_Engine e, int i, double *v) {
    double f0=0,fmin=0,d=0,dt=0; int rc=swmm_subcatch_get_infil_horton(e,i,&f0,&fmin,&d,&dt); *v=dt; return rc; }
int hortonWrite(SWMM_Engine e, int i, double f0, double fmin, double d, double dt) {
    int model=0; swmm_subcatch_get_infil_model(e,i,&model);
    const int rc=swmm_subcatch_set_infil_horton(e,i,f0,fmin,d,dt);
    if (rc==SWMM_OK) swmm_subcatch_set_infil_model(e,i,model);
    return rc; }
int hortonF0Set(SWMM_Engine e, int i, double v) {
    double f0=0,fmin=0,d=0,dt=0; if (swmm_subcatch_get_infil_horton(e,i,&f0,&fmin,&d,&dt)!=SWMM_OK) return SWMM_ERR_BADINDEX; return hortonWrite(e,i,v,fmin,d,dt); }
int hortonFminSet(SWMM_Engine e, int i, double v) {
    double f0=0,fmin=0,d=0,dt=0; if (swmm_subcatch_get_infil_horton(e,i,&f0,&fmin,&d,&dt)!=SWMM_OK) return SWMM_ERR_BADINDEX; return hortonWrite(e,i,f0,v,d,dt); }
int hortonDecaySet(SWMM_Engine e, int i, double v) {
    double f0=0,fmin=0,d=0,dt=0; if (swmm_subcatch_get_infil_horton(e,i,&f0,&fmin,&d,&dt)!=SWMM_OK) return SWMM_ERR_BADINDEX; return hortonWrite(e,i,f0,fmin,v,dt); }
int hortonDrySet(SWMM_Engine e, int i, double v) {
    double f0=0,fmin=0,d=0,dt=0; if (swmm_subcatch_get_infil_horton(e,i,&f0,&fmin,&d,&dt)!=SWMM_OK) return SWMM_ERR_BADINDEX; return hortonWrite(e,i,f0,fmin,d,v); }
int gaSuctionGet(SWMM_Engine e, int i, double *v) {
    double s=0,k=0,d=0; int rc=swmm_subcatch_get_infil_green_ampt(e,i,&s,&k,&d); *v=s; return rc; }
int gaConductGet(SWMM_Engine e, int i, double *v) {
    double s=0,k=0,d=0; int rc=swmm_subcatch_get_infil_green_ampt(e,i,&s,&k,&d); *v=k; return rc; }
int gaDeficitGet(SWMM_Engine e, int i, double *v) {
    double s=0,k=0,d=0; int rc=swmm_subcatch_get_infil_green_ampt(e,i,&s,&k,&d); *v=d; return rc; }
int gaWrite(SWMM_Engine e, int i, double s, double k, double d) {
    int model=0; swmm_subcatch_get_infil_model(e,i,&model);
    const int rc=swmm_subcatch_set_infil_green_ampt(e,i,s,k,d);
    if (rc==SWMM_OK) swmm_subcatch_set_infil_model(e,i,model);
    return rc; }
int gaSuctionSet(SWMM_Engine e, int i, double v) {
    double s=0,k=0,d=0; if (swmm_subcatch_get_infil_green_ampt(e,i,&s,&k,&d)!=SWMM_OK) return SWMM_ERR_BADINDEX; return gaWrite(e,i,v,k,d); }
int gaConductSet(SWMM_Engine e, int i, double v) {
    double s=0,k=0,d=0; if (swmm_subcatch_get_infil_green_ampt(e,i,&s,&k,&d)!=SWMM_OK) return SWMM_ERR_BADINDEX; return gaWrite(e,i,s,v,d); }
int gaDeficitSet(SWMM_Engine e, int i, double v) {
    double s=0,k=0,d=0; if (swmm_subcatch_get_infil_green_ampt(e,i,&s,&k,&d)!=SWMM_OK) return SWMM_ERR_BADINDEX; return gaWrite(e,i,s,k,v); }

// ATTRIBUTE_EDITOR_WIRING parity pass (2026-06-04) — outfall fixed
// stage. The engine stores stage / tidal-curve-idx / timeseries-idx in
// a shared union slot (`outfall_param`), so the getter reads as 0 when
// the outfall isn't FIXED — mirrors
// SWMMNodePropertyAdapter::outfallStage(). The setter flips the
// outfall type to FIXED (deliberate engine invariant).
int outfallStageGet(SWMM_Engine e, int idx, double *v) {
    *v = 0.0;
    int type = -1;
    const int rc = swmm_node_get_outfall_type(e, idx, &type);
    if (rc != SWMM_OK) return rc;
    if (type != /*FIXED*/ 2) return SWMM_OK;
    return swmm_node_get_outfall_param(e, idx, v);
}

// Parity pass — rain gage data-file path. The engine keys file paths
// by object NAME through the relative-path registry
// (swmm_file_path_get/set), so adapt to the (engine, idx) shape the
// dispatch table expects. Mirrors SWMMRainGagePropertyAdapter::
// filePath / setFilePath (displays the original, possibly relative,
// path; the resolved absolute path stays a browser-only row).
int gageFilePathGet(SWMM_Engine e, int idx, char *buf, int len) {
    const char *id = swmm_gage_id(e, idx);
    if (!id) return SWMM_ERR_BADINDEX;
    char abs[1024] = {};
    char orig[1024] = {};
    const int rc = swmm_file_path_get(e, SWMM_FILE_RAINGAGE_DATA, id,
                                       abs, int(sizeof(abs)),
                                       orig, int(sizeof(orig)));
    if (rc == SWMM_OK) qstrncpy(buf, orig, len);
    return rc;
}
int gageFilePathSet(SWMM_Engine e, int idx, const char *path) {
    const char *id = swmm_gage_id(e, idx);
    if (!id) return SWMM_ERR_BADINDEX;
    return swmm_file_path_set(e, SWMM_FILE_RAINGAGE_DATA, id, path);
}

// DA.2 parity — recording interval as a legacy H:MM clock string. The engine
// stores/returns seconds (swmm_gage_get/set_rain_interval); these string
// wrappers let the column ride the SetterEntry string path so the cell shows
// "0:15" (matching the Property Browser combo) instead of raw seconds.
int gageIntervalGet(SWMM_Engine e, int idx, char *buf, int len) {
    double secs = 0.0;
    const int rc = swmm_gage_get_rain_interval(e, idx, &secs);
    if (rc == SWMM_OK)
        qstrncpy(buf,
                 rain_interval::secondsToHMM(static_cast<int>(secs + 0.5))
                     .toUtf8().constData(),
                 len);
    return rc;
}
int gageIntervalSet(SWMM_Engine e, int idx, const char *text) {
    const int secs = rain_interval::hmmToSeconds(QString::fromUtf8(text));
    if (secs < 0) return SWMM_ERR_BADPARAM;   // malformed — reject the edit
    return swmm_gage_set_rain_interval(e, idx, static_cast<double>(secs));
}

// Slice AG.4 — storage-unit geometry. The engine keeps the shape, the tabular curve
// index, the functional power-law coefficients (A, B, C) and the raw geometric
// dimensions (p1, p2, p3) in independent slots. These wrappers adapt that to the
// (engine, idx, scalar) dispatch shape the table uses, mirroring
// SWMMNodePropertyAdapter's storage accessors so both views agree.
int storageShapeGetI(SWMM_Engine e, int idx, int *v) {
    return swmm_node_get_storage_shape(e, idx, v);
}
int storageShapeSetI(SWMM_Engine e, int idx, int v) {
    if (v == openswmmvis::kStorageTabularId) {
        // Tabular needs a curve; keep the one already attached, else bind the first
        // [STORAGE]-type curve in the model. With none available the switch can't
        // complete and the cell snaps back on re-read.
        int curveIdx = -1;
        swmm_node_get_storage_curve(e, idx, &curveIdx);
        if (curveIdx >= 0) return SWMM_OK;
        const int n = swmm_table_count(e);
        for (int i = 0; i < n; ++i) {
            int t = -1;
            if (swmm_table_get_type(e, i, &t) == SWMM_OK && t == 1 /*CURVE_STORAGE*/)
                return swmm_node_set_storage_curve(e, idx, i);
        }
        return SWMM_OK;
    }
    // Functional / geometric — the engine detaches any curve and re-derives the area
    // coefficients from the node's current dimensions.
    const int rc = swmm_node_set_storage_shape(e, idx, v);
    // A geometric shape needs valid L/W/Z, validated atomically by the engine. A
    // node freshly switched to one carries zeroed dims, so a single-cell dimension
    // edit would be rejected and the switch would never "take" (same reasoning as
    // SWMMNodePropertyAdapter::setStorageShape). Seed a valid unit default when the
    // current dims don't validate; keep them when they do so the round-trip is
    // lossless. Both views share this so the panel and the table stay in sync.
    if (rc == SWMM_OK && openswmmvis::storageShapeIsGeometric(v)) {
        double p1 = 0.0, p2 = 0.0, p3 = 0.0;
        swmm_node_get_storage_geometry(e, idx, &p1, &p2, &p3);
        if (swmm_node_set_storage_geometry(e, idx, p1, p2, p3) != SWMM_OK)
            swmm_node_set_storage_geometry(e, idx, 1.0, 1.0, 1.0);
    }
    return rc;
}
// Raw dimensions. Read-modify-write the engine's atomic (p1,p2,p3) triple, exactly as
// the coefficient wrappers below do for (A,B,C). The engine rejects invalid dimensions
// outright, so a bad cell edit leaves the node on its previous geometry.
int storageParam1Get(SWMM_Engine e, int idx, double *v) {
    double p1 = 0, p2 = 0, p3 = 0;
    const int rc = swmm_node_get_storage_geometry(e, idx, &p1, &p2, &p3);
    if (rc == SWMM_OK) *v = p1;
    return rc;
}
int storageParam1Set(SWMM_Engine e, int idx, double v) {
    double p1 = 0, p2 = 0, p3 = 0;
    const int rc = swmm_node_get_storage_geometry(e, idx, &p1, &p2, &p3);
    if (rc != SWMM_OK) return rc;
    return swmm_node_set_storage_geometry(e, idx, v, p2, p3);
}
int storageParam2Get(SWMM_Engine e, int idx, double *v) {
    double p1 = 0, p2 = 0, p3 = 0;
    const int rc = swmm_node_get_storage_geometry(e, idx, &p1, &p2, &p3);
    if (rc == SWMM_OK) *v = p2;
    return rc;
}
int storageParam2Set(SWMM_Engine e, int idx, double v) {
    double p1 = 0, p2 = 0, p3 = 0;
    const int rc = swmm_node_get_storage_geometry(e, idx, &p1, &p2, &p3);
    if (rc != SWMM_OK) return rc;
    return swmm_node_set_storage_geometry(e, idx, p1, v, p3);
}
int storageParam3Get(SWMM_Engine e, int idx, double *v) {
    double p1 = 0, p2 = 0, p3 = 0;
    const int rc = swmm_node_get_storage_geometry(e, idx, &p1, &p2, &p3);
    if (rc == SWMM_OK) *v = p3;
    return rc;
}
int storageParam3Set(SWMM_Engine e, int idx, double v) {
    double p1 = 0, p2 = 0, p3 = 0;
    const int rc = swmm_node_get_storage_geometry(e, idx, &p1, &p2, &p3);
    if (rc != SWMM_OK) return rc;
    return swmm_node_set_storage_geometry(e, idx, p1, p2, v);
}
int storageCoeffAGet(SWMM_Engine e, int idx, double *v) {
    double a = 0, b = 0, c = 0;
    const int rc = swmm_node_get_storage_functional(e, idx, &a, &b, &c);
    if (rc == SWMM_OK) *v = a;
    return rc;
}
int storageCoeffASet(SWMM_Engine e, int idx, double v) {
    double a = 0, b = 0, c = 0;
    const int rc = swmm_node_get_storage_functional(e, idx, &a, &b, &c);
    if (rc != SWMM_OK) return rc;
    return swmm_node_set_storage_functional(e, idx, v, b, c);
}
int storageExpBGet(SWMM_Engine e, int idx, double *v) {
    double a = 0, b = 0, c = 0;
    const int rc = swmm_node_get_storage_functional(e, idx, &a, &b, &c);
    if (rc == SWMM_OK) *v = b;
    return rc;
}
int storageExpBSet(SWMM_Engine e, int idx, double v) {
    double a = 0, b = 0, c = 0;
    const int rc = swmm_node_get_storage_functional(e, idx, &a, &b, &c);
    if (rc != SWMM_OK) return rc;
    return swmm_node_set_storage_functional(e, idx, a, v, c);
}
int storageConstCGet(SWMM_Engine e, int idx, double *v) {
    double a = 0, b = 0, c = 0;
    const int rc = swmm_node_get_storage_functional(e, idx, &a, &b, &c);
    if (rc == SWMM_OK) *v = c;
    return rc;
}
int storageConstCSet(SWMM_Engine e, int idx, double v) {
    double a = 0, b = 0, c = 0;
    const int rc = swmm_node_get_storage_functional(e, idx, &a, &b, &c);
    if (rc != SWMM_OK) return rc;
    return swmm_node_set_storage_functional(e, idx, a, b, v);
}

// Dispatch table — map a setter-tag string to the engine call.
// Double-typed setters drive Numeric columns; Int-typed setters
// drive Enum / Integer / Bool columns.  Each entry populates one
// pair (Fn / FnI) — the other stays null.  The kind field picks
// the right `swmm_*_index` for the name → idx lookup.
struct SetterEntry {
    EntityKind kind = EntityKind::Node;
    // Double path — Numeric columns.
    int (*setFn)(SWMM_Engine, int, double) = nullptr;
    int (*getFn)(SWMM_Engine, int, double*) = nullptr;
    // Int path — Enum / Integer / Bool columns.
    int (*setFnI)(SWMM_Engine, int, int) = nullptr;
    int (*getFnI)(SWMM_Engine, int, int*) = nullptr;
    // String path — Text columns whose engine setter takes `const char*`
    // and whose getter takes `(char* buf, int buflen)`. Used for tag
    // (DB.3) and reusable for any future string attribute.
    int (*setFnS)(SWMM_Engine, int, const char*) = nullptr;
    int (*getFnS)(SWMM_Engine, int, char*, int)  = nullptr;
};
SetterEntry setterFor(const QString &tag) {
    SetterEntry e;
    // Node — numeric
    if (tag == QStringLiteral("node_invert_elev"))
        return {EntityKind::Node, &swmm_node_set_invert_elev, &swmm_node_get_invert_elev};
    if (tag == QStringLiteral("node_max_depth"))
        return {EntityKind::Node, &swmm_node_set_max_depth,   &swmm_node_get_max_depth};
    if (tag == QStringLiteral("node_initial_depth"))
        return {EntityKind::Node, &swmm_node_set_initial_depth,   &swmm_node_get_initial_depth};
    if (tag == QStringLiteral("node_surcharge_depth"))
        return {EntityKind::Node, &swmm_node_set_surcharge_depth, &swmm_node_get_surcharge_depth};
    if (tag == QStringLiteral("node_ponded_area"))
        return {EntityKind::Node, &swmm_node_set_pond_area,       &swmm_node_get_ponded_area};
    if (tag == QStringLiteral("node_storage_seep_rate"))
        return {EntityKind::Node, &swmm_node_set_storage_seep_rate,
                                  &swmm_node_get_storage_seep_rate};
    // Slice AG.4 — storage functional coefficients (read-modify-write over
    // the engine's atomic (A,B,C) triple).
    if (tag == QStringLiteral("node_storage_coeff_a"))
        return {EntityKind::Node, &storageCoeffASet, &storageCoeffAGet};
    if (tag == QStringLiteral("node_storage_exp_b"))
        return {EntityKind::Node, &storageExpBSet,   &storageExpBGet};
    if (tag == QStringLiteral("node_storage_const_c"))
        return {EntityKind::Node, &storageConstCSet, &storageConstCGet};
    // Storage geometric dimensions (read-modify-write over the engine's atomic
    // (p1,p2,p3) triple). Meaning is per-shape — see storageshapegeom.h.
    if (tag == QStringLiteral("node_storage_param1"))
        return {EntityKind::Node, &storageParam1Set, &storageParam1Get};
    if (tag == QStringLiteral("node_storage_param2"))
        return {EntityKind::Node, &storageParam2Set, &storageParam2Get};
    if (tag == QStringLiteral("node_storage_param3"))
        return {EntityKind::Node, &storageParam3Set, &storageParam3Get};
    // ATTRIBUTE_EDITOR_WIRING parity pass — outfall fixed stage. The
    // setter also flips the outfall type to FIXED (engine invariant).
    if (tag == QStringLiteral("node_outfall_stage"))
        return {EntityKind::Node, &swmm_node_set_outfall_stage, &outfallStageGet};

    // Node — int / enum
    if (tag == QStringLiteral("node_outfall_type")) {
        e.kind = EntityKind::Node;
        e.setFnI = &swmm_node_set_outfall_type;
        e.getFnI = &swmm_node_get_outfall_type;
        return e;
    }
    if (tag == QStringLiteral("node_outfall_flap_gate")) {
        e.kind = EntityKind::Node;
        e.setFnI = &swmm_node_set_outfall_flap_gate;
        e.getFnI = &swmm_node_get_outfall_flap_gate;
        return e;
    }
    if (tag == QStringLiteral("node_divider_type")) {
        e.kind = EntityKind::Node;
        e.setFnI = &swmm_node_set_divider_type;
        e.getFnI = &swmm_node_get_divider_type;
        return e;
    }
    // Slice AG.4 — storage shape (TABULAR / FUNCTIONAL / the four geometric
    // shapes). The setter attaches or detaches a storage curve for TABULAR and
    // otherwise writes the engine's shape field (see storageShapeSetI).
    if (tag == QStringLiteral("node_storage_shape")) {
        e.kind = EntityKind::Node;
        e.setFnI = &storageShapeSetI;
        e.getFnI = &storageShapeGetI;
        return e;
    }

    // Node — string (DB.3 tag)
    if (tag == QStringLiteral("node_tag")) {
        e.kind   = EntityKind::Node;
        e.setFnS = &swmm_node_set_tag;
        e.getFnS = &swmm_node_get_tag;
        return e;
    }

    // Link — numeric
    if (tag == QStringLiteral("link_length"))
        return {EntityKind::Link, &swmm_link_set_length,    &swmm_link_get_length};
    if (tag == QStringLiteral("link_roughness"))
        return {EntityKind::Link, &swmm_link_set_roughness, &swmm_link_get_roughness};
    if (tag == QStringLiteral("link_offset_up"))
        return {EntityKind::Link, &swmm_link_set_offset_up, &swmm_link_get_offset_up};
    if (tag == QStringLiteral("link_offset_dn"))
        return {EntityKind::Link, &swmm_link_set_offset_dn, &swmm_link_get_offset_dn};
    if (tag == QStringLiteral("link_crest_height"))
        return {EntityKind::Link, &swmm_link_set_crest_height,    &swmm_link_get_crest_height};
    if (tag == QStringLiteral("link_discharge_coeff"))
        return {EntityKind::Link, &swmm_link_set_discharge_coeff, &swmm_link_get_discharge_coeff};
    if (tag == QStringLiteral("link_end_contractions"))
        return {EntityKind::Link, &swmm_link_set_end_contractions,
                                  &swmm_link_get_end_contractions};
    // ATTRIBUTE_EDITOR_WIRING Phase 1 — conduit scalar parity.
    if (tag == QStringLiteral("link_initial_flow"))
        return {EntityKind::Link, &swmm_link_set_initial_flow, &swmm_link_get_initial_flow};
    if (tag == QStringLiteral("link_max_flow"))
        return {EntityKind::Link, &swmm_link_set_max_flow,     &swmm_link_get_max_flow};
    if (tag == QStringLiteral("link_loss_inlet"))
        return {EntityKind::Link, &lossSetInlet,  &lossGetInlet};
    if (tag == QStringLiteral("link_loss_outlet"))
        return {EntityKind::Link, &lossSetOutlet, &lossGetOutlet};
    if (tag == QStringLiteral("link_loss_avg"))
        return {EntityKind::Link, &lossSetAvg,    &lossGetAvg};
    if (tag == QStringLiteral("link_seep_rate"))
        return {EntityKind::Link, &swmm_link_set_seep_rate, &swmm_link_get_seep_rate};
    // Inline cross-section geom1..geom4 (read-modify-write over the engine
    // xsect tuple; see xsectGeomGet/Set). flags() greys the geoms that
    // don't apply to a given row's shape.
    if (tag == QStringLiteral("link_xsect_geom1"))
        return {EntityKind::Link, &xsectGeom1Set, &xsectGeom1Get};
    if (tag == QStringLiteral("link_xsect_geom2"))
        return {EntityKind::Link, &xsectGeom2Set, &xsectGeom2Get};
    if (tag == QStringLiteral("link_xsect_geom3"))
        return {EntityKind::Link, &xsectGeom3Set, &xsectGeom3Get};
    if (tag == QStringLiteral("link_xsect_geom4"))
        return {EntityKind::Link, &xsectGeom4Set, &xsectGeom4Get};
    // Phase 1 — pump startup/shutoff (BN-LINK-05).
    if (tag == QStringLiteral("link_pump_startup_depth"))
        return {EntityKind::Link, &swmm_link_set_pump_startup_depth,
                                  &swmm_link_get_pump_startup_depth};
    if (tag == QStringLiteral("link_pump_shutoff_depth"))
        return {EntityKind::Link, &swmm_link_set_pump_shutoff_depth,
                                  &swmm_link_get_pump_shutoff_depth};
    // Phase 1 — orifice open/close rate (BN-LINK-06) + outlet exponent
    // (BN-LINK-04).
    if (tag == QStringLiteral("link_orifice_open_close_rate"))
        return {EntityKind::Link, &swmm_link_set_orifice_open_close_rate,
                                  &swmm_link_get_orifice_open_close_rate};
    if (tag == QStringLiteral("link_outlet_expon"))
        return {EntityKind::Link, &swmm_link_set_outlet_expon,
                                  &swmm_link_get_outlet_expon};

    // Link — int / enum / bool
    if (tag == QStringLiteral("link_flap_gate")) {
        e.kind = EntityKind::Link;
        e.setFnI = &swmm_link_set_flap_gate;
        e.getFnI = &swmm_link_get_flap_gate;
        return e;
    }
    if (tag == QStringLiteral("link_pump_init_state")) {
        e.kind = EntityKind::Link;
        e.setFnI = &swmm_link_set_pump_init_state;
        e.getFnI = &swmm_link_get_pump_init_state;
        return e;
    }
    if (tag == QStringLiteral("link_culvert_code")) {
        e.kind = EntityKind::Link;
        e.setFnI = &swmm_link_set_culvert_code;
        e.getFnI = &swmm_link_get_culvert_code;
        return e;
    }
    // ATTRIBUTE_EDITOR_WIRING Phase 1 — link sub-type enums + barrels.
    if (tag == QStringLiteral("link_orifice_type")) {
        e.kind = EntityKind::Link;
        e.setFnI = &swmm_link_set_orifice_type;
        e.getFnI = &swmm_link_get_orifice_type;
        return e;
    }
    if (tag == QStringLiteral("link_weir_type")) {
        e.kind = EntityKind::Link;
        e.setFnI = &swmm_link_set_weir_type;
        e.getFnI = &swmm_link_get_weir_type;
        return e;
    }
    if (tag == QStringLiteral("link_outlet_rating_type")) {
        e.kind = EntityKind::Link;
        e.setFnI = &swmm_link_set_outlet_rating_type;
        e.getFnI = &swmm_link_get_outlet_rating_type;
        return e;
    }
    if (tag == QStringLiteral("link_barrels")) {
        e.kind = EntityKind::Link;
        e.setFnI = &swmm_link_set_barrels;
        e.getFnI = &swmm_link_get_barrels;
        return e;
    }

    // Link — string ([TAGS], mirrors node_tag).
    if (tag == QStringLiteral("link_tag")) {
        e.kind   = EntityKind::Link;
        e.setFnS = &swmm_link_set_tag;
        e.getFnS = &swmm_link_get_tag;
        return e;
    }

    // ATTRIBUTE_EDITOR_WIRING parity pass — rain gage rows.
    if (tag == QStringLiteral("gage_rain_type")) {
        e.kind = EntityKind::Gage;
        e.setFnI = &swmm_gage_set_rain_type;
        e.getFnI = &swmm_gage_get_rain_type;
        return e;
    }
    if (tag == QStringLiteral("gage_data_source")) {
        e.kind = EntityKind::Gage;
        e.setFnI = &swmm_gage_set_data_source;
        e.getFnI = &swmm_gage_get_data_source;
        return e;
    }
    if (tag == QStringLiteral("gage_file_path")) {
        e.kind   = EntityKind::Gage;
        e.setFnS = &gageFilePathSet;
        e.getFnS = &gageFilePathGet;
        return e;
    }
    if (tag == QStringLiteral("gage_interval")) {
        // String path: cell displays/edits the legacy H:MM clock form via
        // the wrappers above (engine stores seconds).
        e.kind   = EntityKind::Gage;
        e.setFnS = &gageIntervalSet;
        e.getFnS = &gageIntervalGet;
        return e;
    }
    if (tag == QStringLiteral("gage_snow_factor")) {
        e.kind  = EntityKind::Gage;
        e.setFn = &swmm_gage_set_snow_factor;
        e.getFn = &swmm_gage_get_snow_factor;
        return e;
    }
    // ATTRIBUTE_EDITOR_WIRING Phase 4 — rain gage scale factor (engine setters
    // shipped with SWMM 5.3; the GUI had never surfaced them).
    if (tag == QStringLiteral("gage_scale_factor")) {
        e.kind  = EntityKind::Gage;
        e.setFn = &swmm_gage_set_scale_factor;
        e.getFn = &swmm_gage_get_scale_factor;
        return e;
    }
    if (tag == QStringLiteral("gage_station_id")) {
        e.kind   = EntityKind::Gage;
        e.setFnS = &swmm_gage_set_station_id;
        e.getFnS = &swmm_gage_get_station_id;
        return e;
    }
    if (tag == QStringLiteral("gage_rain_units")) {
        e.kind   = EntityKind::Gage;
        e.setFnI = &swmm_gage_set_rain_units;
        e.getFnI = &swmm_gage_get_rain_units;
        return e;
    }

    // Subcatchment — numeric
    if (tag == QStringLiteral("subcatch_area"))
        return {EntityKind::Subcatch, &swmm_subcatch_set_area,       &swmm_subcatch_get_area};
    if (tag == QStringLiteral("subcatch_width"))
        return {EntityKind::Subcatch, &swmm_subcatch_set_width,      &swmm_subcatch_get_width};
    if (tag == QStringLiteral("subcatch_slope"))
        return {EntityKind::Subcatch, &swmm_subcatch_set_slope,      &swmm_subcatch_get_slope};
    if (tag == QStringLiteral("subcatch_imperv_pct"))
        return {EntityKind::Subcatch, &swmm_subcatch_set_imperv_pct, &swmm_subcatch_get_imperv_pct};
    if (tag == QStringLiteral("subcatch_n_imperv"))
        return {EntityKind::Subcatch, &swmm_subcatch_set_n_imperv,   &swmm_subcatch_get_n_imperv};
    if (tag == QStringLiteral("subcatch_n_perv"))
        return {EntityKind::Subcatch, &swmm_subcatch_set_n_perv,     &swmm_subcatch_get_n_perv};
    if (tag == QStringLiteral("subcatch_ds_imperv"))
        return {EntityKind::Subcatch, &swmm_subcatch_set_ds_imperv,  &swmm_subcatch_get_ds_imperv};
    if (tag == QStringLiteral("subcatch_ds_perv"))
        return {EntityKind::Subcatch, &swmm_subcatch_set_ds_perv,    &swmm_subcatch_get_ds_perv};

    // Precipitation scale factors (optional [SUBCATCHMENTS] tokens 9 and 10).
    if (tag == QStringLiteral("subcatch_rain_scale_factor"))
        return {EntityKind::Subcatch, &swmm_subcatch_set_rain_scale_factor,
                                      &swmm_subcatch_get_rain_scale_factor};
    if (tag == QStringLiteral("subcatch_snow_scale_factor"))
        return {EntityKind::Subcatch, &swmm_subcatch_set_snow_scale_factor,
                                      &swmm_subcatch_get_snow_scale_factor};

    // Phase 3 — infiltration model (int path) + per-model parameters (double).
    if (tag == QStringLiteral("subcatch_infil_model")) {
        SetterEntry e; e.kind = EntityKind::Subcatch;
        e.setFnI = &infilModelSetI; e.getFnI = &infilModelGetI; return e;
    }
    if (tag == QStringLiteral("subcatch_horton_f0"))
        return {EntityKind::Subcatch, &hortonF0Set,    &hortonF0Get};
    if (tag == QStringLiteral("subcatch_horton_fmin"))
        return {EntityKind::Subcatch, &hortonFminSet,  &hortonFminGet};
    if (tag == QStringLiteral("subcatch_horton_decay"))
        return {EntityKind::Subcatch, &hortonDecaySet, &hortonDecayGet};
    if (tag == QStringLiteral("subcatch_horton_drytime"))
        return {EntityKind::Subcatch, &hortonDrySet,   &hortonDryGet};
    if (tag == QStringLiteral("subcatch_ga_suction"))
        return {EntityKind::Subcatch, &gaSuctionSet,   &gaSuctionGet};
    if (tag == QStringLiteral("subcatch_ga_conduct"))
        return {EntityKind::Subcatch, &gaConductSet,   &gaConductGet};
    if (tag == QStringLiteral("subcatch_ga_deficit"))
        return {EntityKind::Subcatch, &gaDeficitSet,   &gaDeficitGet};
    if (tag == QStringLiteral("subcatch_cn"))
        return {EntityKind::Subcatch, &swmm_subcatch_set_infil_curve_number,
                                      &swmm_subcatch_get_infil_curve_number};

    return {};
}

// Enum pair-list builders — the Enum delegate consumes a list of
// {label QString, data QVariant} pairs.  Keeping these inline so
// the schema declarations stay compact.
QVariantList makePair(const char *label, int data) {
    QVariantList p; p << QString::fromLatin1(label) << QVariant(data);
    return p;
}
QVariantList outfallTypeValues() {
    return {
        makePair("FREE",       0),
        makePair("NORMAL",     1),
        makePair("FIXED",      2),
        makePair("TIDAL",      3),
        makePair("TIMESERIES", 4),
    };
}
QVariantList yesNoValues() {
    return {
        makePair("NO",  0),
        makePair("YES", 1),
    };
}
QVariantList offOnValues() {
    return {
        makePair("OFF", 0),
        makePair("ON",  1),
    };
}
QVariantList dividerTypeValues() {
    // Mirrors SWMM_DividerType in openswmm_nodes.h.
    return {
        makePair("CUTOFF",   0),
        makePair("OVERFLOW", 1),
        makePair("TABULAR",  2),
        makePair("WEIR",     3),
    };
}
// Slice AG.4 — storage geometry shape. Mirrors
// SWMMNodePropertyAdapter::StorageShape, whose ordinals in turn match the engine's
// SWMM_StorageShape / the legacy solver's enum StorageType.
//
// NOTE: Tabular is 0 and Functional is 1 — they were 0/1 the OTHER way round before
// the geometric shapes landed. The numbers are the engine's now, not the GUI's.
QVariantList storageShapeValues() {
    return {
        makePair("TABULAR",     0),
        makePair("FUNCTIONAL",  1),
        makePair("CYLINDRICAL", 2),
        makePair("CONICAL",     3),
        makePair("PARABOLIC",   4),
        makePair("PYRAMIDAL",   5),
    };
}
// Phase 3 — infiltration model. Mirrors SWMMSubcatchPropertyAdapter::InfilModel
// and the engine [INFILTRATION] code order.
QVariantList infilModelValues() {
    return {
        makePair("HORTON",         0),
        makePair("MODIFIED_HORTON",1),
        makePair("GREEN_AMPT",     2),
        makePair("MODIFIED_GREEN_AMPT", 3),
        makePair("CURVE_NUMBER",   4),
    };
}
// ATTRIBUTE_EDITOR_WIRING Phase 1 — link sub-type enums. Values mirror
// the engine enums in openswmm_links.h (single ordering source: the
// Q_ENUMs on SWMMLinkPropertyAdapter cite the same legacy combos).
QVariantList orificeTypeValues() {
    // SWMM_OrificeType; legacy combo at objprops.txt OrificeProps[5].
    return {
        makePair("SIDE",   0),
        makePair("BOTTOM", 1),
    };
}
QVariantList weirTypeValues() {
    // SWMM_WeirType; legacy combo at objprops.txt WeirProps[5].
    return {
        makePair("TRANSVERSE",  0),
        makePair("SIDEFLOW",    1),
        makePair("V-NOTCH",     2),
        makePair("TRAPEZOIDAL", 3),
        makePair("ROADWAY",     4),
    };
}
QVariantList outletRatingTypeValues() {
    // SWMM_OutletRatingType; combo listed in the legacy display order
    // (objprops.txt OutletProps[7]) while the data values carry the
    // engine's numeric encoding.
    return {
        makePair("FUNCTIONAL/DEPTH", 1),
        makePair("TABULAR/DEPTH",    3),
        makePair("FUNCTIONAL/HEAD",  0),
        makePair("TABULAR/HEAD",     2),
    };
}
// ATTRIBUTE_EDITOR_WIRING parity pass — rain gage enums. Values mirror
// SWMM_GageRainType / SWMM_GageDataSource in openswmm_gages.h.
QVariantList gageRainTypeValues() {
    return {
        makePair("INTENSITY",  0),
        makePair("VOLUME",     1),
        makePair("CUMULATIVE", 2),
    };
}
QVariantList gageDataSourceValues() {
    return {
        makePair("TIMESERIES", 0),
        makePair("FILE",       1),
    };
}
// DA.2 parity — file rain-depth units. Mirrors GageData.rain_units (0=IN, 1=MM).
QVariantList gageRainUnitsValues() {
    return {
        makePair("IN", 0),
        makePair("MM", 1),
    };
}
QVariantList culvertCodeValues() {
    // Per legacy SWMM5 — 0 = no inlet control (default), 1..57 are
    // HDS-5 culvert codes. ATTRIBUTE_EDITOR_WIRING Phase 0
    // (2026-06-04): built from the shared table in
    // ui/properties/culvertcodes.h so this combo, the Property
    // Browser combobox, and any future UI show identical labels.
    QVariantList values;
    {
        QVariantList p;
        p << culvertCodeLabel(0) << QVariant(0);
        values << QVariant(p);
    }
    for (const CulvertCodeInfo &c : culvertCodes()) {
        QVariantList p;
        p << culvertCodeLabel(c.code) << QVariant(c.code);
        values << QVariant(p);
    }
    return values;
}

} // anonymous

SWMMAttributeTableModel::SWMMAttributeTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
    rebuildColumnSchema();

    // Round-4 follow-up 2026-05-12 — refresh the header strip when
    // the user toggles flow units mid-session.  The header label is
    // computed at render time from `unitLabel(spec.unit)`, so this
    // just nudges the view to re-query.
    if (auto *us = UnitSystem::instance()) {
        connect(us, &UnitSystem::unitsChanged, this, [this]() {
            if (columnCount() > 0)
                emit headerDataChanged(Qt::Horizontal, 0, columnCount() - 1);
        });
    }
}

void SWMMAttributeTableModel::setSource(SWMMModelLayer *layer,
                                         SWMMModelLayer::Category category)
{
    beginResetModel();
    m_layer    = layer;
    m_category = category;
    rebuildColumnSchema();
    const int n = m_layer ? m_layer->categoryCount(m_category) : 0;
    m_rowCache.assign(n, {});
    m_rowCacheValid.assign(n, false);
    invalidateCompoundCache();
    endResetModel();
}

void SWMMAttributeTableModel::rebuildColumnSchema()
{
    m_columnSpecs = schemaForCategory(m_category);
    appendUserFlagColumns();
    m_columnKeys.clear();
    m_columnLabels.clear();
    m_columnKeys.reserve(m_columnSpecs.size());
    m_columnLabels.reserve(m_columnSpecs.size());
    for (const auto &spec : m_columnSpecs) {
        m_columnKeys   << spec.key;
        m_columnLabels << spec.label;
    }
}

void SWMMAttributeTableModel::reload()
{
    beginResetModel();
    const int n = m_layer ? m_layer->categoryCount(m_category) : 0;
    m_rowCache.assign(n, {});
    m_rowCacheValid.assign(n, false);
    invalidateCompoundCache();
    endResetModel();
}

void SWMMAttributeTableModel::appendUserFlagColumns()
{
    if (!m_layer) return;
    const QString objType = userFlagObjectType(m_category);
    if (objType.isEmpty()) return;
    auto *ufm = m_layer->ensureUserFlagsModel();
    if (!ufm) return;

    // ATTRIBUTE_EDITOR_WIRING follow-up (2026-06-04) — per-object
    // "User Flags" cell, Property Browser parity: every category whose
    // objects carry [USER_FLAG_VALUES] assignments gets the same
    // summary + UserFlagValuesDialog button the browser row shows,
    // ahead of the per-flag columns below.
    m_columnSpecs.append(compoundCol(QStringLiteral("User flags"),
                                      QStringLiteral("User Flags"),
                                      QStringLiteral("userflags_ref")));

    using openswmmvis::ui::UserFlagsModel;
    for (const auto &def : ufm->defs()) {
        ColumnSpec spec;
        spec.key     = kUserFlagKeyPrefix + def.name;
        spec.label   = def.name;
        spec.setter  = QStringLiteral("userflag");  // marks editable; commit
                                                    // dispatches on the key
        spec.tooltip = def.description;
        if (def.type == UserFlagsModel::FlagType::Boolean) {
            // Explicit (unset) entry so the user can return a boolean flag
            // to the unassigned state from the combo. Labels match the INP
            // tokens the engine round-trips (YES / NO).
            spec.editor = EditorKind::Enum;
            spec.enumValues = { makePair("(unset)", -1),
                                makePair("YES", 1),
                                makePair("NO", 0) };
        } else {
            // Integer / Real / String all edit as text: a blank commit
            // clears the assignment (returns the flag to unset), and the
            // engine validates numeric strings against the declared type.
            spec.editor = EditorKind::Text;
        }
        m_columnSpecs.append(spec);
    }
}

void SWMMAttributeTableModel::invalidateCompoundCache()
{
    m_inflowCountByNode.clear();
    m_dwfCountByNode.clear();
    m_rdiiCountByNode.clear();
    m_treatmentActiveByNode.clear();
    m_compoundPollutantCount = 0;
    m_compoundCacheBuilt     = false;
}

void SWMMAttributeTableModel::ensureCompoundCacheBuilt() const
{
    if (m_compoundCacheBuilt) return;
    if (!m_layer) { m_compoundCacheBuilt = true; return; }
    SWMM_Engine eng = m_layer->engine();
    if (!eng)     { m_compoundCacheBuilt = true; return; }

    m_inflowCountByNode.clear();
    m_dwfCountByNode.clear();
    m_rdiiCountByNode.clear();
    m_treatmentActiveByNode.clear();

    // Inflows — one engine scan over all external inflows; bucket
    // counts by destination node index. Replaces what used to be a
    // full O(inflows) loop per *cell*.
    {
        const int total = swmm_ext_inflow_count(eng);
        char consBuf[64], tsBuf[64], typeBuf[16], patBuf[64];
        for (int i = 0; i < total; ++i) {
            int ni = -1;
            double mf = 0.0, sf = 0.0, base = 0.0;
            if (swmm_ext_inflow_get(eng, i, &ni,
                                      consBuf, sizeof(consBuf),
                                      tsBuf,   sizeof(tsBuf),
                                      typeBuf, sizeof(typeBuf),
                                      &mf, &sf, &base,
                                      patBuf,  sizeof(patBuf)) != SWMM_OK)
                continue;
            if (ni >= 0) ++m_inflowCountByNode[ni];
        }
    }
    {
        const int total = swmm_dwf_count(eng);
        char consBuf[64], p1Buf[64], p2Buf[64], p3Buf[64], p4Buf[64];
        for (int i = 0; i < total; ++i) {
            int ni = -1;
            double avg = 0.0;
            if (swmm_dwf_get(eng, i, &ni,
                              consBuf, sizeof(consBuf),
                              &avg,
                              p1Buf, sizeof(p1Buf),
                              p2Buf, sizeof(p2Buf),
                              p3Buf, sizeof(p3Buf),
                              p4Buf, sizeof(p4Buf)) != SWMM_OK)
                continue;
            if (ni >= 0) ++m_dwfCountByNode[ni];
        }
    }
    {
        const int total = swmm_rdii_count(eng);
        char uhBuf[128];
        for (int i = 0; i < total; ++i) {
            int ni = -1; double area = 0.0;
            if (swmm_rdii_get(eng, i, &ni, uhBuf,
                                sizeof(uhBuf), &area) != SWMM_OK) continue;
            if (ni >= 0) ++m_rdiiCountByNode[ni];
        }
    }
    // Treatment — pollutant_count is a one-time read; the per-node
    // "active" count requires probing each (node, pollutant) pair. We
    // defer that until a Treatment cell is actually requested
    // (computed inline against the cached pollutant count) so we
    // don't pay it on tables that never look at treatment.
    m_compoundPollutantCount = swmm_pollutant_count(eng);
    m_compoundCacheBuilt = true;
}

int SWMMAttributeTableModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    if (!m_layer)         return 0;
    return m_layer->categoryCount(m_category);
}

int SWMMAttributeTableModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_columnKeys.size();
}

QVariant SWMMAttributeTableModel::headerData(int section,
                                              Qt::Orientation orientation,
                                              int role) const
{
    // Round-4 follow-up 2026-05-12 — resolve the semantic unit at
    // render time so the header reflects the project's active flow
    // units (US-customary vs SI).
    if (orientation == Qt::Horizontal && section >= 0
        && section < m_columnSpecs.size()) {
        const auto &spec = m_columnSpecs[section];
        const QString u = unitLabel(spec.unit);
        if (role == Qt::ToolTipRole) {
            // User-flag columns carry the flag description instead of a
            // unit suffix.
            if (!spec.tooltip.isEmpty()) return spec.tooltip;
            return u.isEmpty() ? QVariant() : QVariant(tr("Units: %1").arg(u));
        }
        if (role == Qt::DisplayRole) {
            if (u.isEmpty()) return spec.label;
            return tr("%1 (%2)").arg(spec.label, u);
        }
    }
    if (orientation == Qt::Vertical && role == Qt::DisplayRole)
        return section + 1;  // 1-based row numbers; minor nicety
    return {};
}

QString SWMMAttributeTableModel::objectNameAt(int row) const
{
    if (!m_layer) return {};
    if (row < 0 || row >= m_layer->categoryCount(m_category)) return {};
    return m_layer->objectNameAt(m_category, row);
}

int SWMMAttributeTableModel::rowForName(const QString &name) const
{
    if (!m_layer || name.isEmpty()) return -1;
    SWMMModelLayer::Category cat;
    int row = -1;
    if (!m_layer->findObjectLocation(name, &cat, &row)) return -1;
    return (cat == m_category) ? row : -1;
}

QVariantMap SWMMAttributeTableModel::rowData(int row) const
{
    if (!m_layer) return {};
    if (row < 0 || row >= m_rowCache.size()) return {};
    if (!m_rowCacheValid[row]) {
        const QString name = m_layer->objectNameAt(m_category, row);
        m_rowCache[row]      = m_layer->identifyByName(name);
        m_rowCacheValid[row] = true;
    }
    return m_rowCache[row];
}

QVariant SWMMAttributeTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) return {};
    if (role != Qt::DisplayRole && role != Qt::EditRole &&
        role != Qt::ToolTipRole) return {};

    const int row = index.row();
    const int col = index.column();
    if (col < 0 || col >= m_columnSpecs.size()) return {};

    // Column 0 is always Name — sourced directly from the layer to
    // avoid a per-paint identifyByName lookup for the most-common case.
    if (col == 0) {
        if (role == Qt::ToolTipRole) return {};
        return objectNameAt(row);
    }

    const auto &spec = m_columnSpecs[col];

    // Slice Round-4 polish 2026-05-12 — cell tooltip shows the unit
    // for columns that carry one.  Read-only / unitless columns return
    // an empty variant so Qt suppresses the tooltip entirely.
    if (role == Qt::ToolTipRole) {
        // Inline geom cells carry a shape-specific tooltip ("Diameter",
        // "Max Depth", …) since the column header is the generic "Geom N".
        if (const int ord = xsectGeomOrdinalForTag(spec.setter); ord > 0) {
            const int shape = linkShapeForName(m_layer, objectNameAt(row));
            if (shape < 0 || !openswmmvis::xsectGeomApplies(shape, ord))
                return tr("Not used by this cross-section shape");
            const QString meaning = openswmmvis::xsectGeomLabel(shape, ord);
            return meaning.isEmpty() ? QVariant() : QVariant(meaning);
        }
        // Storage dimension cells do the same — the header is the generic
        // "Shape param N", so the tooltip is where "Base Length" / "Side Slope
        // (run/rise)" actually reaches the user.
        if (const int ord = storageParamOrdinalForTag(spec.setter); ord > 0) {
            const int shape = storageShapeForName(m_layer, objectNameAt(row));
            if (shape < 0 || !openswmmvis::storageGeomApplies(shape, ord))
                return tr("Not used by this storage shape");
            const QString meaning = openswmmvis::storageGeomLabel(shape, ord);
            return meaning.isEmpty() ? QVariant() : QVariant(meaning);
        }
        const QString u = unitLabel(spec.unit);
        return u.isEmpty() ? QVariant() : QVariant(tr("Units: %1").arg(u));
    }

    // Round-4 follow-up 2026-05-12 — Enum columns render the label
    // string (e.g. "CUTOFF") for DisplayRole, the raw int for
    // EditRole.  Showing the raw 0/1/2/3 is meaningless to the user
    // and the delegate's combo only needs the int for round-trip.
    auto labelFor = [&spec](int v) -> QString {
        for (const QVariant &pair : spec.enumValues) {
            const QVariantList lst = pair.toList();
            if (lst.size() == 2 && lst[1].toInt() == v)
                return lst[0].toString();
        }
        return QString::number(v);
    };

    // Compound columns — build a NodeCompoundEditRef live so the
    // delegate's button shows an up-to-date summary. Mirrors the
    // logic in SWMMNodePropertyAdapter::{inflowsRef,dwfRef,rdiiRef,
    // treatmentRef} so both views agree.
    if (spec.editor == EditorKind::Compound && m_layer) {
        const QString name = objectNameAt(row);
        SWMM_Engine eng = m_layer->engine();
        if (!eng || name.isEmpty()) return {};

        // ATTRIBUTE_EDITOR_WIRING follow-up (2026-06-04) — per-object
        // "User Flags" cell, Property Browser parity. Mirrors
        // SWMM*PropertyAdapter::userFlagsRef(); the button's dialog
        // (UserFlagValuesDialog) performs the writes.
        if (spec.setter == QStringLiteral("userflags_ref")) {
            UserFlagsEditRef ref;
            ref.objectType = userFlagObjectType(m_category);
            ref.objectName = name;
            ref.model      = m_layer->ensureUserFlagsModel();
            ref.summary    = userFlagsSummaryFor(ref.model, ref.objectType,
                                                  ref.objectName);
            return QVariant::fromValue(ref);
        }

        // DA.2 parity — rain gage TIME SERIES picker (DataObjectRef cell).
        // Mirrors SWMMRainGagePropertyAdapter::seriesNameRef; the engine
        // write happens in commitValueDirect.
        if (spec.setter == QStringLiteral("gage_series_ref")) {
            const int gIdx = swmm_gage_index(eng, name.toUtf8().constData());
            if (gIdx < 0) return {};
            DataObjectRef dref;
            dref.engine = eng;
            dref.layer  = m_layer;
            dref.kind   = DataObjectRef::TimeSeries;
            char buf[256] = {};
            if (swmm_gage_get_timeseries(eng, gIdx, buf, sizeof(buf)) == SWMM_OK)
                dref.currentName = QString::fromUtf8(buf);
            return QVariant::fromValue(dref);
        }

        // §S.SC.1.c — Link-side compound (XSection / InletUsage).
        // The setter tag's "link_" prefix routes here;
        // node setters fall through to the existing branch below.
        if (spec.setter.startsWith(QStringLiteral("link_"))) {
            const int linkIdx = swmm_link_index(eng, name.toUtf8().constData());
            if (linkIdx < 0) return {};

            // ATTRIBUTE_EDITOR_WIRING follow-up (2026-06-04) — pump
            // curve picker cell. Mirrors
            // SWMMLinkPropertyAdapter::pumpCurveRef(); the engine
            // write happens in commitValueDirect (the picker editor
            // carries no setter callback by design).
            if (spec.setter == QStringLiteral("link_pump_curve_ref")) {
                DataObjectRef dref;
                dref.engine = eng;
                dref.layer  = m_layer;
                dref.kind   = DataObjectRef::AnyCurve;
                int curveIdx = -1;
                if (swmm_link_get_pump_curve(eng, linkIdx, &curveIdx) == SWMM_OK
                    && curveIdx >= 0) {
                    if (const char *id = swmm_table_id(eng, curveIdx))
                        dref.currentName = QString::fromUtf8(id);
                }
                return QVariant::fromValue(dref);
            }

            LinkCompoundEditRef lref;
            lref.engine   = eng;
            lref.linkName = name;
            lref.layer    = m_layer;
            if (spec.setter == QStringLiteral("link_xsect_ref")) {
                lref.kind    = LinkCompoundEditRef::XSection;
                lref.summary = xsectSummaryFor(eng, linkIdx);
            } else if (spec.setter == QStringLiteral("link_inlet_usage_ref")) {
                // Parity pass — placeholder summary mirrors
                // SWMMLinkPropertyAdapter::inletUsageRef (BN-LINK-11).
                lref.kind    = LinkCompoundEditRef::InletUsage;
                lref.summary = tr("(engine API pending — Slice BO 6.5.8)");
            }
            return QVariant::fromValue(lref);
        }

        // Slice AG.4 — storage tabular curve picker (DataObjectRef cell).
        // Mirrors SWMMNodePropertyAdapter::storageCurveRef: resolves to the
        // assigned [STORAGE]-type curve name, empty when the node is using
        // the functional form. The engine write happens in commitValueDirect.
        if (spec.setter == QStringLiteral("node_storage_curve_ref")) {
            const int nodeIdx = swmm_node_index(eng, name.toUtf8().constData());
            if (nodeIdx < 0) return {};
            DataObjectRef dref;
            dref.engine = eng;
            dref.layer  = m_layer;
            dref.kind   = DataObjectRef::StorageCurve;
            int curveIdx = -1;
            if (swmm_node_get_storage_curve(eng, nodeIdx, &curveIdx) == SWMM_OK
                && curveIdx >= 0) {
                if (const char *id = swmm_table_id(eng, curveIdx))
                    dref.currentName = QString::fromUtf8(id);
            }
            return QVariant::fromValue(dref);
        }

        // Parity pass — outfall stage-data pickers (DataObjectRef
        // cells). Mirrors SWMMNodePropertyAdapter::outfallTidalCurveRef
        // / outfallTimeseriesRef: the name resolves only when the
        // outfall's current type matches what the cell means.
        if (spec.setter == QStringLiteral("node_outfall_tidal_ref")
            || spec.setter == QStringLiteral("node_outfall_timeseries_ref")) {
            const bool tidal =
                (spec.setter == QStringLiteral("node_outfall_tidal_ref"));
            const int nodeIdx = swmm_node_index(eng, name.toUtf8().constData());
            if (nodeIdx < 0) return {};
            DataObjectRef dref;
            dref.engine = eng;
            dref.layer  = m_layer;
            dref.kind   = tidal ? DataObjectRef::TidalCurve
                                : DataObjectRef::TimeSeries;
            int type = -1;
            swmm_node_get_outfall_type(eng, nodeIdx, &type);
            const int wantType = tidal ? /*TIDAL*/ 3 : /*TIMESERIES*/ 4;
            if (type == wantType) {
                int tblIdx = -1;
                const int rc = tidal
                    ? swmm_node_get_outfall_tidal(eng, nodeIdx, &tblIdx)
                    : swmm_node_get_outfall_timeseries(eng, nodeIdx, &tblIdx);
                if (rc == SWMM_OK && tblIdx >= 0) {
                    if (const char *id = swmm_table_id(eng, tblIdx))
                        dref.currentName = QString::fromUtf8(id);
                }
            }
            return QVariant::fromValue(dref);
        }

        // Phase 3 — subcatchment rain gage + outlet pickers (DataObjectRef
        // cells). Mirror SWMMSubcatchPropertyAdapter::rainGageRef / outletRef;
        // the engine write happens in commitValueDirect.
        if (spec.setter == QStringLiteral("subcatch_rain_gage_ref")
            || spec.setter == QStringLiteral("subcatch_outlet_ref")) {
            const int sIdx = swmm_subcatch_index(eng, name.toUtf8().constData());
            if (sIdx < 0) return {};
            DataObjectRef dref;
            dref.engine = eng;
            dref.layer  = m_layer;
            if (spec.setter == QStringLiteral("subcatch_rain_gage_ref")) {
                dref.kind = DataObjectRef::RainGage;
                int g = -1;
                if (swmm_subcatch_get_gage(eng, sIdx, &g) == SWMM_OK && g >= 0)
                    if (const char *id = swmm_gage_id(eng, g))
                        dref.currentName = QString::fromUtf8(id);
            } else {
                dref.kind = DataObjectRef::SubcatchOutlet;
                int sc = -1;
                if (swmm_subcatch_get_outlet_subcatch(eng, sIdx, &sc) == SWMM_OK && sc >= 0) {
                    if (const char *id = swmm_subcatch_id(eng, sc))
                        dref.currentName = QString::fromUtf8(id);
                } else {
                    int nd = -1;
                    if (swmm_subcatch_get_outlet(eng, sIdx, &nd) == SWMM_OK && nd >= 0)
                        if (const char *id = swmm_node_id(eng, nd))
                            dref.currentName = QString::fromUtf8(id);
                }
            }
            return QVariant::fromValue(dref);
        }

        // Phase 3 — subcatchment compound cells (land use / groundwater / LID
        // usage). The SubcatchCompoundEditDialog performs the engine writes;
        // the cell only carries the coordinate + a live summary.
        if (spec.setter == QStringLiteral("subcatch_landuse_ref")
            || spec.setter == QStringLiteral("subcatch_groundwater_ref")
            || spec.setter == QStringLiteral("subcatch_lid_ref")) {
            const int sIdx = swmm_subcatch_index(eng, name.toUtf8().constData());
            if (sIdx < 0) return {};
            SubcatchCompoundEditRef sref;
            sref.engine = eng;
            sref.layer  = m_layer;
            sref.subName = name;
            if (spec.setter == QStringLiteral("subcatch_landuse_ref")) {
                sref.kind = SubcatchCompoundEditRef::LandUse;
                int assigned = 0;
                const int nLu = swmm_landuse_count(eng);
                for (int lu = 0; lu < nLu; ++lu) {
                    double frac = 0.0;
                    if (swmm_subcatch_get_coverage(eng, sIdx, lu, &frac) == SWMM_OK && frac > 0.0)
                        ++assigned;
                }
                sref.summary = assigned > 0 ? tr("%1 land use(s)").arg(assigned) : tr("(none)");
            } else if (spec.setter == QStringLiteral("subcatch_groundwater_ref")) {
                sref.kind = SubcatchCompoundEditRef::Groundwater;
                int aq = -1;
                swmm_subcatch_get_aquifer(eng, sIdx, &aq);
                sref.summary = aq >= 0 ? tr("aquifer set") : tr("(none)");
            } else {
                sref.kind = SubcatchCompoundEditRef::LidUsage;
                int mine = 0;
                const int nU = swmm_lid_usage_count(eng);
                for (int u = 0; u < nU; ++u) {
                    int sc = -1;
                    if (swmm_lid_usage_get(eng, u, &sc, nullptr, nullptr, nullptr,
                                           nullptr, nullptr, nullptr, nullptr, nullptr) == SWMM_OK
                        && sc == sIdx)
                        ++mine;
                }
                sref.summary = mine > 0 ? tr("%1 LID(s)").arg(mine) : tr("(none)");
            }
            return QVariant::fromValue(sref);
        }

        const int nodeIdx = swmm_node_index(eng, name.toUtf8().constData());
        if (nodeIdx < 0) return {};

        NodeCompoundEditRef ref;
        ref.engine   = eng;
        ref.nodeName = name;
        ref.layer    = m_layer;

        ensureCompoundCacheBuilt();

        if (spec.setter == QStringLiteral("node_inflows_ref")) {
            ref.kind = NodeCompoundEditRef::Inflows;
            const int matched = m_inflowCountByNode.value(nodeIdx, 0);
            ref.summary = (matched > 0)
                ? tr("%1 entries").arg(matched)
                : tr("(none)");
        } else if (spec.setter == QStringLiteral("node_dwf_ref")) {
            ref.kind = NodeCompoundEditRef::Dwf;
            const int matched = m_dwfCountByNode.value(nodeIdx, 0);
            ref.summary = (matched > 0)
                ? tr("%1 entries").arg(matched)
                : tr("(none)");
        } else if (spec.setter == QStringLiteral("node_rdii_ref")) {
            ref.kind = NodeCompoundEditRef::Rdii;
            const int matched = m_rdiiCountByNode.value(nodeIdx, 0);
            ref.summary = (matched > 0)
                ? tr("%1 entries").arg(matched)
                : tr("(none)");
        } else if (spec.setter == QStringLiteral("node_treatment_ref")) {
            ref.kind = NodeCompoundEditRef::Treatment;
            const int nPollut = m_compoundPollutantCount;
            // Treatment "active" requires per-(node, pollutant) probes;
            // cache them lazily per node so the row is paid once, not
            // once per cell paint.
            int active = 0;
            auto it = m_treatmentActiveByNode.constFind(nodeIdx);
            if (it != m_treatmentActiveByNode.constEnd()) {
                active = it.value();
            } else {
                char buf[256];
                for (int p = 0; p < nPollut; ++p) {
                    buf[0] = '\0';
                    if (swmm_treatment_get(eng, nodeIdx, p, buf, sizeof(buf)) != SWMM_OK)
                        continue;
                    if (buf[0] != '\0') ++active;
                }
                m_treatmentActiveByNode.insert(nodeIdx, active);
            }
            ref.summary = (active > 0)
                ? tr("%1 / %2 pollutants").arg(active).arg(nPollut)
                : tr("(none)");
        }
        return QVariant::fromValue(ref);
    }

    // User-flag columns (Phase 3, docs/USER_FLAGS_UI_PLAN_2026-06-03.md):
    // values live in the engine's UserFlags store, not the identify map
    // or the setter dispatch table. Unset reads as blank; booleans hand
    // the delegate's combo -1 / 0 / 1 for (unset) / NO / YES.
    if (spec.key.startsWith(kUserFlagKeyPrefix) && m_layer) {
        auto *ufm = m_layer->ensureUserFlagsModel();
        if (!ufm) return {};
        const QString flagName = spec.key.mid(kUserFlagKeyPrefix.size());
        const QString objType  = userFlagObjectType(m_category);
        const QString name     = objectNameAt(row);
        bool found = false;
        const QString v = ufm->value(objType, name, flagName, &found);
        if (spec.editor == EditorKind::Enum) {  // Boolean flag
            const int iv = !found ? -1
                         : (v == QStringLiteral("YES") ? 1 : 0);
            if (role == Qt::DisplayRole)
                return found ? QVariant(v) : QVariant(QString());
            return iv;  // EditRole — combo data
        }
        return v;  // blank when unset (both Display and Edit roles)
    }

    // Editable columns: read from the engine setter's matching
    // getter so the value reflects post-commit state (the
    // identifyByName cache doesn't track per-attribute updates).
    if (spec.editor != EditorKind::ReadOnly && !spec.setter.isEmpty() && m_layer) {
        // Inline geom cell that doesn't apply to this row's shape → blank
        // (the cell is also non-editable via flags()), so a stale 0 isn't
        // shown as if it were a real, editable dimension.
        if (const int ord = xsectGeomOrdinalForTag(spec.setter); ord > 0) {
            const int shape = linkShapeForName(m_layer, objectNameAt(row));
            if (shape < 0 || !openswmmvis::xsectGeomApplies(shape, ord))
                return {};
        }
        // Same for a storage dimension the live shape doesn't use — a paraboloid
        // still carries whatever side slope a previous shape left in p3, and showing
        // that stale number as an editable value is worse than showing nothing.
        if (const int ord = storageParamOrdinalForTag(spec.setter); ord > 0) {
            const int shape = storageShapeForName(m_layer, objectNameAt(row));
            if (shape < 0 || !openswmmvis::storageGeomApplies(shape, ord))
                return {};
        }
        const auto entry = setterFor(spec.setter);
        const QString name = objectNameAt(row);
        const int entIdx = indexForName(m_layer->engine(), entry.kind,
                                          name.toUtf8().constData());
        if (entIdx >= 0) {
            if (entry.getFn) {
                double v = 0.0;
                if (entry.getFn(m_layer->engine(), entIdx, &v) == SWMM_OK)
                    return v;
            } else if (entry.getFnI) {
                int v = 0;
                if (entry.getFnI(m_layer->engine(), entIdx, &v) == SWMM_OK) {
                    if (spec.editor == EditorKind::Enum && role == Qt::DisplayRole)
                        return labelFor(v);
                    return v;
                }
            } else if (entry.getFnS) {
                // String path (DB.5 tag): read into a stack buffer and
                // hand back as QString. Empty string is a valid value
                // (meaning "no tag"); display as blank in the cell.
                char sbuf[256] = {0};
                if (entry.getFnS(m_layer->engine(), entIdx, sbuf, sizeof(sbuf)) == SWMM_OK)
                    return QString::fromUtf8(sbuf);
            }
        }
    }

    // ATTRIBUTE_EDITOR_WIRING parity pass (2026-06-04) — link
    // endpoints. identifyByName doesn't carry From/To node keys, so
    // resolve them live from the engine (mirrors
    // SWMMLinkPropertyAdapter::fromNode / toNode).
    if (spec.editor == EditorKind::ReadOnly && m_layer
        && (spec.key == QStringLiteral("From node")
            || spec.key == QStringLiteral("To node"))) {
        SWMM_Engine eng = m_layer->engine();
        const QString name = objectNameAt(row);
        if (!eng || name.isEmpty()) return {};
        const int linkIdx = swmm_link_index(eng, name.toUtf8().constData());
        if (linkIdx < 0) return {};
        int nodeIdx = -1;
        const int rc = (spec.key == QStringLiteral("From node"))
            ? swmm_link_get_from_node(eng, linkIdx, &nodeIdx)
            : swmm_link_get_to_node(eng, linkIdx, &nodeIdx);
        if (rc != SWMM_OK || nodeIdx < 0) return {};
        const char *n = swmm_node_id(eng, nodeIdx);
        return n ? QString::fromUtf8(n) : QString();
    }

    const QVariantMap m = rowData(row);
    QVariant fallback = m.value(spec.key);
    // For enum columns whose engine getter isn't wired (or
    // identifyByName has already pre-formatted), still map an
    // integer fallback value to its label for DisplayRole.
    if (spec.editor == EditorKind::Enum && role == Qt::DisplayRole
        && fallback.isValid()) {
        bool ok = false;
        const int v = fallback.toInt(&ok);
        if (ok) return labelFor(v);
    }
    return fallback;
}

Qt::ItemFlags SWMMAttributeTableModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags f = QAbstractTableModel::flags(index);
    if (!index.isValid()) return f;
    const int col = index.column();
    if (col < 0 || col >= m_columnSpecs.size()) return f;
    const auto &spec = m_columnSpecs[col];
    if (spec.editor != EditorKind::ReadOnly && !spec.setter.isEmpty()) {
        // Lock edits while a simulation is running.
        if (m_layer && m_layer->engine()) {
            int state = 0;
            if (swmm_engine_get_state(m_layer->engine(), &state) == SWMM_OK
                && state == SWMM_STATE_RUNNING)
                return f;  // no ItemIsEditable
        }
        // Inline cross-section geom cells are editable only when the geom
        // applies to this row's shape (e.g. Geom 2 on a CIRCULAR conduit,
        // or any geom on IRREGULAR/STREET, is greyed — set via the dialog).
        if (const int ord = xsectGeomOrdinalForTag(spec.setter); ord > 0) {
            const int shape = linkShapeForName(m_layer, objectNameAt(index.row()));
            if (shape < 0 || !openswmmvis::xsectGeomApplies(shape, ord))
                return f;  // no ItemIsEditable
        }
        // Storage dimension cells: editable only for the shapes that use them.
        // Also keeps the curve / functional-coefficient cells from being edited on a
        // node whose shape doesn't use them — the engine would reject or silently
        // reinterpret the write, and the panel already greys the same rows.
        if (const int ord = storageParamOrdinalForTag(spec.setter); ord > 0) {
            const int shape = storageShapeForName(m_layer, objectNameAt(index.row()));
            if (shape < 0 || !openswmmvis::storageGeomApplies(shape, ord))
                return f;  // no ItemIsEditable
        }
        f |= Qt::ItemIsEditable;
    }
    return f;
}

namespace {

// One undo command per cell edit.
class AttributeEditCommand : public QUndoCommand {
public:
    AttributeEditCommand(SWMMAttributeTableModel *model, int row, int col,
                          QVariant oldVal, QVariant newVal,
                          const QString &columnLabel)
        : m_model(model), m_row(row), m_col(col),
          m_old(std::move(oldVal)), m_new(std::move(newVal)) {
        setText(QObject::tr("Edit %1").arg(columnLabel));
    }
    void redo() override { apply(m_new); }
    void undo() override { apply(m_old); }
private:
    void apply(const QVariant &v) {
        if (!m_model) return;
        const QModelIndex idx = m_model->index(m_row, m_col);
        m_model->commitValueDirect(idx, v);
    }
    QPointer<SWMMAttributeTableModel> m_model;
    int m_row, m_col;
    QVariant m_old, m_new;
};

// One undo command per name rename.
class AttributeRenameCommand : public QUndoCommand {
public:
    AttributeRenameCommand(SWMMAttributeTableModel *model, int row,
                            QString oldName, QString newName)
        : m_model(model), m_row(row),
          m_old(std::move(oldName)), m_new(std::move(newName)) {
        setText(QObject::tr("Rename to \"%1\"").arg(m_new));
    }
    void redo() override { apply(m_old, m_new); }
    void undo() override { apply(m_new, m_old); }
private:
    void apply(const QString &from, const QString &to) {
        if (!m_model || !m_model->layer()) return;
        m_model->layer()->applyRename(
            from, to, SWMMModelLayer::kindBitForCategory(m_model->category()));
        // Refresh the model row so the Name cell re-reads from the engine.
        if (m_row >= 0 && m_row < m_model->rowCount()) {
            const QModelIndex nameIdx = m_model->index(m_row, 0);
            emit m_model->dataChanged(nameIdx, nameIdx,
                                      {Qt::DisplayRole, Qt::EditRole});
        }
    }
    QPointer<SWMMAttributeTableModel> m_model;
    int m_row;
    QString m_old, m_new;
};

} // anonymous

bool SWMMAttributeTableModel::commitValueDirect(const QModelIndex &index,
                                                  const QVariant &value)
{
    if (!index.isValid() || !m_layer) return false;
    const int row = index.row();
    const int col = index.column();
    if (col < 0 || col >= m_columnSpecs.size()) return false;

    const auto &spec = m_columnSpecs[col];
    if (spec.editor == EditorKind::ReadOnly || spec.setter.isEmpty()) return false;

    // Name column (col 0, EditorKind::Text, setter == "rename").
    if (col == 0 && spec.editor == EditorKind::Text) {
        const QString oldName = objectNameAt(row);
        const QString newName = value.toString().trimmed();
        if (newName.isEmpty() || newName == oldName) return false;
        if (!m_layer->applyRename(oldName, newName,
                                  SWMMModelLayer::kindBitForCategory(m_category)))
            return false;
        if (row >= 0 && row < m_rowCacheValid.size())
            m_rowCacheValid[row] = false;
        emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
        emit objectEdited(newName);
        return true;
    }

    // User-flag columns (Phase 3, docs/USER_FLAGS_UI_PLAN_2026-06-03.md).
    // Boolean combos commit -1 / 0 / 1 ((unset) / NO / YES); text columns
    // commit strings where blank clears the assignment. All writes route
    // through UserFlagsModel so the Attribute Panel and any other observer
    // see the same valueChanged() notification.
    if (spec.key.startsWith(kUserFlagKeyPrefix)) {
        auto *ufm = m_layer->ensureUserFlagsModel();
        if (!ufm) return false;
        const QString flagName = spec.key.mid(kUserFlagKeyPrefix.size());
        const QString objType  = userFlagObjectType(m_category);
        const QString name     = objectNameAt(row);
        if (objType.isEmpty() || name.isEmpty()) return false;

        bool ok = false;
        if (spec.editor == EditorKind::Enum) {  // Boolean flag
            const int iv = value.toInt();
            ok = (iv < 0)
                ? ufm->clearValue(objType, name, flagName)
                : ufm->setValue(objType, name, flagName,
                                iv ? QStringLiteral("YES")
                                   : QStringLiteral("NO"));
        } else {
            const QString s = value.toString().trimmed();
            ok = s.isEmpty()
                ? ufm->clearValue(objType, name, flagName)
                : ufm->setValue(objType, name, flagName, s);
        }
        if (!ok) return false;

        emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
        emit objectEdited(name);
        return true;
    }

    // ATTRIBUTE_EDITOR_WIRING (2026-06-04) — picker cells
    // (DataObjectRef). Unlike the dialog-backed compound cells, the
    // DataObjectPickerEditor carries no setter callback (MVC contract:
    // the ref is just a coordinate), so the engine write happens here,
    // dispatched on the setter tag. Mirrors the matching adapter WRITE
    // slots (setPumpCurveRef / setOutfallTidalCurveRef /
    // setOutfallTimeseriesRef).
    if (value.userType() == qMetaTypeId<DataObjectRef>()) {
        SWMM_Engine eng = m_layer->engine();
        const QString name = objectNameAt(row);
        if (!eng || name.isEmpty()) return false;
        const auto dref = value.value<DataObjectRef>();

        int rc = -1;
        if (spec.setter == QStringLiteral("link_pump_curve_ref")) {
            // Pump curve / tabular outlet curve (shared engine slot).
            // Empty name clears the assignment (curveIdx == -1).
            const int linkIdx = swmm_link_index(eng, name.toUtf8().constData());
            if (linkIdx < 0) return false;
            int curveIdx = -1;
            if (!dref.currentName.isEmpty()) {
                curveIdx = swmm_table_index(eng,
                                             dref.currentName.toUtf8().constData());
                if (curveIdx < 0) return false;   // unknown curve — ignore
            }
            rc = swmm_link_set_pump_curve(eng, linkIdx, curveIdx);
        } else if (spec.setter == QStringLiteral("node_outfall_tidal_ref")
                   || spec.setter == QStringLiteral("node_outfall_timeseries_ref")) {
            // Engine setters also flip the outfall type (TIDAL /
            // TIMESERIES) — deliberate invariant, same as the browser.
            // No-op on empty name, mirroring the adapter slots.
            const int nodeIdx = swmm_node_index(eng, name.toUtf8().constData());
            if (nodeIdx < 0 || dref.currentName.isEmpty()) return false;
            const int tblIdx = swmm_table_index(eng,
                                                 dref.currentName.toUtf8().constData());
            if (tblIdx < 0) return false;
            rc = (spec.setter == QStringLiteral("node_outfall_tidal_ref"))
                ? swmm_node_set_outfall_tidal(eng, nodeIdx, tblIdx)
                : swmm_node_set_outfall_timeseries(eng, nodeIdx, tblIdx);
        } else if (spec.setter == QStringLiteral("node_storage_curve_ref")) {
            // Slice AG.4 — assign a [STORAGE]-type curve (TABULAR) or, on an
            // empty name, detach it (curveIdx == -1) reverting to FUNCTIONAL.
            // Mirrors SWMMNodePropertyAdapter::setStorageCurveRef.
            const int nodeIdx = swmm_node_index(eng, name.toUtf8().constData());
            if (nodeIdx < 0) return false;
            int curveIdx = -1;
            if (!dref.currentName.isEmpty()) {
                curveIdx = swmm_table_index(eng,
                                             dref.currentName.toUtf8().constData());
                if (curveIdx < 0) return false;   // unknown curve — ignore
            }
            rc = swmm_node_set_storage_curve(eng, nodeIdx, curveIdx);
        } else if (spec.setter == QStringLiteral("gage_series_ref")) {
            // DA.2 parity — assign a [TIMESERIES] source to the gage. The
            // engine setter also flips the data source to TIMESERIES.
            // Mirrors SWMMRainGagePropertyAdapter::setSeriesNameRef; an
            // empty pick is ignored (series is required for this source).
            const int gIdx = swmm_gage_index(eng, name.toUtf8().constData());
            if (gIdx < 0 || dref.currentName.isEmpty()) return false;
            rc = swmm_gage_set_timeseries(eng, gIdx,
                                          dref.currentName.toUtf8().constData());
        } else if (spec.setter == QStringLiteral("subcatch_rain_gage_ref")) {
            // Mirrors SWMMSubcatchPropertyAdapter::setRainGageRef; gage is
            // required, so an empty pick is ignored.
            const int sIdx = swmm_subcatch_index(eng, name.toUtf8().constData());
            if (sIdx < 0 || dref.currentName.isEmpty()) return false;
            const int g = swmm_gage_index(eng, dref.currentName.toUtf8().constData());
            if (g < 0) return false;
            rc = swmm_subcatch_set_gage(eng, sIdx, g);
        } else if (spec.setter == QStringLiteral("subcatch_outlet_ref")) {
            // Combined node/subcatch outlet; node takes precedence on a name
            // collision. Mirrors SWMMSubcatchPropertyAdapter::setOutletRef.
            const int sIdx = swmm_subcatch_index(eng, name.toUtf8().constData());
            if (sIdx < 0 || dref.currentName.isEmpty()) return false;
            const QByteArray nm = dref.currentName.toUtf8();
            const int nd = swmm_node_index(eng, nm.constData());
            if (nd >= 0) {
                rc = swmm_subcatch_set_outlet(eng, sIdx, nd);
            } else {
                const int sc = swmm_subcatch_index(eng, nm.constData());
                if (sc < 0 || sc == sIdx) return false;
                rc = swmm_subcatch_set_outlet_subcatch(eng, sIdx, sc);
            }
        } else {
            return false;
        }
        if (rc != SWMM_OK) return false;

        if (row >= 0 && row < m_rowCacheValid.size())
            m_rowCacheValid[row] = false;
        // Outfall picker writes flip the type enum too — repaint the
        // whole row so the Boundary Type cell tracks the change.
        const int lastCol = columnCount() - 1;
        emit dataChanged(this->index(row, 0), this->index(row, lastCol),
                         {Qt::DisplayRole, Qt::EditRole});
        emit objectEdited(name);
        return true;
    }

    // Compound columns — the actual writes (Add RDII, set treatment
    // expression, etc.) happen inside NodeCompoundEditDialog as the
    // user commits each row. By the time the delegate fires setData
    // here, engine state is already updated. We just invalidate the
    // row cache so the summary recomputes on the next paint and
    // notify external listeners (Property Browser) to refresh.
    if (spec.editor == EditorKind::Compound) {
        const QString name = objectNameAt(row);
        if (row >= 0 && row < m_rowCacheValid.size())
            m_rowCacheValid[row] = false;
        const int lastCol = columnCount() - 1;
        emit dataChanged(this->index(row, 0), this->index(row, lastCol),
                         {Qt::DisplayRole, Qt::EditRole});
        emit objectEdited(name);
        return true;
    }

    // Slice DB — node X/Y coordinate edits route through applyNodeMove
    // (not the single-double SetterEntry table) so the scene-coord cache
    // and attached-link bboxes refresh atomically with the engine write.
    if (spec.setter == QStringLiteral("node_coord_x") ||
        spec.setter == QStringLiteral("node_coord_y")) {
        const QString name = objectNameAt(row);
        const int nodeIdx = swmm_node_index(m_layer->engine(),
                                              name.toUtf8().constData());
        if (nodeIdx < 0) return false;
        double cx = 0.0, cy = 0.0;
        if (swmm_spatial_get_node_coord(m_layer->engine(), nodeIdx,
                                          &cx, &cy) != SWMM_OK) return false;
        bool ok = false;
        const double nv = value.toDouble(&ok);
        if (!ok) return false;
        const bool isX = (spec.setter == QStringLiteral("node_coord_x"));
        const double newX = isX ? nv : cx;
        const double newY = isX ? cy : nv;
        if (!m_layer->applyNodeMove(nodeIdx, newX, newY)) return false;
        if (row >= 0 && row < m_rowCacheValid.size())
            m_rowCacheValid[row] = false;
        // Repaint the whole row — moving the node updates X, Y, and the
        // stat block reads (Max Depth Sim. is location-independent but
        // re-issuing dataChanged keeps the view consistent).
        const int lastCol = columnCount() - 1;
        emit dataChanged(this->index(row, 0), this->index(row, lastCol),
                         {Qt::DisplayRole, Qt::EditRole});
        emit objectEdited(name);
        return true;
    }

    // Slice AG.4 — storage shape flips between functional coefficients and a
    // tabular curve, which mutates the sibling Storage Curve cell. Commit via
    // the same dispatch table, then repaint the whole row so the curve cell
    // tracks the change (the generic tail below repaints only the edited cell).
    if (spec.setter == QStringLiteral("node_storage_shape")) {
        const QString name = objectNameAt(row);
        const int nodeIdx = swmm_node_index(m_layer->engine(),
                                             name.toUtf8().constData());
        if (nodeIdx < 0) return false;
        bool ok = false;
        const int iv = value.toInt(&ok);
        if (!ok) return false;
        if (storageShapeSetI(m_layer->engine(), nodeIdx, iv) != SWMM_OK)
            return false;
        if (row >= 0 && row < m_rowCacheValid.size())
            m_rowCacheValid[row] = false;
        const int lastCol = columnCount() - 1;
        emit dataChanged(this->index(row, 0), this->index(row, lastCol),
                         {Qt::DisplayRole, Qt::EditRole});
        emit objectEdited(name);
        return true;
    }

    const auto entry = setterFor(spec.setter);
    if (!entry.setFn && !entry.setFnI && !entry.setFnS) return false;

    const QString name = objectNameAt(row);
    const int entIdx = indexForName(m_layer->engine(), entry.kind,
                                      name.toUtf8().constData());
    if (entIdx < 0) return false;

    int rc = -1;
    if (entry.setFn) {
        bool ok = false;
        double v = value.toDouble(&ok);
        if (!ok) return false;
        if (v < spec.minValue) v = spec.minValue;
        if (v > spec.maxValue) v = spec.maxValue;
        rc = entry.setFn(m_layer->engine(), entIdx, v);
    } else if (entry.setFnI) {
        bool ok = false;
        const int iv = value.toInt(&ok);
        if (!ok) return false;
        rc = entry.setFnI(m_layer->engine(), entIdx, iv);
    } else /* entry.setFnS */ {
        const QByteArray bytes = value.toString().toUtf8();
        rc = entry.setFnS(m_layer->engine(), entIdx, bytes.constData());
    }
    if (rc != SWMM_OK) return false;

    if (row >= 0 && row < m_rowCacheValid.size())
        m_rowCacheValid[row] = false;
    emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
    emit objectEdited(name);
    return true;
}

void SWMMAttributeTableModel::refreshObject(const QString &name)
{
    if (!m_layer || name.isEmpty()) return;
    const int row = rowForName(name);
    if (row < 0) return;
    if (row < m_rowCacheValid.size())
        m_rowCacheValid[row] = false;
    const int lastCol = columnCount() - 1;
    if (lastCol < 0) return;
    emit dataChanged(index(row, 0), index(row, lastCol),
                     {Qt::DisplayRole, Qt::EditRole});
}

bool SWMMAttributeTableModel::setData(const QModelIndex &index,
                                        const QVariant &value, int role)
{
    if (!index.isValid() || role != Qt::EditRole) return false;
    if (!m_layer) return false;

    const int row = index.row();
    const int col = index.column();
    if (col < 0 || col >= m_columnSpecs.size()) return false;
    const auto &spec = m_columnSpecs[col];
    if (spec.editor == EditorKind::ReadOnly || spec.setter.isEmpty()) return false;

    // Name column rename — duplicate-name check happens inside applyRename().
    if (col == 0 && spec.editor == EditorKind::Text) {
        const QString oldName = objectNameAt(row);
        const QString newName = value.toString().trimmed();
        if (newName.isEmpty() || newName == oldName) return true;  // no-op

        if (!m_undoStack)
            return commitValueDirect(index, value);

        // Push an undo command; commitValueDirect will be called inside it.
        m_undoStack->push(
            new AttributeRenameCommand(this, row, oldName, newName));
        return true;
    }

    // Compound columns mutate engine state inside the dialog over
    // potentially many individual rows; wrapping them in a single
    // QUndoCommand here would be misleading (undo would only revert
    // the last cell flip, not the in-dialog mutations). Commit
    // directly so the row refreshes but no command is pushed.
    if (spec.editor == EditorKind::Compound)
        return commitValueDirect(index, value);

    // Wrap each numeric/enum commit in an undo command when a stack is attached.
    if (!m_undoStack)
        return commitValueDirect(index, value);

    const QVariant oldValue = data(index, Qt::EditRole);
    if (oldValue == value) return true;  // no-op edit

    m_undoStack->push(
        new AttributeEditCommand(this, row, col, oldValue, value, spec.label));
    return true;
}
