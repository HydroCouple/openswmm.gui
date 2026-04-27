/*!
 * \file   swmmmodellayer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 */

#include "layers/swmmmodellayer.h"
#include "core/preferencesmanager.h"
#include "core/unitsystem.h"
#include "map/swmmlayeritem.h"
#include "map/spatialreferencesystem.h"
#include "map/mapextent.h"

#include <QGraphicsScene>
#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QtMath>

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
    m_outfallSym.fillColor   = QColor(255, 80,  0);
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
}

SWMMModelLayer::~SWMMModelLayer()
{
    closeEngine();

    if (m_transform)
    {
        OGRCoordinateTransformation::DestroyCT(m_transform);
        m_transform = nullptr;
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

    if (swmm_engine_initialize(m_engine) != 0)
    {
        errors.append(QStringLiteral("Failed to initialize model: %1").arg(m_modelFilePath));
        swmm_engine_close(m_engine);
        swmm_engine_destroy(m_engine);
        m_engine = nullptr;
        return false;
    }

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
    // A link's polyline is: [from-node coord] + [interior vertices from engine]
    //                     + [to-node coord]. The engine's vertex API returns
    //                     ONLY the interior vertices; we must look up the
    //                     endpoint nodes ourselves and prepend / append them.
    //                     Without these endpoints links either don't render
    //                     (most links have no interior vertices) or render as
    //                     disconnected stubs in the middle of the canvas.
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

        int vertCount = 0;
        swmm_spatial_get_link_vertex_count(m_engine, i, &vertCount);

        const bool hasFrom = fromIdx >= 0 && fromIdx < m_nodes.size();
        const bool hasTo   = toIdx   >= 0 && toIdx   < m_nodes.size();
        const int total = vertCount + (hasFrom ? 1 : 0) + (hasTo ? 1 : 0);
        g.vertices.resize(total);

        int vertexIndex = 0;
        if (hasFrom)
            g.vertices[vertexIndex++] =
                QPointF(m_nodes[fromIdx].x, m_nodes[fromIdx].y);

        if (vertCount > 0)
        {
            QVector<double> vx(vertCount), vy(vertCount);
            swmm_spatial_get_link_vertices(m_engine, i, vx.data(), vy.data(), vertCount);
            for (int v = 0; v < vertCount; ++v)
                g.vertices[vertexIndex++] = QPointF(vx[v], vy[v]);
        }

        if (hasTo)
            g.vertices[vertexIndex] =
                QPointF(m_nodes[toIdx].x, m_nodes[toIdx].y);

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
    //   2. User-preference default (PreferencesManager → EPSG:4326 out
    //      of the box). Lets a user working in a specific region pick
    //      a local projected CRS once and have every future project
    //      default to it.
    //   3. Untitled local SRS — only if the preferred code lookup
    //      fails (bad custom EPSG, missing PROJ data, etc.).
    {
        SpatialReferenceSystem *layerSRS = nullptr;
        char crsBuf[512] = {};
        if (swmm_get_crs(m_engine, crsBuf, sizeof(crsBuf)) == 0 && crsBuf[0] != '\0')
            layerSRS = SpatialReferenceSystem::fromWktOrProj(QString::fromUtf8(crsBuf), this);
        if (!layerSRS) {
            auto *prefs = PreferencesManager::instance();
            layerSRS = SpatialReferenceSystem::fromAuthCode(prefs->defaultCrsAuthority(),
                                                            prefs->defaultCrsCode(), this);
        }
        if (!layerSRS)
            layerSRS = SpatialReferenceSystem::untitled(this);
        setSRS(layerSRS, true);
    }

    buildGeometryCache();
    m_needsRebuild = true;

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
    }

    // Links — linkType 0..4 matches Category 0..4 offset from CatConduits.
    for (int i = 0; i < m_links.size(); ++i)
    {
        const int t = (m_links[i].linkType >= 0 && m_links[i].linkType < 5)
                    ? m_links[i].linkType : 0;
        const Category cat = Category(int(CatConduits) + t);
        m_linksByType[t].append(i);
        m_objectLocation.insert(m_links[i].name,
                                {cat, m_linksByType[t].size() - 1});
    }

    // Subcatchments + gages are their own categories; row = SoA index.
    for (int i = 0; i < m_catchments.size(); ++i)
        m_objectLocation.insert(m_catchments[i].name, {CatSubcatchments, i});
    for (int i = 0; i < m_gages.size(); ++i)
        m_objectLocation.insert(m_gages[i].name, {CatRainGages, i});

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
// Selection
// ---------------------------------------------------------------------------

QStringList SWMMModelLayer::selectedElementNames() const { return m_selectedNames; }

void SWMMModelLayer::setSelectedElementNames(const QStringList &names)
{
    if (names == m_selectedNames)
        return;
    QElapsedTimer t; t.start();
    m_selectedNames = names;
    // Selection does not change geometry — SWMMLayerItem::paint() reads
    // m_selectedNames live each frame, so a repaint is sufficient.
    // Flipping m_needsRebuild here would force depopulate/populate of the
    // batched layer item on every rubber-band tick (see refreshScene()),
    // which is the dominant cost on large models (100k+ links).
    emit selectionChanged(names);
    emit repaintRequested();
    qDebug().noquote() << "[setSelectedElementNames] count=" << names.size()
                       << "elapsed_ms=" << t.elapsed();
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
        m[QStringLiteral("Vertex count")] = l.vertices.size();
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
        return bboxOf(m_links[idx].vertices);
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
        for (const LinkGeom &l : m_links)
        {
            if (m_hiddenObjects.contains(l.name)) continue;
            const auto &verts = l.vertices;
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

QVector<QPointF> SWMMModelLayer::cachedLinkPolyline(int idx) const
{
    if (idx < 0 || idx >= m_links.size())
        return {};
    return m_links[idx].vertices;
}

QVector<QPointF> SWMMModelLayer::cachedLinkInteriorVertices(int idx) const
{
    if (idx < 0 || idx >= m_links.size())
        return {};
    const auto &full = m_links[idx].vertices;
    // Cached polyline is [from_endpoint, ...interior..., to_endpoint] when
    // both endpoints were resolvable during loadModel. A single-point or
    // empty polyline is treated as having no interior.
    if (full.size() <= 2)
        return {};
    return full.mid(1, full.size() - 2);
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
    if (name.isEmpty()) return r;

    if (!findObjectLocation(name, &r.cat, &r.soaIndex)) return r;

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

    // Mirror the endpoint update into each attached link's cached
    // polyline so the live preview shows the link stretching with the
    // cursor. Engine state is UNTOUCHED — MoveNodeCommand::redo commits
    // via applyNodeMove on release.
    const QVector<int> attached = linksAttachedToNode(idx);
    for (int linkIdx : attached)
    {
        const int end = linkEndForNode(linkIdx, idx);
        if (end < 0) continue;
        auto &verts = m_links[linkIdx].vertices;
        if (verts.isEmpty()) continue;
        const int slot = (end == 0) ? 0 : (verts.size() - 1);
        verts[slot] = QPointF(newX, newY);
        refreshSceneCoordsForLink(linkIdx);
    }

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

    // Mirror the endpoint update into each attached link's cached polyline
    // so the next scene rebuild renders connected links without having to
    // reload geometry from the engine.
    const QVector<int> attached = linksAttachedToNode(idx);
    for (int linkIdx : attached)
    {
        const int end = linkEndForNode(linkIdx, idx);
        if (end < 0) continue;
        auto &verts = m_links[linkIdx].vertices;
        if (verts.isEmpty()) continue;
        const int slot = (end == 0) ? 0 : (verts.size() - 1);
        verts[slot] = QPointF(newX, newY);
        // Refresh this link's bbox so linksInRect stays correct.
        if (linkIdx < m_linkBboxes.size()) {
            double x0 = verts.first().x(), x1 = x0;
            double y0 = verts.first().y(), y1 = y0;
            for (const QPointF &p : verts) {
                if (p.x() < x0) x0 = p.x(); else if (p.x() > x1) x1 = p.x();
                if (p.y() < y0) y0 = p.y(); else if (p.y() > y1) y1 = p.y();
            }
            m_linkBboxes[linkIdx] = MapExtent(x0, y0, x1, y1);
        }
        refreshSceneCoordsForLink(linkIdx);
    }

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

    if (outIdx) *outIdx = idx;

    // m_nodes changed → category index buckets + name→(cat,row) map go
    // stale. Rebuild before emitting repaintRequested so the Object
    // Browser model sees a coherent snapshot on the next data() cycle.
    rebuildCategoryIndex();
    m_kdDirty = true;
    m_needsRebuild = true;
    emit repaintRequested();
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
    m_kdDirty = true;
    m_needsRebuild = true;
    emit repaintRequested();
    return true;
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

    auto &full = m_links[linkIdx].vertices;
    QPointF fromPt, toPt;
    const bool hasFrom = !full.isEmpty();
    const bool hasTo   = full.size() >= 2;
    if (hasFrom) fromPt = full.first();
    if (hasTo)   toPt   = full.last();

    QVector<QPointF> rebuilt;
    rebuilt.reserve((hasFrom ? 1 : 0) + interior.size() + (hasTo ? 1 : 0));
    if (hasFrom) rebuilt.append(fromPt);
    rebuilt.append(interior);
    if (hasTo)   rebuilt.append(toPt);
    full = rebuilt;

    // Refresh this link's bbox so linksInRect stays correct.
    if (linkIdx < m_linkBboxes.size() && !full.isEmpty()) {
        double x0 = full.first().x(), x1 = x0;
        double y0 = full.first().y(), y1 = y0;
        for (const QPointF &p : full) {
            if (p.x() < x0) x0 = p.x(); else if (p.x() > x1) x1 = p.x();
            if (p.y() < y0) y0 = p.y(); else if (p.y() > y1) y1 = p.y();
        }
        m_linkBboxes[linkIdx] = MapExtent(x0, y0, x1, y1);
    }
    refreshSceneCoordsForLink(linkIdx);

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
    for (const LinkGeom &l : m_links) m_linkBboxes.append(bboxOf(l.vertices));

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
}

// ---------------------------------------------------------------------------
// Scene-coordinate cache
// ---------------------------------------------------------------------------

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

    m_linkScenePts.resize(m_links.size());
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
    if (m_linkScenePts.size()    != m_links.size()) m_linkScenePts.resize(m_links.size());
    if (m_linkSceneBBoxes.size() != m_links.size()) m_linkSceneBBoxes.resize(m_links.size());

    const auto &verts = m_links[linkIdx].vertices;
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
    m_linkScenePts[linkIdx]    = std::move(sp);
    m_linkSceneBBoxes[linkIdx] = bbox;
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
