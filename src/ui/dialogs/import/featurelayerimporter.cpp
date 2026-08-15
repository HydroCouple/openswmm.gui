/*!
 * \file   featurelayerimporter.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/import/featurelayerimporter.h"

#include "layers/gisvectorlayer.h"
#include "layers/swmmmodellayer.h"
#include "map/importcommands.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "map/spatialreferencesystem.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_gages.h>
#include <openswmm/engine/openswmm_spatial.h>

#include <gdal_priv.h>
#include <ogrsf_frmts.h>

#include <QDateTime>

#include <memory>

namespace openswmmvis::import {

// ===========================================================================
// Construction
// ===========================================================================

FeatureLayerImporter::FeatureLayerImporter(GISVectorLayer *source,
                                           SWMMModelLayer *target,
                                           MapCanvas      *canvas,
                                           ImportMapping   mapping,
                                           QObject        *parent)
    : QObject(parent),
      m_source(source),
      m_target(target),
      m_canvas(canvas),
      m_mapping(std::move(mapping))
{
}

// ===========================================================================
// Phase 1 — main-thread capture
// ===========================================================================

SourceSpec FeatureLayerImporter::sourceSpec() const
{
    SourceSpec spec;
    if (!m_source) return spec;

    spec.filePath     = m_source->filePath();
    spec.layerName    = m_source->ogrLayerName();
    spec.filterExpr   = m_source->filterExpression();
    spec.selectedOnly = m_mapping.selectedFeaturesOnly;
    if (spec.selectedOnly)
        spec.selectedFids = m_source->selectedFeatureIds();

    if (const SpatialReferenceSystem *srs = m_source->srs())
        spec.sourceWkt = srs->toWkt();
    if (m_target)
        if (const SpatialReferenceSystem *dst = m_target->srs())
            spec.targetWkt = dst->toWkt();
    return spec;
}

ModelSnapshot FeatureLayerImporter::captureModelSnapshot() const
{
    ModelSnapshot snap;
    if (!m_target) return snap;
    SWMM_Engine eng = m_target->engine();
    if (!eng) return snap;

    const int nNodes = swmm_node_count(eng);
    for (int i = 0; i < nNodes; ++i) {
        const char *id = swmm_node_id(eng, i);
        if (!id) continue;
        const QString name = QString::fromUtf8(id);
        double x = 0.0, y = 0.0;
        swmm_spatial_get_node_coord(eng, i, &x, &y);
        int type = 0;
        swmm_node_get_type(eng, i, &type);
        snap.nodes.insert(name, QPointF(x, y));
        snap.nodeTypes.insert(name, type);
    }

    const int nGages = swmm_gage_count(eng);
    for (int i = 0; i < nGages; ++i) {
        const char *id = swmm_gage_id(eng, i);
        if (!id) continue;
        double x = 0.0, y = 0.0;
        swmm_spatial_get_gage_coord(eng, i, &x, &y);
        snap.gages.insert(QString::fromUtf8(id), QPointF(x, y));
    }

    const int nLinks = swmm_link_count(eng);
    for (int i = 0; i < nLinks; ++i) {
        const char *id = swmm_link_id(eng, i);
        if (!id) continue;
        const QString name = QString::fromUtf8(id);
        int type = 0;
        swmm_link_get_type(eng, i, &type);
        snap.linkTypes.insert(name, type);

        ModelSnapshot::LinkGeom geom;
        int fromIdx = -1, toIdx = -1;
        if (swmm_link_get_from_node(eng, i, &fromIdx) == 0 && fromIdx >= 0)
            if (const char *fn = swmm_node_id(eng, fromIdx))
                geom.from = QString::fromUtf8(fn);
        if (swmm_link_get_to_node(eng, i, &toIdx) == 0 && toIdx >= 0)
            if (const char *tn = swmm_node_id(eng, toIdx))
                geom.to = QString::fromUtf8(tn);

        // cachedLinkPolyline returns endpoints + interior in layer CRS;
        // strip the endpoints to get the interior list.
        const QVector<QPointF> poly = m_target->cachedLinkPolyline(i);
        if (poly.size() > 2)
            geom.interior = poly.mid(1, poly.size() - 2);
        snap.linkGeoms.insert(name, geom);
    }
    return snap;
}

// ===========================================================================
// Phase 2 — worker-safe feature read
// ===========================================================================

namespace {

QVariant ogrFieldToVariant(OGRFeature *feat, int fieldIdx)
{
    if (!feat->IsFieldSetAndNotNull(fieldIdx))
        return {};
    const OGRFieldDefn *defn = feat->GetFieldDefnRef(fieldIdx);
    switch (defn->GetType()) {
    case OFTInteger:
        if (defn->GetSubType() == OFSTBoolean)
            return QVariant(feat->GetFieldAsInteger(fieldIdx) != 0);
        return QVariant(feat->GetFieldAsInteger(fieldIdx));
    case OFTInteger64:
        return QVariant(static_cast<qlonglong>(
            feat->GetFieldAsInteger64(fieldIdx)));
    case OFTReal:
        return QVariant(feat->GetFieldAsDouble(fieldIdx));
    default:
        return QVariant(QString::fromUtf8(
            feat->GetFieldAsString(fieldIdx)));
    }
}

} // namespace

QVector<SourceFeature>
FeatureLayerImporter::readSourceFeatures(const SourceSpec &spec,
                                         QString *errorOut)
{
    QVector<SourceFeature> out;

    GDALDataset *ds = static_cast<GDALDataset *>(GDALOpenEx(
        spec.filePath.toUtf8().constData(),
        GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    if (!ds) {
        if (errorOut)
            *errorOut = tr("Could not open \"%1\" as a vector datasource.")
                            .arg(spec.filePath);
        return out;
    }
    const std::unique_ptr<GDALDataset> closer(ds);

    OGRLayer *layer = spec.layerName.isEmpty()
                          ? ds->GetLayer(0)
                          : ds->GetLayerByName(
                                spec.layerName.toUtf8().constData());
    if (!layer) {
        if (errorOut)
            *errorOut = tr("Layer \"%1\" not found in \"%2\".")
                            .arg(spec.layerName, spec.filePath);
        return out;
    }

    if (!spec.filterExpr.isEmpty())
        layer->SetAttributeFilter(spec.filterExpr.toUtf8().constData());

    // CRS transform (source → model). Both sides use the repo-wide
    // OAMS_TRADITIONAL_GIS_ORDER convention (spatialreferencesystem.cpp).
    OGRCoordinateTransformation *ct = nullptr;
    OGRSpatialReference srcSRS, dstSRS;
    if (!spec.sourceWkt.isEmpty() && !spec.targetWkt.isEmpty()) {
        const QByteArray src8 = spec.sourceWkt.toUtf8();
        const QByteArray dst8 = spec.targetWkt.toUtf8();
        if (srcSRS.importFromWkt(src8.constData()) == OGRERR_NONE
            && dstSRS.importFromWkt(dst8.constData()) == OGRERR_NONE) {
            srcSRS.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
            dstSRS.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
            if (!srcSRS.IsSame(&dstSRS))
                ct = OGRCreateCoordinateTransformation(&srcSRS, &dstSRS);
        }
    }

    const OGRFeatureDefn *layerDefn = layer->GetLayerDefn();
    const int fieldCount = layerDefn->GetFieldCount();

    layer->ResetReading();
    OGRFeature *feat = nullptr;
    while ((feat = layer->GetNextFeature()) != nullptr) {
        const std::unique_ptr<OGRFeature, decltype(&OGRFeature::DestroyFeature)>
            featCloser(feat, &OGRFeature::DestroyFeature);

        const long long fid = static_cast<long long>(feat->GetFID());
        if (spec.selectedOnly && !spec.selectedFids.contains(fid))
            continue;

        SourceFeature sf;
        sf.fid = fid;

        for (int i = 0; i < fieldCount; ++i) {
            const OGRFieldDefn *fd = feat->GetFieldDefnRef(i);
            sf.attrs.insert(QString::fromUtf8(fd->GetNameRef()),
                            ogrFieldToVariant(feat, i));
        }

        OGRGeometry *g = feat->GetGeometryRef();
        if (!g || g->IsEmpty()) {
            sf.geometryOk = false;
            sf.geometryError = tr("feature has no geometry");
            out.append(sf);
            continue;
        }

        const OGRwkbGeometryType flat = wkbFlatten(g->getGeometryType());
        const OGRLineString *line = nullptr;
        const OGRPoint      *pt   = nullptr;

        switch (flat) {
        case wkbPoint:
            pt = g->toPoint();
            break;
        case wkbMultiPoint: {
            const OGRMultiPoint *mp = g->toMultiPoint();
            if (mp->getNumGeometries() == 1)
                pt = mp->getGeometryRef(0)->toPoint();
            else {
                sf.geometryOk = false;
                sf.geometryError =
                    tr("multi-point with %1 parts is not importable")
                        .arg(mp->getNumGeometries());
            }
            break;
        }
        case wkbLineString:
            line = g->toLineString();
            break;
        case wkbMultiLineString: {
            const OGRMultiLineString *ml = g->toMultiLineString();
            if (ml->getNumGeometries() == 1)
                line = ml->getGeometryRef(0)->toLineString();
            else {
                sf.geometryOk = false;
                sf.geometryError =
                    tr("multi-part polyline with %1 parts is not importable")
                        .arg(ml->getNumGeometries());
            }
            break;
        }
        default:
            sf.geometryOk = false;
            sf.geometryError = tr("unsupported geometry type \"%1\"")
                                   .arg(QString::fromUtf8(
                                       OGRGeometryTypeToName(flat)));
            break;
        }

        if (pt) {
            double x = pt->getX(), y = pt->getY();
            if (ct) ct->Transform(1, &x, &y);
            sf.points.append(QPointF(x, y));
        } else if (line) {
            const int n = line->getNumPoints();
            sf.points.reserve(n);
            for (int i = 0; i < n; ++i) {
                double x = line->getX(i), y = line->getY(i);
                if (ct) ct->Transform(1, &x, &y);
                sf.points.append(QPointF(x, y));
            }
        }

        out.append(sf);
    }

    if (ct)
        OGRCoordinateTransformation::DestroyCT(ct);
    return out;
}

// ===========================================================================
// Phase 3 — main-thread execute (one undo macro)
// ===========================================================================

namespace {

/*! Translate targetKey → adapter-property map for one item. */
QVariantMap adapterValues(TargetKind kind, const QVariantMap &byTargetKey)
{
    QVariantMap byProperty;
    for (auto it = byTargetKey.constBegin();
         it != byTargetKey.constEnd(); ++it) {
        const TargetAttribute ta =
            ImportTargetRegistry::attribute(kind, it.key());
        if (!ta.adapterProperty.isEmpty())
            byProperty.insert(ta.adapterProperty, it.value());
    }
    return byProperty;
}

