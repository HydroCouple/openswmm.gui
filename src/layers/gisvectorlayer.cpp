/*!
 * \file   gisvectorlayer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 */

#include "layers/gisvectorlayer.h"
#include "layers/gisvectorsymboladapter.h"
#include "ui/dialogs/ilayerstylesubject.h"
#include "map/graphicsitems.h"
#include "map/spatialreferencesystem.h"
#include "map/mapextent.h"

#include "render/ifeaturerenderer.h"
#include "render/renderers/singlesymbolrenderer.h"
// Slice B.3 — renderer ownership migrated to the Rule Model RuleList.
#include "render/rule.h"
#include "render/rulelist.h"
#include "render/symbollayer.h"
#include "render/symbolstyle.h"
#include "render/featureref.h"

#include <QGraphicsScene>
#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLoggingCategory>
#include <QPointer>
#include <QtConcurrent/QtConcurrentRun>

#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <ogr_spatialref.h>
#include <ogr_geometry.h>

// Load-phase profiling (opt-in). Enable with:
//   QT_LOGGING_RULES="openswmm.load.*=true"
// Times sublayer enumeration (GetFeatureCount forces a full scan on some
// drivers) and the open path (GDALOpenEx vs. GetExtent). Off by default.
Q_LOGGING_CATEGORY(lcLoadVector, "openswmm.load.vector")

// ---------------------------------------------------------------------------
// Rule ↔ GISVectorSymbol bridge
// ---------------------------------------------------------------------------
//
// The Rule Model (RENDERING_RULE_MODEL_PLAN.md §3) holds styling in a
// SymbolStyle of compositional SymbolLayers, each carrying an opaque
// QVariantMap of props. The SymbolStyleAdapter (Slice B.6c) writes user
// edits into the first SymbolLayer's prop bag using archetype-agnostic
// keys: "fillColor", "color", "outlineColor", "outlineWidth", "width",
// "size", "shape", "penStyle".
//
// GISVectorLayer's paint loop still consumes the typed GISVectorSymbol
// (point/line/polygon defaults in one value type). These helpers
// translate between the two so Symbology-tab edits reach the canvas
// without rewriting populateScene's per-geometry-type item construction.

