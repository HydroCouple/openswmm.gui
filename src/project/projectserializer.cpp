/*!
 * \file   projectserializer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "project/projectserializer.h"

#include "connections/basemapconnection.h"
#include "layers/annotationlayer.h"
#include "layers/gisrasterlayer.h"
#include "layers/gisvectorlayer.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "layers/swmm2dresultslayer.h"
#include "layers/swmmmodellayer.h"
#include "layers/swmmresultslayer.h"
#include "layers/wmslayer.h"
#include "layers/wmtslayer.h"
#include "layers/xyztilelayer.h"
#include "map/mapcanvas.h"
#include "map/mapextent.h"
#include "map/spatialreferencesystem.h"
#include "project/swmmvisproject.h"
#include "swmmvisprojectwindow.h"

// Slice S6.1 — round-trip per-layer sublayer state.
#include "render/isublayerhost.h"

// Slice BI-MK.3 — per-layer IFeatureRenderer round-trip needs the concrete
// renderer types so the factory in `makeRendererFromJson` can dispatch on
// the "id" discriminator. MultiKindRenderer included so MultiKind ↔ Multi
// layer renderers (BI-MK.1.41 once it ships) round-trip too.
#include "render/ifeaturerenderer.h"
#include "render/labelconfig.h"
#include "render/multikindrenderer.h"
// Slice Z.13-attach — per-layer TemporalSpec persistence.
#include "render/temporalspec.h"
// Slice Z.14-attach — per-layer MaskSpec persistence.
#include "render/maskspec.h"
// Slice Z.15-attach — per-layer AuxiliaryStorageSpec persistence.
#include "render/auxiliarystoragespec.h"
// Slice Z.16-attach — per-layer external-table joins persistence.
#include "render/joinspec.h"
// Slice Z.12-attach — per-layer DiagramSpec persistence.
#include "render/diagramspec.h"
#include "render/renderers/categorizedrenderer.h"
#include "render/renderers/graduatedrenderer.h"
#include "render/renderers/rulebasedrenderer.h"
#include "render/renderers/singlesymbolrenderer.h"
#include "render/legendoverlaystyle.h"
// Slice B.7 — Rule-level metadata persistence.
#include "render/rule.h"
#include "render/rulelist.h"
#include "ui/widgets/legendoverlay.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace {

// Keys — centralised so the schema is easy to audit in one place
// and the same strings aren't repeated across serialize/parse paths.
// QString-typed so QJsonObject::operator[] / .value() accept them
// across Qt versions without overload ambiguity.
const QString kSchemaVersion = QStringLiteral("schemaVersion");
const QString kInpPath       = QStringLiteral("inpPath");
const QString kEngineVersion = QStringLiteral("engineVersion");
const QString kLayer         = QStringLiteral("layer");
const QString kCrsAuthority  = QStringLiteral("crsAuthority");
const QString kCrsCode       = QStringLiteral("crsCode");
const QString kCategoryOrder = QStringLiteral("categoryOrder");
const QString kObjectOrder   = QStringLiteral("objectOrder");
const QString kHiddenObjects = QStringLiteral("hiddenObjects");
const QString kCanvas        = QStringLiteral("canvas");
const QString kExtent        = QStringLiteral("extent");
const QString kResultLayers  = QStringLiteral("resultLayers");
// Slice S6.1 (RENDERING_OUTPUT_SUBLAYERS_PLAN.md) — companion to kResultLayers.
// Object keyed by the same relative path as each entry in kResultLayers;
// value is `ISublayerHost::saveSublayersToJson(*host)`. Purely additive —
// older readers ignore this key; legacy `.oswp` files without it pick up
// sublayer defaults on load.
const QString kResultLayerSublayers = QStringLiteral("resultLayerSublayers");

// Slice X.14 — companion map for SWMMResultsLayer per-kind renderers.
// Keyed by the same relative results-file path used in kResultLayers;
// value is a map of `kindKey` → renderer JSON for any kind that has a
// non-default renderer installed.  Schema additive; older readers
// ignore it and the layer's compiled defaults are used.
const QString kResultLayerKindRenderers = QStringLiteral("resultLayerKindRenderers");

// Companion map for per-run report files. Keyed by the same relative
// results-file path used in kResultLayers; value is the run's `.rpt`
// path relative to the .oswp. Schema additive — older readers ignore
// it; loads without it leave the layer's report path empty.
const QString kResultLayerReports = QStringLiteral("resultLayerReports");

// Schema v4 — multi-instance project (Slice AA-3.2)
const QString kSessions      = QStringLiteral("sessions");
const QString kSessionId     = QStringLiteral("id");
const QString kSessionTitle  = QStringLiteral("title");
const QString kNotesHtml     = QStringLiteral("notesHtml");

// Terrain editing state (schema v5+)
const QString kTerrain            = QStringLiteral("terrain");
const QString kTerrainLayer       = QStringLiteral("activeLayerPath");
const QString kTerrainNodeOffset  = QStringLiteral("nodeOffsetM");
const QString kTerrainLinkOffset  = QStringLiteral("linkOffsetM");
const QString kTerrainVertUnit    = QStringLiteral("verticalUnit");

// 2D mesh layer display state — Slice AZ.3.7 (schema v5+, additive).
// Mesh layers are auto-loaded by openSingleINP from the [2D_MESH_FILE]
// referenced in the .inp; this block carries only display state and is
// matched back to the live layer by resolved sourcePath.
const QString kMeshLayers           = QStringLiteral("meshLayers");
const QString kMeshSourcePath       = QStringLiteral("sourcePath");
const QString kMeshActive           = QStringLiteral("active");
const QString kMeshShowNodes        = QStringLiteral("showMeshNodes");
const QString kMeshShowEdges        = QStringLiteral("showEdges");
const QString kMeshHillshade        = QStringLiteral("hillshade");
const QString kMeshHsAzimuth        = QStringLiteral("azimuth");
const QString kMeshHsAltitude       = QStringLiteral("altitude");
const QString kMeshHsZExag          = QStringLiteral("zExag");
const QString kMeshHsMinLit         = QStringLiteral("minLit");
const QString kMeshContours         = QStringLiteral("contours");
const QString kMeshContShow         = QStringLiteral("show");
const QString kMeshContIntervals    = QStringLiteral("intervals");
const QString kMeshContColor        = QStringLiteral("color");
const QString kMeshContWidth        = QStringLiteral("width");
const QString kMeshContFilled       = QStringLiteral("filled");      // BJ.2-filled
const QString kMeshContFilledAlpha  = QStringLiteral("filledOpacity");// BJ.2-filled

// Per-layer IFeatureRenderer JSON (BI-MK.3, schema v5+, additive).
// Holds whatever rendererId the layer's renderer() returns — typically
// "single", "graduated", "categorized", "rule" or "multikind". Empty /
// missing → keep the layer's compiled default renderer.
const QString kRenderer             = QStringLiteral("renderer");

// 2D results layers (schema additive). Each entry reopens a previous
// run's HDF5 2D output on project load with its saved styling. The
// layer itself is created by the async 2D auto-load in SWMMVis
// (maybeLoad2DResults) — applySession only stashes these entries on the
// project window; the auto-load consumes them once the .h5 is open.
const QString kResults2D          = QStringLiteral("results2DLayers");
const QString kResults2DPath      = QStringLiteral("path");
const QString kResults2DName      = QStringLiteral("name");
const QString kResults2DVisible   = QStringLiteral("visible");
const QString kResults2DOpacity   = QStringLiteral("opacity");
const QString kResults2DDryDepth  = QStringLiteral("dryDepth");
const QString kResults2DMaxDepth  = QStringLiteral("maxDepth");
const QString kResults2DMaxVel    = QStringLiteral("maxVelocity");
const QString kResults2DSublayers = QStringLiteral("sublayers");

// Slice X.14 — per-kind IFeatureRenderer JSON for SWMMModelLayer /
// SWMMResultsLayer.  Mapped object keyed by the kind name returned by
// `SWMMModelLayer::kindKey` (e.g. "junctions", "conduits").  Each value
// is the renderer's `toJson()` payload.  Schema purely additive —
// older readers ignore this key, and a saved project without it falls
// back to the layer's compiled default renderers.
const QString kKindRenderers        = QStringLiteral("kindRenderers");
// Slice B.7 — Rule-level metadata (name/filter/blend/rebinPerFrame/
// symbolLevelsEnabled) that the existing kindRenderers block doesn't
// cover. Keyed by the Rule's name in the layer's RuleList.
const QString kRuleMetadata         = QStringLiteral("ruleMetadata");

// Slice X.18 — full label config JSON.  Schema additive; older readers
// ignore it and the legacy `showLabels` bool elsewhere keeps working.
const QString kLabelConfig          = QStringLiteral("labelConfig");

// Slice Z.13-attach — per-layer TemporalSpec. Default-constructed
// TemporalSpec (enabled=false) elides to keep JSON minimal for projects
// that never touched the Temporal tab.
const QString kTemporalSpec         = QStringLiteral("temporal");

// Slice Z.14-attach — per-layer MaskSpec. Same elide-on-default behaviour.
const QString kMaskSpec             = QStringLiteral("mask");

// Slice Z.15-attach — per-layer AuxiliaryStorageSpec.
const QString kAuxStorageSpec       = QStringLiteral("auxStorage");

// Slice Z.16-attach — per-layer external-table joins (array).
const QString kJoins                = QStringLiteral("joins");

// Slice Z.12-attach — per-layer embedded chart diagram.
const QString kDiagramSpec          = QStringLiteral("diagram");

// Slice BB Phase 8.6.16 — on-canvas legend overlay chrome (font / frame /
// background / anchor / opacity). Per-session; missing key ⇒ defaults.
const QString kLegendOverlay        = QStringLiteral("legendOverlay");

// Text annotation layer — array of styled text items placed on the map.
// Schema additive; missing key ⇒ no annotations were saved.
const QString kAnnotations          = QStringLiteral("annotations");

// Factory: construct a concrete IFeatureRenderer from a JSON object whose
// "id" field discriminates the renderer kind. Mirrors the local factory
// in multikindrenderer.cpp; kept duplicate here so projectserializer
// doesn't need a header-promoted version. Returns nullptr on missing or
// unknown id so callers can keep the layer's compiled default.
std::unique_ptr<OpenSWMM::Render::IFeatureRenderer>
makeRendererFromJson(const QJsonObject &j)
{
    using namespace OpenSWMM::Render;
    const QString id = j.value(QStringLiteral("id")).toString();
    std::unique_ptr<IFeatureRenderer> r;
    if (id == QLatin1String("single"))
        r = std::make_unique<SingleSymbolRenderer>();
    else if (id == QLatin1String("graduated"))
        r = std::make_unique<GraduatedRenderer>();
    else if (id == QLatin1String("categorized"))
        r = std::make_unique<CategorizedRenderer>();
    else if (id == QLatin1String("rule"))
        r = std::make_unique<RuleBasedRenderer>();
    else if (id == QLatin1String("multikind"))
        r = std::make_unique<MultiKindRenderer>();
    if (r) r->fromJson(j);
    return r;
}

// Basemap keys (schema v3+)
const QString kBasemaps      = QStringLiteral("basemaps");
const QString kBmType        = QStringLiteral("type");
const QString kBmName        = QStringLiteral("name");
const QString kBmUrl         = QStringLiteral("url");
const QString kBmHeaders     = QStringLiteral("headers");
const QString kBmTilePixRatio = QStringLiteral("tilePixelRatio");
const QString kBmAxisOrder   = QStringLiteral("axisOrder");
// WMS/WMTS
const QString kBmLayer       = QStringLiteral("layerName");
const QString kBmStyle       = QStringLiteral("style");
const QString kBmFormat      = QStringLiteral("imageFormat");
const QString kBmCrs         = QStringLiteral("crs");
const QString kBmTms         = QStringLiteral("tileMatrixSet");
const QString kBmDpiMode     = QStringLiteral("dpiMode");

// GIS data-layer keys (schema v4+) — loaded raster (GDAL) and vector (OGR)
// layers persisted by source path so they reopen on project load.
const QString kGisLayers     = QStringLiteral("gisLayers");
const QString kGisType       = QStringLiteral("type");        // "raster" | "vector"
const QString kGisPath       = QStringLiteral("path");        // relative to the .oswp
const QString kGisName       = QStringLiteral("name");
const QString kGisVisible    = QStringLiteral("visible");
const QString kGisOpacity    = QStringLiteral("opacity");
const QString kGisLayerName  = QStringLiteral("layerName");   // vector sublayer (OGR layer)

QJsonArray toJsonInts(const QVector<int> &v)
{
    QJsonArray a;
    for (int x : v) a.append(x);
    return a;
}

QVector<int> fromJsonInts(const QJsonArray &a)
{
    QVector<int> v;
    v.reserve(a.size());
    for (const QJsonValue &x : a) v.append(x.toInt());
    return v;
}

} // anonymous

// ---------------------------------------------------------------------------

// (toRelativePath / resolveStoredPath / sidecarPathFor now inline in
// the header so unit tests can exercise them without linking the full
// GUI graph. Slice AA-3.2 + Slice RB.5.)

// ---------------------------------------------------------------------------
// Session helpers — one entry per SWMM model instance.  Holds the
// per-instance state that travels with that .inp:  inpPath (relative),
// engineVersion, layer CRS, category/object order, hidden objects.
// ---------------------------------------------------------------------------

QJsonObject ProjectSerializer::serializeSession(SWMMVisProjectWindow *pw,
                                                 const QString &oswpFile)
{
    QJsonObject obj;
    if (!pw) return obj;

    auto *layer = pw->modelLayer();
    if (!layer) return obj;

    obj[kInpPath]       = toRelativePath(layer->modelFilePath(), oswpFile);
    obj[kEngineVersion] = pw->engineVersion();

    const QString notesHtml = pw->notesHtml();
    if (!notesHtml.isEmpty())
        obj[kNotesHtml] = notesHtml;

    QJsonObject layerObj;

    if (auto *srs = layer->srs()) {
        const QString auth = srs->authName();
        const int     code = srs->code();
        if (!auth.isEmpty() && code > 0) {
            layerObj[kCrsAuthority] = auth;
            layerObj[kCrsCode]      = code;
        }
    }

    // Category order (Slice T.2). Only emitted when non-default so
    // first-time saves from a default-ordered project stay tidy.
    const auto order = layer->categoryOrder();
    bool catOrderIsDefault = true;
    for (int i = 0; i < order.size(); ++i) {
        if (int(order[i]) != i) { catOrderIsDefault = false; break; }
    }
    if (!catOrderIsDefault) {
        QJsonArray a;
        for (auto c : order) a.append(int(c));
        layerObj[kCategoryOrder] = a;
    }

    // Per-category object order overrides (Slice T.3).
    QJsonObject objOrderObj;
    for (int i = 0; i < int(SWMMModelLayer::NumCategories); ++i) {
        const auto cat = static_cast<SWMMModelLayer::Category>(i);
        const QVector<int> ov = layer->objectOrder(cat);
        if (!ov.isEmpty())
            objOrderObj[QString::number(i)] = toJsonInts(ov);
    }
    if (!objOrderObj.isEmpty())
        layerObj[kObjectOrder] = objOrderObj;

    // Hidden object set (Object Browser leaf checkboxes).
    const QSet<QString> hidden = layer->hiddenObjects();
    if (!hidden.isEmpty()) {
        QJsonArray a;
        for (const QString &n : hidden) a.append(n);
        layerObj[kHiddenObjects] = a;
    }

    // Per-layer IFeatureRenderer JSON — Slice BI-MK.3.
    // Carries whatever renderer the layer currently holds (typically
    // SingleSymbolRenderer or MultiKindRenderer once BI-MK.1.41 lands).
    // Schema is additive; older readers ignore the "renderer" key.
    if (const auto *r = layer->renderer())
        layerObj[kRenderer] = r->toJson();

    // Slice X.14 — per-kind renderers for the model layer.  layer->renderer()
    // only sees the layer-level slot; the 11 per-Category renderers live
    // in m_kindRenderers and otherwise wouldn't round-trip.  Only emit a
    // kind entry when it differs from the default SingleSymbolRenderer (any
    // graduated / categorized / rule-based / customised single is worth
    // saving).  Older readers ignore this key.
    {
        QJsonObject kindObj;
        for (int i = 0; i < int(SWMMModelLayer::NumCategories); ++i) {
            const auto cat = static_cast<SWMMModelLayer::Category>(i);
            const auto *kr = layer->kindRenderer(cat);
            if (!kr) continue;
            // Skip vanilla SingleSymbolRenderer slots that carry no edits —
            // keeps the JSON compact and means the .oswp diff stays minimal
            // for users who never touch the symbology.
            if (kr->rendererId() == QStringLiteral("single")) {
                const QJsonObject j = kr->toJson();
                const QJsonObject sym = j.value(QStringLiteral("symbol")).toObject();
                if (sym.isEmpty()) continue;
            }
            kindObj[SWMMModelLayer::kindKey(cat)] = kr->toJson();
        }
        if (!kindObj.isEmpty())
            layerObj[kKindRenderers] = kindObj;
    }

    // Slice B.7 — Rule-level metadata for the model layer's RuleList.
    // Keyed by Rule name (= kindKey). Only emit fields that differ from
    // their compile-time defaults so .oswp diffs stay minimal for users
    // who never touch Rule metadata.
    if (auto *rl = layer->ruleList()) {
        QJsonObject ruleObj;
        for (int i = 0; i < rl->count(); ++i) {
            const OpenSWMM::Render::Rule *r = rl->at(i);
            if (!r) continue;
            QJsonObject meta;
            if (!r->isVisible())               meta[QStringLiteral("isVisible")] = false;
            if (!r->filterExpression().isEmpty())
                meta[QStringLiteral("filterExpression")] = r->filterExpression();
            if (r->minScale() != 0.0)          meta[QStringLiteral("minScale")] = r->minScale();
            if (r->maxScale() != 0.0)          meta[QStringLiteral("maxScale")] = r->maxScale();
            if (r->blendMode() != QStringLiteral("Normal"))
                meta[QStringLiteral("blendMode")] = r->blendMode();
            if (r->rebinPerFrame())            meta[QStringLiteral("rebinPerFrame")] = true;
            if (r->symbolLevelsEnabled())      meta[QStringLiteral("symbolLevelsEnabled")] = true;
            if (!meta.isEmpty())
                ruleObj[r->name()] = meta;
        }
        if (!ruleObj.isEmpty())
            layerObj[kRuleMetadata] = ruleObj;
    }

    // Slice X.18 — model-layer label config.  Default-constructed
    // (enabled=false, default font, no halo) elides to keep JSON
    // compact for users who never touched the Labels tab.
    {
        const auto &lc = layer->labelConfig();
        const OpenSWMM::Render::LabelConfig defaultLc;
        if (lc != defaultLc)
            layerObj[kLabelConfig] = lc.toJson();
    }

    // Slice Z.13-attach — per-layer TemporalSpec. Elide when default.
    {
        const auto &ts = layer->temporalSpec();
        const OpenSWMM::Render::TemporalSpec defaultTs;
        if (ts != defaultTs)
            layerObj[kTemporalSpec] = ts.toJson();
    }

    // Slice Z.14-attach — per-layer MaskSpec. Same elide-on-default rule.
    {
        const auto &ms = layer->maskSpec();
        const OpenSWMM::Render::MaskSpec defaultMs;
        if (ms != defaultMs)
            layerObj[kMaskSpec] = ms.toJson();
    }

    // Slice Z.15-attach — per-layer AuxiliaryStorageSpec.
    {
        const auto &as = layer->auxStorageSpec();
        const OpenSWMM::Render::AuxiliaryStorageSpec defaultAs;
        if (as != defaultAs)
            layerObj[kAuxStorageSpec] = as.toJson();
    }

    // Slice Z.16-attach — per-layer joins list. Empty list elides.
    {
        const auto &js = layer->joins();
        if (!js.isEmpty()) {
            QJsonArray arr;
            for (const auto &j : js) arr.append(j.toJson());
            layerObj[kJoins] = arr;
        }
    }

    // Slice Z.12-attach — per-layer DiagramSpec. Elide when default.
    {
        const auto &ds = layer->diagramSpec();
        const OpenSWMM::Render::DiagramSpec defaultDs;
        if (ds != defaultDs)
            layerObj[kDiagramSpec] = ds.toJson();
    }

    obj[kLayer] = layerObj;

    // Result layers — paths stored relative to the .oswp so the project
    // is relocatable.
    if (auto *canvas = pw->canvas()) {
        QJsonArray resultArr;
        // Slice S6.1 — companion map (path → sublayer state) populated in
        // the same loop. Schema additive; only emitted when at least one
        // result layer is a sublayer host with state to save.
        QJsonObject sublayerMap;
        // Slice X.14 — companion map (path → kindKey → renderer JSON).
        QJsonObject kindRendererMap;
        // Companion map (path → relative .rpt path) for the Report Viewer.
        QJsonObject reportMap;
        for (OpenSWMMVisLayer *l : canvas->layers()) {
            if (auto *rl = qobject_cast<SWMMResultsLayer *>(l)) {
                const QString rel = toRelativePath(rl->resultsFilePath(), oswpFile);
                if (!rel.isEmpty()) {
                    resultArr.append(rel);
                    if (!rl->reportFilePath().isEmpty())
                        reportMap.insert(rel,
                            toRelativePath(rl->reportFilePath(), oswpFile));
                    // S2.4 adopted ISublayerHost on SWMMResultsLayer. The
                    // dynamic_cast tolerates layers that haven't adopted
                    // yet (or future variants); they simply skip the
                    // sublayer map entry.
                    if (auto *host = dynamic_cast<OpenSWMM::Render::ISublayerHost *>(rl))
                        sublayerMap.insert(rel,
                            OpenSWMM::Render::ISublayerHost::saveSublayersToJson(*host));

                    // Per-kind renderers — every non-default slot survives
                    // a save/reload.  Default detection mirrors the model
                    // layer's elision rule: drop vanilla SingleSymbolRenderer
                    // slots with empty symbol payload so the .oswp stays
                    // minimal for users who never touched the styling.
                    // Gap A2.1 — openResults() now installs Graduated
                    // defaults eagerly on every result-bearing kind;
                    // kindRendererIsDefault() elides those untouched slots.
                    QJsonObject kindObj;
                    for (int i = 0; i < int(SWMMModelLayer::NumCategories); ++i) {
                        const auto cat = static_cast<SWMMModelLayer::Category>(i);
                        const auto *kr = rl->kindRenderer(cat);
                        if (!kr) continue;
                        if (rl->kindRendererIsDefault(cat)) continue;
                        const QJsonObject j = kr->toJson();
                        if (kr->rendererId() == QStringLiteral("single")) {
                            const QJsonObject sym = j.value(QStringLiteral("symbol")).toObject();
                            if (sym.isEmpty()) continue;
                        }
                        kindObj[SWMMModelLayer::kindKey(cat)] = j;
                    }
                    if (!kindObj.isEmpty())
                        kindRendererMap.insert(rel, kindObj);
                }
            }
        }
        if (!resultArr.isEmpty())
            obj[kResultLayers] = resultArr;
        if (!sublayerMap.isEmpty())
            obj[kResultLayerSublayers] = sublayerMap;
        if (!kindRendererMap.isEmpty())
            obj[kResultLayerKindRenderers] = kindRendererMap;
        if (!reportMap.isEmpty())
            obj[kResultLayerReports] = reportMap;
    }

    // Terrain editing state — active raster path + invert offsets.
    {
        QJsonObject terrainObj;
        const QString layerPath = pw->activeTerrainLayerPath();
        if (!layerPath.isEmpty())
            terrainObj[kTerrainLayer] = toRelativePath(layerPath, oswpFile);
        const double nodeOff = pw->terrainNodeOffset();
        const double linkOff = pw->terrainLinkOffset();
        if (nodeOff != 0.0) terrainObj[kTerrainNodeOffset] = nodeOff;
        if (linkOff != 0.0) terrainObj[kTerrainLinkOffset] = linkOff;
        const QString vertUnit = pw->terrainVerticalUnit();
        if (!vertUnit.isEmpty()) terrainObj[kTerrainVertUnit] = vertUnit;
        if (!terrainObj.isEmpty())
            obj[kTerrain] = terrainObj;
    }

    // 2D mesh-layer display state — Slice AZ.3.7.
    // Walk canvas layers; each SWMM2DMeshLayer becomes one entry keyed by
    // its sourcePath (relative to the .oswp). On restore we match the
    // sourcePath against whatever mesh layers openSingleINP already
    // auto-loaded from the .inp's [2D_MESH_FILE] reference.
    if (auto *canvas = pw->canvas()) {
        QJsonArray meshArr;
        for (OpenSWMMVisLayer *l : canvas->layers()) {
            auto *ml = qobject_cast<SWMM2DMeshLayer *>(l);
            if (!ml) continue;
            QJsonObject m;
            const QString rel = toRelativePath(ml->sourcePath(), oswpFile);
            if (!rel.isEmpty())
                m[kMeshSourcePath] = rel;
            m[kMeshActive]      = ml->isActiveMesh();
            m[kMeshShowNodes]   = ml->showMeshNodes();
            m[kMeshShowEdges]   = ml->showEdges();

            QJsonObject hs;
            hs[kMeshHsAzimuth]  = ml->hillshadeAzimuth();
            hs[kMeshHsAltitude] = ml->hillshadeAltitude();
            hs[kMeshHsZExag]    = ml->hillshadeZExag();
            hs[kMeshHsMinLit]   = ml->hillshadeMinLit();
            m[kMeshHillshade]   = hs;

            QJsonObject c;
            c[kMeshContShow]        = ml->showContours();
            c[kMeshContIntervals]   = ml->contourIntervalCount();
            // QColor::name(HexArgb) preserves alpha; setNamedColor parses it.
            c[kMeshContColor]       = ml->contourColor().name(QColor::HexArgb);
            c[kMeshContWidth]       = ml->contourLineWidth();
            c[kMeshContFilled]      = ml->filledContours();
            c[kMeshContFilledAlpha] = ml->filledContoursOpacity();
            m[kMeshContours]        = c;

            meshArr.append(m);
        }
        if (!meshArr.isEmpty())
            obj[kMeshLayers] = meshArr;
    }

    // 2D results layers — persist the backing .h5 (relative), the layer's
    // display settings, and the full sublayer styling so a previous run's
    // 2D results reopen on project load looking exactly as they were left.
    if (auto *canvas = pw->canvas()) {
        QJsonArray r2dArr;
        for (OpenSWMMVisLayer *l : canvas->layers()) {
            auto *rl = qobject_cast<SWMM2DResultsLayer *>(l);
            if (!rl) continue;
            // Both creation paths (post-run swap and model-open auto-load)
            // tag the layer with its backing .h5; a layer without the tag
            // has nothing on disk to reopen.
            const QString h5Abs =
                rl->property("snoopy_h5_path").toString();
            if (h5Abs.isEmpty() || !QFileInfo::exists(h5Abs)) continue;
            const QString rel = toRelativePath(h5Abs, oswpFile);
            if (rel.isEmpty()) continue;

            QJsonObject r;
            r[kResults2DPath]     = rel;
            r[kResults2DName]     = rl->name();
            r[kResults2DVisible]  = rl->isVisible();
            r[kResults2DOpacity]  = rl->opacity();
            r[kResults2DDryDepth] = rl->dryDepth();
            r[kResults2DMaxDepth] = rl->maxDepth();
            r[kResults2DMaxVel]   = rl->maxVelocity();
            if (auto *host = dynamic_cast<OpenSWMM::Render::ISublayerHost *>(rl))
                r[kResults2DSublayers] =
                    OpenSWMM::Render::ISublayerHost::saveSublayersToJson(*host);
            r2dArr.append(r);
        }
        if (!r2dArr.isEmpty())
            obj[kResults2D] = r2dArr;
    }

    // Slice BB Phase 8.6.16 — on-canvas legend overlay chrome. Persisted
    // per-session so each project window restores its own legend look.
    // Read off the canvas's currently-installed overlay (lazily created
    // when the user toggles actionShowLegend); when no overlay exists
    // yet the session's defaults flow back into one when actionShowLegend
    // first fires.
    if (auto *canvas = pw->canvas()) {
        if (auto *overlay = canvas->findChild<openswmmvis::ui::LegendOverlay *>(
                QString(), Qt::FindDirectChildrenOnly)) {
            if (auto *style = overlay->style())
                obj[kLegendOverlay] = style->toJson();
        }
    }

    // Text annotations — flatten every annotation layer into a single array.
    // The MVP creates one lazy layer per project, so there's typically zero
    // or one entry to serialize; the loop tolerates future multi-layer
    // configurations without schema changes.
    if (auto *canvas = pw->canvas()) {
        QJsonArray annArr;
        for (OpenSWMMVisLayer *l : canvas->layers()) {
            auto *al = qobject_cast<OpenSWMMVisAnnotationLayer *>(l);
            if (!al) continue;
            const QJsonArray a = al->toJson();
            for (const QJsonValue &v : a) annArr.append(v);
        }
        if (!annArr.isEmpty())
            obj[kAnnotations] = annArr;
    }

    return obj;
}

bool ProjectSerializer::applySession(const QJsonObject &sessionObj,
                                      SWMMVisProjectWindow *pw,
                                      const QString &oswpFile,
                                      QStringList *warningsOut)
{
    if (!pw) return false;

    if (sessionObj.contains(kEngineVersion))
        pw->setEngineVersion(sessionObj.value(kEngineVersion).toString("6.0.0"));

    if (sessionObj.contains(kNotesHtml))
        pw->setNotesHtml(sessionObj.value(kNotesHtml).toString());

    auto *layer = pw->modelLayer();
    if (!layer) return true;

    const QJsonObject layerObj = sessionObj.value(kLayer).toObject();

    // Layer CRS. Applied before the canvas CRS so the on-the-fly
    // reprojection path in MapCanvas picks up the right transform.
    if (layerObj.contains(kCrsAuthority) && layerObj.contains(kCrsCode)) {
        const QString auth = layerObj.value(kCrsAuthority).toString();
        const int     code = layerObj.value(kCrsCode).toInt();
        if (!auth.isEmpty() && code > 0) {
            if (auto *srs = SpatialReferenceSystem::fromAuthCode(auth, code, layer))
                layer->setSRS(srs, true);
        }
    }

    if (layerObj.contains(kCategoryOrder)) {
        QVector<SWMMModelLayer::Category> order;
        for (const QJsonValue &v : layerObj.value(kCategoryOrder).toArray()) {
            const int c = v.toInt(-1);
            if (c >= 0 && c < int(SWMMModelLayer::NumCategories))
                order.append(static_cast<SWMMModelLayer::Category>(c));
        }
        if (order.size() == int(SWMMModelLayer::NumCategories))
            layer->setCategoryOrder(order);
    }

    if (layerObj.contains(kObjectOrder)) {
        const QJsonObject obj = layerObj.value(kObjectOrder).toObject();
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
            bool ok = false;
            const int c = it.key().toInt(&ok);
            if (!ok || c < 0 || c >= int(SWMMModelLayer::NumCategories))
                continue;
            const auto cat = static_cast<SWMMModelLayer::Category>(c);
            const QVector<int> v = fromJsonInts(it.value().toArray());
            if (!v.isEmpty())
                layer->setObjectOrder(cat, v);
        }
    }

    if (layerObj.contains(kHiddenObjects)) {
        QStringList names;
        for (const QJsonValue &v : layerObj.value(kHiddenObjects).toArray())
            names << v.toString();
        if (!names.isEmpty())
            layer->setObjectsVisible(names, /*visible=*/false);
    }

    // Per-layer IFeatureRenderer JSON — Slice BI-MK.3.
    // Factory the renderer type from the "id" discriminator and hand it
    // to layer->setRenderer(). Missing "renderer" key (legacy schema) →
    // keep the layer's compiled default. Unknown id → factory returns
    // null → setRenderer is silently no-op'd (its own contract).
    if (layerObj.contains(kRenderer)) {
        if (auto r = makeRendererFromJson(layerObj.value(kRenderer).toObject()))
            layer->setRenderer(std::move(r));
    }

    // Slice X.14 — per-kind renderers for the model layer.  Walk the
    // saved object keyed by kindKey and install each via setKindRenderer;
    // missing keys keep the compiled default for that slot.  Unknown
    // discriminator IDs are dropped (factory returns null).
    if (layerObj.contains(kKindRenderers)) {
        const QJsonObject kindObj = layerObj.value(kKindRenderers).toObject();
        for (int i = 0; i < int(SWMMModelLayer::NumCategories); ++i) {
            const auto cat = static_cast<SWMMModelLayer::Category>(i);
            const QString key = SWMMModelLayer::kindKey(cat);
            if (!kindObj.contains(key)) continue;
            if (auto r = makeRendererFromJson(kindObj.value(key).toObject()))
                layer->setKindRenderer(cat, std::move(r));
        }
    }

    // Slice B.7 — Rule-level metadata. Looked up by Rule name (=
    // kindKey). Missing entries leave the Rule at its lazy-init
    // defaults. Setters fire ruleChanged but that's harmless on load.
    if (layerObj.contains(kRuleMetadata)) {
        const QJsonObject ruleObj = layerObj.value(kRuleMetadata).toObject();
        if (auto *rl = layer->ruleList()) {
            for (int i = 0; i < rl->count(); ++i) {
                OpenSWMM::Render::Rule *r = rl->at(i);
                if (!r) continue;
                const QJsonObject meta = ruleObj.value(r->name()).toObject();
                if (meta.isEmpty()) continue;
                if (meta.contains(QStringLiteral("isVisible")))
                    r->setVisible(meta.value(QStringLiteral("isVisible")).toBool(true));
                if (meta.contains(QStringLiteral("filterExpression")))
                    r->setFilterExpression(
                        meta.value(QStringLiteral("filterExpression")).toString());
                if (meta.contains(QStringLiteral("minScale")))
                    r->setMinScale(meta.value(QStringLiteral("minScale")).toDouble(0.0));
                if (meta.contains(QStringLiteral("maxScale")))
                    r->setMaxScale(meta.value(QStringLiteral("maxScale")).toDouble(0.0));
                if (meta.contains(QStringLiteral("blendMode")))
                    r->setBlendMode(meta.value(QStringLiteral("blendMode")).toString());
                if (meta.contains(QStringLiteral("rebinPerFrame")))
                    r->setRebinPerFrame(meta.value(QStringLiteral("rebinPerFrame")).toBool(false));
                if (meta.contains(QStringLiteral("symbolLevelsEnabled")))
                    r->setSymbolLevelsEnabled(
                        meta.value(QStringLiteral("symbolLevelsEnabled")).toBool(false));
            }
        }
    }

    // Slice X.18 — restore label config if persisted.  Missing key falls
    // back to the layer's default-constructed config (legacy showLabels
    // path keeps working through SWMMModelLayer::setShowLabels).
    if (layerObj.contains(kLabelConfig)) {
        OpenSWMM::Render::LabelConfig lc;
        lc.fromJson(layerObj.value(kLabelConfig).toObject());
        layer->setLabelConfig(lc);
    }

    // Slice Z.13-attach — restore TemporalSpec if persisted. Missing key
    // leaves the layer's default-constructed spec (enabled=false), so
    // projects authored before this slice keep their existing animation
    // behavior driven by the legacy toolbar.
    if (layerObj.contains(kTemporalSpec)) {
        layer->setTemporalSpec(OpenSWMM::Render::TemporalSpec::fromJson(
            layerObj.value(kTemporalSpec).toObject()));
    }

    // Slice Z.14-attach — restore MaskSpec if persisted. Missing key
    // leaves the layer unmasked.
    if (layerObj.contains(kMaskSpec)) {
        layer->setMaskSpec(OpenSWMM::Render::MaskSpec::fromJson(
            layerObj.value(kMaskSpec).toObject()));
    }

    // Slice Z.15-attach — restore AuxiliaryStorageSpec if persisted.
    if (layerObj.contains(kAuxStorageSpec)) {
        layer->setAuxStorageSpec(
            OpenSWMM::Render::AuxiliaryStorageSpec::fromJson(
                layerObj.value(kAuxStorageSpec).toObject()));
    }

    // Slice Z.16-attach — restore joins list if persisted. We tolerate
    // malformed entries individually (skip rather than abort the layer
    // load) so a partially-corrupted project still opens.
    if (layerObj.contains(kJoins)) {
        const QJsonArray arr = layerObj.value(kJoins).toArray();
        QVector<OpenSWMM::Render::JoinSpec> js;
        js.reserve(arr.size());
        for (const auto &v : arr) {
            if (!v.isObject()) continue;
            js.append(OpenSWMM::Render::JoinSpec::fromJson(v.toObject()));
        }
        if (!js.isEmpty()) layer->setJoins(js);
    }

    // Slice Z.12-attach — restore DiagramSpec if persisted.
    if (layerObj.contains(kDiagramSpec)) {
        layer->setDiagramSpec(OpenSWMM::Render::DiagramSpec::fromJson(
            layerObj.value(kDiagramSpec).toObject()));
    }

    // Result layers — reopen each persisted output file.
    if (sessionObj.contains(kResultLayers) && pw->canvas() && pw->modelLayer()) {
        // Slice S6.1 — sublayer state map (path → host JSON). Looked up
        // by rel path right after each layer is constructed so the JSON
        // helpers can re-apply visibility / opacity / per-sublayer style.
        const QJsonObject sublayerMap =
            sessionObj.value(kResultLayerSublayers).toObject();
        const QJsonObject kindRendererMap =
            sessionObj.value(kResultLayerKindRenderers).toObject();
        const QJsonObject reportMap =
            sessionObj.value(kResultLayerReports).toObject();
        for (const QJsonValue &v : sessionObj.value(kResultLayers).toArray()) {
            const QString relPath = v.toString();
            if (relPath.isEmpty()) continue;
            const QString absPath = resolveStoredPath(relPath, oswpFile);
            if (!QFile::exists(absPath)) {
                if (warningsOut)
                    *warningsOut << QObject::tr(
                        "Results file not found — layer skipped: %1").arg(absPath);
                continue;
            }
            auto *rl = new SWMMResultsLayer(absPath, pw->modelLayer());
            rl->setName(QFileInfo(absPath).fileName());

            // Restore the run's .rpt association for the Report Viewer.
            const QString relRpt = reportMap.value(relPath).toString();
            if (!relRpt.isEmpty())
                rl->setReportFilePath(resolveStoredPath(relRpt, oswpFile));
            pw->canvas()->addLayer(rl, /*pushUndo=*/false);
            QList<QString> w, e;
            rl->openResults(w, e);

            // Slice S6.1 — restore sublayer state, if persisted.
            // No-op for projects saved before S6.1 (the map is empty)
            // and for layers that don't have a matching entry.
            if (auto *host = dynamic_cast<OpenSWMM::Render::ISublayerHost *>(rl)) {
                const QJsonObject entry = sublayerMap.value(relPath).toObject();
                if (!entry.isEmpty())
                    OpenSWMM::Render::ISublayerHost::loadSublayersFromJson(*host, entry);
            }

            // Slice X.14 — restore per-kind renderers, if persisted.
            const QJsonObject kindObj = kindRendererMap.value(relPath).toObject();
            for (int i = 0; !kindObj.isEmpty() && i < int(SWMMModelLayer::NumCategories); ++i) {
                const auto cat = static_cast<SWMMModelLayer::Category>(i);
                const QString key = SWMMModelLayer::kindKey(cat);
                if (!kindObj.contains(key)) continue;
                if (auto r = makeRendererFromJson(kindObj.value(key).toObject()))
                    rl->setKindRenderer(cat, std::move(r));
            }
        }
    }

    // Terrain editing state.
    if (sessionObj.contains(kTerrain)) {
        const QJsonObject t = sessionObj.value(kTerrain).toObject();
        const QString relLayer = t.value(kTerrainLayer).toString();
        const QString absLayer = relLayer.isEmpty()
                                     ? QString()
                                     : resolveStoredPath(relLayer, oswpFile);
        const double nodeOff  = t.value(kTerrainNodeOffset).toDouble(0.0);
        const double linkOff  = t.value(kTerrainLinkOffset).toDouble(0.0);
        const QString vertUnit = t.value(kTerrainVertUnit).toString();
        pw->restoreTerrainState(absLayer, nodeOff, linkOff, vertUnit);
    }

    // 2D mesh-layer display state — Slice AZ.3.7.
    // openSingleINP has already auto-loaded the mesh layer from the .inp's
    // [2D_MESH_FILE] reference before applySession runs. Match each saved
    // entry to the live layer by canonical sourcePath and re-apply the
    // display state.
    if (sessionObj.contains(kMeshLayers) && pw->canvas()) {
        // Build a lookup of live mesh layers keyed by canonical source path.
        // QFileInfo::canonicalFilePath is empty for non-existing paths, so
        // we fall back to absoluteFilePath for inline / generated meshes.
        QHash<QString, SWMM2DMeshLayer *> bySource;
        for (OpenSWMMVisLayer *l : pw->canvas()->layers()) {
            auto *ml = qobject_cast<SWMM2DMeshLayer *>(l);
            if (!ml) continue;
            QFileInfo fi(ml->sourcePath());
            const QString key = fi.canonicalFilePath().isEmpty()
                                    ? fi.absoluteFilePath()
                                    : fi.canonicalFilePath();
            bySource.insert(key, ml);
        }

        for (const QJsonValue &v : sessionObj.value(kMeshLayers).toArray()) {
            const QJsonObject m = v.toObject();
            const QString rel = m.value(kMeshSourcePath).toString();
            if (rel.isEmpty()) continue;
            const QString abs = resolveStoredPath(rel, oswpFile);
            QFileInfo fi(abs);
            const QString key = fi.canonicalFilePath().isEmpty()
                                    ? fi.absoluteFilePath()
                                    : fi.canonicalFilePath();
            SWMM2DMeshLayer *ml = bySource.value(key, nullptr);
            if (!ml) continue;   // mesh wasn't loaded this open — skip silently

            if (m.contains(kMeshActive))    ml->setActiveMesh(m.value(kMeshActive).toBool());
            if (m.contains(kMeshShowNodes)) ml->setShowMeshNodes(m.value(kMeshShowNodes).toBool());
            if (m.contains(kMeshShowEdges)) ml->setShowEdges(m.value(kMeshShowEdges).toBool());

            const QJsonObject hs = m.value(kMeshHillshade).toObject();
            if (hs.contains(kMeshHsAzimuth))  ml->setHillshadeAzimuth(hs.value(kMeshHsAzimuth).toDouble());
            if (hs.contains(kMeshHsAltitude)) ml->setHillshadeAltitude(hs.value(kMeshHsAltitude).toDouble());
            if (hs.contains(kMeshHsZExag))    ml->setHillshadeZExag(hs.value(kMeshHsZExag).toDouble());
            if (hs.contains(kMeshHsMinLit))   ml->setHillshadeMinLit(hs.value(kMeshHsMinLit).toDouble());

            const QJsonObject c = m.value(kMeshContours).toObject();
            if (c.contains(kMeshContShow))      ml->setShowContours(c.value(kMeshContShow).toBool());
            if (c.contains(kMeshContIntervals)) ml->setContourIntervalCount(c.value(kMeshContIntervals).toInt());
            if (c.contains(kMeshContColor)) {
                const QColor col = QColor::fromString(c.value(kMeshContColor).toString());
                if (col.isValid()) ml->setContourColor(col);
            }
            if (c.contains(kMeshContWidth)) ml->setContourLineWidth(c.value(kMeshContWidth).toDouble());
            if (c.contains(kMeshContFilled))
                ml->setFilledContours(c.value(kMeshContFilled).toBool());
            if (c.contains(kMeshContFilledAlpha))
                ml->setFilledContoursOpacity(c.value(kMeshContFilledAlpha).toDouble());
        }
    }

    // 2D results layers — the .h5 open is asynchronous (it rides the same
    // watcher as the mesh auto-load, which runs AFTER applySession), so
    // creating/styling the layer here would race it. Stash the entries —
    // with paths resolved absolute — on the project window; SWMMVis's 2D
    // auto-load consumes them once the source is open.
    if (sessionObj.contains(kResults2D)) {
        QJsonArray stash;
        for (const QJsonValue &v : sessionObj.value(kResults2D).toArray()) {
            QJsonObject r = v.toObject();
            const QString rel = r.value(kResults2DPath).toString();
            if (rel.isEmpty()) continue;
            r[kResults2DPath] = resolveStoredPath(rel, oswpFile);
            stash.append(r);
        }
        if (!stash.isEmpty())
            pw->setPending2DResultsRestore(stash);
    }

    // Slice BB Phase 8.6.16 — restore on-canvas legend overlay chrome.
    // If no overlay exists yet (user hasn't toggled actionShowLegend in
    // this session), create one so the saved style takes effect the next
    // time the user shows the legend. The overlay stays hidden until the
    // toolbar toggle fires.
    if (sessionObj.contains(kLegendOverlay)) {
        if (auto *canvas = pw->canvas()) {
            auto *overlay = canvas->findChild<openswmmvis::ui::LegendOverlay *>(
                QString(), Qt::FindDirectChildrenOnly);
            if (!overlay)
                overlay = new openswmmvis::ui::LegendOverlay(canvas);
            if (auto *style = overlay->style())
                style->fromJson(sessionObj.value(kLegendOverlay).toObject());
        }
    }

    // Text annotations — restore each into a freshly-created annotation
    // layer (lazy creation in the project window). Missing key ⇒ no layer
    // is added, keeping the layer tree empty for projects that never used
    // annotations.
    if (sessionObj.contains(kAnnotations)) {
        const QJsonArray annArr = sessionObj.value(kAnnotations).toArray();
        if (!annArr.isEmpty()) {
            if (auto *al = pw->ensureAnnotationLayer())
                al->fromJson(annArr);
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Save — schema v4 builder over a list of windows.
// ---------------------------------------------------------------------------

bool ProjectSerializer::writeRootJson(const QString &oswpPath,
                                       const QVector<SWMMVisProjectWindow *> &windows,
                                       QString *errorOut)
{
    auto setErr = [&](const QString &m) { if (errorOut) *errorOut = m; };
    if (oswpPath.isEmpty())
    { setErr(QObject::tr("Empty .oswp path")); return false; }
    if (windows.isEmpty())
    { setErr(QObject::tr("No project windows to serialize")); return false; }

    QJsonObject root;
    root[kSchemaVersion] = ProjectSerializer::kCurrentSchemaVersion;

    // Sessions[] — one entry per instance.
    QJsonArray sessionsArr;
    for (SWMMVisProjectWindow *pw : windows) {
        if (!pw) continue;
        const QJsonObject sessionObj = serializeSession(pw, oswpPath);
        if (!sessionObj.isEmpty())
            sessionsArr.append(sessionObj);
    }
    root[kSessions] = sessionsArr;

    // Project-level: canvas + basemaps come from the first window's canvas
    // today.  When Phase 13 lands and a project owns a single shared
    // canvas, switch to project->canvas() — for now the active window is
    // the canonical view.
    auto *canvas = windows.front() ? windows.front()->canvas() : nullptr;
    if (canvas) {
        QJsonObject canvasObj;
        if (auto *srs = canvas->canvasSRS()) {
            const QString auth = srs->authName();
            const int code     = srs->code();
            if (!auth.isEmpty() && code > 0) {
                canvasObj[kCrsAuthority] = auth;
                canvasObj[kCrsCode]      = code;
            }
        }
        const MapExtent e = canvas->extent();
        if (e.isValid()) {
            QJsonArray bbox;
            bbox.append(e.xMin()); bbox.append(e.yMin());
            bbox.append(e.xMax()); bbox.append(e.yMax());
            canvasObj[kExtent] = bbox;
        }
        if (!canvasObj.isEmpty())
            root[kCanvas] = canvasObj;

        QJsonArray basemapsArr;
        for (OpenSWMMVisLayer *l : canvas->layers()) {
            if (!l->isBasemapLayer()) continue;
            const QJsonObject bm = serializeBasemapLayer(l);
            if (!bm.isEmpty())
                basemapsArr.append(bm);
        }
        if (!basemapsArr.isEmpty())
            root[kBasemaps] = basemapsArr;

        // GIS data layers (schema v4+) — loaded rasters/shapefiles persisted
        // by source path so they reopen on project load. Basemaps are handled
        // above; these are the non-basemap GDAL/OGR data layers.
        QJsonArray gisArr;
        for (OpenSWMMVisLayer *l : canvas->layers()) {
            if (l->isBasemapLayer()) continue;
            const QJsonObject g = serializeGisLayer(l, oswpPath);
            if (!g.isEmpty())
                gisArr.append(g);
        }
        if (!gisArr.isEmpty())
            root[kGisLayers] = gisArr;
    }

    QFile f(oswpPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setErr(QObject::tr("Cannot write %1: %2").arg(oswpPath, f.errorString()));
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

// Public single-instance overload — for existing callers (Slice X / AA / AA-2).
bool ProjectSerializer::saveToFile(const QString &oswpPath,
                                    SWMMVisProjectWindow *pw,
                                    QString *errorOut)
{
    auto setErr = [&](const QString &m) { if (errorOut) *errorOut = m; };
    if (!pw) { setErr(QObject::tr("No project window")); return false; }
    if (!pw->modelLayer())
    { setErr(QObject::tr("No SWMM layer to serialize")); return false; }

    QVector<SWMMVisProjectWindow *> windows{pw};
    return writeRootJson(oswpPath, windows, errorOut);
}

// Public multi-instance overload — Slice AA-3.4.
bool ProjectSerializer::saveToFile(const QString &oswpPath,
                                    SWMMVisProject *proj,
                                    QString *errorOut)
{
    auto setErr = [&](const QString &m) { if (errorOut) *errorOut = m; };
    if (!proj) { setErr(QObject::tr("No project")); return false; }

    QVector<SWMMVisProjectWindow *> windows = proj->instances();
    if (windows.isEmpty())
    { setErr(QObject::tr("Project has no instances")); return false; }

    return writeRootJson(oswpPath, windows, errorOut);
}

// ---------------------------------------------------------------------------
// Apply
// ---------------------------------------------------------------------------

bool ProjectSerializer::applyFromFile(const QString &oswpPath,
                                       SWMMVisProjectWindow *pw,
                                       QString *errorOut,
                                       QStringList *warningsOut)
{
    auto setErr = [&](const QString &m) { if (errorOut) *errorOut = m; };
    if (oswpPath.isEmpty()) { setErr(QObject::tr("Empty .oswp path")); return false; }
    if (!pw)                { setErr(QObject::tr("No project window")); return false; }

    QFile f(oswpPath);
    if (!f.open(QIODevice::ReadOnly)) {
        setErr(QObject::tr("Cannot open %1: %2").arg(oswpPath, f.errorString()));
        return false;
    }
    QJsonParseError jerr{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &jerr);
    if (doc.isNull()) {
        setErr(QObject::tr("Invalid JSON in %1: %2").arg(oswpPath, jerr.errorString()));
        return false;
    }

    const QJsonObject root = doc.object();
    const int ver = root.value(kSchemaVersion).toInt(0);

    // v0 = pre-Slice-X format (just a `layers` array of inp paths) —
    // nothing we apply from here; the caller's .inp is already open.
    if (ver < 1) return true;

    // ── Resolve the per-instance state ───────────────────────────────
    // v1/v2/v3:  inpPath / engineVersion / layer at root
    // v4+    :   sessions[N] array, applied entry sessions[0] → pw
    //
    // Single-instance applyFromFile reads sessions[0] only — multi-
    // instance hydration is the caller's responsibility (Phase 13 will
    // walk sessions[N] and create one SWMMVisProjectWindow per entry).
    QJsonObject sessionObj;
    if (root.contains(kSessions)) {
        const QJsonArray sessions = root.value(kSessions).toArray();
        if (!sessions.isEmpty())
            sessionObj = sessions.first().toObject();
    } else {
        // Legacy v1/v2/v3 — synthesize a session object from root keys.
        if (root.contains(kEngineVersion))
            sessionObj[kEngineVersion] = root.value(kEngineVersion);
        if (root.contains(kInpPath))
            sessionObj[kInpPath]       = root.value(kInpPath);
        if (root.contains(kLayer))
            sessionObj[kLayer]         = root.value(kLayer);
    }

    auto *layer  = pw->modelLayer();
    auto *canvas = pw->canvas();

    if (layer) {
        applySession(sessionObj, pw, oswpPath, warningsOut);
    } else if (sessionObj.contains(kEngineVersion)) {
        // No layer yet but engineVersion still useful.
        pw->setEngineVersion(sessionObj.value(kEngineVersion).toString("6.0.0"));
    }

    // --- Canvas block (project-scope, same shape v1→v4) -------------------
    if (canvas) {
        const QJsonObject canvasObj = root.value(kCanvas).toObject();
        if (canvasObj.contains(kCrsAuthority) && canvasObj.contains(kCrsCode)) {
            const QString auth = canvasObj.value(kCrsAuthority).toString();
            const int code = canvasObj.value(kCrsCode).toInt();
            if (!auth.isEmpty() && code > 0) {
                if (auto *srs = SpatialReferenceSystem::fromAuthCode(auth, code, canvas))
                    canvas->setCanvasSRS(srs, true);
            }
        }
        const QJsonArray bbox = canvasObj.value(kExtent).toArray();
        if (bbox.size() == 4) {
            const MapExtent e(bbox[0].toDouble(), bbox[1].toDouble(),
                              bbox[2].toDouble(), bbox[3].toDouble());
            if (e.isValid()) canvas->setExtent(e);
        }
    }

    // --- Basemap layers (schema v3+) ---------------------------------------
    if (ver >= 3 && canvas) {
        const QJsonArray basemapsArr = root.value(kBasemaps).toArray();
        for (const QJsonValue &v : basemapsArr) {
            OpenSWMMVisLayer *bm = deserializeBasemapLayer(v.toObject(), canvas);
            if (bm)
                canvas->addLayer(bm, /*pushUndo=*/false);
        }
    }

    // --- GIS data layers (schema v4+) --------------------------------------
    // Rasters/shapefiles open asynchronously (GDAL/OGR on a worker thread);
    // each layer adds itself to the canvas on openFinished — see
    // deserializeGisLayer. Read whenever present for forward compatibility.
    if (canvas) {
        const QJsonArray gisArr = root.value(kGisLayers).toArray();
        for (const QJsonValue &v : gisArr)
            deserializeGisLayer(v.toObject(), canvas, oswpPath, warningsOut);
    }

    return true;
}

// ---------------------------------------------------------------------------
// Basemap helpers
// ---------------------------------------------------------------------------

static QJsonObject headersToJson(const BasemapHttpHeaders &headers)
{
    QJsonObject obj;
    for (auto it = headers.cbegin(); it != headers.cend(); ++it)
        obj[it.key()] = it.value();
    return obj;
}

static BasemapHttpHeaders headersFromJson(const QJsonObject &obj)
{
    BasemapHttpHeaders headers;
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
        headers.insert(it.key(), it.value().toString());
    return headers;
}

QJsonObject ProjectSerializer::serializeBasemapLayer(OpenSWMMVisLayer *layer)
{
    QJsonObject obj;
    if (auto *xyz = qobject_cast<XYZTileLayer *>(layer)) {
        obj[kBmType]         = QStringLiteral("xyz");
        obj[kBmName]         = xyz->name();
        obj[kBmUrl]          = xyz->urlTemplate();
        obj[kBmTilePixRatio] = xyz->tilePixelRatio();
        obj[kBmAxisOrder]    = static_cast<int>(xyz->axisOrder());
        const QJsonObject hdrs = headersToJson(xyz->httpHeaders());
        if (!hdrs.isEmpty()) obj[kBmHeaders] = hdrs;
    } else if (auto *wmts = qobject_cast<WMTSLayer *>(layer)) {
        obj[kBmType]  = QStringLiteral("wmts");
        obj[kBmName]  = wmts->name();
        obj[kBmUrl]   = wmts->serviceUrl().toString();
        obj[kBmLayer] = wmts->activeLayerId();
        obj[kBmStyle] = wmts->activeStyle();
        obj[kBmFormat]= wmts->imageFormat();
        obj[kBmTms]   = wmts->activeTileMatrixSet();
        const QJsonObject hdrs = headersToJson(wmts->httpHeaders());
        if (!hdrs.isEmpty()) obj[kBmHeaders] = hdrs;
    } else if (auto *wms = qobject_cast<WMSLayer *>(layer)) {
        obj[kBmType]   = QStringLiteral("wms");
        obj[kBmName]   = wms->name();
        obj[kBmUrl]    = wms->serviceUrl().toString();
        obj[kBmLayer]  = wms->activeLayerName();
        obj[kBmStyle]  = wms->activeStyle();
        obj[kBmFormat] = wms->imageFormat();
        obj[kBmCrs]    = wms->crs();
        obj[kBmDpiMode]= wms->dpiMode();
        const QJsonObject hdrs = headersToJson(wms->httpHeaders());
        if (!hdrs.isEmpty()) obj[kBmHeaders] = hdrs;
    }
    return obj;
}

OpenSWMMVisLayer *ProjectSerializer::deserializeBasemapLayer(const QJsonObject &obj,
                                                             QObject *parent)
{
    const QString type = obj.value(kBmType).toString();
    const BasemapHttpHeaders headers = headersFromJson(obj.value(kBmHeaders).toObject());

    if (type == QStringLiteral("xyz")) {
        auto *layer = new XYZTileLayer(obj.value(kBmUrl).toString(), 256, parent);
        layer->setName(obj.value(kBmName).toString());
        layer->setTilePixelRatio(obj.value(kBmTilePixRatio).toInt(0));
        layer->setAxisOrder(static_cast<TileAxisOrder>(obj.value(kBmAxisOrder).toInt(0)));
        layer->setHttpHeaders(headers);
        return layer;
    }
    if (type == QStringLiteral("wmts")) {
        auto *layer = new WMTSLayer(QUrl(obj.value(kBmUrl).toString()));
        layer->setName(obj.value(kBmName).toString());
        layer->setActiveLayerId(obj.value(kBmLayer).toString());
        layer->setActiveStyle(obj.value(kBmStyle).toString());
        layer->setImageFormat(obj.value(kBmFormat).toString());
        layer->setActiveTileMatrixSet(obj.value(kBmTms).toString());
        layer->setHttpHeaders(headers);
        return layer;
    }
    if (type == QStringLiteral("wms")) {
        auto *layer = new WMSLayer(QUrl(obj.value(kBmUrl).toString()));
        layer->setName(obj.value(kBmName).toString());
        layer->setActiveLayerName(obj.value(kBmLayer).toString());
        layer->setActiveStyle(obj.value(kBmStyle).toString());
        layer->setImageFormat(obj.value(kBmFormat).toString());
        layer->setCrs(obj.value(kBmCrs).toString());
        layer->setDpiMode(obj.value(kBmDpiMode).toInt(7));
        layer->setHttpHeaders(headers);
        return layer;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// GIS data-layer helpers (schema v4+) — reopen loaded rasters/shapefiles.
// Persist the source path (relative to the .oswp) plus name/visibility/
// opacity; reconstruct via the same async GDAL/OGR open the File ▸ Add
// Layer path uses. See workplans/GIS_LAYER_PERSISTENCE_HANDOFF_2026-07-24.md.
// ---------------------------------------------------------------------------

QJsonObject ProjectSerializer::serializeGisLayer(OpenSWMMVisLayer *layer,
                                                 const QString &oswpPath)
{
    QJsonObject obj;
    if (!layer) return obj;

    if (auto *r = qobject_cast<GISRasterLayer *>(layer)) {
        if (r->filePath().isEmpty()) return obj;   // nothing to reopen from
        obj[kGisType]    = QStringLiteral("raster");
        obj[kGisPath]    = toRelativePath(r->filePath(), oswpPath);
        obj[kGisName]    = r->name();
        obj[kGisVisible] = r->isVisible();
        obj[kGisOpacity] = r->opacity();
    } else if (auto *v = qobject_cast<GISVectorLayer *>(layer)) {
        if (v->filePath().isEmpty()) return obj;
        obj[kGisType]      = QStringLiteral("vector");
        obj[kGisPath]      = toRelativePath(v->filePath(), oswpPath);
        obj[kGisName]      = v->name();
        obj[kGisVisible]   = v->isVisible();
        obj[kGisOpacity]   = v->opacity();
        obj[kGisLayerName] = v->ogrLayerName();   // OGR sublayer (may be empty)
    }
    return obj;
}

void ProjectSerializer::deserializeGisLayer(const QJsonObject &obj,
                                            MapCanvas *canvas,
                                            const QString &oswpPath,
                                            QStringList *warningsOut)
{
    if (!canvas) return;
    const QString type = obj.value(kGisType).toString();
    const QString rel  = obj.value(kGisPath).toString();
    if (rel.isEmpty()) return;
    const QString path = resolveStoredPath(rel, oswpPath);
    if (!QFile::exists(path)) {
        if (warningsOut)
            *warningsOut << QObject::tr(
                "%1 layer file not found — layer skipped: %2")
                   .arg(type == QStringLiteral("raster")
                            ? QObject::tr("Raster") : QObject::tr("Vector"),
                        path);
        return;
    }

    const QString name    = obj.value(kGisName).toString();
    const bool    visible = obj.value(kGisVisible).toBool(true);
    const double  opacity = obj.value(kGisOpacity).toDouble(1.0);

    // Restore name/visibility/opacity as soon as the async open finishes, then
    // add the layer to the canvas — mirroring the File ▸ Add Layer flow.
    auto applyCommon = [name, visible, opacity](OpenSWMMVisLayer *l) {
        if (!name.isEmpty()) l->setName(name);
        l->setVisible(visible);
        l->setOpacity(opacity);
    };

    if (type == QStringLiteral("raster")) {
        auto *layer = new GISRasterLayer(QString());
        QObject::connect(
            layer, &GISRasterLayer::openFinished, canvas,
            [canvas, layer, applyCommon](bool ok) {
                if (ok) { applyCommon(layer); canvas->addLayer(layer, /*pushUndo=*/false); }
                else    { layer->deleteLater(); }
            },
            static_cast<Qt::ConnectionType>(Qt::SingleShotConnection));
        layer->openAsync(path);
    } else if (type == QStringLiteral("vector")) {
        const QString layerName = obj.value(kGisLayerName).toString();
        auto *layer = new GISVectorLayer(QString());
        QObject::connect(
            layer, &GISVectorLayer::openFinished, canvas,
            [canvas, layer, applyCommon](bool ok) {
                if (ok) { applyCommon(layer); canvas->addLayer(layer, /*pushUndo=*/false); }
                else    { layer->deleteLater(); }
            },
            static_cast<Qt::ConnectionType>(Qt::SingleShotConnection));
        layer->openAsync(path, layerName);
    }
}
