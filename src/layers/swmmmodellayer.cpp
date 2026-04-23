/*!
 * \file   swmmmodellayer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 */

#include "layers/swmmmodellayer.h"
#include "core/unitsystem.h"
#include "map/graphicsitems.h"
#include "map/spatialreferencesystem.h"
#include "map/mapextent.h"

#include <QGraphicsScene>
#include <QDebug>
#include <QFileInfo>
#include <QtMath>

#include <ogr_spatialref.h>
#include <ogr_geometry.h>

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_subcatchments.h>
#include <openswmm/engine/openswmm_gages.h>
#include <openswmm/engine/openswmm_spatial.h>

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
    m_outfallSym.size        = 10.0;
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
    {
        SpatialReferenceSystem *layerSRS = nullptr;
        char crsBuf[512] = {};
        if (swmm_get_crs(m_engine, crsBuf, sizeof(crsBuf)) == 0 && crsBuf[0] != '\0')
            layerSRS = SpatialReferenceSystem::fromWktOrProj(QString::fromUtf8(crsBuf), this);
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
    bool changed = false;
    if (visible)
    {
        changed = m_hiddenObjects.remove(name) > 0;
    }
    else if (!m_hiddenObjects.contains(name))
    {
        m_hiddenObjects.insert(name);
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
            if (m_hiddenObjects.remove(n) > 0)
                changed = true;
    }
    else
    {
        for (const QString &n : names)
        {
            if (!m_hiddenObjects.contains(n))
            {
                m_hiddenObjects.insert(n);
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
    m_selectedNames = names;
    m_needsRebuild = true;
    emit selectionChanged(names);
    emit repaintRequested();
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

QVariantMap SWMMModelLayer::identifyAt(double mapX, double mapY,
                                        const SpatialReferenceSystem * /*canvasSRS*/,
                                        double tolerance) const
{
    double bestDist2 = tolerance * tolerance;
    QVariantMap best;

    // Check nodes
    for (const NodeGeom &n : m_nodes)
    {
        double dx = n.x - mapX;
        double dy = n.y - mapY;
        double d2 = dx * dx + dy * dy;
        if (d2 < bestDist2)
        {
            bestDist2 = d2;
            best.clear();
            best[QStringLiteral("elementType")] = QStringLiteral("Node");
            best[QStringLiteral("elementName")] = n.name;
            best[QStringLiteral("x")]           = n.x;
            best[QStringLiteral("y")]           = n.y;
        }
    }

    // Check links (use midpoint of each segment)
    for (const LinkGeom &l : m_links)
    {
        const auto &verts = l.vertices;
        for (int i = 1; i < verts.size(); ++i)
        {
            double mx = (verts[i - 1].x() + verts[i].x()) / 2.0;
            double my = (verts[i - 1].y() + verts[i].y()) / 2.0;
            double dx = mx - mapX;
            double dy = my - mapY;
            double d2 = dx * dx + dy * dy;
            if (d2 < bestDist2)
            {
                bestDist2 = d2;
                best.clear();
                best[QStringLiteral("elementType")] = QStringLiteral("Link");
                best[QStringLiteral("elementName")] = l.name;
            }
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

    auto applyTransform = [this](double &x, double &y) {
        if (m_transform)
            m_transform->Transform(1, &x, &y);
    };

    // ---- Subcatchments ----
    if (m_showSubcatchments)
    {
        for (const CatchGeom &c : m_catchments)
        {
            if (m_hiddenObjects.contains(c.name))
                continue;
            QVector<QPointF> pts;
            pts.reserve(c.vertices.size());
            for (QPointF v : c.vertices)
            {
                double x = v.x(), y = v.y();
                applyTransform(x, y);
                pts.append(toScene(x, y));
            }

            bool sel = m_selectedNames.contains(c.name);
            QPolygonF poly(pts);
            auto *item = new CatchmentGraphicsItem(c.name, poly);
            item->setBrush(sel ? QBrush(QColor(255, 255, 0, 120)) : QBrush(m_subcatchSym.fillColor));

            // Dark green outline with rounded caps/joins so corners are smooth
            // rather than mitered/jagged. Cosmetic width keeps the stroke a
            // constant pixel thickness regardless of zoom.
            QPen pen(m_subcatchSym.outlineColor, m_subcatchSym.outlineWidth);
            pen.setCosmetic(true);
            pen.setCapStyle(Qt::RoundCap);
            pen.setJoinStyle(Qt::RoundJoin);
            item->setPen(pen);

            item->setHighlighted(sel);
            item->setOwnerLayer(this);
            item->setZValue(baseZ);
            item->setOpacity(opacity());
            item->setFlag(QGraphicsItem::ItemIsSelectable, true);
            scene->addItem(item);
        }
    }

    // ---- Links ----
    if (m_showLinks)
    {
        for (const LinkGeom &l : m_links)
        {
            if (m_hiddenObjects.contains(l.name))
                continue;

            QVector<QPointF> pts;
            pts.reserve(l.vertices.size());
            for (QPointF v : l.vertices)
            {
                double x = v.x(), y = v.y();
                applyTransform(x, y);
                pts.append(toScene(x, y));
            }

            if (pts.size() < 2)
                continue;

            bool sel = m_selectedNames.contains(l.name);
            const SWMMElementSymbol &sym = m_conduitSym;

            auto *item = new LinkGraphicsItem(l.name, pts);
            QPen pen(sel ? Qt::yellow : sym.fillColor, sym.outlineWidth + (sel ? 2 : 0));
            pen.setCosmetic(true);  // constant pixel width regardless of zoom
            item->setPen(pen);
            item->setHighlighted(sel);
            item->setOwnerLayer(this);
            item->setZValue(baseZ + 1);
            item->setOpacity(opacity());
            item->setFlag(QGraphicsItem::ItemIsSelectable, true);
            scene->addItem(item);
        }
    }

    // ---- Nodes ----
    if (m_showNodes)
    {
        // nodeType: 0=Junction, 1=Outfall, 2=Storage, 3=Divider — pick
        // symbology below; per-sub-type visibility is driven entirely by
        // the leaf checkboxes / m_hiddenObjects.
        for (const NodeGeom &n : m_nodes)
        {
            if (m_hiddenObjects.contains(n.name))
                continue;

            double x = n.x, y = n.y;
            applyTransform(x, y);

            // Pick symbology and shape by node type
            // nodeType: 0=Junction, 1=Outfall, 2=Storage, 3=Divider
            const SWMMElementSymbol *sym = &m_junctionSym;
            NodeGraphicsItem::NodeShape shape = NodeGraphicsItem::Circle;
            switch (n.nodeType)
            {
                case 1: sym = &m_outfallSym;  shape = NodeGraphicsItem::Triangle; break;
                case 2: sym = &m_storageSym;  shape = NodeGraphicsItem::Square;   break;
                case 3: sym = &m_dividerSym;  shape = NodeGraphicsItem::Diamond;  break;
                default: break;
            }

            bool sel = m_selectedNames.contains(n.name);
            auto *item = new NodeGraphicsItem(n.name, x, -y, sym->size / 2.0, shape);
            item->setBrush(QBrush(sel ? Qt::yellow : sym->fillColor));
            item->setPen(QPen(sym->outlineColor, sym->outlineWidth));
            item->setHighlighted(sel);
            item->setOwnerLayer(this);
            item->setZValue(baseZ + 2);
            item->setOpacity(opacity());
            item->setFlag(QGraphicsItem::ItemIsSelectable, true);
            scene->addItem(item);
        }
    }

    // ---- Rain gages ----
    if (m_showRainGages)
    {
        for (const NodeGeom &g : m_gages)
        {
            if (m_hiddenObjects.contains(g.name))
                continue;
            double x = g.x, y = g.y;
            applyTransform(x, y);

            auto *item = new NodeGraphicsItem(g.name, x, -y, m_gageSym.size / 2.0,
                                               NodeGraphicsItem::Diamond);
            item->setBrush(m_gageSym.fillColor);
            item->setPen(QPen(m_gageSym.outlineColor, m_gageSym.outlineWidth));
            item->setOwnerLayer(this);
            item->setZValue(baseZ + 3);
            item->setOpacity(opacity());
            item->setFlag(QGraphicsItem::ItemIsSelectable, true);
            scene->addItem(item);
        }
    }
}

void SWMMModelLayer::depopulateScene(QGraphicsScene *scene)
{
    if (!scene)
        return;

    const auto items = scene->items();
    QList<QGraphicsItem *> toRemove;
    for (auto *item : items)
    {
        if (auto *n = dynamic_cast<NodeGraphicsItem *>(item))
        { if (n->ownerLayer() == this) toRemove.append(item); }
        else if (auto *l = dynamic_cast<LinkGraphicsItem *>(item))
        { if (l->ownerLayer() == this) toRemove.append(item); }
        else if (auto *c = dynamic_cast<CatchmentGraphicsItem *>(item))
        { if (c->ownerLayer() == this) toRemove.append(item); }
    }

    for (auto *item : toRemove)
    {
        scene->removeItem(item);
        delete item;
    }

    // After depopulation the scene no longer carries this layer's items; a
    // subsequent refreshScene() must rebuild from the geometry cache rather
    // than short-circuit on `!m_needsRebuild`. This is what makes the
    // visibility checkbox round-trip correctly (off → on repopulates the
    // network without a manual extent change to force a rebuild).
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
    for (const NodeGeom &n : m_nodes)    if (n.name == name) return 1;
    for (const LinkGeom &l : m_links)    if (l.name == name) return 2;
    for (const CatchGeom &c : m_catchments) if (c.name == name) return 3;
    for (const NodeGeom &g : m_gages)    if (g.name == name) return 4;
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

double SWMMModelLayer::engineLinkLength(int linkIdx) const
{
    if (!m_engine || !isConduit(linkIdx))
        return -1.0;
    double len = 0.0;
    if (swmm_link_get_length(m_engine, linkIdx, &len) != 0)
        return -1.0;
    return len;
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
    }

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

    m_needsRebuild = true;
    emit repaintRequested();
    return true;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void SWMMModelLayer::buildGeometryCache()
{
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
        return;

    if (!srs()->ogrSpatialReference()->IsSame(canvasSRS->ogrSpatialReference()))
    {
        m_transform = OGRCreateCoordinateTransformation(
            srs()->ogrSpatialReference(),
            canvasSRS->ogrSpatialReference());
    }
}
