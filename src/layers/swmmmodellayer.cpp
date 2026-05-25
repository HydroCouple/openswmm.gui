/*!
 * \file   swmmmodellayer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 */

#include "layers/swmmmodellayer.h"
#include "layers/hydrographmodels.h"
#include "timeseries/timeseriesregistry.h"
#include "pattern/patternregistry.h"
#include "curve/curveregistry.h"
#include "core/editgeometry.h"
#include "core/preferencesmanager.h"
#include "core/unitsystem.h"
#include "map/swmmlayeritem.h"
#include "map/spatialreferencesystem.h"
#include "map/mapextent.h"
#include "render/ifeaturerenderer.h"
#include "render/multikindrenderer.h"
#include "render/renderers/categorizedrenderer.h"
#include "render/renderers/graduatedrenderer.h"
#include "render/renderers/rulebasedrenderer.h"
#include "render/renderers/singlesymbolrenderer.h"
#include "render/symbollayer.h"
#include "render/symbolstyle.h"

#include <QFile>
#include <QGraphicsScene>
#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QtMath>

#include <cmath>
#include <limits>

#include <ogr_spatialref.h>
#include <ogr_geometry.h>

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_subcatchments.h>
#include <openswmm/engine/openswmm_gages.h>
#include <openswmm/engine/openswmm_spatial.h>
#include <openswmm/engine/openswmm_edit.h>
// Slice BM.0 — non-spatial data-object accessors.
#include <openswmm/engine/openswmm_tables.h>
#include <openswmm/engine/openswmm_infrastructure.h>
#include <openswmm/engine/openswmm_pollutants.h>
#include <openswmm/engine/openswmm_quality.h>
#include <openswmm/engine/openswmm_controls.h>
#include <openswmm/engine/openswmm_inflows.h>

#include <nanoflann.hpp>

// ---------------------------------------------------------------------------
// nanoflann adaptor + KD-tree types (private to this translation unit)
// ---------------------------------------------------------------------------
namespace {

// Adaptor for a pair of parallel double arrays (xs, ys), each of length n.
struct PtAdaptor
{
    const double *xs = nullptr;
    const double *ys = nullptr;
    std::size_t   n  = 0;

    std::size_t kdtree_get_point_count() const { return n; }
    double kdtree_get_pt(std::size_t i, std::size_t dim) const
    {
        return dim == 0 ? xs[i] : ys[i];
    }
    template<class BBOX>
    bool kdtree_get_bbox(BBOX &) const { return false; }
};

using Kd2 = nanoflann::KDTreeSingleIndexAdaptor<
    nanoflann::L2_Simple_Adaptor<double, PtAdaptor>,
    PtAdaptor, 2>;

} // anonymous namespace

// SWMMKdTrees owns the flat x/y arrays that the PtAdaptors reference plus
// the two KD-trees.  Defined here (not in the header) so nanoflann.hpp is
// never pulled into consumers of swmmmodellayer.h.
struct SWMMKdTrees
{
    QVector<double> nodeX, nodeY;   ///< parallel to SWMMModelLayer::m_nodes
    QVector<double> gageX, gageY;   ///< parallel to SWMMModelLayer::m_gages

    PtAdaptor nodeAdaptor;
    PtAdaptor gageAdaptor;

    std::unique_ptr<Kd2> nodeTree;
    std::unique_ptr<Kd2> gageTree;
};

// ---------------------------------------------------------------------------
// Helper: map coordinate → scene coordinate (Y-flipped)
// ---------------------------------------------------------------------------

static inline QPointF toScene(double mx, double my)
{
    return QPointF(mx, -my);
}

// ---------------------------------------------------------------------------
// Per-kind renderer helpers (Slice BI-MK.1, 2026-05-24)
// ---------------------------------------------------------------------------

namespace
{

using OpenSWMM::Render::SymbolLayer;
using OpenSWMM::Render::SymbolLayerKind;
using OpenSWMM::Render::SymbolStyle;

// Pick the natural SymbolLayer kind for a given Category geometry —
// point glyphs for nodes / rain gages, line glyphs for the five link
// kinds, and a filled-polygon style for subcatchments.
SymbolLayerKind layerKindFor(SWMMModelLayer::Category c)
{
    switch (c) {
    case SWMMModelLayer::CatJunctions:
    case SWMMModelLayer::CatOutfalls:
    case SWMMModelLayer::CatStorage:
    case SWMMModelLayer::CatDividers:
    case SWMMModelLayer::CatRainGages:
        return SymbolLayerKind::SimpleMarker;
    case SWMMModelLayer::CatConduits:
    case SWMMModelLayer::CatPumps:
    case SWMMModelLayer::CatOrifices:
    case SWMMModelLayer::CatWeirs:
    case SWMMModelLayer::CatOutlets:
        return SymbolLayerKind::SimpleLine;
    case SWMMModelLayer::CatSubcatchments:
        return SymbolLayerKind::SimpleFill;
    case SWMMModelLayer::NumCategories:
        break;
    }
    return SymbolLayerKind::SimpleMarker;
}

// Build a SingleSymbolRenderer that mirrors `s` for the given category's
// geometry. The renderer is the legend / dialog / .oswp source of truth
// for "this kind looks like…"; the legacy m_*Sym field remains the paint
// source of truth (write-through both ways via the setters).
SymbolStyle styleFromElementSymbol(const SWMMElementSymbol &s, SWMMModelLayer::Category c)
{
    SymbolStyle style;
    SymbolLayer layer;
    layer.kind = layerKindFor(c);
    layer.props.insert(QStringLiteral("color"),
                       s.fillColor.name(QColor::HexArgb));
    layer.props.insert(QStringLiteral("outlineColor"),
                       s.outlineColor.name(QColor::HexArgb));
    layer.props.insert(QStringLiteral("outlineWidth"), s.outlineWidth);
    if (layer.kind == SymbolLayerKind::SimpleLine)
        layer.props.insert(QStringLiteral("width"), s.size);
    else
        layer.props.insert(QStringLiteral("size"), s.size);
    style.layers.append(layer);
    return style;
}

// Inverse of styleFromElementSymbol — extract a legacy SWMMElementSymbol
// from a SingleSymbol renderer's SymbolStyle. Used to write through dialog
// edits to the legacy field so the existing bucketed paint loop reflects
// the change without a per-feature symbolFor() refactor.
SWMMElementSymbol elementSymbolFromStyle(const SymbolStyle &style,
                                         const SWMMElementSymbol &fallback)
{
    SWMMElementSymbol out = fallback;
    if (style.layers.isEmpty()) return out;
    const SymbolLayer &layer = style.layers.first();
    if (layer.props.contains(QStringLiteral("color"))) {
        const QColor c(layer.props.value(QStringLiteral("color")).toString());
        if (c.isValid()) out.fillColor = c;
    }
    if (layer.props.contains(QStringLiteral("outlineColor"))) {
        const QColor c(layer.props.value(QStringLiteral("outlineColor")).toString());
        if (c.isValid()) out.outlineColor = c;
    }
    if (layer.props.contains(QStringLiteral("outlineWidth")))
        out.outlineWidth = layer.props.value(QStringLiteral("outlineWidth")).toDouble();
    if (layer.props.contains(QStringLiteral("size")))
        out.size = layer.props.value(QStringLiteral("size")).toDouble();
    else if (layer.props.contains(QStringLiteral("width")))
        out.size = layer.props.value(QStringLiteral("width")).toDouble();
    return out;
}

std::unique_ptr<OpenSWMM::Render::SingleSymbolRenderer>
makeSingleSymbolRenderer(const SWMMElementSymbol &s, SWMMModelLayer::Category c)
{
    return std::make_unique<OpenSWMM::Render::SingleSymbolRenderer>(
        styleFromElementSymbol(s, c));
}

} // namespace

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

SWMMModelLayer::SWMMModelLayer(const QString &modelFilePath,
                               OpenSWMMVisWorkspace *parent)
    : OpenSWMMVisLayer(parent),
      m_modelFilePath(modelFilePath)
{
    setLayerType(OpenSWMMVisLayer::SWMMModelLayer);

    // Default symbology
    m_junctionSym.fillColor  = QColor(0, 120, 255);
    m_junctionSym.size       = 8.0;
    m_outfallSym.fillColor   = QColor(220, 0, 0);     // red — outfalls stand out
    m_outfallSym.size        = 12.5;   // 1.25× the legacy 10 px triangle
    m_storageSym.fillColor   = QColor(180, 60, 200);
    m_storageSym.size        = 12.0;
    m_dividerSym.fillColor   = Qt::green;
    m_dividerSym.size        = 8.0;
    m_conduitSym.fillColor   = QColor(50,  50, 200);
    m_conduitSym.outlineWidth = 1.5;
    m_pumpSym.fillColor      = Qt::red;
    m_pumpSym.outlineWidth   = 2.0;
    m_orificeSym.fillColor   = QColor(200, 150, 0);
    m_weirSym.fillColor      = QColor(0, 180, 100);
    m_subcatchSym.fillColor    = QColor(180, 220, 180);
    m_subcatchSym.outlineColor = QColor(0,    60,   0);   // dark forest green
    m_subcatchSym.outlineWidth = 1.5;
    m_gageSym.fillColor      = Qt::cyan;
    m_gageSym.size           = 10.0;

    // Slice BI Phase 8.13.6.5 — initialise renderer so renderer() never
    // returns nullptr. Placeholder SingleSymbolRenderer is unused by the
    // current paint loop (which still reads m_*Sym directly); the paint
    // refactor swaps in a MultiKindRenderer adapter and flips the path.
    m_renderer = std::make_unique<OpenSWMM::Render::SingleSymbolRenderer>();

    // Slice BI-MK.1 / BI-MK.LT (2026-05-24) — seed the 11 per-kind
    // renderers from the matching SWMMElementSymbol defaults above so
    // first-open visuals are identical to today's hardcoded glyphs.
    m_kindRenderers.resize(static_cast<size_t>(NumCategories));
    m_kindRenderers[CatJunctions]      = makeSingleSymbolRenderer(m_junctionSym,    CatJunctions);
    m_kindRenderers[CatOutfalls]       = makeSingleSymbolRenderer(m_outfallSym,     CatOutfalls);
    m_kindRenderers[CatStorage]        = makeSingleSymbolRenderer(m_storageSym,     CatStorage);
    m_kindRenderers[CatDividers]       = makeSingleSymbolRenderer(m_dividerSym,     CatDividers);
    m_kindRenderers[CatConduits]       = makeSingleSymbolRenderer(m_conduitSym,     CatConduits);
    m_kindRenderers[CatPumps]          = makeSingleSymbolRenderer(m_pumpSym,        CatPumps);
    m_kindRenderers[CatOrifices]       = makeSingleSymbolRenderer(m_orificeSym,     CatOrifices);
    m_kindRenderers[CatWeirs]          = makeSingleSymbolRenderer(m_weirSym,        CatWeirs);
    // No legacy m_outletSym field — seed from a defaulted symbol so the
    // sub-row still has a renderer (the paint loop currently uses the
    // weir colour for outlets; can be reset to defaults via the tree menu).
    {
        SWMMElementSymbol outletDefault;
        outletDefault.fillColor    = QColor(140, 100, 60);
        outletDefault.outlineWidth = 1.5;
        m_kindRenderers[CatOutlets] = makeSingleSymbolRenderer(outletDefault, CatOutlets);
    }
    m_kindRenderers[CatSubcatchments]  = makeSingleSymbolRenderer(m_subcatchSym,    CatSubcatchments);
    m_kindRenderers[CatRainGages]      = makeSingleSymbolRenderer(m_gageSym,        CatRainGages);
}

