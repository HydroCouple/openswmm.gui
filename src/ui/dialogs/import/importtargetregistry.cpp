/*!
 * \file   importtargetregistry.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/import/importtargetregistry.h"

namespace openswmmvis::import {

namespace {

// Shorthand builders --------------------------------------------------------

TargetAttribute nameAttr()
{
    return { QStringLiteral("name"),
             ImportTargetRegistry::tr("Name (unique ID)"),
             QMetaType::QString, /*required*/ true, QString(), {} };
}

TargetAttribute endpointAttr(const char *key, const QString &label)
{
    // Consumed by applyLinkAdd, not by an adapter property. Marked
    // optional here; the planner enforces required-ness only when the
    // "use attribute columns" endpoint strategy is enabled alone.
    return { QLatin1String(key), label, QMetaType::QString,
             /*required*/ false, QString(), {} };
}

TargetAttribute dbl(const char *key, const QString &label,
                    const char *prop, bool required = false)
{
    return { QLatin1String(key), label, QMetaType::Double, required,
             QLatin1String(prop), {} };
}

TargetAttribute intAttr(const char *key, const QString &label,
                        const char *prop)
{
    return { QLatin1String(key), label, QMetaType::Int, false,
             QLatin1String(prop), {} };
}

TargetAttribute str(const char *key, const QString &label,
                    const char *prop)
{
    return { QLatin1String(key), label, QMetaType::QString, false,
             QLatin1String(prop), {} };
}

TargetAttribute enm(const char *key, const QString &label,
                    const char *prop, QVector<EnumChoice> choices)
{
    return { QLatin1String(key), label, QMetaType::Int, false,
             QLatin1String(prop), std::move(choices) };
}

// Enum choice tables — codes mirror the adapter Q_ENUMs
// (SWMMNodePropertyAdapter / SWMMLinkPropertyAdapter /
// SWMMRainGagePropertyAdapter), which mirror the engine.

QVector<EnumChoice> flapGateChoices()
{ return { {QStringLiteral("NO"), 0}, {QStringLiteral("YES"), 1} }; }

QVector<EnumChoice> outfallTypeChoices()
{
    return { {QStringLiteral("FREE"), 0}, {QStringLiteral("NORMAL"), 1},
             {QStringLiteral("FIXED"), 2}, {QStringLiteral("TIDAL"), 3},
             {QStringLiteral("TIMESERIES"), 4} };
}

QVector<EnumChoice> dividerTypeChoices()
{
    return { {QStringLiteral("CUTOFF"), 0}, {QStringLiteral("OVERFLOW"), 1},
             {QStringLiteral("TABULAR"), 2}, {QStringLiteral("WEIR"), 3} };
}

QVector<EnumChoice> rainTypeChoices()
{
    return { {QStringLiteral("INTENSITY"), 0}, {QStringLiteral("VOLUME"), 1},
             {QStringLiteral("CUMULATIVE"), 2} };
}

QVector<EnumChoice> rainSourceChoices()
{ return { {QStringLiteral("TIMESERIES"), 0}, {QStringLiteral("FILE"), 1} }; }

QVector<EnumChoice> rainUnitChoices()
{ return { {QStringLiteral("IN"), 0}, {QStringLiteral("MM"), 1} }; }

QVector<EnumChoice> pumpStateChoices()
{ return { {QStringLiteral("OFF"), 0}, {QStringLiteral("ON"), 1} }; }

QVector<EnumChoice> orificeTypeChoices()
{ return { {QStringLiteral("SIDE"), 0}, {QStringLiteral("BOTTOM"), 1} }; }

QVector<EnumChoice> weirTypeChoices()
{
    return { {QStringLiteral("TRANSVERSE"), 0}, {QStringLiteral("SIDEFLOW"), 1},
             {QStringLiteral("V-NOTCH"), 2}, {QStringLiteral("TRAPEZOIDAL"), 3},
             {QStringLiteral("ROADWAY"), 4} };
}

QVector<EnumChoice> outletRatingChoices()
{
    return { {QStringLiteral("FUNCTIONAL/HEAD"), 0},
             {QStringLiteral("FUNCTIONAL/DEPTH"), 1},
             {QStringLiteral("TABULAR/HEAD"), 2},
             {QStringLiteral("TABULAR/DEPTH"), 3} };
}

} // namespace

QVector<TargetKind> ImportTargetRegistry::allKinds()
{
    return { TargetKind::Junction, TargetKind::Outfall, TargetKind::Storage,
             TargetKind::Divider,  TargetKind::RainGage,
             TargetKind::Conduit,  TargetKind::Pump, TargetKind::Orifice,
             TargetKind::Weir,     TargetKind::Outlet };
}