namespace {

void seedSymbolStyleFromGISVectorSymbol(OpenSWMM::Render::SymbolStyle &style,
                                        const GISVectorSymbol &sym)
{
    using OpenSWMM::Render::SymbolLayer;
    using OpenSWMM::Render::SymbolLayerKind;

    SymbolLayer sl;
    sl.kind = SymbolLayerKind::SimpleMarker;
    // Marker props — read by point paint and by the SymbolStyleAdapter's
    // markerSize / markerShape / fillColor / outlineColor accessors.
    sl.props.insert(QStringLiteral("shape"),           int(sym.markerShape));
    sl.props.insert(QStringLiteral("size"),            sym.markerSize);
    sl.props.insert(QStringLiteral("fillColor"),       sym.markerFill);
    sl.props.insert(QStringLiteral("outlineColor"),    sym.markerOutline);
    sl.props.insert(QStringLiteral("outlineWidth"),    sym.markerOutlineW);
    sl.props.insert(QStringLiteral("outlinePenStyle"), int(Qt::SolidLine));
    // Line / polygon props — adapter writes "color"/"width" alongside
    // "outlineColor"/"outlineWidth" so line + polygon outline track the
    // marker outline edit. Seed them with the existing line pen + polygon
    // outline so the panel reads sensible initial values.
    sl.props.insert(QStringLiteral("color"),           sym.linePen.color());
    sl.props.insert(QStringLiteral("width"),           sym.linePen.widthF());
    sl.props.insert(QStringLiteral("penStyle"),        int(sym.linePen.style()));

    style.layers.clear();
    style.layers.append(sl);
    style.opacity = 1.0;
}

GISVectorSymbol deriveGISVectorSymbolFromStyle(const OpenSWMM::Render::SymbolStyle &style,
                                               const GISVectorSymbol &fallback)
{
    if (style.layers.isEmpty())
        return fallback;

    GISVectorSymbol out = fallback;
    const auto &p = style.layers.first().props;

    auto readColor = [&](const QString &key, const QColor &def) {
        const auto it = p.constFind(key);
        if (it == p.constEnd()) return def;
        const QColor c = it.value().value<QColor>();
        if (c.isValid()) return c;
        // SymbolStyleAdapter sometimes stores colours as their #AARRGGBB
        // name string — accept that form too.
        const QColor s(it.value().toString());
        return s.isValid() ? s : def;
    };
    auto readReal = [&](const QString &key, qreal def) {
        const auto it = p.constFind(key);
        if (it == p.constEnd()) return def;
        bool ok = false;
        const qreal v = it.value().toReal(&ok);
        return ok ? v : def;
    };
    auto readInt = [&](const QString &key, int def) {
        const auto it = p.constFind(key);
        if (it == p.constEnd()) return def;
        bool ok = false;
        const int v = it.value().toInt(&ok);
        return ok ? v : def;
    };

    // ── Marker (point features) ─────────────────────────────────────
    out.markerShape    = static_cast<GISVectorSymbol::MarkerShape>(
        readInt(QStringLiteral("shape"), int(out.markerShape)));
    out.markerSize     = readReal(QStringLiteral("size"),         out.markerSize);
    out.markerFill     = readColor(QStringLiteral("fillColor"),   out.markerFill);
    out.markerOutline  = readColor(QStringLiteral("outlineColor"), out.markerOutline);
    out.markerOutlineW = readReal(QStringLiteral("outlineWidth"), out.markerOutlineW);

    // ── Line (linestring features) ──────────────────────────────────
    // SymbolStyleAdapter writes "color"/"width" for the stroke; fall
    // back to the marker outline so a single colour edit affects both.
    QPen linePen = out.linePen;
    linePen.setColor(readColor(QStringLiteral("color"), linePen.color()));
    linePen.setWidthF(readReal(QStringLiteral("width"), linePen.widthF()));
    linePen.setStyle(static_cast<Qt::PenStyle>(
        readInt(QStringLiteral("penStyle"), int(linePen.style()))));
    out.linePen = linePen;

    // ── Polygon (polygon features) ──────────────────────────────────
    out.polygonFill = QBrush(readColor(QStringLiteral("fillColor"),
                                        out.polygonFill.color()));
    QPen polyOutline = out.polygonOutline;
    polyOutline.setColor(readColor(QStringLiteral("outlineColor"), polyOutline.color()));
    polyOutline.setWidthF(readReal(QStringLiteral("outlineWidth"), polyOutline.widthF()));
    out.polygonOutline = polyOutline;

    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

GISVectorLayer::GISVectorLayer(const QString &filePath,
                               const QString &layerName,
                               OpenSWMMVisWorkspace *parent)
    : OpenSWMMVisLayer(parent)
{
    setLayerType(SWMMVectorLayer);

    // Slice BI Phase 8.13.6.6 — renderer plumbing.  Default to a
    // SingleSymbolRenderer so renderer() never returns null.  Paint loop
    // still reads m_symbol directly; refactor deferred to 8.13.6.4.
    // Slice B.3 — RuleList is the canonical renderer home. Seed with one
    // Rule wrapping a default SingleSymbolRenderer so renderer() never
    // returns null. LayerStyleDialog's Symbology tab (Slice B.2) reads
    // m_ruleList and mounts RuleSymbologyTab.
    m_ruleList = std::make_unique<OpenSWMM::Render::RuleList>(this);
    {
        // Seed the default Rule's SingleSymbolRenderer with a SymbolStyle
        // populated from the layer's GISVectorSymbol defaults. Without
        // this seed the renderer ships an empty SymbolStyle and the
        // Symbology tab reads invalid colours / size 0 on first open.
        auto single = std::make_unique<OpenSWMM::Render::SingleSymbolRenderer>();
        OpenSWMM::Render::SymbolStyle style;
        seedSymbolStyleFromGISVectorSymbol(style, m_symbol);
        single->setSymbol(std::move(style));
        m_ruleList->append(std::make_unique<OpenSWMM::Render::Rule>(
            tr("Default"), std::move(single)));
    }

    // Propagate Rule-side edits to the canvas. The SymbolStyleAdapter
    // writes user edits into the active Rule's renderer prop bag and
    // calls notifyRendererStateChanged() — that surfaces here as
    // RuleList::ruleChanged. Derive a fresh GISVectorSymbol from the
    // renderer and feed it through setSymbol(), which marks the scene
    // dirty + emits repaintRequested.
    connect(m_ruleList.get(), &OpenSWMM::Render::RuleList::ruleChanged,
            this, [this](int) {
                syncSymbolFromRenderer();
                emit rendererChanged();
            });

    GDALAllRegister(); // Idempotent – safe to call multiple times

    if (!filePath.isEmpty())
        openDataset(filePath, layerName);
}

GISVectorLayer::~GISVectorLayer()
{
    closeDataset();
}

// ---------------------------------------------------------------------------
// Dataset info
// ---------------------------------------------------------------------------

QString GISVectorLayer::filePath()     const
{
    // Properties window shows full on-disk path for the dataset.
    return m_filePath.isEmpty()
               ? m_filePath
               : QFileInfo(m_filePath).absoluteFilePath();
}
QString GISVectorLayer::ogrLayerName() const { return m_ogrLayerName; }

QString GISVectorLayer::sourceDescription() const
{
    const QString p = filePath();
    return p.isEmpty() ? tr("(in-memory)") : p;
}

QVector<QPair<QString, QString>> GISVectorLayer::extendedMetadata() const
{
    QVector<QPair<QString, QString>> md;
    md.append({ tr("Features"), QString::number(featureCount()) });
    const QStringList fields = fieldNames();
    md.append({ tr("Fields"), QString::number(fields.size()) });
    if (!fields.isEmpty())
        md.append({ tr("Field names"), fields.join(QStringLiteral(", ")) });
    return md;
}

int GISVectorLayer::featureCount() const
{
    if (!m_ogrLayer)
        return 0;

    return static_cast<int>(m_ogrLayer->GetFeatureCount(/*bForce=*/false));
}

QStringList GISVectorLayer::fieldNames() const
{
    if (!m_ogrLayer)
        return {};

    OGRFeatureDefn *defn = m_ogrLayer->GetLayerDefn();
    QStringList names;
    for (int i = 0; i < defn->GetFieldCount(); ++i)
        names.append(QString::fromUtf8(defn->GetFieldDefn(i)->GetNameRef()));

    return names;
}

// ---------------------------------------------------------------------------
// Slice DM.3 — IAttributeProvider
// ---------------------------------------------------------------------------
//
// Walks the OGR field defn and wraps each entry as an AttributeField.
// Type maps OGR field types to Qt metatype enums best-effort. All
// fields are isDynamic=false (vector attributes are static). Category
// arg ignored — GIS layers don't carry a SWMM category.

QVector<OpenSWMM::Render::AttributeField>
GISVectorLayer::availableAttributes(OpenSWMMVis::SwmmCategory /*cat*/) const
{
    using OpenSWMM::Render::AttributeField;
    QVector<AttributeField> out;
    if (!m_ogrLayer) return out;

    OGRFeatureDefn *defn = m_ogrLayer->GetLayerDefn();
    out.reserve(defn->GetFieldCount());
    for (int i = 0; i < defn->GetFieldCount(); ++i) {
        OGRFieldDefn *fd = defn->GetFieldDefn(i);
        AttributeField f;
        f.name        = QString::fromUtf8(fd->GetNameRef());
        f.displayName = f.name;
        f.isDynamic   = false;
        switch (fd->GetType()) {
        case OFTInteger:
        case OFTInteger64:  f.type = QMetaType::LongLong; break;
        case OFTReal:       f.type = QMetaType::Double;   break;
        case OFTString:     f.type = QMetaType::QString;  break;
        case OFTDate:
        case OFTDateTime:   f.type = QMetaType::QDateTime;break;
        default:            f.type = QMetaType::QString;  break;
        }
        out.append(f);
    }
    return out;
}

void GISVectorLayer::appendScenePolygonsTo(QPainterPath &out) const
{
    // Slice Z.14-paint — Yields the layer's polygon geometry in scene
    // coords for the mask-clip resolver. The OGR walk + Y-flip mirrors
    // populateScene's polygon branch (avoiding a refactor of that path
    // which is hot during canvas paint).
    if (!m_ogrLayer) return;

    // Save + clear the canvas's spatial filter so we walk every feature
    // regardless of what canvasExtent populateScene last set. Restore
    // on the way out so concurrent canvas painting isn't disturbed.
    OGRGeometry *priorFilter = m_ogrLayer->GetSpatialFilter();
    OGRGeometry *priorFilterCopy = priorFilter ? priorFilter->clone() : nullptr;
    m_ogrLayer->SetSpatialFilter(nullptr);
    m_ogrLayer->ResetReading();

    auto addRing = [&](OGRLinearRing *ring) {
        const int n = ring ? ring->getNumPoints() : 0;
        if (n < 3) return;
        // Mirror populateScene: scene-coords are map-coords with Y
        // flipped. populateScene calls toScene(x, y) after the OGR
        // transform; we replicate that step inline since toScene is
        // file-local in gisvectorlayer.cpp.
        out.moveTo(QPointF(ring->getX(0), -ring->getY(0)));
        for (int i = 1; i < n; ++i)
            out.lineTo(QPointF(ring->getX(i), -ring->getY(i)));
        out.closeSubpath();
    };

    OGRFeature *feat = nullptr;
    while ((feat = m_ogrLayer->GetNextFeature()) != nullptr) {
        OGRGeometry *geom = feat->GetGeometryRef();
        if (geom && m_transform) geom->transform(m_transform);
        if (!geom) { OGRFeature::DestroyFeature(feat); continue; }

        const OGRwkbGeometryType gt = wkbFlatten(geom->getGeometryType());
        if (gt == wkbPolygon) {
            auto *poly = geom->toPolygon();
            addRing(poly->getExteriorRing());
            for (int h = 0; h < poly->getNumInteriorRings(); ++h)
                addRing(poly->getInteriorRing(h));
        } else if (gt == wkbMultiPolygon) {
            auto *mp = geom->toMultiPolygon();
            for (int p = 0; p < mp->getNumGeometries(); ++p) {
                auto *poly = mp->getGeometryRef(p)->toPolygon();
                if (!poly) continue;
                addRing(poly->getExteriorRing());
                for (int h = 0; h < poly->getNumInteriorRings(); ++h)
                    addRing(poly->getInteriorRing(h));
            }
        }
        // Non-polygon types (lines / points) silently skipped — the
        // mask-source contract is "polygon vector layer".
        OGRFeature::DestroyFeature(feat);
    }

    m_ogrLayer->SetSpatialFilter(priorFilterCopy);
    OGRGeometryFactory::destroyGeometry(priorFilterCopy);
}

// ---------------------------------------------------------------------------
// Filtering
// ---------------------------------------------------------------------------

QString GISVectorLayer::filterExpression() const { return m_filterExpr; }

void GISVectorLayer::setFilterExpression(const QString &expr)
{
    if (m_filterExpr == expr)
        return;

    m_filterExpr = expr;

    if (m_ogrLayer)
    {
        m_ogrLayer->SetAttributeFilter(
            expr.isEmpty() ? nullptr : expr.toUtf8().constData());
    }

    m_needsRebuild = true;
    emit filterExpressionChanged(expr);
    emit repaintRequested();
}

// ---------------------------------------------------------------------------
// Symbology
// ---------------------------------------------------------------------------

GISVectorSymbol GISVectorLayer::symbol() const { return m_symbol; }

// ---------------------------------------------------------------------------
// GISVectorSymbol JSON round-trip (Slice X.19)
// ---------------------------------------------------------------------------
//
// Stored flat — older readers can pick out the fields they understand
// and ignore newer ones.  Pens/brushes round-trip via color + width to
// stay schema-friendly (Qt's QPen::write isn't human-readable JSON).

QJsonObject GISVectorSymbol::toJson() const
{
    QJsonObject j;
    j[QStringLiteral("markerShape")]   = int(markerShape);
    j[QStringLiteral("markerSize")]    = markerSize;
    j[QStringLiteral("markerFill")]    = markerFill.name(QColor::HexArgb);
    j[QStringLiteral("markerOutline")] = markerOutline.name(QColor::HexArgb);
    j[QStringLiteral("markerOutlineW")] = markerOutlineW;

    j[QStringLiteral("lineColor")]     = linePen.color().name(QColor::HexArgb);
    j[QStringLiteral("lineWidth")]     = linePen.widthF();
    j[QStringLiteral("lineDash")]      = int(linePen.style());

    j[QStringLiteral("polygonFill")]   = polygonFill.color().name(QColor::HexArgb);
    j[QStringLiteral("polygonOutline")] = polygonOutline.color().name(QColor::HexArgb);
    j[QStringLiteral("polygonOutlineW")] = polygonOutline.widthF();

    // Labels — write both the legacy scalars AND the rich LabelConfig.
    j[QStringLiteral("showLabels")] = showLabels;
    if (!labelField.isEmpty())
        j[QStringLiteral("labelField")] = labelField;
    j[QStringLiteral("labelColor")]    = labelColor.name(QColor::HexArgb);
    j[QStringLiteral("labelFontFamily")] = labelFont.family();
    j[QStringLiteral("labelFontSize")] = labelFont.pointSizeF() > 0
        ? labelFont.pointSizeF() : 9.0;
    j[QStringLiteral("labelConfig")]   = labelConfig.toJson();
    return j;
}

void GISVectorSymbol::fromJson(const QJsonObject &j)
{
    if (j.contains(QStringLiteral("markerShape")))
        markerShape = static_cast<MarkerShape>(
            j.value(QStringLiteral("markerShape")).toInt(int(Circle)));
    if (j.contains(QStringLiteral("markerSize")))
        markerSize = j.value(QStringLiteral("markerSize")).toDouble(6.0);
    if (j.contains(QStringLiteral("markerFill"))) {
        const QColor c(j.value(QStringLiteral("markerFill")).toString());
        if (c.isValid()) markerFill = c;
    }
    if (j.contains(QStringLiteral("markerOutline"))) {
        const QColor c(j.value(QStringLiteral("markerOutline")).toString());
        if (c.isValid()) markerOutline = c;
    }
    if (j.contains(QStringLiteral("markerOutlineW")))
        markerOutlineW = j.value(QStringLiteral("markerOutlineW")).toDouble(1.0);

    if (j.contains(QStringLiteral("lineColor"))) {
        const QColor c(j.value(QStringLiteral("lineColor")).toString());
        if (c.isValid()) linePen.setColor(c);
    }
    if (j.contains(QStringLiteral("lineWidth")))
        linePen.setWidthF(j.value(QStringLiteral("lineWidth")).toDouble(2.0));
    if (j.contains(QStringLiteral("lineDash")))
        linePen.setStyle(static_cast<Qt::PenStyle>(
            j.value(QStringLiteral("lineDash")).toInt(int(Qt::SolidLine))));

    if (j.contains(QStringLiteral("polygonFill"))) {
        const QColor c(j.value(QStringLiteral("polygonFill")).toString());
        if (c.isValid()) polygonFill = QBrush(c);
    }
    if (j.contains(QStringLiteral("polygonOutline"))) {
        const QColor c(j.value(QStringLiteral("polygonOutline")).toString());
        if (c.isValid()) polygonOutline.setColor(c);
    }
    if (j.contains(QStringLiteral("polygonOutlineW")))
        polygonOutline.setWidthF(j.value(QStringLiteral("polygonOutlineW")).toDouble(2.0));

    if (j.contains(QStringLiteral("showLabels")))
        showLabels = j.value(QStringLiteral("showLabels")).toBool(false);
    if (j.contains(QStringLiteral("labelField")))
        labelField = j.value(QStringLiteral("labelField")).toString();
    if (j.contains(QStringLiteral("labelColor"))) {
        const QColor c(j.value(QStringLiteral("labelColor")).toString());
        if (c.isValid()) labelColor = c;
    }
    if (j.contains(QStringLiteral("labelFontFamily")))
        labelFont.setFamily(j.value(QStringLiteral("labelFontFamily")).toString());
    if (j.contains(QStringLiteral("labelFontSize")))
        labelFont.setPointSizeF(j.value(QStringLiteral("labelFontSize")).toDouble(9.0));
    if (j.contains(QStringLiteral("labelConfig")))
        labelConfig.fromJson(j.value(QStringLiteral("labelConfig")).toObject());
}

void GISVectorLayer::setSymbol(const GISVectorSymbol &symbol)
{
    m_symbol = symbol;
    m_needsRebuild = true;
    emit symbolChanged(symbol);
    emit repaintRequested();
}

void GISVectorLayer::syncSymbolFromRenderer()
{
    auto *single = dynamic_cast<OpenSWMM::Render::SingleSymbolRenderer *>(renderer());
    if (!single)
        return;  // Graduated / Categorized — per-feature dispatch follow-up.

    // SingleSymbol returns the same style for every feature, so an
    // empty FeatureRef + empty attrs is sufficient.
    const OpenSWMM::Render::SymbolStyle style =
        single->symbolFor(OpenSWMM::Render::FeatureRef{}, QVariantMap{});
    const GISVectorSymbol derived = deriveGISVectorSymbolFromStyle(style, m_symbol);
    if (derived.markerShape    == m_symbol.markerShape    &&
        qFuzzyCompare(derived.markerSize + 1.0, m_symbol.markerSize + 1.0) &&
        derived.markerFill     == m_symbol.markerFill     &&
        derived.markerOutline  == m_symbol.markerOutline  &&
        qFuzzyCompare(derived.markerOutlineW + 1.0, m_symbol.markerOutlineW + 1.0) &&
        derived.linePen        == m_symbol.linePen        &&
        derived.polygonFill    == m_symbol.polygonFill    &&
        derived.polygonOutline == m_symbol.polygonOutline)
        return;  // No-op edits (e.g. blendMode-only change) skip the rebuild.
    setSymbol(derived);
}

// ---------------------------------------------------------------------------
// Label configuration (Slice X.18)
// ---------------------------------------------------------------------------

// VS.10 — labelConfig() inherited from OpenSWMMVisLayer; only the setter is
// overridden to mirror the relevant fields onto the legacy GISVectorSymbol
// bag, then chain to the base which stores the config + emits the signals.
void GISVectorLayer::setLabelConfig(const OpenSWMM::Render::LabelConfig &cfg)
{
    if (labelConfig() == cfg) return;
    GISVectorSymbol sym = m_symbol;
    sym.showLabels  = cfg.enabled;
    sym.labelField  = cfg.fieldName;
    sym.labelColor  = cfg.color;
    sym.labelFont   = cfg.effectiveFont();
    sym.labelConfig = cfg;     // Slice X.19 — full mirror for JSON round-trip.
    if (sym.showLabels != m_symbol.showLabels
        || sym.labelField != m_symbol.labelField
        || sym.labelColor != m_symbol.labelColor
        || sym.labelFont  != m_symbol.labelFont)
    {
        m_symbol = sym;
        emit symbolChanged(m_symbol);
    }
    OpenSWMMVisLayer::setLabelConfig(cfg);   // stores + emits labelConfigChanged + repaint
}

QStringList GISVectorLayer::ogrFieldNames() const
{
    if (!m_ogrLayer) return {};
    QStringList names;
    OGRFeatureDefn *defn = m_ogrLayer->GetLayerDefn();
    if (!defn) return names;
    for (int i = 0; i < defn->GetFieldCount(); ++i) {
        if (OGRFieldDefn *fd = defn->GetFieldDefn(i))
            names << QString::fromUtf8(fd->GetNameRef());
    }
    return names;
}

// Slice U-7 — expose the GISVectorSymbol via a QObject adapter (owned by
// this layer) as the single styleable subject. The adapter forwards
// edits to setSymbol() which already flags the rebuild + emits
// symbolChanged + repaintRequested.
std::vector<std::unique_ptr<openswmmvis::ui::ILayerStyleSubject>>
GISVectorLayer::styleSubjects()
{
    using openswmmvis::ui::ILayerStyleSubject;
    using openswmmvis::ui::LayerStyleSubject;

    auto *adapter = new GisVectorSymbolAdapter(
        m_symbol,
        [this](const GISVectorSymbol &s) { setSymbol(s); },
        this);

    std::vector<std::unique_ptr<ILayerStyleSubject>> out;
    out.push_back(std::make_unique<LayerStyleSubject>(
        tr("Symbology"), adapter, QStringLiteral("vector.symbol"), QString()));
    return out;
}

// ---------------------------------------------------------------------------
// Renderer (Slice BI Phase 8.13.6.6)
// ---------------------------------------------------------------------------

OpenSWMM::Render::IFeatureRenderer *GISVectorLayer::renderer() const
{
    // Slice B.3 — delegate to the active Rule's owned renderer.
    if (!m_ruleList) return nullptr;
    OpenSWMM::Render::Rule *r = m_ruleList->activeRule();
    return r ? r->renderer() : nullptr;
}

void GISVectorLayer::setRenderer(std::unique_ptr<OpenSWMM::Render::IFeatureRenderer> r)
{
    if (!r || !m_ruleList)
        return;
    OpenSWMM::Render::Rule *active = m_ruleList->activeRule();
    if (!active)
        return;
    if (active->renderer() == r.get())
        return;
    active->setRenderer(std::move(r));
    // Rule's ruleChanged signal already routes through to
    // rendererChanged via the connect() in the ctor. Emit explicitly
    // here too so callers that bypass the Rule path still see the
    // signal in the same dispatch.
    emit rendererChanged();
}

OpenSWMM::Render::RuleList *GISVectorLayer::ruleList()
{
    return m_ruleList.get();
}

const OpenSWMM::Render::RuleList *GISVectorLayer::ruleList() const
{
    return m_ruleList.get();
}

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------

QSet<long long> GISVectorLayer::selectedFeatureIds() const { return m_selectedIds; }

void GISVectorLayer::setSelectedFeatureIds(const QSet<long long> &ids)
{
    m_selectedIds = ids;
    m_needsRebuild = true;
    emit selectionChanged(ids);
    emit repaintRequested();
}

void GISVectorLayer::clearSelection()
{
    setSelectedFeatureIds({});
}

// ---------------------------------------------------------------------------
// Identify  (overloaded for MapTool convenience – no canvasSRS needed)
// ---------------------------------------------------------------------------

QList<QVariantMap> GISVectorLayer::identifyAt(double mapX, double mapY,
                                               double tolerance) const
{
    return identifyAt(mapX, mapY, nullptr, tolerance);
}

QList<QVariantMap> GISVectorLayer::identifyAt(double mapX, double mapY,
                                               const SpatialReferenceSystem * /*canvasSRS*/,
                                               double tolerance) const
{
    QList<QVariantMap> results;

    if (!m_ogrLayer)
        return results;

    // Set a spatial filter around the click point
    m_ogrLayer->SetSpatialFilterRect(mapX - tolerance, mapY - tolerance,
                                     mapX + tolerance, mapY + tolerance);
    m_ogrLayer->ResetReading();

    OGRFeature *feat = nullptr;
    while ((feat = m_ogrLayer->GetNextFeature()) != nullptr)
    {
        QVariantMap attrs;
        attrs[QStringLiteral("fid")] = static_cast<long long>(feat->GetFID());

        const OGRFeatureDefn *defn = feat->GetDefnRef();
        for (int i = 0; i < defn->GetFieldCount(); ++i)
        {
            QString fname = QString::fromUtf8(defn->GetFieldDefn(i)->GetNameRef());
            OGRFieldType type = defn->GetFieldDefn(i)->GetType();

            if (!feat->IsFieldSet(i) || feat->IsFieldNull(i))
            {
                attrs[fname] = QVariant{};
            }
            else if (type == OFTInteger)
            {
                attrs[fname] = feat->GetFieldAsInteger(i);
            }
            else if (type == OFTInteger64)
            {
                attrs[fname] = static_cast<long long>(feat->GetFieldAsInteger64(i));
            }
            else if (type == OFTReal)
            {
                attrs[fname] = feat->GetFieldAsDouble(i);
            }
            else
            {
                attrs[fname] = QString::fromUtf8(feat->GetFieldAsString(i));
            }
        }

        results.append(attrs);
        OGRFeature::DestroyFeature(feat);
    }

    // Clear spatial filter
    m_ogrLayer->SetSpatialFilter(nullptr);

    return results;
}

// ---------------------------------------------------------------------------
// Scene population (QGraphicsScene / QGraphicsItems)
// ---------------------------------------------------------------------------

/*!
 * \brief Converts a map coordinate to scene coordinates (Y-flipped).
 */
static inline QPointF toScene(double mapX, double mapY)
{
    return QPointF(mapX, -mapY);
}

void GISVectorLayer::populateScene(QGraphicsScene *scene,
                                    const MapExtent &canvasExtent,
                                    const SpatialReferenceSystem * /*canvasSRS*/)
{
    if (!m_ogrLayer || !isVisible())
        return;

    // Spatial filter must be in LAYER CRS (OGR layer holds layer-CRS data).
    // Two cases:
    //   - layer CRS == canvas CRS  →  m_transform is null  →  use canvas
    //     extent directly (matching CRSes).
    //   - layer CRS != canvas CRS  →  m_transform exists   →  inverse-
    //     transform the four corners of canvasExtent into layer CRS, take
    //     their bounding box. Without this, the filter rejects every
    //     feature and the shapefile "doesn't render".
    if (!m_transform)
    {
        m_ogrLayer->SetSpatialFilterRect(canvasExtent.xMin(), canvasExtent.yMin(),
                                         canvasExtent.xMax(), canvasExtent.yMax());
    }
    else if (auto *inv = m_transform->GetInverse())
    {
        double xs[4] = {canvasExtent.xMin(), canvasExtent.xMax(),
                        canvasExtent.xMax(), canvasExtent.xMin()};
        double ys[4] = {canvasExtent.yMin(), canvasExtent.yMin(),
                        canvasExtent.yMax(), canvasExtent.yMax()};
        if (inv->Transform(4, xs, ys))
        {
            double xMin = xs[0], xMax = xs[0], yMin = ys[0], yMax = ys[0];
            for (int i = 1; i < 4; ++i)
            {
                xMin = qMin(xMin, xs[i]); xMax = qMax(xMax, xs[i]);
                yMin = qMin(yMin, ys[i]); yMax = qMax(yMax, ys[i]);
            }
            m_ogrLayer->SetSpatialFilterRect(xMin, yMin, xMax, yMax);
        }
        else
        {
            // Inverse transform failed (e.g. PROJ-side error) — fall back
            // to no filter rather than silently dropping features.
            m_ogrLayer->SetSpatialFilter(nullptr);
        }
        OGRCoordinateTransformation::DestroyCT(inv);
    }
    else
    {
        m_ogrLayer->SetSpatialFilter(nullptr);
    }
    m_ogrLayer->ResetReading();

    const double baseZ = layerZValue();

    auto addPoint = [&](double mx, double my, qint64 fid, bool selected) {
        auto *item = new VectorPointItem(fid, mx, -my, m_symbol.markerSize / 2.0);
        item->setBrush(QBrush(selected ? Qt::yellow : m_symbol.markerFill));
        item->setPen(QPen(m_symbol.markerOutline, m_symbol.markerOutlineW));
        item->setMarkerShape(int(m_symbol.markerShape));   // G-1 — canonical shape
        item->setHighlighted(selected);
        item->setOwnerLayer(this);
        item->setZValue(baseZ + 2);
        item->setOpacity(opacity());
        item->setFlag(QGraphicsItem::ItemIsSelectable, true);
        scene->addItem(item);
        m_sceneItems.append(item);
    };

    auto addLine = [&](const QVector<QPointF> &scenePts, qint64 fid, bool selected) {
        if (scenePts.size() < 2)
            return;
        auto *item = new VectorLineItem(fid, scenePts);
        QPen pen = selected ? QPen(Qt::yellow, m_symbol.linePen.widthF() + 2)
                            : m_symbol.linePen;
        // Cosmetic pen → width stays in screen pixels regardless of zoom.
        // Without this, a layer in a projected CRS (e.g. coords ~1e7) at
        // canvas scale ~1e-3 px/unit renders pen widths < 0.01 viewport
        // pixels — invisible. The SWMM model layer escapes this via
        // Slice R's per-frame painter, but generic GIS-vector items use
        // standard QGraphicsItems.
        pen.setCosmetic(true);
        item->setPen(pen);
        item->setHighlighted(selected);
        item->setOwnerLayer(this);
        item->setZValue(baseZ + 1);
        item->setOpacity(opacity());
        item->setFlag(QGraphicsItem::ItemIsSelectable, true);
        scene->addItem(item);
        m_sceneItems.append(item);
    };

    auto addPolygon = [&](const QVector<QPointF> &scenePts, qint64 fid, bool selected) {
        if (scenePts.size() < 3)
            return;
        QPolygonF poly(scenePts);
        auto *item = new VectorPolygonItem(fid, poly);
        QBrush brush = selected ? QBrush(QColor(255, 255, 0, 100)) : m_symbol.polygonFill;
        item->setBrush(brush);
        QPen polyPen = m_symbol.polygonOutline;
        polyPen.setCosmetic(true);
        item->setPen(polyPen);
        item->setHighlighted(selected);
        item->setOwnerLayer(this);
        item->setZValue(baseZ);
        item->setOpacity(opacity());
        item->setFlag(QGraphicsItem::ItemIsSelectable, true);
        scene->addItem(item);
        m_sceneItems.append(item);
    };

    OGRFeature *feat = nullptr;
    while ((feat = m_ogrLayer->GetNextFeature()) != nullptr)
    {
        qint64 fid = static_cast<qint64>(feat->GetFID());
        bool selected = m_selectedIds.contains(static_cast<long long>(fid));

        OGRGeometry *geom = feat->GetGeometryRef();
        if (!geom) { OGRFeature::DestroyFeature(feat); continue; }
        if (m_transform)
            geom->transform(m_transform);

        {
            OGRwkbGeometryType gt = wkbFlatten(geom->getGeometryType());

            if (gt == wkbPoint)
            {
                auto *pt = geom->toPoint();
                addPoint(pt->getX(), pt->getY(), fid, selected);
            }
            else if (gt == wkbLineString)
            {
                auto *ls = geom->toLineString();
                QVector<QPointF> pts;
                pts.reserve(ls->getNumPoints());
                for (int i = 0; i < ls->getNumPoints(); ++i)
                    pts.append(toScene(ls->getX(i), ls->getY(i)));
                addLine(pts, fid, selected);
            }
            else if (gt == wkbPolygon)
            {
                auto *poly = geom->toPolygon();
                OGRLinearRing *ring = poly->getExteriorRing();
                QVector<QPointF> pts;
                pts.reserve(ring->getNumPoints());
                for (int i = 0; i < ring->getNumPoints(); ++i)
                    pts.append(toScene(ring->getX(i), ring->getY(i)));
                addPolygon(pts, fid, selected);
            }
            else if (gt == wkbMultiPoint)
            {
                const auto *mp = geom->toMultiPoint();
                for (int j = 0; j < mp->getNumGeometries(); ++j)
                {
                    auto *pt = mp->getGeometryRef(j)->toPoint();
                    addPoint(pt->getX(), pt->getY(), fid, selected);
                }
            }
            else if (gt == wkbMultiLineString)
            {
                const auto *ml = geom->toMultiLineString();
                for (int j = 0; j < ml->getNumGeometries(); ++j)
                {
                    auto *ls = ml->getGeometryRef(j)->toLineString();
                    QVector<QPointF> pts;
                    for (int k = 0; k < ls->getNumPoints(); ++k)
                        pts.append(toScene(ls->getX(k), ls->getY(k)));
                    addLine(pts, fid, selected);
                }
            }
            else if (gt == wkbMultiPolygon)
            {
                const auto *mpoly = geom->toMultiPolygon();
                for (int j = 0; j < mpoly->getNumGeometries(); ++j)
                {
                    const auto *poly = mpoly->getGeometryRef(j)->toPolygon();
                    const OGRLinearRing *ring = poly->getExteriorRing();
                    QVector<QPointF> pts;
                    for (int k = 0; k < ring->getNumPoints(); ++k)
                        pts.append(toScene(ring->getX(k), ring->getY(k)));
                    addPolygon(pts, fid, selected);
                }
            }
        }

        OGRFeature::DestroyFeature(feat);
    }

    // Remove spatial filter when done
    m_ogrLayer->SetSpatialFilter(nullptr);
}

void GISVectorLayer::depopulateScene(QGraphicsScene *scene)
{
    if (!scene || m_sceneItems.isEmpty())
        return;

    for (auto *item : std::as_const(m_sceneItems))
    {
        scene->removeItem(item);
        delete item;
    }
    m_sceneItems.clear();
    m_needsRebuild = true;
}

void GISVectorLayer::onCanvasCRSChanged(const SpatialReferenceSystem *newCanvasSRS)
{
    rebuildTransform(newCanvasSRS);
    m_needsRebuild = true;
}

void GISVectorLayer::refreshScene(QGraphicsScene *scene,
                                   const MapExtent &canvasExtent,
                                   const SpatialReferenceSystem *canvasSRS)
{
    if (!m_needsRebuild) {
        // Geometry/symbol unchanged — but a layer-opacity edit (from the
        // tree's Opacity column) doesn't flag a rebuild, yet must still
        // reflect. Re-apply the current opacity to the cached items in
        // place; cheap and avoids a full re-read of the OGR source.
        for (auto *item : std::as_const(m_sceneItems))
            if (item) item->setOpacity(opacity());
        return;
    }

    depopulateScene(scene);
    populateScene(scene, canvasExtent, canvasSRS);
    m_needsRebuild = false;
}

// ---------------------------------------------------------------------------
// Multi-layer enumeration (static — no instance, no full load)
// ---------------------------------------------------------------------------

QList<GISVectorLayer::OgrSublayerInfo>
GISVectorLayer::enumerateSublayers(const QString &filePath, QString *errorOut)
{
    QList<OgrSublayerInfo> out;

    QElapsedTimer enumTimer;
    enumTimer.start();

    GDALAllRegister();  // idempotent

    auto *ds = static_cast<GDALDataset *>(
        GDALOpenEx(filePath.toUtf8().constData(),
                   GDAL_OF_VECTOR | GDAL_OF_READONLY,
                   nullptr, nullptr, nullptr));
    if (!ds)
    {
        if (errorOut)
            *errorOut = QStringLiteral("Not a readable vector datasource: %1").arg(filePath);
        return out;
    }

    const int layerCount = ds->GetLayerCount();
    out.reserve(layerCount);
    for (int i = 0; i < layerCount; ++i)
    {
        OGRLayer *lyr = ds->GetLayer(i);
        if (!lyr)
            continue;

        OgrSublayerInfo info;
        info.index        = i;
        info.name         = QString::fromUtf8(lyr->GetName());
        info.geometryType = QString::fromUtf8(OGRGeometryTypeToName(lyr->GetGeomType()));
        info.featureCount = static_cast<long long>(lyr->GetFeatureCount(/*bForce=*/true));

        if (const OGRSpatialReference *srs = lyr->GetSpatialRef())
        {
            if (const char *name = srs->GetName(); name && *name)
                info.crsDescription = QString::fromUtf8(name);
            else
            {
                const char *auth = srs->GetAuthorityName(nullptr);
                const char *code = srs->GetAuthorityCode(nullptr);
                if (auth && code)
                    info.crsDescription = QStringLiteral("%1:%2")
                                              .arg(QString::fromUtf8(auth),
                                                   QString::fromUtf8(code));
            }
        }

        out.append(info);
    }

    GDALClose(ds);
    qCInfo(lcLoadVector).noquote()
        << QStringLiteral("%1: enumerated %2 sublayer(s) in %3 ms "
                          "(GetFeatureCount forces a scan on some drivers)")
               .arg(QFileInfo(filePath).fileName())
               .arg(out.size())
               .arg(enumTimer.elapsed());
    return out;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

// Worker-thread payload — see gisvectorlayer.h. Plain values plus the owned
// GDALDataset handle and a non-owning OGRLayer pointer into it (single-owner,
// handed to the GUI thread on completion); no QObject state is touched
// off-thread.
struct GISVectorLayer::OpenResult
{
    GDALDataset *dataset = nullptr;
    OGRLayer    *ogrLayer = nullptr;
    QString      filePath;
    QString      layerName;
    bool         hasExtent = false;
    MapExtent    extent;
    QString      wkt;
    qint64       msOpen = 0, msTotal = 0;
};

GISVectorLayer::OpenResult GISVectorLayer::doOpenWork(const QString &filePath,
                                                     const QString &layerName)
{
    QElapsedTimer loadTimer;
    loadTimer.start();

    OpenResult r;
    r.filePath = filePath;

    r.dataset = static_cast<GDALDataset *>(
        GDALOpenEx(filePath.toUtf8().constData(),
                   GDAL_OF_VECTOR | GDAL_OF_READONLY,
                   nullptr, nullptr, nullptr));
    if (!r.dataset)
    {
        qWarning() << "GISVectorLayer: failed to open" << filePath;
        return r;
    }

    r.ogrLayer = layerName.isEmpty()
                     ? r.dataset->GetLayer(0)
                     : r.dataset->GetLayerByName(layerName.toUtf8().constData());
    if (!r.ogrLayer)
    {
        qWarning() << "GISVectorLayer: layer not found in" << filePath;
        GDALClose(r.dataset);   // otherwise the dataset would leak
        r.dataset = nullptr;
        return r;
    }
    r.msOpen     = loadTimer.elapsed();  // GDALOpenEx + GetLayer
    r.layerName  = QString::fromUtf8(r.ogrLayer->GetName());

    // Layer extent (forces a full scan on some drivers — the point of the
    // worker thread).
    OGREnvelope env;
    if (r.ogrLayer->GetExtent(&env) == OGRERR_NONE)
    {
        r.extent    = MapExtent(env.MinX, env.MinY, env.MaxX, env.MaxY);
        r.hasExtent = true;
    }

    // CRS WKT copied to a QString; the SpatialReferenceSystem QObject is built
    // on the GUI thread in applyOpenResult (QObject affinity).
    if (const OGRSpatialReference *srs = r.ogrLayer->GetSpatialRef())
    {
        char *wkt = nullptr;
        srs->exportToWkt(&wkt);
        if (wkt) { r.wkt = QString::fromUtf8(wkt); CPLFree(wkt); }
    }
    r.msTotal = loadTimer.elapsed();
    return r;
}

void GISVectorLayer::applyOpenResult(const OpenResult &r)
{
    closeDataset();          // drop any previously-open dataset (re-open case)

    m_dataset  = r.dataset;
    m_ogrLayer = r.ogrLayer;
    if (!m_dataset || !m_ogrLayer)
        return;              // doOpenWork already logged + cleaned up

    m_filePath     = r.filePath;
    m_ogrLayerName = r.layerName;

    if (r.hasExtent)
        setExtent(r.extent);

    if (!r.wkt.isEmpty())
        setSRS(SpatialReferenceSystem::fromWktOrProj(r.wkt), /*ownsSRS=*/true);

    setName(m_ogrLayerName);

    emit filePathChanged(r.filePath);
    emit layerNameChanged(m_ogrLayerName);
    emit featureCountChanged(featureCount());

    qCInfo(lcLoadVector).noquote()
        << QStringLiteral("%1 [%2]: open (ms): gdal_open=%3 extent+crs=%4 total=%5")
               .arg(QFileInfo(r.filePath).fileName(), m_ogrLayerName)
               .arg(r.msOpen)
               .arg(r.msTotal - r.msOpen)
               .arg(r.msTotal);
}

void GISVectorLayer::openDataset(const QString &filePath, const QString &layerName)
{
    applyOpenResult(doOpenWork(filePath, layerName));
}

void GISVectorLayer::openAsync(const QString &filePath, const QString &layerName)
{
    // Same shape as loadModelAsync: GDALOpenEx + GetExtent on a worker,
    // adoption on the GUI thread. If the layer dies mid-load the finished
    // handler closes the orphaned dataset.
    QPointer<GISVectorLayer> self(this);
    auto *watcher = new QFutureWatcher<OpenResult>();
    QObject::connect(watcher, &QFutureWatcherBase::finished, watcher,
                     [watcher, self]() {
        OpenResult r = watcher->result();
        watcher->deleteLater();
        if (!self) {
            if (r.dataset) GDALClose(r.dataset);
            return;
        }
        self->applyOpenResult(r);
        emit self->openFinished(r.dataset != nullptr && r.ogrLayer != nullptr);
    });
    const QString path = filePath, layer = layerName;
    watcher->setFuture(QtConcurrent::run([path, layer]() {
        return doOpenWork(path, layer);
    }));
}

void GISVectorLayer::closeDataset()
{
    if (m_transform)
    {
        OGRCoordinateTransformation::DestroyCT(m_transform);
        m_transform = nullptr;
    }

    m_ogrLayer = nullptr;

    if (m_dataset)
    {
        GDALClose(m_dataset);
        m_dataset = nullptr;
    }
}

void GISVectorLayer::rebuildTransform(const SpatialReferenceSystem *canvasSRS)
{
    if (m_transform)
    {
        OGRCoordinateTransformation::DestroyCT(m_transform);
        m_transform = nullptr;
    }

    if (!m_ogrLayer || !canvasSRS || !canvasSRS->ogrSpatialReference())
        return;

    const OGRSpatialReference *layerSRS = m_ogrLayer->GetSpatialRef();
    if (!layerSRS)
        return;

    // Only create a transform if the CRS differs
    if (!layerSRS->IsSame(canvasSRS->ogrSpatialReference()))
    {
        m_transform = OGRCreateCoordinateTransformation(
            layerSRS, canvasSRS->ogrSpatialReference());
    }
}