/*! Read the current values of \p keys through a fresh adapter (used to
 *  capture undo state — engine defaults after a create, prior values
 *  before an update). */
QVariantMap readAdapterValues(SWMMModelLayer *layer, quint8 kind,
                              const QString &name, const QVariantMap &keys)
{
    QVariantMap old;
    QObject *adapter =
        SetAdapterPropertiesCommand::createAdapter(layer, kind, name);
    if (!adapter) return old;
    for (auto it = keys.constBegin(); it != keys.constEnd(); ++it)
        old.insert(it.key(),
                   adapter->property(it.key().toUtf8().constData()));
    delete adapter;
    return old;
}

} // namespace

ImportPlan FeatureLayerImporter::execute(
        const ImportPlan &plan,
        const std::function<bool(int, int)> &progress)
{
    ImportPlan result = plan;
    if (!m_target || !m_canvas || !m_canvas->undoStack())
        return result;

    const TargetKind kind     = m_mapping.kind;
    const bool       linkKind = ImportTargetRegistry::isLinkKind(kind);
    const bool       gageKind = (kind == TargetKind::RainGage);
    const quint8 kindBit = linkKind ? SWMMModelLayer::kKindLink
                         : gageKind ? SWMMModelLayer::kKindGage
                                    : SWMMModelLayer::kKindNode;

    QUndoStack *stack = m_canvas->undoStack();
    const int total = result.items.size();

    stack->beginMacro(
        tr("Import %1 %2 object(s)")
            .arg(result.createCount + result.updateCount)
            .arg(ImportTargetRegistry::kindLabel(kind)));

    int done = 0;
    for (PlannedItem &item : result.items) {
        ++done;
        const bool keepGoing = !progress || progress(done, total);

        if (item.action == PlannedItem::Action::Skip
            || item.action == PlannedItem::Action::Error) {
            if (!keepGoing) break;
            continue;
        }

        if (item.action == PlannedItem::Action::Create) {
            // Auto-created endpoint junctions first (links only).
            for (int i = 0; i < item.autoNodeNames.size(); ++i) {
                stack->push(new AddNodeCommand(
                    m_target, item.autoNodeNames.at(i), /*Junction*/ 0,
                    item.autoNodePos.at(i).x(), item.autoNodePos.at(i).y(),
                    m_canvas));
                if (m_target->nodeIndex(item.autoNodeNames.at(i)) < 0) {
                    item.action = PlannedItem::Action::Error;
                    item.messages << tr("engine rejected junction \"%1\"")
                                         .arg(item.autoNodeNames.at(i));
                    break;
                }
            }
            if (item.action == PlannedItem::Action::Error) {
                if (!keepGoing) break;
                continue;
            }

            bool created = false;
            if (linkKind) {
                stack->push(new AddLinkCommand(
                    m_target, item.name,
                    ImportTargetRegistry::swmmLinkType(kind),
                    item.fromNode, item.toNode,
                    item.interiorVertices, m_canvas));
                created = m_target->linkIndex(item.name) >= 0;
            } else if (gageKind) {
                stack->push(new AddGageCommand(
                    m_target, item.name, item.x, item.y, m_canvas));
                created = SetAdapterPropertiesCommand::createAdapter(
                              m_target, kindBit, item.name) != nullptr;
            } else {
                stack->push(new AddNodeCommand(
                    m_target, item.name,
                    ImportTargetRegistry::swmmNodeType(kind),
                    item.x, item.y, m_canvas));
                created = m_target->nodeIndex(item.name) >= 0;
            }

            if (!created) {
                item.action = PlannedItem::Action::Error;
                item.messages << tr("engine rejected \"%1\"").arg(item.name);
                if (!keepGoing) break;
                continue;
            }

            const QVariantMap newVals = adapterValues(kind,
                                                      item.attributeValues);
            if (!newVals.isEmpty()) {
                const QVariantMap oldVals =
                    readAdapterValues(m_target, kindBit, item.name, newVals);
                stack->push(new SetAdapterPropertiesCommand(
                    m_target, kindBit, item.name, newVals, oldVals,
                    m_canvas));
            }
        } else {   // Update
            const QVariantMap newVals = adapterValues(kind,
                                                      item.attributeValues);
            if (!newVals.isEmpty()) {
                const QVariantMap oldVals =
                    readAdapterValues(m_target, kindBit, item.name, newVals);
                if (oldVals.isEmpty()) {
                    item.action = PlannedItem::Action::Error;
                    item.messages << tr("\"%1\" no longer exists")
                                         .arg(item.name);
                    if (!keepGoing) break;
                    continue;
                }
                stack->push(new SetAdapterPropertiesCommand(
                    m_target, kindBit, item.name, newVals, oldVals,
                    m_canvas));
            }

            if (item.geometryDiffers && m_mapping.updateGeometry) {
                if (linkKind) {
                    const int idx = m_target->linkIndex(item.name);
                    if (idx >= 0) {
                        const QVector<QPointF> poly =
                            m_target->cachedLinkPolyline(idx);
                        const QVector<QPointF> oldInterior =
                            poly.size() > 2 ? poly.mid(1, poly.size() - 2)
                                            : QVector<QPointF>();
                        stack->push(new SetLinkVerticesCommand(
                            m_target, item.name, oldInterior,
                            item.interiorVertices, m_canvas));
                    }
                } else if (!gageKind) {
                    const int idx = m_target->nodeIndex(item.name);
                    double oldX = 0.0, oldY = 0.0;
                    if (idx >= 0
                        && m_target->cachedNodeCoord(idx, &oldX, &oldY)) {
                        stack->push(new MoveNodeCommand(
                            m_target, idx, oldX, oldY, item.x, item.y,
                            {}, m_canvas));
                    }
                }
                // Rain-gage geometry updates are deferred (no
                // applyGageMove in the layer API today); the planner
                // never sets geometryDiffers for gages.
            }
        }

        if (!keepGoing) break;
    }

    stack->endMacro();

    m_canvas->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                         QStringLiteral("feature-import"));

    result.recount();
    return result;
}

} // namespace openswmmvis::import