QString ImportTargetRegistry::kindLabel(TargetKind k)
{
    switch (k) {
    case TargetKind::Junction: return tr("Junction");
    case TargetKind::Outfall:  return tr("Outfall");
    case TargetKind::Storage:  return tr("Storage Unit");
    case TargetKind::Divider:  return tr("Flow Divider");
    case TargetKind::RainGage: return tr("Rain Gage");
    case TargetKind::Conduit:  return tr("Conduit");
    case TargetKind::Pump:     return tr("Pump");
    case TargetKind::Orifice:  return tr("Orifice");
    case TargetKind::Weir:     return tr("Weir");
    case TargetKind::Outlet:   return tr("Outlet");
    }
    return {};
}

bool ImportTargetRegistry::isNodeKind(TargetKind k)
{
    return k == TargetKind::Junction || k == TargetKind::Outfall
        || k == TargetKind::Storage  || k == TargetKind::Divider;
}

bool ImportTargetRegistry::isLinkKind(TargetKind k)
{
    return k == TargetKind::Conduit || k == TargetKind::Pump
        || k == TargetKind::Orifice || k == TargetKind::Weir
        || k == TargetKind::Outlet;
}

bool ImportTargetRegistry::isPointKind(TargetKind k)
{
    return isNodeKind(k) || k == TargetKind::RainGage;
}

int ImportTargetRegistry::swmmNodeType(TargetKind k)
{
    switch (k) {
    case TargetKind::Junction: return 0;
    case TargetKind::Outfall:  return 1;
    case TargetKind::Storage:  return 2;
    case TargetKind::Divider:  return 3;
    default:                   return -1;
    }
}

int ImportTargetRegistry::swmmLinkType(TargetKind k)
{
    switch (k) {
    case TargetKind::Conduit: return 0;
    case TargetKind::Pump:    return 1;
    case TargetKind::Orifice: return 2;
    case TargetKind::Weir:    return 3;
    case TargetKind::Outlet:  return 4;
    default:                  return -1;
    }
}

