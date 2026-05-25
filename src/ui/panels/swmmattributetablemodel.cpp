/*!
 * \file   swmmattributetablemodel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/panels/swmmattributetablemodel.h"

#include "core/unitsystem.h"
#include "ui/properties/nodecompoundeditref.h"

#include <QUndoCommand>
#include <QUndoStack>

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_gages.h>
#include <openswmm/engine/openswmm_inflows.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_pollutants.h>
#include <openswmm/engine/openswmm_quality.h>
#include <openswmm/engine/openswmm_spatial.h>
#include <openswmm/engine/openswmm_subcatchments.h>

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

// Compound-attribute column (Inflows / DWF / RDII / Treatment). The
// cell holds a NodeCompoundEditRef built live in data(); the delegate
// (CompoundEditDelegate) instantiates a NodeCompoundEditButton that
// reuses the same NodeCompoundEditDialog the Property Browser opens.
// The `setter` tag is a sentinel — actual writes happen inside the
// dialog via swmm_rdii_add / swmm_treatment_set / etc., so the table
// model's commitValueDirect path just refreshes the row when the
// delegate fires setData (so other columns reflecting the same engine
// state stay current).
ColumnSpec compoundCol(const QString &key, const QString &label,
                        const QString &setterTag) {
    ColumnSpec c;
    c.key    = key;
    c.label  = label;
    c.editor = EditorKind::Compound;
    c.setter = setterTag;
    return c;
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
            enumCol("Flap gate",   "Flap Gate",
                                                "node_outfall_flap_gate",
                                                yesNoValues()),
        };
        cols.append(nodeStatBlock());
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
        cols.append(nodeStatBlock());
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
        };
        cols.append(nodeStatBlock());
        return cols;
    }
    case SWMMModelLayer::CatConduits:
        return {
            nameCol(),
            ro("Link type",    "Link Type"),
            ro("Vertex count", "Vertex Count"),
            num("Length",      "Length",        "link_length",
                                                  0.0, 1e9, 2, UnitKind::Length),
            num("Roughness",   "Manning's n",   "link_roughness",
                                                  1e-6, 1.0, 4),
            num("Offset up",   "Upstream Offset",   "link_offset_up",
                                                  -1e6, 1e6, 4, UnitKind::Length),
            num("Offset dn",   "Downstream Offset", "link_offset_dn",
                                                  -1e6, 1e6, 4, UnitKind::Length),
            enumCol("Flap gate",    "Flap Gate",     "link_flap_gate",     yesNoValues()),
            enumCol("Culvert code", "Culvert Code",  "link_culvert_code",  culvertCodeValues()),
        };
    case SWMMModelLayer::CatPumps:
        // SWMM5 [PUMPS]: Name | FromNode | ToNode | PumpCurve | Status | Startup | Shutoff.
        // Startup/Shutoff have no engine accessors today; PumpCurve is
        // read-only (name picker lands in AG.5).
        return {
            nameCol(),
            ro("Link type",    "Link Type"),
            ro("Vertex count", "Vertex Count"),
            enumCol("Initial state", "Initial State",
                                                "link_pump_init_state",
                                                offOnValues()),
        };
    case SWMMModelLayer::CatWeirs:
        // SWMM5 [WEIRS]: Name | FromNode | ToNode | Type | CrestHt | Cd |
        // Gated | EndCon | EndCoeff.  No offsets — weirs sit at the
        // crest elevation directly.
        return {
            nameCol(),
            ro("Link type",    "Link Type"),
            ro("Vertex count", "Vertex Count"),
            num("Crest height", "Crest Height",   "link_crest_height",
                                                  0.0, 1e6, 4, UnitKind::Length),
            num("Discharge coeff", "Discharge Coefficient",
                                                  "link_discharge_coeff",
                                                  0.0, 100.0, 4),
            num("End contractions", "End Contractions",
                                                  "link_end_contractions",
                                                  0.0, 1e3, 0),
            enumCol("Flap gate", "Flap Gate",     "link_flap_gate", yesNoValues()),
        };
    case SWMMModelLayer::CatOrifices:
        // SWMM5 [ORIFICES]: Name | FromNode | ToNode | Type | Offset |
        // Cd | Gated.  One offset (no downstream offset).
        return {
            nameCol(),
            ro("Link type",    "Link Type"),
            ro("Vertex count", "Vertex Count"),
            num("Offset up",   "Offset",   "link_offset_up",
                                                  -1e6, 1e6, 4, UnitKind::Length),
            num("Discharge coeff", "Discharge Coefficient",
                                                  "link_discharge_coeff",
                                                  0.0, 100.0, 4),
            enumCol("Flap gate", "Flap Gate",       "link_flap_gate", yesNoValues()),
        };
    case SWMMModelLayer::CatOutlets:
        // SWMM5 [OUTLETS]: Name | FromNode | ToNode | Offset | Type |
        // Coeff | Expon.  Coeff/Expon land with AG.4's conditional
        // type picker.
        return {
            nameCol(),
            ro("Link type",    "Link Type"),
            ro("Vertex count", "Vertex Count"),
            num("Offset up",   "Offset",   "link_offset_up",
                                                  -1e6, 1e6, 4, UnitKind::Length),
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
        };
    case SWMMModelLayer::CatRainGages:
        return {
            nameCol(),
            ro("X",    "X Coordinate"),
            ro("Y",    "Y Coordinate"),
        };
    default:
        return { nameCol() };
    }
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
QVariantList culvertCodeValues() {
    // Per legacy SWMM5 — 0 = no inlet control (default), 1..57 are
    // HDS-5 culvert codes.  Surfacing the common entries here; for
    // the long tail users can still type a number via the schema's
    // Integer delegate (a future polish).
    return {
        makePair("None (0)",                       0),
        makePair("Circular concrete (1)",          1),
        makePair("Circular CMP (2)",               2),
        makePair("Box concrete (29)",              29),
        makePair("Box CMP (30)",                   30),
    };
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
    endResetModel();
}

void SWMMAttributeTableModel::rebuildColumnSchema()
{
    m_columnSpecs = schemaForCategory(m_category);
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
    endResetModel();
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
        if (role == Qt::ToolTipRole)
            return u.isEmpty() ? QVariant() : QVariant(tr("Units: %1").arg(u));
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
        const int nodeIdx = swmm_node_index(eng, name.toUtf8().constData());
        if (nodeIdx < 0) return {};

        NodeCompoundEditRef ref;
        ref.engine   = eng;
        ref.nodeName = name;
        ref.layer    = m_layer;

        if (spec.setter == QStringLiteral("node_inflows_ref")) {
            ref.kind = NodeCompoundEditRef::Inflows;
            const int total = swmm_ext_inflow_count(eng);
            int matched = 0;
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
                if (ni == nodeIdx) ++matched;
            }
            ref.summary = (matched > 0)
                ? tr("%1 entries").arg(matched)
                : tr("(none)");
        } else if (spec.setter == QStringLiteral("node_dwf_ref")) {
            ref.kind = NodeCompoundEditRef::Dwf;
            const int total = swmm_dwf_count(eng);
            int matched = 0;
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
                if (ni == nodeIdx) ++matched;
            }
            ref.summary = (matched > 0)
                ? tr("%1 entries").arg(matched)
                : tr("(none)");
        } else if (spec.setter == QStringLiteral("node_rdii_ref")) {
            ref.kind = NodeCompoundEditRef::Rdii;
            const int total = swmm_rdii_count(eng);
            int matched = 0;
            char uhBuf[128];
            for (int i = 0; i < total; ++i) {
                int ni = -1; double area = 0.0;
                if (swmm_rdii_get(eng, i, &ni, uhBuf,
                                    sizeof(uhBuf), &area) != SWMM_OK) continue;
                if (ni == nodeIdx) ++matched;
            }
            ref.summary = (matched > 0)
                ? tr("%1 entries").arg(matched)
                : tr("(none)");
        } else if (spec.setter == QStringLiteral("node_treatment_ref")) {
            ref.kind = NodeCompoundEditRef::Treatment;
            const int nPollut = swmm_pollutant_count(eng);
            int active = 0;
            char buf[256];
            for (int p = 0; p < nPollut; ++p) {
                buf[0] = '\0';
                if (swmm_treatment_get(eng, nodeIdx, p, buf, sizeof(buf)) != SWMM_OK)
                    continue;
                if (buf[0] != '\0') ++active;
            }
            ref.summary = (active > 0)
                ? tr("%1 / %2 pollutants").arg(active).arg(nPollut)
                : tr("(none)");
        }
        return QVariant::fromValue(ref);
    }

    // Editable columns: read from the engine setter's matching
    // getter so the value reflects post-commit state (the
    // identifyByName cache doesn't track per-attribute updates).
    if (spec.editor != EditorKind::ReadOnly && !spec.setter.isEmpty() && m_layer) {
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
        m_model->layer()->applyRename(from, to);
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
        if (!m_layer->applyRename(oldName, newName)) return false;
        if (row >= 0 && row < m_rowCacheValid.size())
            m_rowCacheValid[row] = false;
        emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
        emit objectEdited(newName);
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