SWMMModelLayer::~SWMMModelLayer()
{
    closeEngine();

    if (m_transform)
    {
        OGRCoordinateTransformation::DestroyCT(m_transform);
        m_transform = nullptr;
    }
    if (m_inverseTransform)
    {
        OGRCoordinateTransformation::DestroyCT(m_inverseTransform);
        m_inverseTransform = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Model file
// ---------------------------------------------------------------------------

QString SWMMModelLayer::modelFilePath() const { return m_modelFilePath; }

void SWMMModelLayer::setModelFilePath(const QString &path)
{
    if (m_modelFilePath != path)
    {
        m_modelFilePath = path;
        emit modelFilePathChanged(path);
    }
}

SWMM_Engine SWMMModelLayer::engine() const { return m_engine; }

void SWMMModelLayer::closeEngine()
{
    if (m_engine)
    {
        swmm_engine_close(m_engine);
        swmm_engine_destroy(m_engine);
        m_engine = nullptr;
    }
    m_nodes.clear();
    m_links.clear();
    m_catchments.clear();
    m_gages.clear();
    // Drop any per-object hides so opening a different model doesn't
    // accidentally hide a similarly-named object from the new project.
    m_hiddenObjects.clear();
    // Per-category caches (populated in buildGeometryCache).
    for (auto &b : m_nodesByType) b.clear();
    for (auto &b : m_linksByType) b.clear();
    for (int &c : m_hiddenCountByCategory) c = 0;
    m_objectLocation.clear();
    m_kdTrees.reset();
    m_kdDirty = false;   // no data to index — nothing to rebuild
    m_linkBboxes.clear();
    m_catchBboxes.clear();
    m_needsRebuild = true;
}

// Scan the [MAP] section of an .inp for the "Units" keyword.
// Returns "FEET", "METERS", "DEGREES", etc. (uppercased), or empty string if absent.
static QString readMapUnitsFromInp(const QString &inpPath)
{
    QFile f(inpPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    bool inMap = false;
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.startsWith('[')) {
            inMap = (line.compare(QStringLiteral("[MAP]"), Qt::CaseInsensitive) == 0);
            continue;
        }
        if (!inMap || line.startsWith(';') || line.isEmpty()) continue;
        const QStringList tok = line.split(ws, Qt::SkipEmptyParts);
        if (tok.size() >= 2 &&
            tok[0].compare(QStringLiteral("UNITS"), Qt::CaseInsensitive) == 0)
            return tok[1].toUpper();
    }
    return {};
}

bool SWMMModelLayer::loadModel(QList<QString> &warnings, QList<QString> &errors)
{
    closeEngine();

    if (m_modelFilePath.isEmpty())
    {
        errors.append(QStringLiteral("No model file path specified."));
        return false;
    }

    QFileInfo fi(m_modelFilePath);
    if (!fi.exists())
    {
        errors.append(QStringLiteral("Model file not found: %1").arg(m_modelFilePath));
        return false;
    }

    // Open model (read-only: pass empty strings for rpt/out)
    m_engine = swmm_engine_create();
    if (!m_engine)
    {
        errors.append(QStringLiteral("Failed to create SWMM engine."));
        return false;
    }

    QByteArray inpPath = m_modelFilePath.toUtf8();
    if (swmm_engine_open(m_engine, inpPath.constData(), "", "", nullptr) != 0)
    {
        errors.append(QStringLiteral("Failed to open model: %1").arg(m_modelFilePath));
        swmm_engine_destroy(m_engine);
        m_engine = nullptr;
        return false;
    }

    // Leave the engine in OPENED state at load time so that property
    // setters (CHECK_GEOMETRY only allows BUILDING/OPENED) succeed on
    // user edits in the Attribute Table + Property Browser.  The
    // simulation runner calls swmm_engine_initialize itself before
    // running — see src/simulation/simulationrunner.cpp:198 — so
    // skipping it here doesn't affect run behaviour.
    //
    // Previously the GUI called swmm_engine_initialize here, which
    // advanced state to INITIALIZED and silently rejected every
    // subsequent property edit with SWMM_ERR_LIFECYCLE.  The Attribute
    // Table appeared to commit edits (cells flashed the new value) but
    // the engine getter then returned the unchanged SoA value, so
    // cells reverted (caleb 2026-05-12).

    // Sync flow units from loaded model
    UnitSystem::instance()->syncFromEngine(m_engine);

    // ---- Nodes ----
    int nodeCount = swmm_node_count(m_engine);
    m_nodes.reserve(nodeCount);
    for (int i = 0; i < nodeCount; ++i)
    {
        NodeGeom g;
        g.name = QString::fromUtf8(swmm_node_id(m_engine, i));
        swmm_node_get_type(m_engine, i, &g.nodeType);
        g.objectType = 0;
        double x = 0, y = 0;
        swmm_spatial_get_node_coord(m_engine, i, &x, &y);
        g.x = x;
        g.y = y;
        m_nodes.append(g);
    }

    // ---- Links ----
    // m_links[i].vertices holds ONLY interior (bend) points.
    // The from/to node endpoint positions are looked up dynamically from
    // m_nodes[] via fromNodeIdx / toNodeIdx so that moving a node
    // automatically affects all attached links without patching vertex arrays.
    int linkCount = swmm_link_count(m_engine);
    m_links.reserve(linkCount);
    for (int i = 0; i < linkCount; ++i)
    {
        LinkGeom g;
        g.name = QString::fromUtf8(swmm_link_id(m_engine, i));
        swmm_link_get_type(m_engine, i, &g.linkType);

        int fromIdx = -1, toIdx = -1;
        swmm_link_get_from_node(m_engine, i, &fromIdx);
        swmm_link_get_to_node(m_engine, i, &toIdx);
        g.fromNodeIdx = fromIdx;
        g.toNodeIdx   = toIdx;

        // GET returns the full polyline: [from-node, interior..., to-node].
        // SET accepts only interior points.  Strip the two endpoint slots so
        // m_links[].vertices holds interior bend points only; cachedLinkPolyline
        // re-prepends / re-appends the node positions dynamically.
        int vertCount = 0;
        swmm_spatial_get_link_vertex_count(m_engine, i, &vertCount);
        const int interiorCount = (vertCount >= 2) ? vertCount - 2 : 0;
        g.vertices.resize(interiorCount);
        if (vertCount > 0)
        {
            QVector<double> vx(vertCount), vy(vertCount);
            swmm_spatial_get_link_vertices(m_engine, i, vx.data(), vy.data(), vertCount);
            for (int v = 0; v < interiorCount; ++v)
                g.vertices[v] = QPointF(vx[v + 1], vy[v + 1]); // skip [0]=from-node, [last]=to-node
        }

        m_links.append(g);
    }

    // ---- Subcatchments ----
    int catchCount = swmm_subcatch_count(m_engine);
    m_catchments.reserve(catchCount);
    for (int i = 0; i < catchCount; ++i)
    {
        CatchGeom g;
        g.name = QString::fromUtf8(swmm_subcatch_id(m_engine, i));

        int polyCount = 0;
        swmm_spatial_get_subcatch_polygon_count(m_engine, i, &polyCount);
        if (polyCount > 0)
        {
            QVector<double> px(polyCount), py(polyCount);
            swmm_spatial_get_subcatch_polygon(m_engine, i, px.data(), py.data(), polyCount);
            g.vertices.resize(polyCount);
            for (int v = 0; v < polyCount; ++v)
                g.vertices[v] = QPointF(px[v], py[v]);
        }
        m_catchments.append(g);
    }

    // ---- Rain gages ----
    int gageCount = swmm_gage_count(m_engine);
    m_gages.reserve(gageCount);
    for (int i = 0; i < gageCount; ++i)
    {
        NodeGeom g;
        g.name = QString::fromUtf8(swmm_gage_id(m_engine, i));
        g.nodeType = 0;
        g.objectType = 1;
        double x = 0, y = 0;
        swmm_spatial_get_gage_coord(m_engine, i, &x, &y);
        g.x = x;
        g.y = y;
        m_gages.append(g);
    }

    // If a rain gage has no [SYMBOLS] coordinate it arrives as (0,0).
    // Reposition such gages to the centroid of the model's renderable
    // features so they don't stack at the origin.
    {
        double sx = 0.0;
        double sy = 0.0;
        std::size_t n = 0;

        auto acc = [&](double x, double y) {
            sx += x;
            sy += y;
            ++n;
        };

        for (const NodeGeom &node : m_nodes)
            acc(node.x, node.y);

        for (const LinkGeom &link : m_links)
            for (const QPointF &v : link.vertices)
                acc(v.x(), v.y());

        for (const CatchGeom &catchment : m_catchments)
            for (const QPointF &v : catchment.vertices)
                acc(v.x(), v.y());

        if (n > 0)
        {
            const double cx = sx / static_cast<double>(n);
            const double cy = sy / static_cast<double>(n);
            for (NodeGeom &g : m_gages)
            {
                if (qFuzzyIsNull(g.x) && qFuzzyIsNull(g.y))
                {
                    g.x = cx;
                    g.y = cy;
                }
            }
        }
    }

    // ---- CRS ----
    // Resolution order:
    //   1. CRS stored in the .inp (via swmm_get_crs) — preferred.
    //   2a. LocalAuto mode (default): auto-generate a local CRS from the
    //       [MAP] Units field — gives a correct unit (ft/m) without
    //       requiring a geographic CRS or user prompt.
    //   2b. EPSG mode: user-configured authority/code (legacy behaviour).
    //   3. Untitled local SRS — triggers the CRS picker in the project
    //      window (truly unknown, no [MAP] Units either).
    {
        SpatialReferenceSystem *layerSRS = nullptr;
        char crsBuf[512] = {};
        if (swmm_get_crs(m_engine, crsBuf, sizeof(crsBuf)) == 0 && crsBuf[0] != '\0')
            layerSRS = SpatialReferenceSystem::fromWktOrProj(QString::fromUtf8(crsBuf), this);

        if (!layerSRS) {
            auto *prefs = PreferencesManager::instance();
            if (prefs->defaultCrsMode() == QStringLiteral("LocalAuto")) {
                const QString mapUnits = readMapUnitsFromInp(m_modelFilePath);
                // "DEGREES" → geographic model, fall through to EPSG preference.
                if (!mapUnits.isEmpty() && mapUnits != QStringLiteral("DEGREES"))
                    layerSRS = SpatialReferenceSystem::localFromMapUnits(mapUnits, this);
            }
            if (!layerSRS)
                layerSRS = SpatialReferenceSystem::fromAuthCode(
                    prefs->defaultCrsAuthority(), prefs->defaultCrsCode(), this);
        }
        if (!layerSRS)
            layerSRS = SpatialReferenceSystem::untitled(this);
        setSRS(layerSRS, true);
    }

    buildGeometryCache();
    m_needsRebuild = true;
    emit repaintRequested();  // ensure canvas redraws after geometry is ready

    // Surface object counts so the user can see at a glance whether the
    // engine actually returned any geometry. Goes into both stderr (qDebug)
    // and the warnings list which the project window pipes into the
    // Message Log dock.
    const QString counts = QStringLiteral(
        "[SWMMModelLayer] %1: nodes=%2 links=%3 subcatchments=%4 gages=%5 "
        "extent valid=%6")
        .arg(fi.fileName())
        .arg(m_nodes.size()).arg(m_links.size())
        .arg(m_catchments.size()).arg(m_gages.size())
        .arg(extent().isValid() ? "yes" : "no");
    qDebug().noquote() << counts;
    warnings.append(counts);

    setName(fi.baseName());
    emit modelFilePathChanged(m_modelFilePath);
    emit modelLoaded();
    return true;
}

// ---------------------------------------------------------------------------
// Element visibility toggles
// ---------------------------------------------------------------------------

bool SWMMModelLayer::showNodes()        const { return m_showNodes; }
bool SWMMModelLayer::showLinks()        const { return m_showLinks; }
bool SWMMModelLayer::showSubcatchments()const { return m_showSubcatchments; }
bool SWMMModelLayer::showRainGages()    const { return m_showRainGages; }
bool SWMMModelLayer::showLabels()       const { return m_showLabels; }

void SWMMModelLayer::setShowNodes(bool show)
{
    if (m_showNodes != show) { m_showNodes = show; m_needsRebuild = true; emit showNodesChanged(show); emit repaintRequested(); }
}
void SWMMModelLayer::setShowLinks(bool show)
{
    if (m_showLinks != show) { m_showLinks = show; m_needsRebuild = true; emit showLinksChanged(show); emit repaintRequested(); }
}
void SWMMModelLayer::setShowSubcatchments(bool show)
{
    if (m_showSubcatchments != show) { m_showSubcatchments = show; m_needsRebuild = true; emit showSubcatchmentsChanged(show); emit repaintRequested(); }
}
void SWMMModelLayer::setShowRainGages(bool show)
{
    if (m_showRainGages != show) { m_showRainGages = show; m_needsRebuild = true; emit showRainGagesChanged(show); emit repaintRequested(); }
}
void SWMMModelLayer::setShowLabels(bool show)
{
    if (m_showLabels != show) { m_showLabels = show; m_needsRebuild = true; emit showLabelsChanged(show); emit repaintRequested(); }
}

bool SWMMModelLayer::isObjectVisible(const QString &name) const
{
    return !m_hiddenObjects.contains(name);
}

void SWMMModelLayer::setObjectVisible(const QString &name, bool visible)
{
    // Single-object visibility toggle routed through the same per-category
    // hidden-count bookkeeping that setObjectVisibleAt / setCategoryVisible
    // use. Keeps categoryCheckState() O(1) even when callers enter by
    // name (map tools, command-line edit paths, SelectionManager) rather
    // than by (category, row).
    bool changed = false;
    const auto it = m_objectLocation.constFind(name);
    const bool knownCat = it != m_objectLocation.constEnd();
    const Category cat  = knownCat ? it->first : CatJunctions;

    if (visible)
    {
        if (m_hiddenObjects.remove(name) > 0)
        {
            if (knownCat && m_hiddenCountByCategory[cat] > 0)
                --m_hiddenCountByCategory[cat];
            changed = true;
        }
    }
    else if (!m_hiddenObjects.contains(name))
    {
        m_hiddenObjects.insert(name);
        if (knownCat)
            ++m_hiddenCountByCategory[cat];
        changed = true;
    }
    if (changed)
    {
        rebuildFlagArrays();
        m_needsRebuild = true;
        emit repaintRequested();
    }
}

void SWMMModelLayer::setObjectsVisible(const QList<QString> &names, bool visible)
{
    bool changed = false;
    if (visible)
    {
        for (const QString &n : names)
        {
            if (m_hiddenObjects.remove(n) > 0)
            {
                const auto it = m_objectLocation.constFind(n);
                if (it != m_objectLocation.constEnd()
                    && m_hiddenCountByCategory[it->first] > 0)
                    --m_hiddenCountByCategory[it->first];
                changed = true;
            }
        }
    }
    else
    {
        for (const QString &n : names)
        {
            if (!m_hiddenObjects.contains(n))
            {
                m_hiddenObjects.insert(n);
                const auto it = m_objectLocation.constFind(n);
                if (it != m_objectLocation.constEnd())
                    ++m_hiddenCountByCategory[it->first];
                changed = true;
            }
        }
    }
    if (changed)
    {
        rebuildFlagArrays();
        m_needsRebuild = true;
        emit repaintRequested();
    }
}

// ---------------------------------------------------------------------------
// Category-aware API (consumed by SWMMObjectTreeModel)
// ---------------------------------------------------------------------------

double SWMMModelLayer::maxMarkerHalfBoundPx() const
{
    // Circles: half-bound = radius = size / 2.
    // Squares / triangles / diamonds: the visible bound a click has to
    // reach is the shape's outer vertex. For a size-N square the
    // worst case (corner) sits at N / 2 * sqrt(2) from the centre.
    // Legacy SWMM treats all glyphs as "within sym.size/2 of centre"
    // for hit-testing so this is conservatively larger than what the
    // renderer strictly needs — but that's the entire point: it
    // guarantees every pixel inside the rendered bounds is pickable.
    constexpr double kDiagScale = 1.41421356;   // sqrt(2)
    double best = 0.0;
    const auto consider = [&](const SWMMElementSymbol &s, double scale) {
        best = std::max(best, (s.size * 0.5) * scale);
    };
    consider(m_junctionSym, 1.0);          // circle
    consider(m_outfallSym,  kDiagScale);   // triangle (apex / base corner)
    consider(m_storageSym,  kDiagScale);   // square (corner)
    consider(m_dividerSym,  kDiagScale);   // diamond (axis tip)
    consider(m_gageSym,     kDiagScale);   // diamond
    return best;
}

int SWMMModelLayer::categoryCount(Category c) const
{
    // Overrides don't change membership, just display order — the
    // count is always the count of the underlying SoA bucket.
    switch (c) {
    case CatJunctions: case CatOutfalls: case CatStorage: case CatDividers:
        return m_nodesByType[int(c) - int(CatJunctions)].size();
    case CatConduits: case CatPumps: case CatOrifices:
    case CatWeirs:    case CatOutlets:
        return m_linksByType[int(c) - int(CatConduits)].size();
    case CatSubcatchments: return m_catchments.size();
    case CatRainGages:     return m_gages.size();
    default:               return 0;
    }
}

QString SWMMModelLayer::objectNameAt(Category c, int row) const
{
    if (row < 0) return {};

    // Intra-category override (Slice T.3): if set, visible `row` maps
    // straight through the user-defined permutation to the SoA index.
    const auto itOverride = m_objectOrderOverrides.constFind(c);
    if (itOverride != m_objectOrderOverrides.constEnd()) {
        const auto &ord = *itOverride;
        if (row >= ord.size()) return {};
        const int soaIdx = ord[row];
        switch (c) {
        case CatJunctions: case CatOutfalls: case CatStorage: case CatDividers:
            return (soaIdx >= 0 && soaIdx < m_nodes.size())
                ? m_nodes[soaIdx].name : QString();
        case CatConduits: case CatPumps: case CatOrifices:
        case CatWeirs:    case CatOutlets:
            return (soaIdx >= 0 && soaIdx < m_links.size())
                ? m_links[soaIdx].name : QString();
        case CatSubcatchments:
            return (soaIdx >= 0 && soaIdx < m_catchments.size())
                ? m_catchments[soaIdx].name : QString();
        case CatRainGages:
            return (soaIdx >= 0 && soaIdx < m_gages.size())
                ? m_gages[soaIdx].name : QString();
        default: return {};
        }
    }

    // Default path — per-category bucket built in rebuildCategoryIndex().
    switch (c) {
    case CatJunctions: case CatOutfalls: case CatStorage: case CatDividers: {
        const auto &b = m_nodesByType[int(c) - int(CatJunctions)];
        return (row < b.size()) ? m_nodes[b[row]].name : QString();
    }
    case CatConduits: case CatPumps: case CatOrifices:
    case CatWeirs:    case CatOutlets: {
        const auto &b = m_linksByType[int(c) - int(CatConduits)];
        return (row < b.size()) ? m_links[b[row]].name : QString();
    }
    case CatSubcatchments:
        return (row < m_catchments.size()) ? m_catchments[row].name : QString();
    case CatRainGages:
        return (row < m_gages.size()) ? m_gages[row].name : QString();
    default:
        return {};
    }
}

// ---------------------------------------------------------------------------
// Slice BM.0 — non-spatial Data Objects
// ---------------------------------------------------------------------------

namespace {

// Engine TableType enum mirror — kept local; the canonical definition
// lives at openswmm/engine/data/TableData.hpp. Used only to partition the
// unified tables array into "time series" vs "curves" for the Object
// Browser data section.
constexpr int kTableTypeTimeSeries = 0;

/*! Walk the engine's unified table list and collect zero-based indices
 *  that match either the TIMESERIES bucket (when \p curves == false) or
 *  any curve bucket (when \p curves == true). Returns an empty vector
 *  when the engine handle is null.
 *
 *  The walk is O(N_tables); BM.0 callers cache the result implicitly
 *  by virtue of `dataObjectCount` being called only on visible rows.
 *  If profiling shows this becomes a hot path (very large models),
 *  promote the partition to a member vector refreshed on modelLoaded. */
QVector<int> partitionTables(SWMM_Engine eng, bool curves)
{
    QVector<int> out;
    if (!eng) return out;
    const int n = swmm_table_count(eng);
    if (n <= 0) return out;
    out.reserve(n);
    for (int i = 0; i < n; ++i) {
        int type = -1;
        if (swmm_table_get_type(eng, i, &type) != SWMM_OK) continue;
        const bool isTs = (type == kTableTypeTimeSeries);
        if (curves ? !isTs : isTs) out.append(i);
    }
    return out;
}

} // anonymous

int SWMMModelLayer::dataObjectCount(DataCategory c) const
{
    if (!m_engine) return 0;
    switch (c) {
    case DataCurves:        return partitionTables(m_engine, /*curves=*/true).size();
    case DataTimeSeries:    return partitionTables(m_engine, /*curves=*/false).size();
    case DataPatterns:      return swmm_pattern_count(m_engine);
    case DataLIDControls:   return swmm_lid_count(m_engine);
    case DataPollutants:    return swmm_pollutant_count(m_engine);
    case DataLandUses:      return swmm_landuse_count(m_engine);
    case DataAquifers:      return swmm_aquifer_count(m_engine);
    case DataSnowpacks:     return swmm_snowpack_count(m_engine);
    case DataControls:      return swmm_control_count(m_engine);
    case DataTransects:     return swmm_transect_count(m_engine);
    // Slice DA.1 — return the *unique group* count, not the raw per-
    // (group, month, response) entry count. The engine surfaces it via
    // `swmm_hydrograph_group_count` (DA-ENG-01). The legacy
    // `swmm_hydrograph_count` returns parameter rows (12 or 36 per
    // group), which left the Object Browser showing mostly blank rows.
    case DataHydrographs:   return swmm_hydrograph_group_count(m_engine);
    case DataStreets:       return swmm_street_count(m_engine);
    case DataInlets:        return swmm_inlet_count(m_engine);
    default:                return 0;
    }
}

QString SWMMModelLayer::dataObjectNameAt(DataCategory c, int row) const
{
    if (!m_engine || row < 0) return {};

    auto nameOrEmpty = [](const char *p) -> QString {
        return p ? QString::fromUtf8(p) : QString();
    };

    switch (c) {
    case DataCurves: {
        const auto idxs = partitionTables(m_engine, /*curves=*/true);
        if (row >= idxs.size()) return {};
        return nameOrEmpty(swmm_table_id(m_engine, idxs[row]));
    }
    case DataTimeSeries: {
        const auto idxs = partitionTables(m_engine, /*curves=*/false);
        if (row >= idxs.size()) return {};
        return nameOrEmpty(swmm_table_id(m_engine, idxs[row]));
    }
    case DataPatterns:    return nameOrEmpty(swmm_pattern_id    (m_engine, row));
    case DataLIDControls: return nameOrEmpty(swmm_lid_id         (m_engine, row));
    case DataPollutants:  return nameOrEmpty(swmm_pollutant_id   (m_engine, row));
    case DataLandUses:    return nameOrEmpty(swmm_landuse_id     (m_engine, row));
    case DataAquifers:    return nameOrEmpty(swmm_aquifer_id     (m_engine, row));
    case DataSnowpacks:   return nameOrEmpty(swmm_snowpack_id    (m_engine, row));
    case DataControls: {
        // Slice DA.1 — surface the user-supplied RULE name via the new
        // engine accessor `swmm_control_get_id` (DA-ENG-02), which parses
        // the first token after the RULE keyword. Only fall back to a
        // sentinel "Rule N [unnamed]" when the rule text is malformed —
        // previously we always synthesised "Rule N" regardless of the
        // user-supplied identifier.
        char buf[128] = {};
        const int rc = swmm_control_get_id(m_engine, row, buf, sizeof(buf));
        if (rc == SWMM_OK) return QString::fromUtf8(buf);
        if (rc == SWMM_ERR_BADPARAM)
            return QObject::tr("Rule %1 [unnamed]").arg(row + 1);
        return {};
    }
    case DataTransects:   return nameOrEmpty(swmm_transect_id    (m_engine, row));
    case DataHydrographs: {
        // Slice DA.1 — group enumeration via `swmm_hydrograph_group_id`
        // (DA-ENG-01) replaces the prior iterate-and-dedup loop. The
        // engine already walks parameter entries + gage assignments in
        // first-occurrence order and emits the unique sequence.
        char buf[128] = {};
        if (swmm_hydrograph_group_id(m_engine, row, buf, sizeof(buf)) != SWMM_OK)
            return {};
        return QString::fromUtf8(buf);
    }
    case DataStreets: return nameOrEmpty(swmm_street_id(m_engine, row));
    case DataInlets:  return nameOrEmpty(swmm_inlet_id (m_engine, row));
    default:          return {};
    }
}

QString SWMMModelLayer::suggestUniqueDataObjectName(DataCategory c) const
{
    // Per-category prefix table — matches the convention specified in
    // docs/GUI_IMPLEMENTATION_PLAN.md slice DA.3.
    auto prefixFor = [](DataCategory dc) -> QString {
        switch (dc) {
        case DataCurves:      return QStringLiteral("Curve");
        case DataTimeSeries:  return QStringLiteral("TS");
        case DataPatterns:    return QStringLiteral("Pattern");
        case DataLIDControls: return QStringLiteral("LID");
        case DataPollutants:  return QStringLiteral("Pollut");
        case DataLandUses:    return QStringLiteral("LandUse");
        case DataAquifers:    return QStringLiteral("Aquifer");
        case DataSnowpacks:   return QStringLiteral("Snowpack");
        case DataControls:    return QStringLiteral("Rule");
        case DataTransects:   return QStringLiteral("Transect");
        case DataHydrographs: return QStringLiteral("UH");
        case DataStreets:     return QStringLiteral("Street");
        case DataInlets:      return QStringLiteral("Inlet");
        default:              return QStringLiteral("Object");
        }
    };

    const QString prefix = prefixFor(c);
    const int n = dataObjectCount(c);

    // Materialise the existing name set once (case-insensitive). For
    // typical N (< 1000) this is well within budget; suggestUniqueDataObjectName
    // runs at most once per New… dialog open.
    QSet<QString> existing;
    existing.reserve(n);
    for (int i = 0; i < n; ++i)
        existing.insert(dataObjectNameAt(c, i).toLower());

    for (int k = 1; ; ++k) {
        const QString candidate = QStringLiteral("%1%2").arg(prefix).arg(k);
        if (!existing.contains(candidate.toLower())) return candidate;
    }
}

// ---------------------------------------------------------------------------
// Slice DA.4.3 — engine-table filter helper for picker pop-up lists
// ---------------------------------------------------------------------------
QStringList SWMMModelLayer::tableIdsOfType(int tableType) const
{
    QStringList out;
    if (!m_engine) return out;
    const int n = swmm_table_count(m_engine);
    out.reserve(n);
    for (int i = 0; i < n; ++i) {
        int t = -1;
        if (swmm_table_get_type(m_engine, i, &t) != SWMM_OK) continue;
        const bool keep = (tableType < 0) ? (t != 0) : (t == tableType);
        if (!keep) continue;
        if (const char *id = swmm_table_id(m_engine, i))
            if (*id) out << QString::fromUtf8(id);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Slice DB.4b — createDataObject (extracted from ObjectBrowserPanel)
// ---------------------------------------------------------------------------
//
// Engine-commit switch for every non-spatial DataCategory. Promoted out
// of ObjectBrowserPanel so non-panel callers (e.g. the picker buttons in
// NodeCompoundEditDialog) can create a new time series / pattern / UH
// inline without dispatching through the browser. The panel's
// `addNewDataObject` is now a thin wrapper that calls this and then
// runs its own refresh + select side effects.

namespace {

/*! Canonical RULE skeleton text for the four pre-canned templates
 *  surfaced by NewDataObjectDialog's "skeleton" combo. Mirrors the
 *  former private helper of the same name in objectbrowserpanel.cpp. */
QString buildRuleSkeleton(const QString &skeleton, const QString &name)
{
    if (skeleton == QLatin1String("pump"))
        return QStringLiteral(
            "RULE %1\n"
            "IF NODE J1 DEPTH > 5.0\n"
            "THEN PUMP P1 STATUS = ON").arg(name);
    if (skeleton == QLatin1String("orifice"))
        return QStringLiteral(
            "RULE %1\n"
            "IF NODE J1 DEPTH > 5.0\n"
            "THEN ORIFICE O1 SETTING = 0.5").arg(name);
    if (skeleton == QLatin1String("weir"))
        return QStringLiteral(
            "RULE %1\n"
            "IF LINK W1 FLOW > 10.0\n"
            "THEN WEIR W1 SETTING = 0\n"
            "ELSE WEIR W1 SETTING = 1").arg(name);
    // "empty" or unknown → headerless template; user fills the body in.
    return QStringLiteral("RULE %1\n").arg(name);
}

} // namespace

bool SWMMModelLayer::createDataObject(DataCategory c,
                                       const QString &name,
                                       const QVariantMap &options,
                                       QString *outError)
{
    auto fail = [outError](const QString &msg) {
        if (outError) *outError = msg;
        return false;
    };

    SWMM_Engine eng = engine();
    if (!eng)               return fail(tr("No active engine handle."));
    if (name.trimmed().isEmpty()) return fail(tr("Name is required."));

    const QByteArray utf = name.toUtf8();
    const char     *idC  = utf.constData();

    // Per-type defaults match the legacy SWMM 5 first-entry-per-type
    // fallback when the caller didn't surface a control for the value.
    constexpr int kCurveTypeStorage     = 0;
    constexpr int kPatternTypeMonthly   = 0;
    constexpr int kLidTypeBioCell       = 0;
    constexpr int kPollutantUnitsMgPerL = 0;

    int rc = -1;
    switch (c) {
    case DataCurves: {
        const int t = options.value(QStringLiteral("curveType"),
                                     kCurveTypeStorage).toInt();
        rc = swmm_curve_add(eng, idC, t);
        break;
    }
    case DataTimeSeries:
        rc = swmm_timeseries_add(eng, idC);
        break;
    case DataPatterns: {
        const int t = options.value(QStringLiteral("patternType"),
                                     kPatternTypeMonthly).toInt();
        rc = swmm_pattern_add(eng, idC, t);
        break;
    }
    case DataLIDControls: {
        const int t = options.value(QStringLiteral("lidType"),
                                     kLidTypeBioCell).toInt();
        rc = swmm_lid_add(eng, idC, t);
        break;
    }
    case DataPollutants: {
        const int u = options.value(QStringLiteral("units"),
                                     kPollutantUnitsMgPerL).toInt();
        rc = swmm_pollutant_add(eng, idC, u);
        break;
    }
    case DataLandUses:    rc = swmm_landuse_add(eng, idC); break;
    case DataAquifers:    rc = swmm_aquifer_add(eng, idC); break;
    case DataSnowpacks:   rc = swmm_snowpack_add(eng, idC); break;
    case DataTransects:   rc = swmm_transect_add(eng, idC); break;
    case DataStreets:     rc = swmm_street_add(eng, idC); break;
    case DataInlets: {
        const QString t = options.value(QStringLiteral("inletType"),
                                          QStringLiteral("GRATE")).toString();
        const QByteArray tu = t.toUtf8();
        rc = swmm_inlet_add(eng, idC, tu.constData());
        break;
    }
    case DataControls: {
        const QString skeleton = options.value(QStringLiteral("skeleton"),
                                                 QStringLiteral("empty")).toString();
        const QByteArray body = buildRuleSkeleton(skeleton, name).toUtf8();
        rc = swmm_control_add_rule(eng, body.constData());
        break;
    }
    case DataHydrographs: {
        const QString gage = options.value(QStringLiteral("rainGage"))
                                    .toString().trimmed();
        const int response = options.value(QStringLiteral("response"), 0).toInt();
        if (!gage.isEmpty()) {
            const QByteArray gu = gage.toUtf8();
            swmm_hydrograph_add_gage(eng, idC, gu.constData());
        }
        rc = swmm_hydrograph_add(eng, idC, -1 /*ALL*/, response,
                                  0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
        break;
    }
    default:
        return fail(tr("Unsupported data category."));
    }

    if (rc != SWMM_OK)
        return fail(tr("Engine rejected create (code %1).").arg(rc));

    // Slice BS Phase 6.9.2 — generic create path doesn't go through the
    // applyHydrograph* MVC seam (it predates BS-02). Emit the signal here
    // so the editor, Object Browser, and any open property panel sync
    // when a hydrograph is created via NewDataObjectDialog.
    if (c == DataHydrographs)
        emit hydrographChanged(name);

    if (outError) outError->clear();
    return true;
}

// ---------------------------------------------------------------------------
// Slice T.3 — intra-category object order
// ---------------------------------------------------------------------------

QVector<int> SWMMModelLayer::objectOrder(Category c) const
{
    return m_objectOrderOverrides.value(c);
}

QVector<int> SWMMModelLayer::defaultObjectOrder(Category c) const
{
    switch (c) {
    case CatJunctions: case CatOutfalls: case CatStorage: case CatDividers:
        return m_nodesByType[int(c) - int(CatJunctions)];
    case CatConduits: case CatPumps: case CatOrifices:
    case CatWeirs:    case CatOutlets:
        return m_linksByType[int(c) - int(CatConduits)];
    case CatSubcatchments: {
        QVector<int> v; v.reserve(m_catchments.size());
        for (int i = 0; i < m_catchments.size(); ++i) v.append(i);
        return v;
    }
    case CatRainGages: {
        QVector<int> v; v.reserve(m_gages.size());
        for (int i = 0; i < m_gages.size(); ++i) v.append(i);
        return v;
    }
    default: return {};
    }
}

void SWMMModelLayer::clearObjectOrder(Category c)
{
    if (m_objectOrderOverrides.remove(c) == 0) return;
    // Rebuild m_objectLocation for this category against the default
    // bucket order, so findObjectLocation() returns the default row
    // the Object Browser now displays.
    const int n = categoryCount(c);
    for (int r = 0; r < n; ++r)
        m_objectLocation.insert(objectNameAt(c, r), {c, r});
    emit categoryOrderChanged();
}

void SWMMModelLayer::setObjectOrder(Category c, const QVector<int> &soaIndices)
{
    const int expected = categoryCount(c);
    if (soaIndices.size() != expected) return;

    // soaIndices must be a permutation of the default SoA index set
    // (guards against silent drop / duplicate from a malformed drag
    // or stale .oswp payload).
    const QVector<int> def = defaultObjectOrder(c);
    if (def.size() != expected) return;
    const QSet<int> defaults(def.cbegin(), def.cend());
    const QSet<int> given(soaIndices.cbegin(), soaIndices.cend());
    if (given != defaults) return;

    m_objectOrderOverrides.insert(c, soaIndices);

    // Rewrite m_objectLocation for this category so findObjectLocation
    // returns the new display row. Other categories untouched.
    for (int r = 0; r < soaIndices.size(); ++r) {
        const QString name = objectNameAt(c, r);
        if (!name.isEmpty())
            m_objectLocation.insert(name, {c, r});
    }

    emit categoryOrderChanged();
}

Qt::CheckState SWMMModelLayer::categoryCheckState(Category c) const
{
    const int total  = categoryCount(c);
    if (total <= 0) return Qt::Checked;
    const int hidden = m_hiddenCountByCategory[c];
    if (hidden == 0)      return Qt::Checked;
    if (hidden == total)  return Qt::Unchecked;
    return Qt::PartiallyChecked;
}

void SWMMModelLayer::setObjectVisibleAt(Category c, int row, bool visible)
{
    const QString name = objectNameAt(c, row);
    if (name.isEmpty()) return;

    bool changed = false;
    if (visible)
    {
        if (m_hiddenObjects.remove(name) > 0)
        {
            if (m_hiddenCountByCategory[c] > 0) --m_hiddenCountByCategory[c];
            changed = true;
        }
    }
    else if (!m_hiddenObjects.contains(name))
    {
        m_hiddenObjects.insert(name);
        ++m_hiddenCountByCategory[c];
        changed = true;
    }
    if (changed)
    {
        rebuildFlagArrays();
        m_needsRebuild = true;
        emit repaintRequested();
    }
}

void SWMMModelLayer::setCategoryVisible(Category c, bool visible)
{
    const int total = categoryCount(c);
    if (total <= 0) return;

    bool changed = false;
    for (int r = 0; r < total; ++r)
    {
        const QString name = objectNameAt(c, r);
        if (name.isEmpty()) continue;
        if (visible)
        {
            if (m_hiddenObjects.remove(name) > 0) changed = true;
        }
        else if (!m_hiddenObjects.contains(name))
        {
            m_hiddenObjects.insert(name);
            changed = true;
        }
    }

    // One-shot counter update — avoids NumCategories comparison per leaf.
    m_hiddenCountByCategory[c] = visible ? 0 : total;

    if (changed)
    {
        rebuildFlagArrays();
        m_needsRebuild = true;
        emit repaintRequested();
    }
}

bool SWMMModelLayer::findObjectLocation(const QString &name,
                                         Category *cat, int *row) const
{
    const auto it = m_objectLocation.constFind(name);
    if (it == m_objectLocation.constEnd()) return false;
    if (cat) *cat = it->first;
    if (row) *row = it->second;
    return true;
}

// ---------------------------------------------------------------------------
// Category index rebuild — called from buildGeometryCache() and after any
// SoA-mutating add/remove so the model + counters stay in sync.
// ---------------------------------------------------------------------------

QVector<SWMMModelLayer::Category> SWMMModelLayer::categoryOrder() const
{
    return m_categoryOrder;
}

void SWMMModelLayer::setCategoryOrder(const QVector<Category> &order)
{
    if (order.size() != int(NumCategories)) return;

    // Sanity-check: the vector must contain each enum value exactly
    // once. Otherwise a malformed input (from a stale .oswp or a drag
    // glitch) could silently hide a category or duplicate a header.
    std::array<int, NumCategories> counts = {};
    for (Category c : order) {
        if (int(c) < 0 || int(c) >= int(NumCategories)) return;
        ++counts[int(c)];
    }
    for (int c : counts) if (c != 1) return;

    if (order == m_categoryOrder) return;
    m_categoryOrder = order;
    emit categoryOrderChanged();
}

void SWMMModelLayer::rebuildCategoryIndex()
{
    for (auto &b : m_nodesByType) b.clear();
    for (auto &b : m_linksByType) b.clear();
    m_objectLocation.clear();
    m_nameToSoa.clear();
    m_nameToSoa.reserve(m_nodes.size() + m_links.size()
                        + m_catchments.size() + m_gages.size());
    // Drop intra-category overrides — the underlying SoA has been
    // rebuilt (add / remove / reload), so stored SoA indices could
    // now reference garbage.  .oswp restore will reinstall any
    // user-saved overrides after this routine returns.
    m_objectOrderOverrides.clear();

    // Seed the display-order vector to the enum sequence if empty
    // (fresh layer) — otherwise leave whatever the user picked
    // untouched across geometry rebuilds.
    if (m_categoryOrder.size() != int(NumCategories)) {
        m_categoryOrder.clear();
        m_categoryOrder.reserve(int(NumCategories));
        for (int i = 0; i < int(NumCategories); ++i)
            m_categoryOrder.append(static_cast<Category>(i));
    }

    // Nodes — bucket by nodeType (0..3). Unknown types fold into CatJunctions.
    for (int i = 0; i < m_nodes.size(); ++i)
    {
        const int t = (m_nodes[i].nodeType >= 0 && m_nodes[i].nodeType < 4)
                    ? m_nodes[i].nodeType : 0;
        const Category cat = Category(int(CatJunctions) + t);
        m_nodesByType[t].append(i);
        m_objectLocation.insert(m_nodes[i].name,
                                {cat, m_nodesByType[t].size() - 1});
        m_nameToSoa.insert(m_nodes[i].name, {SoaKind::Node, i});
    }

    // Links — linkType 0..4 matches Category 0..4 offset from CatConduits.
    //
    // SWMM allows a node and a link to share a name (separate namespaces
    // in the engine — e.g. an outfall "WWTP" and the conduit "WWTP"
    // draining into it).  Our `m_nameToSoa` / `m_objectLocation` are
    // single-keyed hashes, so we can only keep one entry per name.  When
    // a link's name collides with an already-inserted node, prefer the
    // node — visible node glyphs are the dominant click target for
    // selection / profile pick.  The link is still reachable by name
    // via `linkIndex(name)` (linear scan) and by engine index via
    // `swmm_link_index`, so internal flows that already know they're
    // looking at a link aren't affected.
    for (int i = 0; i < m_links.size(); ++i)
    {
        const int t = (m_links[i].linkType >= 0 && m_links[i].linkType < 5)
                    ? m_links[i].linkType : 0;
        const Category cat = Category(int(CatConduits) + t);
        m_linksByType[t].append(i);
        const QString &lname = m_links[i].name;
        if (!m_objectLocation.contains(lname))
            m_objectLocation.insert(lname,
                                    {cat, m_linksByType[t].size() - 1});
        if (!m_nameToSoa.contains(lname))
            m_nameToSoa.insert(lname, {SoaKind::Link, i});
    }

    // Subcatchments + gages are their own categories; row = SoA index.
    for (int i = 0; i < m_catchments.size(); ++i) {
        m_objectLocation.insert(m_catchments[i].name, {CatSubcatchments, i});
        m_nameToSoa.insert(m_catchments[i].name, {SoaKind::Catch, i});
    }
    for (int i = 0; i < m_gages.size(); ++i) {
        m_objectLocation.insert(m_gages[i].name, {CatRainGages, i});
        m_nameToSoa.insert(m_gages[i].name, {SoaKind::Gage, i});
    }

    // Recompute hidden-count per category from m_hiddenObjects (which
    // survives across reloads of the same file — re-derive from whatever
    // state is currently in the set).
    for (int &c : m_hiddenCountByCategory) c = 0;
    for (const QString &n : m_hiddenObjects)
    {
        const auto it = m_objectLocation.constFind(n);
        if (it != m_objectLocation.constEnd())
            ++m_hiddenCountByCategory[it->first];
    }
}

// ---------------------------------------------------------------------------
// Symbology
// ---------------------------------------------------------------------------

SWMMElementSymbol SWMMModelLayer::junctionSymbol()     const { return m_junctionSym; }
SWMMElementSymbol SWMMModelLayer::outfallSymbol()      const { return m_outfallSym; }
SWMMElementSymbol SWMMModelLayer::storageSymbol()      const { return m_storageSym; }
SWMMElementSymbol SWMMModelLayer::dividerSymbol()      const { return m_dividerSym; }
SWMMElementSymbol SWMMModelLayer::conduitSymbol()      const { return m_conduitSym; }
SWMMElementSymbol SWMMModelLayer::pumpSymbol()         const { return m_pumpSym; }
SWMMElementSymbol SWMMModelLayer::orificeSymbol()      const { return m_orificeSym; }
SWMMElementSymbol SWMMModelLayer::weirSymbol()         const { return m_weirSym; }
SWMMElementSymbol SWMMModelLayer::subcatchmentSymbol() const { return m_subcatchSym; }
SWMMElementSymbol SWMMModelLayer::rainGageSymbol()     const { return m_gageSym; }

void SWMMModelLayer::setJunctionSymbol(const SWMMElementSymbol &s)    { m_junctionSym   = s; m_needsRebuild = true; emit repaintRequested(); }
void SWMMModelLayer::setOutfallSymbol(const SWMMElementSymbol &s)     { m_outfallSym    = s; m_needsRebuild = true; emit repaintRequested(); }
void SWMMModelLayer::setStorageSymbol(const SWMMElementSymbol &s)     { m_storageSym    = s; m_needsRebuild = true; emit repaintRequested(); }
void SWMMModelLayer::setDividerSymbol(const SWMMElementSymbol &s)     { m_dividerSym    = s; m_needsRebuild = true; emit repaintRequested(); }
void SWMMModelLayer::setConduitSymbol(const SWMMElementSymbol &s)     { m_conduitSym    = s; m_needsRebuild = true; emit repaintRequested(); }
void SWMMModelLayer::setPumpSymbol(const SWMMElementSymbol &s)        { m_pumpSym       = s; m_needsRebuild = true; emit repaintRequested(); }
void SWMMModelLayer::setOrificeSymbol(const SWMMElementSymbol &s)     { m_orificeSym    = s; m_needsRebuild = true; emit repaintRequested(); }
void SWMMModelLayer::setWeirSymbol(const SWMMElementSymbol &s)        { m_weirSym       = s; m_needsRebuild = true; emit repaintRequested(); }
void SWMMModelLayer::setSubcatchmentSymbol(const SWMMElementSymbol &s){ m_subcatchSym   = s; m_needsRebuild = true; emit repaintRequested(); }
void SWMMModelLayer::setRainGageSymbol(const SWMMElementSymbol &s)    { m_gageSym       = s; m_needsRebuild = true; emit repaintRequested(); }

// ---------------------------------------------------------------------------
// Renderer (Slice BI Phase 8.13.6.5)
// ---------------------------------------------------------------------------
//
// API additions only. The paint loop in SWMMLayerItem still reads m_*Sym
// directly; sub-phase 8.13.6.4 (deferred until Slice BB lands ColorRamp)
// will swap the paint loop to consult m_renderer instead.

OpenSWMM::Render::IFeatureRenderer *SWMMModelLayer::renderer() const
{
    return m_renderer.get();
}

void SWMMModelLayer::setRenderer(std::unique_ptr<OpenSWMM::Render::IFeatureRenderer> r)
{
    if (!r)                                 // contract: renderer() never returns null
        return;
    if (r.get() == m_renderer.get())
        return;                             // same-pointer self-assignment is a no-op
    m_renderer = std::move(r);
    emit rendererChanged();
}

// ---------------------------------------------------------------------------
// Flow-direction arrows (Slice BI Phase 8.13.8-mini, 2026-05-24)
// ---------------------------------------------------------------------------

bool SWMMModelLayer::linkArrowsEnabled(Category c) const
{
    switch (c) {
    case CatConduits: return m_conduitSym.showArrows;
    case CatPumps:    return m_pumpSym.showArrows;
    case CatOrifices: return m_orificeSym.showArrows;
    case CatWeirs:    return m_weirSym.showArrows;
    case CatOutlets:  return m_outletSym.showArrows;  // Slice FX.1
    default:          return false;
    }
}

void SWMMModelLayer::setLinkArrowsEnabled(Category c, bool enabled)
{
    SWMMElementSymbol *sym = nullptr;
    switch (c) {
    case CatConduits: sym = &m_conduitSym; break;
    case CatPumps:    sym = &m_pumpSym;    break;
    case CatOrifices: sym = &m_orificeSym; break;
    case CatWeirs:    sym = &m_weirSym;    break;
    case CatOutlets:  sym = &m_outletSym;  break;  // Slice FX.1
    default: return;
    }
    if (sym->showArrows == enabled) return;
    sym->showArrows = enabled;
    emit repaintRequested();
}

// Slice FX.1 — per-kind arrow style getters/setters. Inline switch
// mirrors the linkArrowsEnabled/setLinkArrowsEnabled pattern above.
double SWMMModelLayer::linkArrowSize(Category c) const
{
    switch (c) {
    case CatConduits: return m_conduitSym.arrowSize;
    case CatPumps:    return m_pumpSym.arrowSize;
    case CatOrifices: return m_orificeSym.arrowSize;
    case CatWeirs:    return m_weirSym.arrowSize;
    case CatOutlets:  return m_outletSym.arrowSize;
    default:          return 10.0;
    }
}

void SWMMModelLayer::setLinkArrowSize(Category c, double pixels)
{
    SWMMElementSymbol *sym = nullptr;
    switch (c) {
    case CatConduits: sym = &m_conduitSym; break;
    case CatPumps:    sym = &m_pumpSym;    break;
    case CatOrifices: sym = &m_orificeSym; break;
    case CatWeirs:    sym = &m_weirSym;    break;
    case CatOutlets:  sym = &m_outletSym;  break;
    default: return;
    }
    if (sym->arrowSize == pixels) return;
    sym->arrowSize = pixels;
    emit repaintRequested();
}

QColor SWMMModelLayer::linkArrowColor(Category c) const
{
    switch (c) {
    case CatConduits: return m_conduitSym.arrowColor;
    case CatPumps:    return m_pumpSym.arrowColor;
    case CatOrifices: return m_orificeSym.arrowColor;
    case CatWeirs:    return m_weirSym.arrowColor;
    case CatOutlets:  return m_outletSym.arrowColor;
    default:          return QColor(34, 34, 34);
    }
}

void SWMMModelLayer::setLinkArrowColor(Category c, const QColor &col)
{
    if (!col.isValid()) return;
    SWMMElementSymbol *sym = nullptr;
    switch (c) {
    case CatConduits: sym = &m_conduitSym; break;
    case CatPumps:    sym = &m_pumpSym;    break;
    case CatOrifices: sym = &m_orificeSym; break;
    case CatWeirs:    sym = &m_weirSym;    break;
    case CatOutlets:  sym = &m_outletSym;  break;
    default: return;
    }
    if (sym->arrowColor == col) return;
    sym->arrowColor = col;
    emit repaintRequested();
}

bool SWMMModelLayer::linkArrowOnlyWhenFlowPos(Category c) const
{
    switch (c) {
    case CatConduits: return m_conduitSym.arrowOnlyWhenFlowPos;
    case CatPumps:    return m_pumpSym.arrowOnlyWhenFlowPos;
    case CatOrifices: return m_orificeSym.arrowOnlyWhenFlowPos;
    case CatWeirs:    return m_weirSym.arrowOnlyWhenFlowPos;
    case CatOutlets:  return m_outletSym.arrowOnlyWhenFlowPos;
    default:          return false;
    }
}

void SWMMModelLayer::setLinkArrowOnlyWhenFlowPos(Category c, bool onlyPos)
{
    SWMMElementSymbol *sym = nullptr;
    switch (c) {
    case CatConduits: sym = &m_conduitSym; break;
    case CatPumps:    sym = &m_pumpSym;    break;
    case CatOrifices: sym = &m_orificeSym; break;
    case CatWeirs:    sym = &m_weirSym;    break;
    case CatOutlets:  sym = &m_outletSym;  break;
    default: return;
    }
    if (sym->arrowOnlyWhenFlowPos == onlyPos) return;
    sym->arrowOnlyWhenFlowPos = onlyPos;
    emit repaintRequested();
}

double SWMMModelLayer::linkFlow(int linkIdx) const
{
    if (!m_engine || linkIdx < 0) return 0.0;
    double v = 0.0;
    if (swmm_link_get_flow(m_engine, linkIdx, &v) != 0) return 0.0;
    return v;
}

// ---------------------------------------------------------------------------
// Per-kind renderer plumbing (Slice BI-MK.1 / BI-MK.LT, 2026-05-24)
// ---------------------------------------------------------------------------

QString SWMMModelLayer::kindKey(Category c)
{
    switch (c) {
    case CatJunctions:     return QStringLiteral("Junctions");
    case CatOutfalls:      return QStringLiteral("Outfalls");
    case CatStorage:       return QStringLiteral("Storage");
    case CatDividers:      return QStringLiteral("Dividers");
    case CatConduits:      return QStringLiteral("Conduits");
    case CatPumps:         return QStringLiteral("Pumps");
    case CatOrifices:      return QStringLiteral("Orifices");
    case CatWeirs:         return QStringLiteral("Weirs");
    case CatOutlets:       return QStringLiteral("Outlets");
    case CatSubcatchments: return QStringLiteral("Subcatchments");
    case CatRainGages:     return QStringLiteral("RainGages");
    case NumCategories:    break;
    }
    return {};
}

OpenSWMM::Render::IFeatureRenderer *SWMMModelLayer::kindRenderer(Category c) const
{
    const size_t idx = static_cast<size_t>(c);
    if (idx >= m_kindRenderers.size())
        return nullptr;
    return m_kindRenderers[idx].get();
}

void SWMMModelLayer::setKindRenderer(
    Category c,
    std::unique_ptr<OpenSWMM::Render::IFeatureRenderer> r)
{
    if (!r) return;
    const size_t idx = static_cast<size_t>(c);
    if (idx >= m_kindRenderers.size()) return;

    // When the new renderer is a SingleSymbol, write its colour / outline /
    // size back to the legacy SWMMElementSymbol field for that kind so the
    // existing bucketed paint loop reflects the change. For Graduated /
    // Categorized / Rule-based renderers, we just store; canvas paint stays
    // at the prior single-symbol look until the Phase 8.13.6.4 refactor
    // routes paint through symbolFor().
    if (auto *ssr = dynamic_cast<OpenSWMM::Render::SingleSymbolRenderer *>(r.get()))
    {
        const SymbolStyle &style = ssr->symbol();
        switch (c) {
        case CatJunctions:     setJunctionSymbol(    elementSymbolFromStyle(style, m_junctionSym));    break;
        case CatOutfalls:      setOutfallSymbol(     elementSymbolFromStyle(style, m_outfallSym));     break;
        case CatStorage:       setStorageSymbol(     elementSymbolFromStyle(style, m_storageSym));     break;
        case CatDividers:      setDividerSymbol(     elementSymbolFromStyle(style, m_dividerSym));     break;
        case CatConduits:      setConduitSymbol(     elementSymbolFromStyle(style, m_conduitSym));     break;
        case CatPumps:         setPumpSymbol(        elementSymbolFromStyle(style, m_pumpSym));        break;
        case CatOrifices:      setOrificeSymbol(     elementSymbolFromStyle(style, m_orificeSym));     break;
        case CatWeirs:         setWeirSymbol(        elementSymbolFromStyle(style, m_weirSym));        break;
        case CatOutlets:       /* no legacy field — store renderer only */                              break;
        case CatSubcatchments: setSubcatchmentSymbol(elementSymbolFromStyle(style, m_subcatchSym));    break;
        case CatRainGages:     setRainGageSymbol(    elementSymbolFromStyle(style, m_gageSym));        break;
        case NumCategories:    break;
        }
    }

    m_kindRenderers[idx] = std::move(r);

    // Phase 8.13.6.4 + 8.13.43-α — rebuildKindFeatureColors decides on its
    // own whether overrides are active: it samples the renderer's output
    // for every feature and flags overrides when per-feature color OR
    // per-feature size varies (covering both Graduated/Categorized and
    // SingleSymbol with data-defined size).
    rebuildKindFeatureColors(c);

    emit rendererChanged();
    emit repaintRequested();
}

bool SWMMModelLayer::kindUsesOverrides(Category c) const
{
    if (c < 0 || c >= NumCategories) return false;
    return m_kindUsesOverrides[c];
}

QColor SWMMModelLayer::featureColor(Category c, int idx) const
{
    if (c < 0 || c >= NumCategories) return {};
    const auto &v = m_kindFeatureColors[c];
    if (idx < 0 || idx >= v.size()) return {};
    return v[idx];
}

double SWMMModelLayer::featureSize(Category c, int idx) const
{
    if (c < 0 || c >= NumCategories) return -1.0;
    const auto &v = m_kindFeatureSizes[c];
    if (idx < 0 || idx >= v.size()) return -1.0;
    return v[idx];   // negative sentinel = no override
}

void SWMMModelLayer::rebuildKindFeatureColors(Category c)
{
    if (c < 0 || c >= NumCategories) return;
    m_kindFeatureColors[c].clear();
    m_kindFeatureSizes[c].clear();
    m_kindUsesOverrides[c] = false;

    auto *r = kindRenderer(c);
    if (!r) return;
    // Slice BI Phase 8.13.43-α — even a SingleSymbol renderer may carry a
    // data-defined size override; only short-circuit when the renderer is
    // a Single WITHOUT data-defined size. We detect by sampling the
    // symbolFor output for feature 0 below — if the resolved size differs
    // across two probe features, we know an override is active.
    const bool isSingle = (r->rendererId() == QStringLiteral("single"));

    const int n = categoryCount(c);
    if (n <= 0) return;

    // Slice FX.2 — cache is indexed by **SoA index** (m_nodes / m_links /
    // m_catchments / m_gages), not by per-category row. The painter
    // builds buckets using SoA indices (see swmmlayeritem.cpp), so the
    // lookup has to match. Previously the cache was sized to the
    // per-category count and indexed by the kind-row iteration variable
    // `i`, which caused size-by-attribute to silently miss every node
    // beyond the first few junctions (and corrupt other categories'
    // lookups by aliasing the same indices).
    int soaSize = 0;
    switch (c) {
    case CatJunctions: case CatOutfalls: case CatStorage: case CatDividers:
        soaSize = m_nodes.size(); break;
    case CatConduits: case CatPumps: case CatOrifices:
    case CatWeirs:    case CatOutlets:
        soaSize = m_links.size(); break;
    case CatSubcatchments: soaSize = m_catchments.size(); break;
    case CatRainGages:     soaSize = m_gages.size();      break;
    default: return;
    }

    m_kindFeatureColors[c].resize(soaSize);
    m_kindFeatureSizes [c].resize(soaSize);
    m_kindFeatureSizes [c].fill(-1.0);  // -1 sentinel = no override

    using OpenSWMM::Render::FeatureRef;
    const QString kind = kindKey(c);

    auto soaIndexFor = [&](int row) -> int {
        switch (c) {
        case CatJunctions: case CatOutfalls: case CatStorage: case CatDividers: {
            const auto &b = m_nodesByType[int(c) - int(CatJunctions)];
            return (row < b.size()) ? b[row] : -1;
        }
        case CatConduits: case CatPumps: case CatOrifices:
        case CatWeirs:    case CatOutlets: {
            const auto &b = m_linksByType[int(c) - int(CatConduits)];
            return (row < b.size()) ? b[row] : -1;
        }
        case CatSubcatchments: return row;   // single SoA
        case CatRainGages:     return row;
        default: return -1;
        }
    };

    bool anyColorOverride = false;
    bool anySizeOverride  = false;
    double firstSize = -1.0;
    for (int i = 0; i < n; ++i)
    {
        const int soa = soaIndexFor(i);
        if (soa < 0 || soa >= soaSize) continue;

        const QString name = objectNameAt(c, i);
        const QVariantMap attrs = identifyByName(name);

        FeatureRef ref;
        ref.layerId      = QStringLiteral("swmm_model");
        ref.featureIndex = soa;     // pass SoA index so renderers can correlate
        ref.categoryHint = kind;

        const auto style = r->symbolFor(ref, attrs);
        QColor col;
        double sz = -1.0;
        if (!style.layers.isEmpty())
        {
            const auto &props = style.layers.first().props;

            const QVariant cv = props.value(QStringLiteral("color"));
            if (cv.isValid())
            {
                const QColor parsed(cv.toString());
                if (parsed.isValid()) col = parsed;
            }
            // Extract per-feature size if the renderer wrote one (Graduated
            // with outputSizeEnabled or SingleSymbol with sizeData set).
            QVariant sv = props.value(QStringLiteral("size"));
            if (!sv.isValid()) sv = props.value(QStringLiteral("width"));
            bool ok = false;
            const double v = sv.toDouble(&ok);
            if (ok && v > 0.0) sz = v;
        }
        m_kindFeatureColors[c][soa] = col;
        m_kindFeatureSizes [c][soa] = sz;

        if (col.isValid()) anyColorOverride = true;
        if (i == 0) firstSize = sz;
        else if (sz > 0.0 && (firstSize < 0.0 || std::abs(sz - firstSize) > 1e-6))
            anySizeOverride = true;
    }

    // Mark overrides active when either a non-single renderer produced
    // distinct per-feature output, OR a single renderer with sizeData
    // produced varying sizes. Single-without-overrides falls back to the
    // legacy bucketed paint path.
    if (!isSingle) {
        m_kindUsesOverrides[c] = true;
    } else if (anySizeOverride) {
        // SingleSymbol + data-defined size — clear the color cache so the
        // painter uses the legacy color but per-feature size.
        m_kindFeatureColors[c].fill(QColor());
        m_kindUsesOverrides[c] = true;
    } else {
        // SingleSymbol without overrides → drop the caches entirely.
        m_kindFeatureColors[c].clear();
        m_kindFeatureSizes[c].clear();
    }
    (void)anyColorOverride;   // reserved for future smarter routing
}

void SWMMModelLayer::resetKindRendererToDefaults(Category c)
{
    const size_t idx = static_cast<size_t>(c);
    if (idx >= m_kindRenderers.size()) return;

    // Re-seed from the current legacy m_*Sym fields (which themselves
    // hold the compile-time factory defaults until the user edits them).
    // The dialog's "Reset Kind to Defaults" button hits this path.
    SWMMElementSymbol seed;
    switch (c) {
    case CatJunctions:     seed = m_junctionSym; break;
    case CatOutfalls:      seed = m_outfallSym;  break;
    case CatStorage:       seed = m_storageSym;  break;
    case CatDividers:      seed = m_dividerSym;  break;
    case CatConduits:      seed = m_conduitSym;  break;
    case CatPumps:         seed = m_pumpSym;     break;
    case CatOrifices:      seed = m_orificeSym;  break;
    case CatWeirs:         seed = m_weirSym;     break;
    case CatOutlets: {
        SWMMElementSymbol outletDefault;
        outletDefault.fillColor    = QColor(140, 100, 60);
        outletDefault.outlineWidth = 1.5;
        seed = outletDefault;
        break;
    }
    case CatSubcatchments: seed = m_subcatchSym; break;
    case CatRainGages:     seed = m_gageSym;     break;
    case NumCategories:    return;
    }
    m_kindRenderers[idx] = makeSingleSymbolRenderer(seed, c);
    emit rendererChanged();
    emit repaintRequested();
}

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------

QStringList SWMMModelLayer::selectedElementNames() const { return m_selectedNames; }

void SWMMModelLayer::setSelectedElementNames(const QStringList &names)
{
    if (names == m_selectedNames)
        return;
    QElapsedTimer t; t.start();
    m_selectedNames = names;
    rebuildFlagArrays();
    const qint64 t_flags = t.elapsed();
    // Selection does not change geometry — SWMMLayerItem::paint() reads
    // m_*SelectedFlag live each frame, so a repaint is sufficient.
    // Flipping m_needsRebuild here would force depopulate/populate of the
    // batched layer item on every rubber-band tick (see refreshScene()),
    // which is the dominant cost on large models (100k+ links).
    emit selectionChanged(names);
    const qint64 t_emit = t.elapsed() - t_flags;
    emit repaintRequested();
    qDebug().noquote() << "[setSelectedElementNames] count=" << names.size()
                       << " flags_ms=" << t_flags
                       << " emit_ms=" << t_emit
                       << " total_ms=" << t.elapsed();
}

void SWMMModelLayer::clearSelection()
{
    setSelectedElementNames({});
}

// ---------------------------------------------------------------------------
// Identify
// ---------------------------------------------------------------------------

QVariantMap SWMMModelLayer::identifyAt(double mapX, double mapY,
                                        double tolerance) const
{
    return identifyAt(mapX, mapY, nullptr, tolerance);
}

QVariantMap SWMMModelLayer::identifyByName(const QString &name) const
{
    QVariantMap m;
    if (name.isEmpty()) return m;

    auto findNode = [&]() -> int {
        for (int i = 0; i < m_nodes.size(); ++i)
            if (m_nodes[i].name == name) return i;
        return -1;
    };
    auto findLink = [&]() -> int {
        for (int i = 0; i < m_links.size(); ++i)
            if (m_links[i].name == name) return i;
        return -1;
    };
    auto findCatch = [&]() -> int {
        for (int i = 0; i < m_catchments.size(); ++i)
            if (m_catchments[i].name == name) return i;
        return -1;
    };
    auto findGage = [&]() -> int {
        for (int i = 0; i < m_gages.size(); ++i)
            if (m_gages[i].name == name) return i;
        return -1;
    };

    if (int i = findNode(); i >= 0)
    {
        const NodeGeom &n = m_nodes[i];
        m[QStringLiteral("Type")] = QStringLiteral("Node");
        m[QStringLiteral("Name")] = n.name;
        m[QStringLiteral("X")]    = n.x;
        m[QStringLiteral("Y")]    = n.y;
        const char *kinds[] = {"Junction", "Outfall", "Storage", "Divider"};
        if (n.nodeType >= 0 && n.nodeType <= 3)
            m[QStringLiteral("Node type")] = QString::fromLatin1(kinds[n.nodeType]);

        // Slice DB — read-only computed + statistics summary fields. Crown
        // elev / full volume / degree are input-time properties (computed
        // when links connect). The stat_* values are populated only after
        // a simulation run; pre-run they read back as zero. The attribute
        // table and property browser surface these so users can see node
        // results without opening the Status Report.
        if (m_engine) {
            const int idx = swmm_node_index(m_engine, n.name.toUtf8().constData());
            if (idx >= 0) {
                double v = 0.0; int iv = 0;
                if (swmm_node_get_crown_elev(m_engine, idx, &v) == SWMM_OK)
                    m[QStringLiteral("Crown elev")] = v;
                if (swmm_node_get_full_volume(m_engine, idx, &v) == SWMM_OK)
                    m[QStringLiteral("Full volume")] = v;
                if (swmm_node_get_degree(m_engine, idx, &iv) == SWMM_OK)
                    m[QStringLiteral("Degree")] = iv;
                if (swmm_node_get_stat_max_depth(m_engine, idx, &v) == SWMM_OK)
                    m[QStringLiteral("Max depth (stat)")] = v;
                if (swmm_node_get_stat_max_overflow(m_engine, idx, &v) == SWMM_OK)
                    m[QStringLiteral("Max overflow")] = v;
                if (swmm_node_get_stat_vol_flooded(m_engine, idx, &v) == SWMM_OK)
                    m[QStringLiteral("Vol flooded")] = v;
                if (swmm_node_get_stat_time_flooded(m_engine, idx, &v) == SWMM_OK)
                    m[QStringLiteral("Time flooded (hr)")] = v;
            }
        }
        return m;
    }
    if (int i = findLink(); i >= 0)
    {
        const LinkGeom &l = m_links[i];
        m[QStringLiteral("Type")] = QStringLiteral("Link");
        m[QStringLiteral("Name")] = l.name;
        const char *kinds[] = {"Conduit", "Pump", "Orifice", "Weir", "Outlet"};
        if (l.linkType >= 0 && l.linkType <= 4)
            m[QStringLiteral("Link type")] = QString::fromLatin1(kinds[l.linkType]);
        m[QStringLiteral("Vertex count")] = cachedLinkPolyline(i).size();
        return m;
    }
    if (int i = findCatch(); i >= 0)
    {
        const CatchGeom &c = m_catchments[i];
        m[QStringLiteral("Type")] = QStringLiteral("Subcatchment");
        m[QStringLiteral("Name")] = c.name;
        m[QStringLiteral("Polygon vertices")] = c.vertices.size();
        return m;
    }
    if (int i = findGage(); i >= 0)
    {
        const NodeGeom &g = m_gages[i];
        m[QStringLiteral("Type")] = QStringLiteral("Rain Gage");
        m[QStringLiteral("Name")] = g.name;
        m[QStringLiteral("X")]    = g.x;
        m[QStringLiteral("Y")]    = g.y;
        return m;
    }
    return m;
}

MapExtent SWMMModelLayer::objectExtent(const QString &name) const
{
    // A default-constructed MapExtent is all zeros — indistinguishable
    // from a node that sits at the origin. Use a NaN sentinel for "not
    // found" so callers can reliably tell unknown-object from degenerate
    // zero-point via std::isfinite / MapExtent::isValid (isValid fails
    // for non-finite bounds).
    const double kNaN = std::numeric_limits<double>::quiet_NaN();
    const MapExtent kUnknown(kNaN, kNaN, kNaN, kNaN);
    if (name.isEmpty()) return kUnknown;

    SWMMModelLayer::Category cat;
    int row = 0;
    if (!findObjectLocation(name, &cat, &row))
        return kUnknown;

    auto bboxOf = [&](const auto &pts) -> MapExtent {
        if (pts.isEmpty()) return kUnknown;
        double x0 = pts.first().x(), x1 = x0;
        double y0 = pts.first().y(), y1 = y0;
        for (const auto &p : pts) {
            if (p.x() < x0) x0 = p.x();
            if (p.x() > x1) x1 = p.x();
            if (p.y() < y0) y0 = p.y();
            if (p.y() > y1) y1 = p.y();
        }
        return {x0, y0, x1, y1};
    };

    switch (cat) {
    case CatJunctions: case CatOutfalls: case CatStorage: case CatDividers: {
        const int idx = m_nodesByType[int(cat) - int(CatJunctions)].value(row, -1);
        if (idx < 0) return kUnknown;
        const auto &n = m_nodes[idx];
        return {n.x, n.y, n.x, n.y};
    }
    case CatConduits: case CatPumps: case CatOrifices:
    case CatWeirs:    case CatOutlets: {
        const int idx = m_linksByType[int(cat) - int(CatConduits)].value(row, -1);
        if (idx < 0) return kUnknown;
        return bboxOf(cachedLinkPolyline(idx));
    }
    case CatSubcatchments:
        if (row < 0 || row >= m_catchments.size()) return kUnknown;
        return bboxOf(m_catchments[row].vertices);
    case CatRainGages:
        if (row < 0 || row >= m_gages.size()) return kUnknown;
        return {m_gages[row].x, m_gages[row].y,
                m_gages[row].x, m_gages[row].y};
    default:
        return kUnknown;
    }
}

QVariantMap SWMMModelLayer::identifyAt(double mapX, double mapY,
                                        const SpatialReferenceSystem * /*canvasSRS*/,
                                        double tolerance) const
{
    // CRS-aware hit-testing.
    //
    // SoA coordinates (m_nodes[i].x/y, etc.) are stored in the LAYER's
    // native CRS. The caller's (mapX, mapY) is in CANVAS CRS (the
    // MapToolSelect converted from pixel → canvas-map via the view
    // transform). When the two CRSes differ, raw (n.x - mapX) is
    // nonsense — the distances aren't comparable and nothing ever
    // falls inside `tolerance` — which is why users felt nodes were
    // "impossible to select" after reprojecting a layer.
    //
    // Fix: convert the click into LAYER coords once per call via the
    // inverse of the layer's forward transform (layer → canvas), then
    // compare against the stored SoA coords. `tolerance` is interpreted
    // in LAYER units — MapToolSelect already passes the layer-unit
    // equivalent of its pixel tolerance, so that side's already right.
    double clickLX = mapX, clickLY = mapY;
    double tolerLayer = tolerance;    // default: caller-supplied units
    if (m_transform) {
        auto *inv = m_transform->GetInverse();
        if (inv) {
            // Transform click and a tolerance-offset point; the offset's
            // layer-space delta is the correct tolerance to compare
            // against layer-unit distances below.
            double offX = mapX + tolerance, offY = mapY + tolerance;
            inv->Transform(1, &clickLX, &clickLY);
            inv->Transform(1, &offX,    &offY);
            OGRCoordinateTransformation::DestroyCT(inv);
            tolerLayer = std::max(std::abs(offX - clickLX),
                                   std::abs(offY - clickLY));
        }
    }

    // Tiered priority — matches what users expect from a SWMM editor:
    //
    //   1. NODES + RAIN GAGES (generous tolerance — tolerLayer).
    //      If any point feature is within tolerance, pick the nearest.
    //      Links and subcatchments passing near the click NEVER steal
    //      the pick from a point feature, because previously that
    //      meant a conduit hugging a junction always won the
    //      "closest" race even when the user clicked squarely on the
    //      junction's glyph.
    //
    //   2. LINKS (TIGHTER tolerance — tolerLayer / 3). Only considered
    //      when no node / gage was in range. Users reported links were
    //      "too easy" — the pick had to be near the drawn stroke, not
    //      anywhere in the neighbourhood.
    //
    //   3. SUBCATCHMENTS (point-in-polygon only). No tolerance — the
    //      click has to land inside the polygon. Matches the "works
    //      perfectly" feedback.
    const double linkTolerLayer = tolerLayer / 3.0;
    QVariantMap best;

    // --- Tier 1: nodes + gages (KD-tree O(log N + k)) -------------------
    {
        ensureKdTrees();
        double bestDist2 = tolerLayer * tolerLayer;
        const double qpt[2] = { clickLX, clickLY };

        auto searchTree = [&](const Kd2 *tree,
                              const QVector<NodeGeom> &src,
                              const char *elemType)
        {
            if (!tree || src.isEmpty()) return;
            std::vector<nanoflann::ResultItem<uint32_t, double>> matches;
            tree->radiusSearch(qpt, bestDist2, matches,
                               nanoflann::SearchParameters());
            for (const auto &hit : matches)
            {
                if (hit.second >= bestDist2) continue;
                const int i = static_cast<int>(hit.first);
                if (m_hiddenObjects.contains(src[i].name)) continue;
                bestDist2 = hit.second;
                best.clear();
                best[QStringLiteral("elementType")] = QString::fromLatin1(elemType);
                best[QStringLiteral("elementName")] = src[i].name;
                best[QStringLiteral("x")]           = src[i].x;
                best[QStringLiteral("y")]           = src[i].y;
            }
        };

        const Kd2 *nodeTree = m_kdTrees ? m_kdTrees->nodeTree.get() : nullptr;
        const Kd2 *gageTree = m_kdTrees ? m_kdTrees->gageTree.get() : nullptr;
        searchTree(nodeTree, m_nodes, "Node");
        searchTree(gageTree, m_gages, "RainGage");

        if (!best.isEmpty()) return best;
    }

    // --- Tier 2: links (tighter tolerance) -----------------------------
    {
        double bestDist2 = linkTolerLayer * linkTolerLayer;
        for (int li = 0; li < m_links.size(); ++li)
        {
            const LinkGeom &l = m_links[li];
            if (m_hiddenObjects.contains(l.name)) continue;
            const QVector<QPointF> verts = cachedLinkPolyline(li);
            for (int i = 1; i < verts.size(); ++i)
            {
                const double ax = verts[i - 1].x(), ay = verts[i - 1].y();
                const double bx = verts[i].x(),     by = verts[i].y();
                const double vx = bx - ax,           vy = by - ay;
                const double wx = clickLX - ax,      wy = clickLY - ay;
                const double len2 = vx * vx + vy * vy;
                double t = len2 > 0.0 ? (vx * wx + vy * wy) / len2 : 0.0;
                if (t < 0.0) t = 0.0; else if (t > 1.0) t = 1.0;
                const double px = ax + t * vx, py = ay + t * vy;
                const double dx = px - clickLX, dy = py - clickLY;
                const double d2 = dx * dx + dy * dy;
                if (d2 < bestDist2)
                {
                    bestDist2 = d2;
                    best.clear();
                    best[QStringLiteral("elementType")] = QStringLiteral("Link");
                    best[QStringLiteral("elementName")] = l.name;
                }
            }
        }
        if (!best.isEmpty()) return best;
    }

    // --- Tier 3: subcatchments (point-in-polygon) ----------------------
    for (const CatchGeom &c : m_catchments)
    {
        if (m_hiddenObjects.contains(c.name)) continue;
        const auto &v = c.vertices;
        if (v.size() < 3) continue;
        bool inside = false;
        for (int i = 0, j = v.size() - 1; i < v.size(); j = i++)
        {
            const double xi = v[i].x(), yi = v[i].y();
            const double xj = v[j].x(), yj = v[j].y();
            const bool crosses = ((yi > clickLY) != (yj > clickLY)) &&
                (clickLX < (xj - xi) * (clickLY - yi) / (yj - yi + 1e-20) + xi);
            if (crosses) inside = !inside;
        }
        if (inside)
        {
            best.clear();
            best[QStringLiteral("elementType")] = QStringLiteral("Subcatchment");
            best[QStringLiteral("elementName")] = c.name;
            break;
        }
    }

    return best;
}

// ---------------------------------------------------------------------------
// Scene population
// ---------------------------------------------------------------------------

void SWMMModelLayer::populateScene(QGraphicsScene *scene,
                                    const MapExtent &canvasExtent,
                                    const SpatialReferenceSystem * /*canvasSRS*/)
{
    qDebug().noquote() << QStringLiteral("[populateScene] visible=%1 nodes=%2 links=%3 catch=%4 gages=%5 needsRebuild=%6 show(N/L/S/G)=%7/%8/%9/%10 hidden=%11")
                              .arg(isVisible() ? "yes" : "no")
                              .arg(m_nodes.size()).arg(m_links.size())
                              .arg(m_catchments.size()).arg(m_gages.size())
                              .arg(m_needsRebuild ? "yes" : "no")
                              .arg(m_showNodes).arg(m_showLinks)
                              .arg(m_showSubcatchments).arg(m_showRainGages)
                              .arg(m_hiddenObjects.size());

    if (!isVisible())
        return;

    const double baseZ = layerZValue();

    // Slice R Phase 3: the scene now carries ONE batched `SWMMLayerItem`
    // per layer — no per-object `NodeGraphicsItem` / `LinkGraphicsItem`
    // / `CatchmentGraphicsItem` placeholders. Every interactive tool
    // (MoveNode, EditVertex, Select, Identify, etc.) hit-tests through
    // the layer's `pickAt` / `identifyAt` APIs, so the placeholders are
    // no longer needed for hit-testing either. Memory for 1 M-object
    // models drops from > 1 GB (per-item approach) to the SoA +
    // batched-item footprint (~200 MB per the Slice R memory target).
    m_batchedItem = new SWMMLayerItem(this);
    m_batchedItem->setZValue(baseZ);
    scene->addItem(m_batchedItem);
}

void SWMMModelLayer::depopulateScene(QGraphicsScene *scene)
{
    if (!scene)
        return;

    // Post Slice R Phase 3: only the batched `SWMMLayerItem` lives in
    // the scene on our behalf — no per-object placeholders. Picking out
    // "our" batched item is a single dynamic_cast + ownerLayer check.
    const auto items = scene->items();
    for (auto *item : items)
    {
        if (auto *b = dynamic_cast<SWMMLayerItem *>(item); b && b->ownerLayer() == this) {
            scene->removeItem(item);
            delete item;
        }
    }
    m_batchedItem = nullptr;

    // After depopulation the scene no longer carries this layer's item;
    // a subsequent refreshScene() must rebuild from the geometry cache
    // rather than short-circuit on `!m_needsRebuild`. This is what makes
    // the visibility checkbox round-trip correctly (off → on repopulates
    // the network without a manual extent change to force a rebuild).
    m_needsRebuild = true;
}

void SWMMModelLayer::onCanvasCRSChanged(const SpatialReferenceSystem *newCanvasSRS)
{
    rebuildTransform(newCanvasSRS);
    m_needsRebuild = true;
}

void SWMMModelLayer::refreshScene(QGraphicsScene *scene,
                                   const MapExtent &canvasExtent,
                                   const SpatialReferenceSystem *canvasSRS)
{
    if (!m_needsRebuild)
        return;  // Items already in scene at correct positions

    depopulateScene(scene);
    populateScene(scene, canvasExtent, canvasSRS);
    m_needsRebuild = false;
}

void SWMMModelLayer::reloadGeometry()
{
    buildGeometryCache();
    emit repaintRequested();
}

int SWMMModelLayer::objectTypeFor(const QString &name) const
{
    // Match SWMMObjectRef::ObjectType integer values: 1=Node, 2=Link,
    // 3=Subcatchment, 4=RainGage. Avoid pulling the selection header into
    // this layer header to keep the dependency one-way (project window
    // bridges between the two).
    const auto it = m_objectLocation.constFind(name);
    if (it == m_objectLocation.constEnd()) return 0;
    const Category c = it.value().first;
    if (c <= CatDividers)      return 1;  // Junctions / Outfalls / Storage / Dividers
    if (c <= CatOutlets)       return 2;  // Conduits / Pumps / Orifices / Weirs / Outlets
    if (c == CatSubcatchments) return 3;
    if (c == CatRainGages)     return 4;
    return 0;
}

// ---------------------------------------------------------------------------
// Geometry editing API (Phase 2)
// ---------------------------------------------------------------------------

int SWMMModelLayer::nodeIndex(const QString &name) const
{
    for (int i = 0; i < m_nodes.size(); ++i)
        if (m_nodes[i].name == name) return i;
    return -1;
}

int SWMMModelLayer::linkIndex(const QString &name) const
{
    for (int i = 0; i < m_links.size(); ++i)
        if (m_links[i].name == name) return i;
    return -1;
}

bool SWMMModelLayer::cachedNodeCoord(int idx, double *x, double *y) const
{
    if (idx < 0 || idx >= m_nodes.size())
        return false;
    if (x) *x = m_nodes[idx].x;
    if (y) *y = m_nodes[idx].y;
    return true;
}

bool SWMMModelLayer::elementPosition(const QString &name, double *x, double *y) const
{
    const auto it = m_nameToSoa.constFind(name);
    if (it == m_nameToSoa.constEnd())
        return false;

    const SoaLocation &loc = it.value();
    switch (loc.kind)
    {
    case SoaKind::Node:
    case SoaKind::Gage:
        return cachedNodeCoord(loc.soaIdx, x, y);

    case SoaKind::Link:
    {
        const QVector<QPointF> &poly = cachedLinkPolyline(loc.soaIdx);
        if (poly.isEmpty())
            return false;
        // Return the midpoint of the polyline.
        const int mid = poly.size() / 2;
        if (x) *x = poly[mid].x();
        if (y) *y = poly[mid].y();
        return true;
    }

    case SoaKind::Catch:
    {
        // Return the centroid stored as the first vertex of the polygon,
        // or the average of all vertices if only interior points exist.
        const MapExtent ext = objectExtent(name);
        if (!std::isfinite(ext.xMin()))
            return false;
        if (x) *x = (ext.xMin() + ext.xMax()) * 0.5;
        if (y) *y = (ext.yMin() + ext.yMax()) * 0.5;
        return true;
    }
    }
    return false;
}

QVector<QPointF> SWMMModelLayer::cachedLinkPolyline(int idx) const
{
    if (idx < 0 || idx >= m_links.size()) return {};
    const LinkGeom &lg = m_links[idx];
    QVector<QPointF> result;
    result.reserve(lg.vertices.size() + 2);
    if (lg.fromNodeIdx >= 0 && lg.fromNodeIdx < m_nodes.size())
        result.append(QPointF(m_nodes[lg.fromNodeIdx].x, m_nodes[lg.fromNodeIdx].y));
    result.append(lg.vertices);
    if (lg.toNodeIdx >= 0 && lg.toNodeIdx < m_nodes.size())
        result.append(QPointF(m_nodes[lg.toNodeIdx].x, m_nodes[lg.toNodeIdx].y));
    return result;
}

QVector<QPointF> SWMMModelLayer::cachedLinkInteriorVertices(int idx) const
{
    if (idx < 0 || idx >= m_links.size())
        return {};
    // vertices stores interior bend points only — return directly.
    return m_links[idx].vertices;
}

QVector<QPointF> SWMMModelLayer::cachedSubcatchVertices(int idx) const
{
    if (idx < 0 || idx >= m_catchments.size())
        return {};
    return m_catchments[idx].vertices;
}

int SWMMModelLayer::cachedSubcatchCount() const
{
    return m_catchments.size();
}

SWMMModelLayer::PickResult
SWMMModelLayer::pickAt(double sceneX, double sceneY, double tolerance) const
{
    // Reuse identifyAt — it already does CRS-aware, tiered-priority
    // hit-testing, and returns both the element type and the name.
    // This wrapper just translates the result into the typed struct
    // tools consume.
    const QVariantMap hit = identifyAt(sceneX, sceneY, nullptr, tolerance);
    PickResult r;
    const QString name = hit.value(QStringLiteral("elementName")).toString();
    const QString type = hit.value(QStringLiteral("elementType")).toString();
    if (name.isEmpty()) return r;

    // Resolve category using the elementType hint from identifyAt rather
    // than going through the name-keyed m_objectLocation, which can
    // collide when the SWMM model has a node and a link sharing a name
    // (e.g. an outfall "WWTP" and the conduit "WWTP" draining into it).
    // In that case the second insertion wins, so an outfall click would
    // return cat=CatConduits and the Select / Profile tools would treat
    // the click as a link hit.  Looking up the SoA directly by name
    // within the correct kind avoids the ambiguity entirely.
    bool resolved = false;
    if (type == QLatin1String("Node")) {
        const int ni = nodeIndex(name);
        if (ni >= 0) {
            const int nt = (m_nodes[ni].nodeType >= 0 && m_nodes[ni].nodeType < 4)
                         ? m_nodes[ni].nodeType : 0;
            r.cat      = Category(int(CatJunctions) + nt);
            r.soaIndex = ni;
            resolved   = true;
        }
    } else if (type == QLatin1String("Link")) {
        const int li = linkIndex(name);
        if (li >= 0) {
            const int lt = (m_links[li].linkType >= 0 && m_links[li].linkType < 5)
                         ? m_links[li].linkType : 0;
            r.cat      = Category(int(CatConduits) + lt);
            r.soaIndex = li;
            resolved   = true;
        }
    } else if (type == QLatin1String("Subcatchment")) {
        for (int i = 0; i < m_catchments.size(); ++i) {
            if (m_catchments[i].name == name) {
                r.cat      = CatSubcatchments;
                r.soaIndex = i;
                resolved   = true;
                break;
            }
        }
    } else if (type == QLatin1String("RainGage")) {
        for (int i = 0; i < m_gages.size(); ++i) {
            if (m_gages[i].name == name) {
                r.cat      = CatRainGages;
                r.soaIndex = i;
                resolved   = true;
                break;
            }
        }
    }

    // Fallback for unknown/missing type strings — preserve the legacy
    // path so callers that bypass identifyAt's tier metadata still work.
    if (!resolved && !findObjectLocation(name, &r.cat, &r.soaIndex))
        return r;
    // When we resolved by elementType directly, soaIndex is already the
    // SoA-level index — skip the display-row mapping below.
    if (resolved) {
        r.valid = true;
        r.name  = name;
        return r;
    }

    // `soaIndex` returned by findObjectLocation is the display row
    // (reflects any Slice T.3 override). Tools expect the SoA-level
    // index so they can reach m_nodes / m_links directly — map
    // through the override if one's installed.
    const auto it = m_objectOrderOverrides.constFind(r.cat);
    if (it != m_objectOrderOverrides.constEnd()
        && r.soaIndex >= 0 && r.soaIndex < it->size())
        r.soaIndex = (*it)[r.soaIndex];
    else {
        // No override — the default display row maps through
        // m_nodesByType / m_linksByType for sub-typed categories.
        switch (r.cat) {
        case CatJunctions: case CatOutfalls: case CatStorage: case CatDividers: {
            const auto &b = m_nodesByType[int(r.cat) - int(CatJunctions)];
            if (r.soaIndex < b.size()) r.soaIndex = b[r.soaIndex];
            break;
        }
        case CatConduits: case CatPumps: case CatOrifices:
        case CatWeirs:    case CatOutlets: {
            const auto &b = m_linksByType[int(r.cat) - int(CatConduits)];
            if (r.soaIndex < b.size()) r.soaIndex = b[r.soaIndex];
            break;
        }
        default: break;   // Subcatchments / Gages: display row == SoA index
        }
    }

    r.valid = true;
    r.name  = name;
    return r;
}

bool SWMMModelLayer::previewNodeMove(int idx, double newX, double newY)
{
    if (idx < 0 || idx >= m_nodes.size()) return false;

    m_nodes[idx].x = newX;
    m_nodes[idx].y = newY;
    refreshSceneCoordsForNode(idx);

    // Link endpoints are looked up dynamically from m_nodes[], so just
    // refresh scene coords for all attached links. Engine state is
    // UNTOUCHED — MoveNodeCommand::redo commits via applyNodeMove on release.
    for (int linkIdx : linksAttachedToNode(idx))
        refreshSceneCoordsForLink(linkIdx);

    // Update outlet lines of subcatchments that drain to this node so the
    // dashed connector line follows the node during the drag preview.
    refreshCatchOutletLinesForNode(idx);

    // Repaint; m_needsRebuild intentionally left unset so the scene's
    // batched item keeps its existing z-value / bounding rect. The
    // batched renderer re-reads coords on every paint anyway, so the
    // preview appears at the new position on the next frame.
    emit repaintRequested();
    return true;
}

double SWMMModelLayer::engineLinkLength(int linkIdx) const
{
    if (!m_engine || !isConduit(linkIdx))
        return -1.0;
    double len = 0.0;
    if (swmm_link_get_length(m_engine, linkIdx, &len) != 0)
        return -1.0;
    return len;
}

double SWMMModelLayer::polylineLengthInModelUnits(
    const QVector<QPointF> &vertices) const
{
    if (vertices.size() < 2) return 0.0;

    auto *crs = srs();

    // Sum segment lengths in metres. For geographic CRSes we use WGS-84
    // great-circle distance per segment so lon/lat polylines don't get
    // multiplied by a meaningless degrees-to-metres factor.
    double metres = 0.0;
    if (crs && crs->isGeographic()) {
        constexpr double R = 6378137.0; // WGS-84 sphere
        for (int i = 1; i < vertices.size(); ++i) {
            const double lon1 = qDegreesToRadians(vertices[i - 1].x());
            const double lat1 = qDegreesToRadians(vertices[i - 1].y());
            const double lon2 = qDegreesToRadians(vertices[i].x());
            const double lat2 = qDegreesToRadians(vertices[i].y());
            const double dlat = lat2 - lat1;
            const double dlon = lon2 - lon1;
            const double a    = std::sin(dlat / 2) * std::sin(dlat / 2)
                              + std::cos(lat1) * std::cos(lat2)
                              * std::sin(dlon / 2) * std::sin(dlon / 2);
            metres += R * 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
        }
    } else {
        const double raw = EditGeometry::polylineLength(vertices);
        const double toMetres = (crs && crs->isProjected())
                                    ? crs->linearUnitsToMetres()
                                    : 1.0;
        metres = raw * toMetres;
    }

    // SWMM stores conduit length in the FLOW_UNITS system's linear unit:
    // feet for US-customary (CFS/GPM/MGD), metres for SI (CMS/LPS/MLD).
    const QString fu = getOption(QByteArrayLiteral("FLOW_UNITS"),
                                 QStringLiteral("CFS")).toUpper();
    const bool isSI = (fu == QLatin1String("CMS")
                       || fu == QLatin1String("LPS")
                       || fu == QLatin1String("MLD"));
    return isSI ? metres : metres / 0.3048;
}

// ---------------------------------------------------------------------------
// OPTIONS pass-through (Slice U MVC entry points)
// ---------------------------------------------------------------------------

QString SWMMModelLayer::getOption(const QByteArray &key,
                                   const QString    &fallback) const
{
    if (!m_engine || key.isEmpty()) return fallback;
    char buf[512] = {};
    if (swmm_options_get(m_engine, key.constData(), buf, sizeof(buf)) != 0)
        return fallback;
    if (buf[0] == '\0') return fallback;
    return QString::fromUtf8(buf).trimmed();
}

bool SWMMModelLayer::setOption(const QByteArray &key, const QString &value)
{
    if (!m_engine || key.isEmpty()) return false;
    const QByteArray valUtf8 = value.toUtf8();
    if (swmm_options_set(m_engine, key.constData(), valUtf8.constData()) != 0)
        return false;
    emit optionsChanged({QString::fromLatin1(key)});
    return true;
}

int SWMMModelLayer::setOptions(const QMap<QByteArray, QString> &values)
{
    if (!m_engine || values.isEmpty()) return 0;
    QStringList written;
    written.reserve(values.size());
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        const QByteArray valUtf8 = it.value().toUtf8();
        if (swmm_options_set(m_engine, it.key().constData(), valUtf8.constData()) == 0)
            written << QString::fromLatin1(it.key());
    }
    if (!written.isEmpty())
        emit optionsChanged(written);
    return written.size();
}

bool SWMMModelLayer::isConduit(int linkIdx) const
{
    // SWMM_LINK_CONDUIT == 0 in openswmm_links.h
    if (linkIdx < 0 || linkIdx >= m_links.size())
        return false;
    return m_links[linkIdx].linkType == 0;
}

QVector<int> SWMMModelLayer::linksAttachedToNode(int nodeIdx) const
{
    QVector<int> result;
    if (!m_engine || nodeIdx < 0 || nodeIdx >= m_nodes.size())
        return result;
    for (int i = 0; i < m_links.size(); ++i)
    {
        int fromIdx = -1, toIdx = -1;
        swmm_link_get_from_node(m_engine, i, &fromIdx);
        swmm_link_get_to_node  (m_engine, i, &toIdx);
        if (fromIdx == nodeIdx || toIdx == nodeIdx)
            result.append(i);
    }
    return result;
}

int SWMMModelLayer::linkEndForNode(int linkIdx, int nodeIdx) const
{
    if (!m_engine || linkIdx < 0 || linkIdx >= m_links.size())
        return -1;
    int fromIdx = -1, toIdx = -1;
    swmm_link_get_from_node(m_engine, linkIdx, &fromIdx);
    swmm_link_get_to_node  (m_engine, linkIdx, &toIdx);
    if (fromIdx == nodeIdx) return 0;
    if (toIdx   == nodeIdx) return 1;
    return -1;
}

int SWMMModelLayer::linkFromNodeIdx(int linkIdx) const
{
    if (!m_engine || linkIdx < 0 || linkIdx >= m_links.size())
        return -1;
    int idx = -1;
    swmm_link_get_from_node(m_engine, linkIdx, &idx);
    return idx;
}

int SWMMModelLayer::linkToNodeIdx(int linkIdx) const
{
    if (!m_engine || linkIdx < 0 || linkIdx >= m_links.size())
        return -1;
    int idx = -1;
    swmm_link_get_to_node(m_engine, linkIdx, &idx);
    return idx;
}

bool SWMMModelLayer::applyNodeMove(int idx, double newX, double newY)
{
    if (idx < 0 || idx >= m_nodes.size())
        return false;

    if (m_engine)
    {
        if (swmm_spatial_set_node_coord(m_engine, idx, newX, newY) != 0)
            return false;
    }

    m_nodes[idx].x = newX;
    m_nodes[idx].y = newY;
    refreshSceneCoordsForNode(idx);

    // Link endpoints are looked up dynamically from m_nodes[], so just
    // refresh scene coords and bbox for all attached links.
    const QVector<int> attached = linksAttachedToNode(idx);
    for (int linkIdx : attached)
    {
        // Recompute bbox from the full polyline (uses updated node position).
        if (linkIdx < m_linkBboxes.size()) {
            const QVector<QPointF> full = cachedLinkPolyline(linkIdx);
            if (!full.isEmpty()) {
                double x0 = full.first().x(), x1 = x0;
                double y0 = full.first().y(), y1 = y0;
                for (const QPointF &p : full) {
                    if (p.x() < x0) x0 = p.x(); else if (p.x() > x1) x1 = p.x();
                    if (p.y() < y0) y0 = p.y(); else if (p.y() > y1) y1 = p.y();
                }
                m_linkBboxes[linkIdx] = MapExtent(x0, y0, x1, y1);
            }
        }
        refreshSceneCoordsForLink(linkIdx);
    }

    // Keep outlet lines pointing at the committed node position.
    refreshCatchOutletLinesForNode(idx);

    m_kdDirty = true;
    m_needsRebuild = true;
    emit repaintRequested();
    return true;
}

bool SWMMModelLayer::applyLinkLength(int linkIdx, double length)
{
    if (!m_engine || !isConduit(linkIdx))
        return false;
    if (swmm_link_set_length(m_engine, linkIdx, length) != 0)
        return false;
    if (linkIdx >= 0 && linkIdx < m_links.size())
        emit attributeChanged(m_links[linkIdx].name);
    return true;
}

bool SWMMModelLayer::applyNodeAdd(const QString &name, int nodeType,
                                   double x, double y, int *outIdx)
{
    if (outIdx) *outIdx = -1;
    if (name.isEmpty()) return false;
    if (!m_engine) return false;

    const QByteArray idUtf8 = name.toUtf8();
    int rc = swmm_node_add(m_engine, idUtf8.constData(), nodeType);
    if (rc != 0) return false;

    const int idx = swmm_node_index(m_engine, idUtf8.constData());
    if (idx < 0) return false;

    if (swmm_spatial_set_node_coord(m_engine, idx, x, y) != 0)
    {
        // Roll back the engine-side add so callers don't observe a
        // half-added node. The cache is still clean at this point.
        swmm_node_pop_last(m_engine, idUtf8.constData());
        return false;
    }

    NodeGeom g;
    g.name       = name;
    g.nodeType   = nodeType;
    g.objectType = 0;
    g.x          = x;
    g.y          = y;
    m_nodes.append(g);
    const int newSoaIdx = m_nodes.size() - 1;

    if (outIdx) *outIdx = idx;

    // m_nodes changed → category index buckets + name→(cat,row) map go
    // stale. Rebuild before emitting repaintRequested so the Object
    // Browser model sees a coherent snapshot on the next data() cycle.
    rebuildCategoryIndex();

    // Populate the scene-coord entry for the new node so SWMMLayerItem::paint()
    // finds it at nps[newSoaIdx] on the very next frame. Without this call,
    // m_nodeScenePts is shorter than m_nodes and the paint loop skips the
    // new node (line: `if (i >= nps.size()) continue`).
    refreshSceneCoordsForNode(newSoaIdx);

    // Grow the selection / hidden flag arrays to match the new m_nodes size
    // so the bounds-checked reads in paint() apply the right state to the new
    // node from the outset.
    if (m_nodeSelectedFlag.size() < size_t(m_nodes.size()))
        m_nodeSelectedFlag.resize(m_nodes.size(), 0);
    if (m_nodeHiddenFlag.size() < size_t(m_nodes.size()))
        m_nodeHiddenFlag.resize(m_nodes.size(), 0);

    m_kdDirty = true;
    m_needsRebuild = true;
    emit repaintRequested();
    emit geometryChanged();
    return true;
}

bool SWMMModelLayer::rollbackTailNodeAdd(const QString &name)
{
    if (m_nodes.isEmpty() || m_nodes.last().name != name)
        return false;
    if (!m_engine) return false;

    const QByteArray idUtf8 = name.toUtf8();
    if (swmm_node_pop_last(m_engine, idUtf8.constData()) != 0)
        return false;

    m_nodes.removeLast();
    rebuildCategoryIndex();

    // Trim the scene-coord and flag arrays to match the reduced m_nodes size.
    const int n = m_nodes.size();
    if (m_nodeScenePts.size()       > n) m_nodeScenePts.resize(n);
    if (int(m_nodeSelectedFlag.size()) > n) m_nodeSelectedFlag.resize(n);
    if (int(m_nodeHiddenFlag.size())   > n) m_nodeHiddenFlag.resize(n);

    m_kdDirty = true;
    m_needsRebuild = true;
    emit repaintRequested();
    emit geometryChanged();
    return true;
}

// ---------------------------------------------------------------------------
// Link add / rollback
// ---------------------------------------------------------------------------

bool SWMMModelLayer::applyLinkAdd(const QString &name, int linkType,
                                   const QString &fromNodeName,
                                   const QString &toNodeName,
                                   const QVector<QPointF> &interiorVertices,
                                   int *outIdx)
{
    if (outIdx) *outIdx = -1;
    if (name.isEmpty() || !m_engine) return false;

    const QByteArray idUtf8  = name.toUtf8();
    const QByteArray fromUtf8 = fromNodeName.toUtf8();
    const QByteArray toUtf8   = toNodeName.toUtf8();

    if (swmm_link_add(m_engine, idUtf8.constData(), linkType) != 0)
        return false;

    const int idx = swmm_link_index(m_engine, idUtf8.constData());
    if (idx < 0) { swmm_link_pop_last(m_engine, idUtf8.constData()); return false; }

    const int n1 = swmm_node_index(m_engine, fromUtf8.constData());
    const int n2 = swmm_node_index(m_engine, toUtf8.constData());
    if (n1 < 0 || n2 < 0) { swmm_link_pop_last(m_engine, idUtf8.constData()); return false; }
    swmm_link_set_nodes(m_engine, idx, n1, n2);

    // Store interior vertices in engine.
    if (!interiorVertices.isEmpty()) {
        QVector<double> vx(interiorVertices.size()), vy(interiorVertices.size());
        for (int i = 0; i < interiorVertices.size(); ++i) {
            vx[i] = interiorVertices[i].x();
            vy[i] = interiorVertices[i].y();
        }
        swmm_spatial_set_link_vertices(m_engine, idx,
                                        vx.constData(), vy.constData(),
                                        interiorVertices.size());
    }

    // Build cache entry — interior vertices only; node endpoints are looked
    // up dynamically from m_nodes[] via fromNodeIdx / toNodeIdx.
    LinkGeom g;
    g.name        = name;
    g.linkType    = linkType;
    g.fromNodeIdx = n1;
    g.toNodeIdx   = n2;
    g.vertices    = interiorVertices;   // interior only, no node endpoints
    m_links.append(g);
    if (outIdx) *outIdx = idx;

    buildGeometryCache();
    m_kdDirty = true;
    m_needsRebuild = true;
    emit repaintRequested();
    emit geometryChanged();
    return true;
}

bool SWMMModelLayer::rollbackTailLinkAdd(const QString &name)
{
    if (m_links.isEmpty() || m_links.last().name != name) return false;
    if (!m_engine) return false;

    const QByteArray idUtf8 = name.toUtf8();
    if (swmm_link_pop_last(m_engine, idUtf8.constData()) != 0) return false;

    m_links.removeLast();
    buildGeometryCache();
    m_kdDirty = true;
    m_needsRebuild = true;
    emit repaintRequested();
    emit geometryChanged();
    return true;
}

// ---------------------------------------------------------------------------
// Gage add / rollback
// ---------------------------------------------------------------------------

bool SWMMModelLayer::applyGageAdd(const QString &name, double x, double y,
                                   int *outIdx)
{
    if (outIdx) *outIdx = -1;
    if (name.isEmpty() || !m_engine) return false;

    const QByteArray idUtf8 = name.toUtf8();
    if (swmm_gage_add(m_engine, idUtf8.constData()) != 0) return false;

    const int idx = swmm_gage_index(m_engine, idUtf8.constData());
    if (idx < 0) { swmm_gage_delete(m_engine, m_gages.size(), nullptr); return false; }

    if (swmm_spatial_set_gage_coord(m_engine, idx, x, y) != 0) {
        swmm_gage_delete(m_engine, idx, nullptr);
        return false;
    }

    NodeGeom g;
    g.name       = name;
    g.objectType = 4;   // RainGage object type constant in SWMMModelLayer
    g.nodeType   = 0;
    g.x          = x;
    g.y          = y;
    m_gages.append(g);
    if (outIdx) *outIdx = idx;

    rebuildCategoryIndex();
    m_kdDirty = true;
    m_needsRebuild = true;
    emit repaintRequested();
    emit geometryChanged();
    return true;
}

bool SWMMModelLayer::rollbackTailGageAdd(const QString &name)
{
    if (m_gages.isEmpty() || m_gages.last().name != name) return false;
    if (!m_engine) return false;

    const int idx = m_gages.size() - 1;
    if (swmm_gage_delete(m_engine, idx, nullptr) != 0) return false;

    m_gages.removeLast();
    rebuildCategoryIndex();
    m_kdDirty = true;
    m_needsRebuild = true;
    emit repaintRequested();
    emit geometryChanged();
    return true;
}

// ---------------------------------------------------------------------------
// Subcatchment add / rollback
// ---------------------------------------------------------------------------

bool SWMMModelLayer::applySubcatchAdd(const QString &name,
                                       const QVector<QPointF> &polygon,
                                       int *outIdx)
{
    if (outIdx) *outIdx = -1;
    if (name.isEmpty() || !m_engine) return false;

    const QByteArray idUtf8 = name.toUtf8();
    if (swmm_subcatch_add(m_engine, idUtf8.constData()) != 0) return false;

    const int idx = swmm_subcatch_index(m_engine, idUtf8.constData());
    if (idx < 0) { swmm_subcatch_delete(m_engine, m_catchments.size(), nullptr); return false; }

    if (!polygon.isEmpty()) {
        QVector<double> vx(polygon.size()), vy(polygon.size());
        for (int i = 0; i < polygon.size(); ++i) {
            vx[i] = polygon[i].x();
            vy[i] = polygon[i].y();
        }
        swmm_spatial_set_subcatch_polygon(m_engine, idx,
                                           vx.constData(), vy.constData(),
                                           polygon.size());
        double cx = 0, cy = 0;
        for (const QPointF &p : polygon) { cx += p.x(); cy += p.y(); }
        cx /= polygon.size(); cy /= polygon.size();
        swmm_spatial_set_subcatch_coord(m_engine, idx, cx, cy);
    }

    CatchGeom g;
    g.name     = name;
    g.vertices = polygon;
    m_catchments.append(g);
    if (outIdx) *outIdx = idx;

    buildGeometryCache();
    m_kdDirty = true;
    m_needsRebuild = true;
    emit repaintRequested();
    emit geometryChanged();
    return true;
}

bool SWMMModelLayer::rollbackTailSubcatchAdd(const QString &name)
{
    if (m_catchments.isEmpty() || m_catchments.last().name != name) return false;
    if (!m_engine) return false;

    const int idx = m_catchments.size() - 1;
    if (swmm_subcatch_delete(m_engine, idx, nullptr) != 0) return false;

    m_catchments.removeLast();
    buildGeometryCache();
    m_kdDirty = true;
    m_needsRebuild = true;
    emit repaintRequested();
    emit geometryChanged();
    return true;
}

// ---------------------------------------------------------------------------
// Rename
// ---------------------------------------------------------------------------

bool SWMMModelLayer::applyRename(const QString &oldName, const QString &newName)
{
    if (oldName.isEmpty() || newName.isEmpty() || oldName == newName) return false;
    if (!m_engine) return false;

    const QByteArray oldUtf8 = oldName.toUtf8();
    const QByteArray newUtf8 = newName.toUtf8();
    const char *oldId = oldUtf8.constData();
    const char *newId = newUtf8.constData();

    // Find which category this element belongs to and rename in the engine.
    int rc = SWMM_ERR_BADPARAM;
    bool isNode  = false, isLink = false, isCatch = false, isGage = false;

    const int nodeIdx  = swmm_node_index(m_engine, oldId);
    const int linkIdx  = nodeIdx < 0 ? swmm_link_index(m_engine, oldId) : -1;
    const int gageIdx  = (nodeIdx < 0 && linkIdx < 0)
                             ? swmm_gage_index(m_engine, oldId) : -1;
    const int catchIdx = (nodeIdx < 0 && linkIdx < 0 && gageIdx < 0)
                             ? swmm_subcatch_index(m_engine, oldId) : -1;

    if      (nodeIdx  >= 0) { rc = swmm_node_rename(m_engine, nodeIdx, newId);  isNode  = true; }
    else if (linkIdx  >= 0) { rc = swmm_link_rename(m_engine, linkIdx, newId);  isLink  = true; }
    else if (gageIdx  >= 0) { rc = swmm_gage_rename(m_engine, gageIdx, newId);  isGage  = true; }
    else if (catchIdx >= 0) { rc = swmm_subcatch_rename(m_engine, catchIdx, newId); isCatch = true; }

    if (rc != SWMM_OK) return false;

    // Update GUI geometry caches.
    if (isNode) {
        if (nodeIdx < m_nodes.size()) m_nodes[nodeIdx].name = newName;
    } else if (isLink) {
        if (linkIdx < m_links.size()) m_links[linkIdx].name = newName;
    } else if (isGage) {
        if (gageIdx < m_gages.size()) m_gages[gageIdx].name = newName;
    } else if (isCatch) {
        if (catchIdx < m_catchments.size()) m_catchments[catchIdx].name = newName;
    }

    // Update selection set if the renamed element was selected.
    if (m_selectedNames.removeOne(oldName))
        m_selectedNames.append(newName);

    // Rebuild the object-location index (keyed by name).
    buildGeometryCache();
    m_kdDirty = true;
    m_needsRebuild = true;
    emit repaintRequested();
    emit geometryChanged();
    return true;
}

// ---------------------------------------------------------------------------
// Delete operations (engine cascade + cache rebuild)
// ---------------------------------------------------------------------------

bool SWMMModelLayer::applyNodeDelete(const QString &name,
                                      QStringList *cascadeLinkNames)
{
    if (!m_engine) return false;
    const QByteArray utf8 = name.toUtf8();
    const int nodeIdx = swmm_node_index(m_engine, utf8.constData());
    if (nodeIdx < 0) return false;

    // Identify cascade links BEFORE deletion (engine indices still valid).
    QVector<int> cascadeLinkSoaIndices;
    for (int i = 0; i < m_links.size(); ++i) {
        int n1 = -1, n2 = -1;
        swmm_link_get_from_node(m_engine, i, &n1);
        swmm_link_get_to_node(m_engine, i, &n2);
        if (n1 == nodeIdx || n2 == nodeIdx) {
            if (cascadeLinkNames) *cascadeLinkNames << m_links[i].name;
            cascadeLinkSoaIndices << i;
        }
    }

    if (swmm_node_delete(m_engine, nodeIdx, nullptr) != 0) return false;

    // Remove cascade links from cache (reverse order preserves validity).
    std::sort(cascadeLinkSoaIndices.rbegin(), cascadeLinkSoaIndices.rend());
    for (int li : cascadeLinkSoaIndices) m_links.removeAt(li);

    // Remove the node itself.
    m_nodes.removeAt(nodeIdx);

    buildGeometryCache();
    m_kdDirty = true;
    m_needsRebuild = true;
    emit repaintRequested();
    emit geometryChanged();
    return true;
}

bool SWMMModelLayer::applyLinkDelete(const QString &name)
{
    if (!m_engine) return false;
    const QByteArray utf8 = name.toUtf8();
    const int linkIdx = swmm_link_index(m_engine, utf8.constData());
    if (linkIdx < 0) return false;

    if (swmm_link_delete(m_engine, linkIdx, nullptr) != 0) return false;

    m_links.removeAt(linkIdx);
    buildGeometryCache();
    m_kdDirty = true;
    m_needsRebuild = true;
    emit repaintRequested();
    emit geometryChanged();
    return true;
}

bool SWMMModelLayer::applyGageDelete(const QString &name)
{
    if (!m_engine) return false;
    const QByteArray utf8 = name.toUtf8();
    const int idx = swmm_gage_index(m_engine, utf8.constData());
    if (idx < 0) return false;

    if (swmm_gage_delete(m_engine, idx, nullptr) != 0) return false;

    m_gages.removeAt(idx);
    rebuildCategoryIndex();
    m_kdDirty = true;
    m_needsRebuild = true;
    emit repaintRequested();
    emit geometryChanged();
    return true;
}

bool SWMMModelLayer::applySubcatchDelete(const QString &name)
{
    if (!m_engine) return false;
    const QByteArray utf8 = name.toUtf8();
    const int idx = swmm_subcatch_index(m_engine, utf8.constData());
    if (idx < 0) return false;

    if (swmm_subcatch_delete(m_engine, idx, nullptr) != 0) return false;

    m_catchments.removeAt(idx);
    buildGeometryCache();
    m_kdDirty = true;
    m_needsRebuild = true;
    emit repaintRequested();
    emit geometryChanged();
    return true;
}

// ---------------------------------------------------------------------------
// Slice BS Phase 6.9.2 — hydrograph + RDII decay MVC helpers
//
// Every mutation to [HYDROGRAPHS] / [RDII_DECAY] data lives here. The single
// hydrographChanged(uhName) signal is the synchronization seam: the four
// hydrograph models in hydrographmodels.cpp listen for it and refresh their
// views, and SWMMHydrographPropertyAdapter forwards it as its own
// changed() so the QPropertyModel-backed property panel re-reads.
// ---------------------------------------------------------------------------

bool SWMMModelLayer::applyHydrographAddGroup(const QString &name,
                                              const QString &gageName,
                                              int initialResponse)
{
    if (!m_engine || name.isEmpty()) return false;
    if (initialResponse < 0 || initialResponse > 2) return false;

    const QByteArray uh = name.toUtf8();
    // Seed an ALL-month parameter row at the requested response with
    // R/T/K = (0, 0, 1) — K must be >= 1 per the engine contract
    // (openswmm_inflows.h:223). The user fills in the real values via the
    // editor's RTK table.
    if (swmm_hydrograph_set_rtk(m_engine, uh.constData(),
                                /*month=*/-1, initialResponse,
                                /*r=*/0.0, /*t=*/0.0, /*k=*/1.0) != SWMM_OK)
        return false;
    if (!gageName.isEmpty()) {
        const QByteArray gage = gageName.toUtf8();
        if (swmm_hydrograph_set_gage(m_engine, uh.constData(),
                                      gage.constData()) != SWMM_OK)
            return false;
    }
    emit hydrographChanged(name);
    return true;
}

bool SWMMModelLayer::applyHydrographRemoveGroup(const QString &name)
{
    if (!m_engine || name.isEmpty()) return false;
    const QByteArray uh = name.toUtf8();
    if (swmm_hydrograph_remove_group(m_engine, uh.constData()) != SWMM_OK)
        return false;
    // Empty name signals "everything potentially changed" — the engine
    // also dropped any [RDII] node assignments referencing this group,
    // and listeners may need a full rebuild rather than a per-name diff.
    emit hydrographChanged(QString());
    return true;
}

bool SWMMModelLayer::applyHydrographRenameGroup(const QString &oldName,
                                                 const QString &newName)
{
    if (!m_engine || oldName.isEmpty() || newName.isEmpty() || oldName == newName)
        return false;
    const QByteArray oldUtf8 = oldName.toUtf8();
    const int gn = swmm_hydrograph_group_count(m_engine);
    int idx = -1;
    char buf[64];
    for (int i = 0; i < gn; ++i) {
        if (swmm_hydrograph_group_id(m_engine, i, buf, sizeof(buf)) != SWMM_OK)
            continue;
        if (std::strcmp(buf, oldUtf8.constData()) == 0) { idx = i; break; }
    }
    if (idx < 0) return false;

    const QByteArray newUtf8 = newName.toUtf8();
    if (swmm_hydrograph_group_rename(m_engine, idx, newUtf8.constData()) != SWMM_OK)
        return false;

    // Empty name → model layer rebuilds everything (old name is gone).
    emit hydrographChanged(QString());
    return true;
}

bool SWMMModelLayer::applyHydrographSetGage(const QString &name,
                                              const QString &gageName)
{
    if (!m_engine || name.isEmpty()) return false;
    const QByteArray uh = name.toUtf8();
    const QByteArray gage = gageName.toUtf8();
    const char *gagePtr = gageName.isEmpty() ? nullptr : gage.constData();
    if (swmm_hydrograph_set_gage(m_engine, uh.constData(), gagePtr) != SWMM_OK)
        return false;
    emit hydrographChanged(name);
    return true;
}

bool SWMMModelLayer::applyHydrographSetRtk(const QString &name,
                                             int month, int response,
                                             double r, double t, double k)
{
    if (!m_engine || name.isEmpty()) return false;
    const QByteArray uh = name.toUtf8();
    if (swmm_hydrograph_set_rtk(m_engine, uh.constData(),
                                month, response, r, t, k) != SWMM_OK)
        return false;
    emit hydrographChanged(name);
    return true;
}

bool SWMMModelLayer::applyHydrographSetIa(const QString &name,
                                            int month, int response,
                                            double dmax, double drecov, double dinit)
{
    if (!m_engine || name.isEmpty()) return false;
    const QByteArray uh = name.toUtf8();
    if (swmm_hydrograph_set_ia(m_engine, uh.constData(),
                                month, response, dmax, drecov, dinit) != SWMM_OK)
        return false;
    emit hydrographChanged(name);
    return true;
}

bool SWMMModelLayer::applyHydrographRemoveEntry(const QString &name,
                                                  int month, int response)
{
    if (!m_engine || name.isEmpty()) return false;
    const QByteArray uh = name.toUtf8();
    if (swmm_hydrograph_remove_entry(m_engine, uh.constData(),
                                      month, response) != SWMM_OK)
        return false;
    emit hydrographChanged(name);
    return true;
}

bool SWMMModelLayer::applyHydrographClearMonths(const QString &name)
{
    if (!m_engine || name.isEmpty()) return false;
    const QByteArray uh = name.toUtf8();
    if (swmm_hydrograph_clear_group_months(m_engine, uh.constData()) != SWMM_OK)
        return false;
    emit hydrographChanged(name);
    return true;
}

bool SWMMModelLayer::applyRdiiDecaySet(const QString &name, int response,
                                        double k_dep, double k_0, double k_T,
                                        double T_ref, double theta_rec, double T_freeze)
{
    if (!m_engine || name.isEmpty()) return false;
    const QByteArray uh = name.toUtf8();
    if (swmm_rdii_decay_set(m_engine, uh.constData(), response,
                            k_dep, k_0, k_T, T_ref, theta_rec, T_freeze) != SWMM_OK)
        return false;
    emit hydrographChanged(name);
    return true;
}

bool SWMMModelLayer::applyRdiiDecayRemove(const QString &name, int response)
{
    if (!m_engine || name.isEmpty()) return false;
    const QByteArray uh = name.toUtf8();
    if (swmm_rdii_decay_remove(m_engine, uh.constData(), response) != SWMM_OK)
        return false;
    emit hydrographChanged(name);
    return true;
}

// Lazy accessors — one shared model instance per kind, constructed on first
// use and parented to the layer so destruction is automatic.
HydrographGroupListModel *SWMMModelLayer::hydrographGroupListModel()
{
    if (!m_uhGroupListModel) m_uhGroupListModel = new HydrographGroupListModel(this);
    return m_uhGroupListModel;
}

HydrographRtkTableModel *SWMMModelLayer::hydrographRtkModel()
{
    if (!m_uhRtkModel) m_uhRtkModel = new HydrographRtkTableModel(this);
    return m_uhRtkModel;
}

HydrographIaTableModel *SWMMModelLayer::hydrographIaModel()
{
    if (!m_uhIaModel) m_uhIaModel = new HydrographIaTableModel(this);
    return m_uhIaModel;
}

HydrographDecayTableModel *SWMMModelLayer::hydrographDecayModel()
{
    if (!m_uhDecayModel) m_uhDecayModel = new HydrographDecayTableModel(this);
    return m_uhDecayModel;
}

bool SWMMModelLayer::applyLinkInteriorVertices(int linkIdx,
                                                const QVector<QPointF> &interior)
{
    if (linkIdx < 0 || linkIdx >= m_links.size())
        return false;

    if (m_engine)
    {
        // Engine stores interior-only vertices; its get API prepends the
        // from-node coord and appends the to-node coord. See the docblock
        // on swmm_spatial_set_link_vertices.
        QVector<double> vx(interior.size());
        QVector<double> vy(interior.size());
        for (int i = 0; i < interior.size(); ++i)
        {
            vx[i] = interior[i].x();
            vy[i] = interior[i].y();
        }
        if (swmm_spatial_set_link_vertices(m_engine, linkIdx,
                                            vx.constData(), vy.constData(),
                                            interior.size()) != 0)
            return false;
    }

    // Store interior-only vertices (no node endpoint wrapping).
    m_links[linkIdx].vertices = interior;

    // Refresh bbox from the full polyline (uses current node positions).
    if (linkIdx < m_linkBboxes.size()) {
        const QVector<QPointF> full = cachedLinkPolyline(linkIdx);
        if (!full.isEmpty()) {
            double x0 = full.first().x(), x1 = x0;
            double y0 = full.first().y(), y1 = y0;
            for (const QPointF &p : full) {
                if (p.x() < x0) x0 = p.x(); else if (p.x() > x1) x1 = p.x();
                if (p.y() < y0) y0 = p.y(); else if (p.y() > y1) y1 = p.y();
            }
            m_linkBboxes[linkIdx] = MapExtent(x0, y0, x1, y1);
        }
    }
    refreshSceneCoordsForLink(linkIdx);

    m_needsRebuild = true;
    emit repaintRequested();
    return true;
}

bool SWMMModelLayer::applySubcatchVertices(int idx, const QVector<QPointF> &vertices)
{
    if (idx < 0 || idx >= m_catchments.size() || vertices.isEmpty())
        return false;

    if (m_engine)
    {
        QVector<double> vx(vertices.size()), vy(vertices.size());
        for (int i = 0; i < vertices.size(); ++i)
        {
            vx[i] = vertices[i].x();
            vy[i] = vertices[i].y();
        }
        if (swmm_spatial_set_subcatch_polygon(m_engine, idx,
                                              vx.constData(), vy.constData(),
                                              vertices.size()) != 0)
            return false;

        double cx = 0.0, cy = 0.0;
        for (const QPointF &p : vertices) { cx += p.x(); cy += p.y(); }
        cx /= vertices.size(); cy /= vertices.size();
        swmm_spatial_set_subcatch_coord(m_engine, idx, cx, cy);
    }

    m_catchments[idx].vertices = vertices;

    // Refresh layer-CRS bbox (used by subcatchmentsInRect).
    if (idx < m_catchBboxes.size() && !vertices.isEmpty()) {
        double x0 = vertices.first().x(), x1 = x0;
        double y0 = vertices.first().y(), y1 = y0;
        for (const QPointF &p : vertices) {
            if (p.x() < x0) x0 = p.x(); else if (p.x() > x1) x1 = p.x();
            if (p.y() < y0) y0 = p.y(); else if (p.y() > y1) y1 = p.y();
        }
        m_catchBboxes[idx] = MapExtent(x0, y0, x1, y1);
    }

    // Rebuild the scene-space coordinate cache for this catchment so
    // SWMMLayerItem::paint() draws the polygon at the new position.
    refreshSceneCoordsForSubcatch(idx);

    m_needsRebuild = true;
    emit repaintRequested();
    return true;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void SWMMModelLayer::buildGeometryCache()
{
    // Also refresh the per-category index buckets used by the virtualised
    // Object Browser tree model. These are cheap (O(N) once, indexing into
    // already-cached SoA) and must stay coherent with m_nodes / m_links /
    // m_catchments / m_gages any time those are repopulated.
    rebuildCategoryIndex();

    // Per-feature bbox caches for linksInRect / subcatchmentsInRect.
    // Computed once here so the rubber-band tool can iterate the
    // arrays in O(N) with constant work, instead of the previous
    // O(N²) name→index linear scan + per-link vertex bbox compute on
    // every iteration.
    auto bboxOf = [](const QVector<QPointF> &pts) {
        if (pts.isEmpty())
            return MapExtent(std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::quiet_NaN());
        double x0 = pts.first().x(), x1 = x0;
        double y0 = pts.first().y(), y1 = y0;
        for (const QPointF &p : pts) {
            if (p.x() < x0) x0 = p.x(); else if (p.x() > x1) x1 = p.x();
            if (p.y() < y0) y0 = p.y(); else if (p.y() > y1) y1 = p.y();
        }
        return MapExtent(x0, y0, x1, y1);
    };
    m_linkBboxes.clear();
    m_linkBboxes.reserve(m_links.size());
    for (int i = 0; i < m_links.size(); ++i)
        m_linkBboxes.append(bboxOf(cachedLinkPolyline(i)));

    m_catchBboxes.clear();
    m_catchBboxes.reserve(m_catchments.size());
    for (const CatchGeom &c : m_catchments) m_catchBboxes.append(bboxOf(c.vertices));

    // Compute the layer's full geometric extent from EVERY drawn element so
    // Zoom-to-Extent fits the entire model on screen, not just the network
    // backbone. Previously this missed subcatchment polygons (which routinely
    // extend well outside the conduit network) and rain gages, causing the
    // canvas to crop them at the edges.
    if (m_nodes.isEmpty() && m_links.isEmpty()
        && m_catchments.isEmpty() && m_gages.isEmpty())
        return;

    double xMin = std::numeric_limits<double>::max();
    double yMin = std::numeric_limits<double>::max();
    double xMax = std::numeric_limits<double>::lowest();
    double yMax = std::numeric_limits<double>::lowest();

    auto expand = [&](double x, double y) {
        xMin = std::min(xMin, x);  yMin = std::min(yMin, y);
        xMax = std::max(xMax, x);  yMax = std::max(yMax, y);
    };

    for (const NodeGeom &n : m_nodes)
        expand(n.x, n.y);

    for (const LinkGeom &l : m_links)
        for (const QPointF &v : l.vertices)
            expand(v.x(), v.y());

    // Subcatchment polygons can extend far beyond the network and were the
    // primary cause of the Zoom-to-Extent height truncation.
    for (const CatchGeom &c : m_catchments)
        for (const QPointF &v : c.vertices)
            expand(v.x(), v.y());

    // Rain gages too — they often sit outside the catchment area.
    for (const NodeGeom &g : m_gages)
        expand(g.x, g.y);

    if (xMin <= xMax && yMin <= yMax)
        setExtent(MapExtent(xMin, yMin, xMax, yMax));

    rebuildKdTrees();
    rebuildSceneCoords();
    rebuildFlagArrays();  // After SoAs + m_nameToSoa are stable.
}

// ---------------------------------------------------------------------------
// Scene-coordinate cache + spatial index
// ---------------------------------------------------------------------------

void SWMMModelLayer::LinkSpatialGrid::rebuild(const QVector<QRectF> &bboxes)
{
    clear();
    if (bboxes.isEmpty()) return;

    // Total extent = union of all valid bboxes; collect diagonals for the
    // cell-size heuristic. Skip only !isValid() (negative-extent) rects —
    // zero-extent rects (a degenerate point/line for, e.g., an
    // axis-aligned orifice between two nodes that share an x or y) are
    // legitimate links that still need to render. The median-diagonal
    // heuristic uses only non-zero diagonals so degenerates don't drag
    // the cell size to zero.
    bool seeded = false;
    QVector<double> diagonals;
    diagonals.reserve(bboxes.size());
    for (const QRectF &b : bboxes) {
        if (!b.isValid()) continue;
        if (!seeded) { extent = b; seeded = true; }
        else         { extent = extent.united(b); }
        const double diag = std::hypot(b.width(), b.height());
        if (diag > 0.0) diagonals.append(diag);
    }
    if (!seeded || diagonals.isEmpty()) return;

    // Cell size: 16x the median link-bbox diagonal. The grid stays small
    // (typically ~100x100) for SWMM-style networks where most links are
    // short, while still letting outliers (long trunks) span only a few
    // cells. Outliers are inserted into every cell they touch — no
    // clipping, no false negatives. Hard cap at 1024x1024 to keep the
    // worst-case grid memory footprint bounded for any model size.
    std::nth_element(diagonals.begin(),
                     diagonals.begin() + diagonals.size() / 2,
                     diagonals.end());
    const double median = diagonals[diagonals.size() / 2];
    const double cellSize = std::max(median * 16.0, 1e-6);

    cellW = cellSize;
    cellH = cellSize;
    cols = std::max(1, int(std::ceil(extent.width()  / cellW)));
    rows = std::max(1, int(std::ceil(extent.height() / cellH)));
    if (qint64(cols) * rows > qint64(1024) * 1024) {
        const double scale = std::sqrt(double(cols) * rows / (1024.0 * 1024.0));
        cellW *= scale;
        cellH *= scale;
        cols = std::max(1, int(std::ceil(extent.width()  / cellW)));
        rows = std::max(1, int(std::ceil(extent.height() / cellH)));
    }
    cells.resize(cols * rows);

    for (int i = 0; i < bboxes.size(); ++i) {
        const QRectF &b = bboxes[i];
        if (!b.isValid()) continue;
        const int cx0 = std::clamp(int(std::floor((b.left()   - extent.left()) / cellW)), 0, cols - 1);
        const int cx1 = std::clamp(int(std::floor((b.right()  - extent.left()) / cellW)), 0, cols - 1);
        const int cy0 = std::clamp(int(std::floor((b.top()    - extent.top())  / cellH)), 0, rows - 1);
        const int cy1 = std::clamp(int(std::floor((b.bottom() - extent.top())  / cellH)), 0, rows - 1);
        for (int cy = cy0; cy <= cy1; ++cy)
            for (int cx = cx0; cx <= cx1; ++cx)
                cells[cy * cols + cx].append(i);
    }
}

QVector<int> SWMMModelLayer::LinkSpatialGrid::query(const QRectF &rect) const
{
    QVector<int> out;
    if (cells.isEmpty() || !rect.isValid() || rect.isEmpty())
        return out;

    QRectF q = rect.intersected(extent);
    if (q.isEmpty()) return out;

    const int cx0 = std::clamp(int(std::floor((q.left()   - extent.left()) / cellW)), 0, cols - 1);
    const int cx1 = std::clamp(int(std::floor((q.right()  - extent.left()) / cellW)), 0, cols - 1);
    const int cy0 = std::clamp(int(std::floor((q.top()    - extent.top())  / cellH)), 0, rows - 1);
    const int cy1 = std::clamp(int(std::floor((q.bottom() - extent.top())  / cellH)), 0, rows - 1);

    // De-dup: a long link spanning multiple cells appears in each, but
    // paint must hit it once. Keyed off the SoA index space, which is
    // bounded by the grid's largest stored entry.
    int maxIdx = -1;
    for (int cy = cy0; cy <= cy1; ++cy)
        for (int cx = cx0; cx <= cx1; ++cx)
            for (int idx : cells[cy * cols + cx])
                if (idx > maxIdx) maxIdx = idx;
    if (maxIdx < 0) return out;

    QVector<bool> seen(maxIdx + 1, false);
    out.reserve(64);
    for (int cy = cy0; cy <= cy1; ++cy)
        for (int cx = cx0; cx <= cx1; ++cx)
            for (int idx : cells[cy * cols + cx])
                if (!seen[idx]) { seen[idx] = true; out.append(idx); }
    return out;
}

void SWMMModelLayer::rebuildFlagArrays()
{
    // Resize and zero in one shot. assign() handles both.
    m_nodeSelectedFlag .assign(m_nodes.size(),     0);
    m_linkSelectedFlag .assign(m_links.size(),     0);
    m_catchSelectedFlag.assign(m_catchments.size(),0);
    m_gageSelectedFlag .assign(m_gages.size(),     0);
    m_nodeHiddenFlag   .assign(m_nodes.size(),     0);
    m_linkHiddenFlag   .assign(m_links.size(),     0);
    m_catchHiddenFlag  .assign(m_catchments.size(),0);
    m_gageHiddenFlag   .assign(m_gages.size(),     0);

    auto setFlag = [this](const QString &name, bool selectedNotHidden) {
        const auto it = m_nameToSoa.constFind(name);
        if (it == m_nameToSoa.constEnd()) return;
        const int idx = it.value().soaIdx;
        switch (it.value().kind) {
        case SoaKind::Node:
            if (size_t(idx) < (selectedNotHidden ? m_nodeSelectedFlag : m_nodeHiddenFlag).size())
                (selectedNotHidden ? m_nodeSelectedFlag : m_nodeHiddenFlag)[idx] = 1;
            break;
        case SoaKind::Link:
            if (size_t(idx) < (selectedNotHidden ? m_linkSelectedFlag : m_linkHiddenFlag).size())
                (selectedNotHidden ? m_linkSelectedFlag : m_linkHiddenFlag)[idx] = 1;
            break;
        case SoaKind::Catch:
            if (size_t(idx) < (selectedNotHidden ? m_catchSelectedFlag : m_catchHiddenFlag).size())
                (selectedNotHidden ? m_catchSelectedFlag : m_catchHiddenFlag)[idx] = 1;
            break;
        case SoaKind::Gage:
            if (size_t(idx) < (selectedNotHidden ? m_gageSelectedFlag : m_gageHiddenFlag).size())
                (selectedNotHidden ? m_gageSelectedFlag : m_gageHiddenFlag)[idx] = 1;
            break;
        }
    };

    for (const QString &n : m_selectedNames)  setFlag(n, true);
    for (const QString &n : m_hiddenObjects)  setFlag(n, false);
}

void SWMMModelLayer::rebuildSceneCoords()
{
    // Transform every SoA point through m_transform once and apply the
    // scene Y-flip up front, so SWMMLayerItem::paint can hand the cached
    // QPointF straight to QPainter without per-frame math. This is the
    // hot path on big-model paints (121k links × N vertices each).
    auto applyTransform = [this](double &x, double &y) {
        if (m_transform) m_transform->Transform(1, &x, &y);
    };
    auto toScenePt = [&](double mx, double my) {
        applyTransform(mx, my);
        return QPointF(mx, -my);  // matches toScene() in swmmlayeritem.cpp
    };

    m_nodeScenePts.resize(m_nodes.size());
    for (int i = 0; i < m_nodes.size(); ++i)
        m_nodeScenePts[i] = toScenePt(m_nodes[i].x, m_nodes[i].y);

    m_gageScenePts.resize(m_gages.size());
    for (int i = 0; i < m_gages.size(); ++i)
        m_gageScenePts[i] = toScenePt(m_gages[i].x, m_gages[i].y);

    // Pack every link's vertices into one big float buffer. Pre-pass to
    // compute total vertex count + offsets so a single resize covers
    // all links — no incremental reallocation as we go.
    // Use cachedLinkPolyline() so each link's full path (from-node →
    // interior... → to-node) is stored.
    m_linkVertexOffset.assign(m_links.size(), 0);
    m_linkVertexCount .assign(m_links.size(), 0);
    uint32_t totalVerts = 0;
    for (int i = 0; i < m_links.size(); ++i) {
        const QVector<QPointF> full = cachedLinkPolyline(i);
        const uint32_t n = uint32_t(full.size());
        m_linkVertexOffset[i] = totalVerts;
        m_linkVertexCount [i] = n;
        totalVerts += n;
    }
    m_linkSceneFlat.assign(size_t(totalVerts) * 2, 0.0);
    m_linkSceneBBoxes.resize(m_links.size());
    for (int i = 0; i < m_links.size(); ++i)
        refreshSceneCoordsForLink(i);

    m_catchScenePts.resize(m_catchments.size());
    m_catchSceneBBoxes.resize(m_catchments.size());
    for (int i = 0; i < m_catchments.size(); ++i)
    {
        const auto &verts = m_catchments[i].vertices;
        QVector<QPointF> sp;
        sp.reserve(verts.size());
        QRectF bbox;
        for (int v = 0; v < verts.size(); ++v) {
            const QPointF p = toScenePt(verts[v].x(), verts[v].y());
            sp.append(p);
            if (v == 0) bbox = QRectF(p, QSizeF(0, 0));
            else {
                if (p.x() < bbox.left())   bbox.setLeft  (p.x());
                if (p.x() > bbox.right())  bbox.setRight (p.x());
                if (p.y() < bbox.top())    bbox.setTop   (p.y());
                if (p.y() > bbox.bottom()) bbox.setBottom(p.y());
            }
        }
        m_catchScenePts[i]    = std::move(sp);
        m_catchSceneBBoxes[i] = bbox;
    }

    // Rebuild the link spatial grid from the freshly-computed bboxes.
    // Paint queries this directly — see SWMMLayerItem::paint().
    QElapsedTimer gt; gt.start();
    m_linkGrid.rebuild(m_linkSceneBBoxes);
    qDebug().noquote() << "[LinkSpatialGrid::rebuild] links=" << m_links.size()
                       << " cols=" << m_linkGrid.cols
                       << " rows=" << m_linkGrid.rows
                       << " elapsed_ms=" << gt.elapsed();

    // Build subcatchment outlet lines: polygon centroid → outlet node or subcatchment.
    // Centroid is used as a practical pole-of-inaccessibility approximation; SWMM
    // subcatchment polygons are typically convex or gently irregular so the centroid
    // reliably sits well inside the polygon.
    m_catchOutletLines.clear();
    if (!m_engine || m_catchments.isEmpty()) return;

    // Pre-compute scene-space centroids for all catchments.
    QVector<QPointF> centroids(m_catchments.size());
    for (int i = 0; i < m_catchments.size(); ++i) {
        const auto &pts = m_catchScenePts[i];
        if (pts.isEmpty()) continue;
        QPointF c(0.0, 0.0);
        for (const QPointF &p : pts) { c.rx() += p.x(); c.ry() += p.y(); }
        centroids[i] = c / double(pts.size());
    }

    m_catchOutletLines.reserve(m_catchments.size());
    for (int i = 0; i < m_catchments.size(); ++i) {
        if (m_catchScenePts[i].isEmpty()) continue;

        // Try outlet node first.
        int nodeIdx = -1;
        if (swmm_subcatch_get_outlet(m_engine, i, &nodeIdx) == 0
                && nodeIdx >= 0 && nodeIdx < m_nodes.size()) {
            m_catchOutletLines.append({QLineF(centroids[i], m_nodeScenePts[nodeIdx]), i});
            continue;
        }

        // Fall back to outlet subcatchment.
        int scIdx = -1;
        if (swmm_subcatch_get_outlet_subcatch(m_engine, i, &scIdx) == 0
                && scIdx >= 0 && scIdx < m_catchments.size()
                && scIdx != i
                && !m_catchScenePts[scIdx].isEmpty()) {
            m_catchOutletLines.append({QLineF(centroids[i], centroids[scIdx]), i});
        }
    }
    ++m_geomRevision;
}

void SWMMModelLayer::refreshSceneCoordsForNode(int nodeIdx)
{
    if (nodeIdx < 0 || nodeIdx >= m_nodes.size()) return;
    if (m_nodeScenePts.size() != m_nodes.size())
        m_nodeScenePts.resize(m_nodes.size());
    double x = m_nodes[nodeIdx].x, y = m_nodes[nodeIdx].y;
    if (m_transform) m_transform->Transform(1, &x, &y);
    m_nodeScenePts[nodeIdx] = QPointF(x, -y);
}

void SWMMModelLayer::refreshSceneCoordsForLink(int linkIdx)
{
    if (linkIdx < 0 || linkIdx >= m_links.size()) return;
    if (m_linkSceneBBoxes.size() != m_links.size())
        m_linkSceneBBoxes.resize(m_links.size());

    const QVector<QPointF> verts = cachedLinkPolyline(linkIdx);
    const uint32_t n = uint32_t(verts.size());

    // If our flat layout is fresh enough to hold this link in place
    // (vertex count unchanged, parallel arrays sized correctly), do an
    // in-place rewrite of the link's slice. Otherwise fall back to a
    // full rebuild — vertex-count changes shift every downstream
    // offset, and editing a single link via this path during a drag
    // preview is rare enough that the full rebuild is acceptable.
    const bool layoutFresh =
        size_t(linkIdx) < m_linkVertexOffset.size()
        && size_t(linkIdx) < m_linkVertexCount.size()
        && m_linkVertexCount[linkIdx] == n
        && size_t((m_linkVertexOffset[linkIdx] + n) * 2) <= m_linkSceneFlat.size();
    if (!layoutFresh) {
        rebuildSceneCoords();
        return;
    }

    const uint32_t off = m_linkVertexOffset[linkIdx];
    QRectF bbox;
    for (uint32_t v = 0; v < n; ++v) {
        double x = verts[v].x(), y = verts[v].y();
        if (m_transform) m_transform->Transform(1, &x, &y);
        const double sx = x, sy = -y;
        m_linkSceneFlat[size_t(off + v) * 2 + 0] = sx;
        m_linkSceneFlat[size_t(off + v) * 2 + 1] = sy;
        const QPointF p(sx, sy);
        if (v == 0) bbox = QRectF(p, QSizeF(0, 0));
        else {
            if (p.x() < bbox.left())   bbox.setLeft  (p.x());
            if (p.x() > bbox.right())  bbox.setRight (p.x());
            if (p.y() < bbox.top())    bbox.setTop   (p.y());
            if (p.y() > bbox.bottom()) bbox.setBottom(p.y());
        }
    }
    // Axis-aligned 2-point links (common for orifices/weirs whose
    // from/to nodes share an x or y) collapse to a zero-extent rect.
    // QRectF::isEmpty() then trips the spatial-grid skip below and the
    // link disappears from the map. Inflate the rect by a sub-pixel
    // epsilon so it stays non-empty without affecting hit-testing.
    if (bbox.width()  == 0.0) bbox.adjust(-1e-3, 0.0, 1e-3, 0.0);
    if (bbox.height() == 0.0) bbox.adjust(0.0, -1e-3, 0.0, 1e-3);
    m_linkSceneBBoxes[linkIdx] = bbox;
}

bool SWMMModelLayer::applySubcatchArea(int idx, double areaInModelUnits)
{
    if (!m_engine || idx < 0 || idx >= m_catchments.size()) return false;
    if (swmm_subcatch_set_area(m_engine, idx, areaInModelUnits) != 0) return false;
    emit attributeChanged(m_catchments[idx].name);
    return true;
}

void SWMMModelLayer::refreshSceneCoordsForSubcatch(int catchIdx)
{
    if (catchIdx < 0 || catchIdx >= m_catchments.size()) return;
    if (m_catchScenePts.size() != m_catchments.size())
        m_catchScenePts.resize(m_catchments.size());
    if (m_catchSceneBBoxes.size() != m_catchments.size())
        m_catchSceneBBoxes.resize(m_catchments.size());

    // Rebuild scene-space polygon points.
    const auto &verts = m_catchments[catchIdx].vertices;
    QVector<QPointF> sp;
    sp.reserve(verts.size());
    QRectF bbox;
    for (int v = 0; v < verts.size(); ++v) {
        double x = verts[v].x(), y = verts[v].y();
        if (m_transform) m_transform->Transform(1, &x, &y);
        const QPointF p(x, -y);
        sp.append(p);
        if (v == 0) bbox = QRectF(p, QSizeF(0, 0));
        else {
            if (p.x() < bbox.left())   bbox.setLeft  (p.x());
            if (p.x() > bbox.right())  bbox.setRight (p.x());
            if (p.y() < bbox.top())    bbox.setTop   (p.y());
            if (p.y() > bbox.bottom()) bbox.setBottom(p.y());
        }
    }
    m_catchScenePts[catchIdx]    = sp;   // keep a copy — centroid loop below reads it
    m_catchSceneBBoxes[catchIdx] = bbox;

    // Recompute the centroid (average of scene-space polygon vertices).
    QPointF centroid(0.0, 0.0);
    if (!sp.isEmpty()) {
        for (const QPointF &p : sp) { centroid.rx() += p.x(); centroid.ry() += p.y(); }
        centroid /= double(sp.size());
    }

    // Remove the old outlet line for this catchment and rebuild it from
    // the new centroid so the arrow to the downstream node/subcatchment
    // updates together with the polygon.
    m_catchOutletLines.erase(
        std::remove_if(m_catchOutletLines.begin(), m_catchOutletLines.end(),
                       [catchIdx](const OutletLine &ol) { return ol.catchIdx == catchIdx; }),
        m_catchOutletLines.end());

    if (m_engine && !sp.isEmpty()) {
        int nodeIdx = -1;
        if (swmm_subcatch_get_outlet(m_engine, catchIdx, &nodeIdx) == 0
                && nodeIdx >= 0 && nodeIdx < m_nodes.size()
                && nodeIdx < int(m_nodeScenePts.size())) {
            m_catchOutletLines.append({QLineF(centroid, m_nodeScenePts[nodeIdx]), catchIdx});
        } else {
            int scIdx = -1;
            if (swmm_subcatch_get_outlet_subcatch(m_engine, catchIdx, &scIdx) == 0
                    && scIdx >= 0 && scIdx < m_catchments.size()
                    && scIdx != catchIdx
                    && !m_catchScenePts[scIdx].isEmpty()) {
                QPointF sc(0.0, 0.0);
                for (const QPointF &p : m_catchScenePts[scIdx])
                    { sc.rx() += p.x(); sc.ry() += p.y(); }
                sc /= double(m_catchScenePts[scIdx].size());
                m_catchOutletLines.append({QLineF(centroid, sc), catchIdx});
            }
        }
    }

    ++m_geomRevision;  // signals SWMMLayerItem to pick up the updated geometry
}

void SWMMModelLayer::refreshCatchOutletLinesForNode(int nodeIdx)
{
    // When a node moves, any subcatchment whose outlet is that node has its
    // outlet-line endpoint stuck at the old scene position. Walk the outlet
    // lines and patch the endpoint in-place — SWMMLayerItem reads this vector
    // directly on every paint so no geomRevision bump is needed.
    if (nodeIdx < 0 || nodeIdx >= int(m_nodeScenePts.size())) return;
    if (!m_engine) return;
    for (auto &ol : m_catchOutletLines)
    {
        int outNode = -1;
        if (swmm_subcatch_get_outlet(m_engine, ol.catchIdx, &outNode) == 0
                && outNode == nodeIdx)
            ol.line.setP2(m_nodeScenePts[nodeIdx]);
    }
}

// ---------------------------------------------------------------------------
// KD-tree management
// ---------------------------------------------------------------------------

void SWMMModelLayer::rebuildKdTrees() const
{
    // Recreate from scratch — ensures the stored raw pointers inside
    // PtAdaptor always point at the latest flat arrays.
    m_kdTrees = std::make_unique<SWMMKdTrees>();
    auto &kd = *m_kdTrees;

    // ---- nodes ----
    const int nNodes = m_nodes.size();
    kd.nodeX.resize(nNodes);
    kd.nodeY.resize(nNodes);
    for (int i = 0; i < nNodes; ++i)
    {
        kd.nodeX[i] = m_nodes[i].x;
        kd.nodeY[i] = m_nodes[i].y;
    }
    kd.nodeAdaptor = { kd.nodeX.constData(), kd.nodeY.constData(),
                       static_cast<std::size_t>(nNodes) };
    kd.nodeTree = std::make_unique<Kd2>(
        2, kd.nodeAdaptor, nanoflann::KDTreeSingleIndexAdaptorParams(10));
    if (nNodes > 0)
        kd.nodeTree->buildIndex();

    // ---- gages ----
    const int nGages = m_gages.size();
    kd.gageX.resize(nGages);
    kd.gageY.resize(nGages);
    for (int i = 0; i < nGages; ++i)
    {
        kd.gageX[i] = m_gages[i].x;
        kd.gageY[i] = m_gages[i].y;
    }
    kd.gageAdaptor = { kd.gageX.constData(), kd.gageY.constData(),
                       static_cast<std::size_t>(nGages) };
    kd.gageTree = std::make_unique<Kd2>(
        2, kd.gageAdaptor, nanoflann::KDTreeSingleIndexAdaptorParams(10));
    if (nGages > 0)
        kd.gageTree->buildIndex();

    m_kdDirty = false;
}

void SWMMModelLayer::ensureKdTrees() const
{
    if (m_kdDirty || !m_kdTrees)
        rebuildKdTrees();
}

// ---------------------------------------------------------------------------
// Spatial-index rect queries
// ---------------------------------------------------------------------------

// Helper: transform a canvas-CRS rect into layer-CRS coords.
// Returns false if no transform is available (already in layer CRS).
static bool transformRectToLayer(
    const OGRCoordinateTransformation *fwdTransform,
    double canvasMinX, double canvasMinY,
    double canvasMaxX, double canvasMaxY,
    double &lMinX, double &lMinY,
    double &lMaxX, double &lMaxY)
{
    if (!fwdTransform)
    {
        lMinX = canvasMinX; lMinY = canvasMinY;
        lMaxX = canvasMaxX; lMaxY = canvasMaxY;
        return false;
    }
    auto *inv = fwdTransform->GetInverse();
    if (!inv)
    {
        lMinX = canvasMinX; lMinY = canvasMinY;
        lMaxX = canvasMaxX; lMaxY = canvasMaxY;
        return false;
    }
    // Transform all 4 corners and take the bounding box of the results.
    double cx[4] = { canvasMinX, canvasMaxX, canvasMinX, canvasMaxX };
    double cy[4] = { canvasMinY, canvasMinY, canvasMaxY, canvasMaxY };
    inv->Transform(4, cx, cy);
    OGRCoordinateTransformation::DestroyCT(inv);
    lMinX = *std::min_element(cx, cx + 4);
    lMaxX = *std::max_element(cx, cx + 4);
    lMinY = *std::min_element(cy, cy + 4);
    lMaxY = *std::max_element(cy, cy + 4);
    return true;
}

QStringList SWMMModelLayer::nodesInRect(double canvasMinX, double canvasMinY,
                                         double canvasMaxX, double canvasMaxY) const
{
    ensureKdTrees();
    if (!m_kdTrees || !m_kdTrees->nodeTree || m_nodes.isEmpty())
        return {};

    double lMinX, lMinY, lMaxX, lMaxY;
    transformRectToLayer(m_transform, canvasMinX, canvasMinY, canvasMaxX, canvasMaxY,
                         lMinX, lMinY, lMaxX, lMaxY);

    // Radius search from the rect centre; circumradius guarantees all corners
    // are covered. Results are post-filtered to the exact rectangle.
    const double cx = (lMinX + lMaxX) * 0.5;
    const double cy = (lMinY + lMaxY) * 0.5;
    const double hw = (lMaxX - lMinX) * 0.5;
    const double hh = (lMaxY - lMinY) * 0.5;
    const double r2 = hw * hw + hh * hh;   // squared circumradius

    const double qpt[2] = { cx, cy };
    std::vector<nanoflann::ResultItem<uint32_t, double>> matches;
    m_kdTrees->nodeTree->radiusSearch(qpt, r2, matches,
                                      nanoflann::SearchParameters());

    QStringList result;
    result.reserve(static_cast<int>(matches.size()));
    for (const auto &m : matches)
    {
        const int i = static_cast<int>(m.first);
        const double x = m_nodes[i].x, y = m_nodes[i].y;
        if (x < lMinX || x > lMaxX || y < lMinY || y > lMaxY) continue;
        if (m_hiddenObjects.contains(m_nodes[i].name))          continue;
        result.append(m_nodes[i].name);
    }
    return result;
}

QStringList SWMMModelLayer::gagesInRect(double canvasMinX, double canvasMinY,
                                         double canvasMaxX, double canvasMaxY) const
{
    ensureKdTrees();
    if (!m_kdTrees || !m_kdTrees->gageTree || m_gages.isEmpty())
        return {};

    double lMinX, lMinY, lMaxX, lMaxY;
    transformRectToLayer(m_transform, canvasMinX, canvasMinY, canvasMaxX, canvasMaxY,
                         lMinX, lMinY, lMaxX, lMaxY);

    const double cx = (lMinX + lMaxX) * 0.5;
    const double cy = (lMinY + lMaxY) * 0.5;
    const double hw = (lMaxX - lMinX) * 0.5;
    const double hh = (lMaxY - lMinY) * 0.5;
    const double r2 = hw * hw + hh * hh;

    const double qpt[2] = { cx, cy };
    std::vector<nanoflann::ResultItem<uint32_t, double>> matches;
    m_kdTrees->gageTree->radiusSearch(qpt, r2, matches,
                                      nanoflann::SearchParameters());

    QStringList result;
    result.reserve(static_cast<int>(matches.size()));
    for (const auto &m : matches)
    {
        const int i = static_cast<int>(m.first);
        const double x = m_gages[i].x, y = m_gages[i].y;
        if (x < lMinX || x > lMaxX || y < lMinY || y > lMaxY) continue;
        if (m_hiddenObjects.contains(m_gages[i].name))          continue;
        result.append(m_gages[i].name);
    }
    return result;
}

bool SWMMModelLayer::snapNearestPoint(double mapX, double mapY, double mapRadius,
                                       QPointF &outPt,
                                       std::optional<QPointF> excludePos) const
{
    double bestDist2 = mapRadius * mapRadius;
    bool found = false;

    // Squared distance threshold below which a candidate equals excludePos.
    // 1e-12 is sub-micrometre — tight enough to reject only the exact
    // drag-start vertex while always accepting every other vertex.
    constexpr double kExcludeEps2 = 1e-12;

    auto isExcluded = [&](double cx, double cy) -> bool {
        if (!excludePos.has_value()) return false;
        const double ex = cx - excludePos->x();
        const double ey = cy - excludePos->y();
        return (ex * ex + ey * ey) < kExcludeEps2;
    };

    // --- Nodes via KD-tree (O(log N)) ---
    ensureKdTrees();
    if (m_kdTrees && m_kdTrees->nodeTree && !m_nodes.isEmpty()) {
        const double qpt[2] = { mapX, mapY };
        std::vector<nanoflann::ResultItem<uint32_t, double>> matches;
        m_kdTrees->nodeTree->radiusSearch(qpt, bestDist2, matches,
                                          nanoflann::SearchParameters());
        for (const auto &m : matches) {
            const int i = static_cast<int>(m.first);
            if (m_hiddenObjects.contains(m_nodes[i].name)) continue;
            if (isExcluded(m_nodes[i].x, m_nodes[i].y)) continue;
            if (m.second < bestDist2) {
                bestDist2 = m.second;
                outPt = QPointF(m_nodes[i].x, m_nodes[i].y);
                found = true;
            }
        }
    }

    // --- Link interior vertices (bbox-filtered) ---
    for (int li = 0; li < m_links.size(); ++li) {
        if (li < m_linkBboxes.size()) {
            const MapExtent &bb = m_linkBboxes[li];
            if (bb.xMax() < mapX - mapRadius || bb.xMin() > mapX + mapRadius) continue;
            if (bb.yMax() < mapY - mapRadius || bb.yMin() > mapY + mapRadius) continue;
        }
        // m_links[li].vertices holds interior bend points only.
        const auto &verts = m_links[li].vertices;
        for (int j = 0; j < verts.size(); ++j) {
            if (isExcluded(verts[j].x(), verts[j].y())) continue;
            const double dx = verts[j].x() - mapX;
            const double dy = verts[j].y() - mapY;
            const double d2 = dx * dx + dy * dy;
            if (d2 < bestDist2) {
                bestDist2 = d2;
                outPt = verts[j];
                found = true;
            }
        }
    }

    // --- Subcatchment polygon vertices (bbox-filtered) ---
    for (int ci = 0; ci < m_catchments.size(); ++ci) {
        if (ci < m_catchBboxes.size()) {
            const MapExtent &bb = m_catchBboxes[ci];
            if (bb.xMax() < mapX - mapRadius || bb.xMin() > mapX + mapRadius) continue;
            if (bb.yMax() < mapY - mapRadius || bb.yMin() > mapY + mapRadius) continue;
        }
        if (m_hiddenObjects.contains(m_catchments[ci].name)) continue;
        for (const QPointF &v : m_catchments[ci].vertices) {
            if (isExcluded(v.x(), v.y())) continue;
            const double dx = v.x() - mapX;
            const double dy = v.y() - mapY;
            const double d2 = dx * dx + dy * dy;
            if (d2 < bestDist2) {
                bestDist2 = d2;
                outPt = v;
                found = true;
            }
        }
    }

    return found;
}

QStringList SWMMModelLayer::linksInRect(double canvasMinX, double canvasMinY,
                                         double canvasMaxX, double canvasMaxY) const
{
    if (m_links.isEmpty() || m_linkBboxes.size() != m_links.size())
        return {};

    double lMinX, lMinY, lMaxX, lMaxY;
    transformRectToLayer(m_transform, canvasMinX, canvasMinY, canvasMaxX, canvasMaxY,
                         lMinX, lMinY, lMaxX, lMaxY);

    QStringList result;
    result.reserve(m_links.size() / 4);
    for (int i = 0; i < m_links.size(); ++i) {
        const MapExtent &b = m_linkBboxes[i];
        // Skip features with NaN bbox (empty polylines).
        if (!std::isfinite(b.xMin())) continue;
        if (b.xMax() < lMinX || b.xMin() > lMaxX
         || b.yMax() < lMinY || b.yMin() > lMaxY) continue;
        if (m_hiddenObjects.contains(m_links[i].name)) continue;
        result.append(m_links[i].name);
    }
    return result;
}

QStringList SWMMModelLayer::subcatchmentsInRect(double canvasMinX, double canvasMinY,
                                                 double canvasMaxX, double canvasMaxY) const
{
    if (m_catchments.isEmpty() || m_catchBboxes.size() != m_catchments.size())
        return {};

    double lMinX, lMinY, lMaxX, lMaxY;
    transformRectToLayer(m_transform, canvasMinX, canvasMinY, canvasMaxX, canvasMaxY,
                         lMinX, lMinY, lMaxX, lMaxY);

    QStringList result;
    result.reserve(m_catchments.size() / 4);
    for (int i = 0; i < m_catchments.size(); ++i) {
        const MapExtent &b = m_catchBboxes[i];
        if (!std::isfinite(b.xMin())) continue;
        if (b.xMax() < lMinX || b.xMin() > lMaxX
         || b.yMax() < lMinY || b.yMin() > lMaxY) continue;
        if (m_hiddenObjects.contains(m_catchments[i].name)) continue;
        result.append(m_catchments[i].name);
    }
    return result;
}

void SWMMModelLayer::rebuildTransform(const SpatialReferenceSystem *canvasSRS)
{
    if (m_transform)
    {
        OGRCoordinateTransformation::DestroyCT(m_transform);
        m_transform = nullptr;
    }
    if (m_inverseTransform)
    {
        OGRCoordinateTransformation::DestroyCT(m_inverseTransform);
        m_inverseTransform = nullptr;
    }

    if (!srs() || !canvasSRS || !srs()->ogrSpatialReference() ||
        !canvasSRS->ogrSpatialReference())
    {
        rebuildSceneCoords();
        return;
    }

    if (!srs()->ogrSpatialReference()->IsSame(canvasSRS->ogrSpatialReference()))
    {
        m_transform = OGRCreateCoordinateTransformation(
            srs()->ogrSpatialReference(),
            canvasSRS->ogrSpatialReference());
    }

    // The cached scene-space coords depend on m_transform; the canvas CRS
    // change just invalidated all of them.
    rebuildSceneCoords();
}

bool SWMMModelLayer::transformCanvasToLayer(double cx, double cy,
                                            double &lx, double &ly) const
{
    lx = cx;
    ly = cy;
    if (!m_transform)                          // canvas CRS == layer CRS
        return true;

    if (!m_inverseTransform)
        m_inverseTransform = m_transform->GetInverse();
    if (!m_inverseTransform)
        return false;

    // Matches the call shape used elsewhere in this file (e.g. nodeAtClick
    // tolerance back-projection) — Transform returns non-zero on success.
    if (!m_inverseTransform->Transform(1, &lx, &ly))
    {
        lx = cx;
        ly = cy;
        return false;
    }
    return true;
}

bool SWMMModelLayer::transformLayerToCanvas(double lx, double ly,
                                            double &cx, double &cy) const
{
    cx = lx;
    cy = ly;
    if (!m_transform)                          // canvas CRS == layer CRS
        return true;

    if (!m_transform->Transform(1, &cx, &cy))
    {
        cx = lx;
        cy = ly;
        return false;
    }
    return true;
}