QVector<TargetAttribute> ImportTargetRegistry::attributesFor(TargetKind kind)
{
    QVector<TargetAttribute> a;
    a << nameAttr();

    if (isLinkKind(kind)) {
        a << endpointAttr("fromNode", tr("From Node (column)"))
          << endpointAttr("toNode",   tr("To Node (column)"));
    }

    switch (kind) {
    case TargetKind::Junction:
        a << dbl("invertElev",     tr("Invert Elevation"), "invertElev")
          << dbl("maxDepth",       tr("Max Depth"),        "maxDepth")
          << dbl("initialDepth",   tr("Initial Depth"),    "initialDepth")
          << dbl("surchargeDepth", tr("Surcharge Depth"),  "surchargeDepth")
          << dbl("pondedArea",     tr("Ponded Area"),      "pondedArea")
          << str("tag",            tr("Tag"),              "tag");
        break;

    case TargetKind::Outfall:
        a << dbl("invertElev",  tr("Invert Elevation"), "invertElev")
          << enm("outfallType", tr("Outfall Type"), "outfallType",
                 outfallTypeChoices())
          // NOTE: writing outfallStage flips the outfall type to FIXED —
          // deliberate engine invariant (see swmmnodepropertyadapter.h,
          // Slice DA.4.3). The dialog tooltip carries this warning.
          << dbl("outfallStage", tr("Fixed Stage"), "outfallStage")
          << enm("flapGate",     tr("Flap Gate"), "outfallFlapGate",
                 flapGateChoices())
          << str("tag",          tr("Tag"), "tag");
        break;

    case TargetKind::Storage:
        a << dbl("invertElev",     tr("Invert Elevation"), "invertElev")
          << dbl("maxDepth",       tr("Max Depth"),        "maxDepth")
          << dbl("initialDepth",   tr("Initial Depth"),    "initialDepth")
          << dbl("surchargeDepth", tr("Surcharge Depth"),  "surchargeDepth")
          << dbl("seepRate",       tr("Seepage Rate"),     "seepRate")
          << str("tag",            tr("Tag"),              "tag");
        break;

    case TargetKind::Divider:
        a << dbl("invertElev",     tr("Invert Elevation"), "invertElev")
          << dbl("maxDepth",       tr("Max Depth"),        "maxDepth")
          << dbl("initialDepth",   tr("Initial Depth"),    "initialDepth")
          << dbl("surchargeDepth", tr("Surcharge Depth"),  "surchargeDepth")
          << dbl("pondedArea",     tr("Ponded Area"),      "pondedArea")
          << enm("dividerType",    tr("Divider Type"), "dividerType",
                 dividerTypeChoices())
          << str("tag",            tr("Tag"),              "tag");
        break;

    case TargetKind::RainGage:
        a << enm("rainType",    tr("Rain Format"), "rainType", rainTypeChoices())
          << dbl("snowFactor",  tr("Snow Catch Factor"),   "snowFactor")
          << dbl("scaleFactor", tr("Rainfall Scale Factor"), "scaleFactor")
          << enm("dataSource",  tr("Data Source"), "dataSource",
                 rainSourceChoices())
          << str("filePath",    tr("Rain File Path"), "filePath")
          << str("stationId",   tr("Station ID"),     "stationId")
          << enm("rainUnits",   tr("Rain Units"), "rainUnits",
                 rainUnitChoices());
        break;

    case TargetKind::Conduit:
        a << dbl("length",      tr("Length"),               "length")
          << dbl("roughness",   tr("Roughness"),            "roughness")
          << dbl("offsetUp",    tr("Inlet Offset"),         "offsetUp")
          << dbl("offsetDn",    tr("Outlet Offset"),        "offsetDn")
          << dbl("initialFlow", tr("Initial Flow"),         "initialFlow")
          << dbl("maxFlow",     tr("Max Flow"),             "maxFlow")
          << dbl("lossInlet",   tr("Entry Loss Coeff."),    "lossInlet")
          << dbl("lossOutlet",  tr("Exit Loss Coeff."),     "lossOutlet")
          << dbl("lossAvg",     tr("Avg. Loss Coeff."),     "lossAvg")
          << dbl("seepRate",    tr("Seepage Rate"),         "seepRate")
          << intAttr("barrels", tr("Barrels"),              "barrels")
          << enm("flapGate",    tr("Flap Gate"), "flapGate", flapGateChoices())
          << dbl("geom1",       tr("Geom1 (depth/diam.)"),  "geom1")
          << dbl("geom2",       tr("Geom2"),                "geom2")
          << dbl("geom3",       tr("Geom3"),                "geom3")
          << dbl("geom4",       tr("Geom4"),                "geom4")
          << str("tag",         tr("Tag"),                  "tag");
        break;

    case TargetKind::Pump:
        a << enm("initState",     tr("Initial Status"), "initState",
                 pumpStateChoices())
          << dbl("startupDepth",  tr("Startup Depth"), "startupDepth")
          << dbl("shutoffDepth",  tr("Shutoff Depth"), "shutoffDepth")
          << str("tag",           tr("Tag"),           "tag");
        break;

    case TargetKind::Orifice:
        a << enm("orificeType",    tr("Orifice Type"), "orificeType",
                 orificeTypeChoices())
          << dbl("offset",         tr("Inlet Offset"),        "offset")
          << dbl("dischargeCoeff", tr("Discharge Coeff."),    "dischargeCoeff")
          << enm("flapGate",       tr("Flap Gate"), "flapGate",
                 flapGateChoices())
          << dbl("openCloseRate",  tr("Open/Close Rate (1/s)"), "openCloseRate")
          << dbl("geom1",          tr("Geom1 (depth/diam.)"), "geom1")
          << dbl("geom2",          tr("Geom2 (width)"),       "geom2")
          << str("tag",            tr("Tag"),                 "tag");
        break;

    case TargetKind::Weir:
        a << enm("weirType",        tr("Weir Type"), "weirType",
                 weirTypeChoices())
          << dbl("offsetUp",        tr("Inlet Offset"),       "offsetUp")
          << dbl("offsetDn",        tr("Outlet Offset"),      "offsetDn")
          << dbl("crestHeight",     tr("Crest Height"),       "crestHeight")
          << dbl("dischargeCoeff",  tr("Discharge Coeff."),   "dischargeCoeff")
          << dbl("endContractions", tr("End Contractions"),   "endContractions")
          << enm("flapGate",        tr("Flap Gate"), "flapGate",
                 flapGateChoices())
          << dbl("geom1",           tr("Geom1 (height)"),     "geom1")
          << dbl("geom2",           tr("Geom2 (length)"),     "geom2")
          << dbl("geom3",           tr("Geom3 (side slope)"), "geom3")
          << str("tag",             tr("Tag"),                "tag");
        break;

    case TargetKind::Outlet:
        a << enm("ratingType",   tr("Rating Curve Type"), "ratingType",
                 outletRatingChoices())
          << dbl("offset",       tr("Inlet Offset"),      "offset")
          << dbl("coefficient",  tr("Coefficient"),       "coefficient")
          << dbl("expon",        tr("Exponent"),          "expon")
          << enm("flapGate",     tr("Flap Gate"), "flapGate",
                 flapGateChoices())
          << str("tag",          tr("Tag"),               "tag");
        break;
    }

    return a;
}

TargetAttribute ImportTargetRegistry::attribute(TargetKind kind,
                                                const QString &key)
{
    const QVector<TargetAttribute> all = attributesFor(kind);
    for (const TargetAttribute &t : all)
        if (t.key == key) return t;
    return {};
}

} // namespace openswmmvis::import
