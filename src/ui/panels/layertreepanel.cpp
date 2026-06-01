/*!
 * \file   layertreepanel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/panels/layertreepanel.h"
#include "ui/panels/layertreecategories.h"   // Slice LTR-2026-05-30 — extracted helpers
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmmmodellayer.h"   // Slice BI-MK.LT — kind sub-rows
#include "layers/swmmresultslayer.h" // Slice OUT.3 — output-layer kind sub-rows
#include "layers/swmm2dresultslayer.h" // active 2D analysis layer (context menu)
#include "render/attributecandidates.h" // Slice CTX.3 — grey out empty Style items
#include "render/ifeaturerenderer.h" // Slice CTX.2 — checkmark active style
// Slice S3 — sublayer-row pattern under ISublayerHost layers without kind rows.
#include "render/isublayer.h"
#include "render/isublayerhost.h"
// Slice LTR-2026-05-30 — concrete FeatureSublayer header so the sublayer
// icon dispatch can look up the per-Category glyph for 1D output rows.
// All 2D sublayer concrete types are dispatched by ISublayer::Kind enum
// (kept opaque) so the per-class headers aren't required here.
#include "render/sublayers/feature/featuresublayer.h"
#include "layers/swmm_category.h"
#include "ui/dialogs/sublayerstyledialog.h"
#include "ui/dialogs/layerstyledialog.h"

#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QDataStream>
#include <QDebug>
#include <QHBoxLayout>
#include <QIcon>
#include <QIODevice>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QSpinBox>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTreeView>
#include <QVBoxLayout>
#include <QWidget>

// ---------------------------------------------------------------------------
// Category bucketing — lives in an anonymous namespace at the top of the
// translation unit so every LayerTreeModel member function below can use
// CatCount, CatSwmm, etc. without forward declarations.
// ---------------------------------------------------------------------------

// Slice LTR-2026-05-30 — keep the LayerTypeOrdinal mirror enum (in
// layertreecategories.h) in lockstep with OpenSWMMVisLayer::OpenSWMMVisLayerType.
// The mirror exists so test code can compile without pulling Qt + the
// layer object graph; these static_asserts live in this TU (which
// already includes openswmmvislayer.h) and catch drift at build time.
static_assert(static_cast<int>(openswmmvis::ui::LayerTypeOrdinal::SWMMDefaultLayer)            == OpenSWMMVisLayer::SWMMDefaultLayer);
static_assert(static_cast<int>(openswmmvis::ui::LayerTypeOrdinal::SWMMModelLayer)              == OpenSWMMVisLayer::SWMMModelLayer);
static_assert(static_cast<int>(openswmmvis::ui::LayerTypeOrdinal::SWMMResultsLayer)            == OpenSWMMVisLayer::SWMMResultsLayer);
static_assert(static_cast<int>(openswmmvis::ui::LayerTypeOrdinal::SWMMGISLayer)                == OpenSWMMVisLayer::SWMMGISLayer);
static_assert(static_cast<int>(openswmmvis::ui::LayerTypeOrdinal::SWMMVectorLayer)             == OpenSWMMVisLayer::SWMMVectorLayer);
static_assert(static_cast<int>(openswmmvis::ui::LayerTypeOrdinal::SWMMRasterLayer)             == OpenSWMMVisLayer::SWMMRasterLayer);
static_assert(static_cast<int>(openswmmvis::ui::LayerTypeOrdinal::SWMMImageryLayer)            == OpenSWMMVisLayer::SWMMImageryLayer);
static_assert(static_cast<int>(openswmmvis::ui::LayerTypeOrdinal::SWMMTabularDataLayer)        == OpenSWMMVisLayer::SWMMTabularDataLayer);
static_assert(static_cast<int>(openswmmvis::ui::LayerTypeOrdinal::SWMMTabularyTimeSeriesLayer) == OpenSWMMVisLayer::SWMMTabularyTimeSeriesLayer);
static_assert(static_cast<int>(openswmmvis::ui::LayerTypeOrdinal::SWMMSubProjectLayer)         == OpenSWMMVisLayer::SWMMSubProjectLayer);
static_assert(static_cast<int>(openswmmvis::ui::LayerTypeOrdinal::SWMMWMSLayer)                == OpenSWMMVisLayer::SWMMWMSLayer);
static_assert(static_cast<int>(openswmmvis::ui::LayerTypeOrdinal::SWMMWMTSLayer)               == OpenSWMMVisLayer::SWMMWMTSLayer);
static_assert(static_cast<int>(openswmmvis::ui::LayerTypeOrdinal::SWMM2DMeshLayer)             == OpenSWMMVisLayer::SWMM2DMeshLayer);
static_assert(static_cast<int>(openswmmvis::ui::LayerTypeOrdinal::SWMM2DResultsLayer)          == OpenSWMMVisLayer::SWMM2DResultsLayer);
static_assert(static_cast<int>(openswmmvis::ui::LayerTypeOrdinal::SWMMAnnotationLayer)         == OpenSWMMVisLayer::SWMMAnnotationLayer);

namespace {

// Slice LTR-2026-05-30 — category bucketing now lives in
// layertreecategories.{h,cpp} so it can be unit-tested headlessly. The
// using-declarations below preserve the unqualified CatSwmm / categoryFor
// / categoryInfo names that the rest of this translation unit was written
// against, so the LayerTreeModel methods below need no edits.
using openswmmvis::ui::CategoryId;
using openswmmvis::ui::CategoryInfo;
using openswmmvis::ui::CatSwmm;
using openswmmvis::ui::CatMeshes;
using openswmmvis::ui::CatSwmm1DOutputs;
using openswmmvis::ui::CatSwmm2DOutputs;
using openswmmvis::ui::CatFeatureLayers;
using openswmmvis::ui::CatRasterLayers;
using openswmmvis::ui::CatBasemaps;
using openswmmvis::ui::CatTables;
using openswmmvis::ui::CatCount;

// Wrapper that keeps the qWarning() on unrecognised types — the extracted
// helper is headless and can't log, so the GUI-side wrapper emits the
// diagnostic before falling back to CatFeatureLayers.
inline CategoryId categoryFor(OpenSWMMVisLayer::OpenSWMMVisLayerType t)
{
    const CategoryId id = openswmmvis::ui::categoryForLayerType(int(t));
    if (id == CatFeatureLayers
        && t != OpenSWMMVisLayer::SWMMVectorLayer
        && t != OpenSWMMVisLayer::SWMMGISLayer
        && t != OpenSWMMVisLayer::SWMMSubProjectLayer
        && t != OpenSWMMVisLayer::SWMMAnnotationLayer)
    {
        qWarning() << "LayerTreeModel: unclassified layer type" << int(t)
                   << "— defaulting to Feature Layers";
    }
    return id;
}

inline CategoryInfo categoryInfo(int id)
{
    return openswmmvis::ui::categoryInfo(static_cast<CategoryId>(id));
}

// Slice LTR-2026-05-30 — sublayer-row icon dispatch. Two paths:
//   1. 1D output (FeatureSublayer) → per-SwmmCategory glyph matching the
//      kind icons already used elsewhere in the GUI.
//   2. 2D output (Mesh* / DepthColorRamp / Contour / Isoline / Velocity
//      / FlowArrow) → dispatch by ISublayer::Kind enum so we don't have
//      to include every concrete sublayer header here. The Kind enum is
//      already on ISublayer's public interface.
//
// All returned aliases are registered in resources/swmmvis.qrc; no new
// SVGs are introduced by this slice.
const char *iconAliasForSwmmCategory(OpenSWMMVis::SwmmCategory c)
{
    using namespace OpenSWMMVis;
    switch (c) {
    case CatJunctions:     return ":/swmmvis/Junction";
    case CatOutfalls:      return ":/swmmvis/Outfall";
    case CatStorage:       return ":/swmmvis/Storage";
    case CatDividers:      return ":/swmmvis/Divider";
    case CatConduits:      return ":/swmmvis/Polyline";
    case CatPumps:         return ":/swmmvis/Pump";
    case CatOrifices:      return ":/swmmvis/Orifice";
    case CatWeirs:         return ":/swmmvis/Weir";
    case CatOutlets:       return ":/swmmvis/Outlet";
    case CatSubcatchments: return ":/swmmvis/Subcatchment";
    case CatRainGages:     return ":/swmmvis/Rainfall";
    case NumCategories:    break;
    }
    return ":/swmmvis/Layers";
}

QString iconAliasForSublayer(const OpenSWMM::Render::ISublayer *s,
                             OpenSWMMVisLayer *parentLayer)
{
    if (!s) return QStringLiteral(":/swmmvis/Layers");

    // 1D path: FeatureSublayer carries its SwmmCategory, which maps to
    // the same glyph used by the SWMM model layer's kind rows.
    if (parentLayer &&
        parentLayer->layerType() == OpenSWMMVisLayer::SWMMResultsLayer)
    {
        if (auto *fs = qobject_cast<const OpenSWMM::Render::FeatureSublayer *>(s))
            return QString::fromLatin1(iconAliasForSwmmCategory(fs->category()));
    }

    // 2D path: dispatch by Kind. MarkerKind/LineKind/FillKind on the 2D
    // mesh-based layer all denote mesh primitives, hence the create-mesh
    // glyph; ramp/contour/isoline/vector each get a domain-suggestive
    // icon already in the resource bundle.
    using K = OpenSWMM::Render::ISublayer::Kind;
    switch (s->kind()) {
    case K::ColorRampFillKind: return QStringLiteral(":/swmmvis/Droplet");
    case K::IsolineKind:       return QStringLiteral(":/swmmvis/Profile");
    case K::ContourBandKind:   return QStringLiteral(":/swmmvis/Profile");
    case K::VectorGlyphKind:   return QStringLiteral(":/swmmvis/Move");
    case K::ArrowKind:         return QStringLiteral(":/swmmvis/Move");
    case K::FillKind:
    case K::LineKind:
    case K::MarkerKind:
        // On 2D layers these denote mesh-fill / mesh-edge / mesh-node
        // primitives. On 1D layers the FeatureSublayer branch above
        // already handled them.
        return QStringLiteral(":/swmmvis/CreateMesh");
    }
    return QStringLiteral(":/swmmvis/Layers");
}

// ---------------------------------------------------------------------------
// LayerOpacityDelegate — inline opacity editor for col 1 of the layer tree.
//
// The model exposes opacity in two roles:
//   DisplayRole / EditRole : "75 %"   (string, human-readable)
//   UserRole               : 0.75     (qreal in 0..1)
//
// We edit through a QSpinBox seeded from UserRole, and write back the percent
// integer via EditRole — the model's setData() already parses both forms
// (see "remove('%').trimmed().toDouble" at line ~810) so no model contract
// changes.  paint() draws a thin horizontal opacity bar behind the percent
// text for at-a-glance feedback.
// ---------------------------------------------------------------------------
class LayerOpacityDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget *createEditor(QWidget *parent,
                          const QStyleOptionViewItem & /*opt*/,
                          const QModelIndex &index) const override
    {
        if (index.column() != 1) return nullptr;
        auto *sb = new QSpinBox(parent);
        sb->setRange(0, 100);
        sb->setSuffix(QStringLiteral(" %"));
        sb->setFrame(false);
        sb->setKeyboardTracking(false);
        return sb;
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        if (auto *sb = qobject_cast<QSpinBox *>(editor)) {
            const double op01 = index.data(Qt::UserRole).toDouble();
            sb->setValue(qBound(0, qRound(op01 * 100.0), 100));
        }
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override
    {
        if (auto *sb = qobject_cast<QSpinBox *>(editor)) {
            model->setData(index, sb->value(), Qt::EditRole);
        }
    }

    void paint(QPainter *p, const QStyleOptionViewItem &opt,
               const QModelIndex &index) const override
    {
        // Only decorate layer / sublayer opacity cells (UserRole carries qreal 0..1).
        const QVariant raw = index.data(Qt::UserRole);
        bool ok = false;
        const double op01 = raw.toDouble(&ok);
        if (!ok || index.column() != 1) {
            QStyledItemDelegate::paint(p, opt, index);
            return;
        }

        // Background fill / selection highlight (default behaviour).
        QStyleOptionViewItem o = opt;
        initStyleOption(&o, index);
        QStyle *style = o.widget ? o.widget->style() : QApplication::style();
        style->drawPrimitive(QStyle::PE_PanelItemViewItem, &o, p, o.widget);

        // Opacity bar — thin pill behind the text. Width tracks the value
        // (0..100 %); colour fades with opacity for instant feedback.
        QRect r = o.rect.adjusted(4, 4, -4, -4);
        const int barH = qMax(4, r.height() / 3);
        QRect track(r.left(), r.center().y() - barH / 2,
                    r.width(), barH);
        QRect fill = track;
        fill.setWidth(int(track.width() * qBound(0.0, op01, 1.0)));

        QColor accent = o.palette.color(QPalette::Highlight);
        accent.setAlphaF(qBound(0.20, op01, 1.0));
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        p->setPen(Qt::NoPen);
        p->setBrush(o.palette.color(QPalette::AlternateBase));
        p->drawRoundedRect(track, 2, 2);
        p->setBrush(accent);
        p->drawRoundedRect(fill, 2, 2);
        p->restore();

        // Percent text on top.
        const QString text = QStringLiteral("%1 %").arg(qRound(op01 * 100.0));
        o.palette.setColor(QPalette::Text,
            o.palette.color(o.state & QStyle::State_Selected
                                 ? QPalette::HighlightedText
                                 : QPalette::Text));
        p->save();
        p->setPen(o.palette.color(QPalette::Text));
        p->drawText(r, Qt::AlignCenter, text);
        p->restore();
    }
};

} // anonymous

// ===========================================================================
// LayerTreeModel
// ===========================================================================

LayerTreeModel::LayerTreeModel(MapCanvas *canvas, QObject *parent)
    : QAbstractItemModel(parent),
      m_canvas(nullptr)
{
    // Default display order: RESULTS groups on top (they overlay the model /
    // mesh for analysis), then the editable model + mesh, then GIS / basemaps,
    // then tables. This is the starting default only — the user can permute it
    // via drag-drop or "Move Category Up/Down", and that permutation persists
    // to the project file (see ProjectSerializer). Any CategoryId not listed
    // here is appended by rebuildCategories() in enum order.
    m_categoryDisplayOrder = {
        CatSwmm1DOutputs,   // 1D results  (top)
        CatSwmm2DOutputs,   // 2D results
        CatSwmm,            // SWMM model (editable)
        CatMeshes,          // 2D mesh (editable)
        CatFeatureLayers,   // GIS vectors / annotations
        CatRasterLayers,    // GIS rasters
        CatBasemaps,        // WMS / WMTS / XYZ
        CatTables,          // tabular data (bottom)
    };

    // Restore a user-customized category order if one was persisted. The layer
    // tree panel is a single shared dock retargeted per active tab, so the
    // group order is a global display preference (QSettings), not per-project.
    // Only accept a saved order that is a valid permutation of [0, CatCount);
    // otherwise fall back to the results-first default above.
    {
        QSettings settings;
        const QVariantList saved =
            settings.value(QStringLiteral("layerTree/categoryDisplayOrder")).toList();
        if (saved.size() == CatCount) {
            QVector<int> order;
            QVector<bool> seen(CatCount, false);
            bool valid = true;
            for (const QVariant &v : saved) {
                bool ok = false;
                const int id = v.toInt(&ok);
                if (!ok || id < 0 || id >= CatCount || seen[id]) { valid = false; break; }
                seen[id] = true;
                order.append(id);
            }
            if (valid)
                m_categoryDisplayOrder = order;
        }
    }

    setCanvas(canvas);
}

LayerTreeModel::~LayerTreeModel() = default;

void LayerTreeModel::setCanvas(MapCanvas *canvas)
{
    if (m_canvas == canvas)
        return;

    beginResetModel();

    if (m_canvas)
        QObject::disconnect(m_canvas, nullptr, this, nullptr);

    m_canvas = canvas;

    if (m_canvas)
    {
        connect(m_canvas, &MapCanvas::layerAdded,
                this,     &LayerTreeModel::onLayerAdded);
        connect(m_canvas, &MapCanvas::layerRemoved,
                this,     &LayerTreeModel::onLayerRemoved);
        connect(m_canvas, &MapCanvas::layerOrderChanged,
                this,     &LayerTreeModel::onLayerOrderChanged);
    }

    rebuildCategories();
    endResetModel();
}

void LayerTreeModel::rebuildCategories()
{
    m_categories.clear();
    m_layerToCategory.clear();
    if (!m_canvas)
        return;

    // Bucket layers by category id, preserving canvas-stack-top-first order.
    QVector<QVector<OpenSWMMVisLayer *>> buckets(CatCount);
    for (int displayRow = 0; displayRow < m_canvas->layerCount(); ++displayRow)
    {
        const int canvasIdx = m_canvas->layerCount() - 1 - displayRow;
        OpenSWMMVisLayer *layer = m_canvas->layerAt(canvasIdx);
        if (!layer) continue;
        const int catId = categoryFor(layer->layerType());
        buckets[catId].append(layer);
    }

    // Materialise non-empty categories in the USER-CONFIGURED display order.
    // Any category id present in the canvas but absent from m_categoryDisplayOrder
    // (shouldn't happen, but guard for forward-compat) falls back to appending.
    QVector<bool> seen(CatCount, false);
    for (int catId : m_categoryDisplayOrder)
    {
        if (catId < 0 || catId >= CatCount || buckets[catId].isEmpty())
            continue;
        seen[catId] = true;
        const CategoryInfo info = categoryInfo(catId);
        Category c;
        c.name      = QString::fromLatin1(info.name);
        c.iconAlias = QString::fromLatin1(info.iconAlias);
        c.layers    = buckets[catId];
        const int catRow = m_categories.size();
        for (OpenSWMMVisLayer *layer : c.layers)
            m_layerToCategory.insert(layer, catRow);
        m_categories.append(std::move(c));
    }
    // Append any buckets not covered by m_categoryDisplayOrder.
    for (int catId = 0; catId < CatCount; ++catId)
    {
        if (seen[catId] || buckets[catId].isEmpty()) continue;
        const CategoryInfo info = categoryInfo(catId);
        Category c;
        c.name      = QString::fromLatin1(info.name);
        c.iconAlias = QString::fromLatin1(info.iconAlias);
        c.layers    = buckets[catId];
        const int catRow = m_categories.size();
        for (OpenSWMMVisLayer *layer : c.layers)
            m_layerToCategory.insert(layer, catRow);
        m_categories.append(std::move(c));
    }

    // VS.8 — refresh the layer's tree row (visibility checkbox + opacity
    // column) when its opacity / visibility / name changes from OUTSIDE the
    // tree (e.g. the layer-properties Rendering tab, or a programmatic
    // edit). onLayerDataChanged() emits dataChanged for the full row.
    // Disconnect-then-connect keeps repeated rebuilds idempotent; the
    // nullptr-slot disconnect only drops connections whose receiver is this
    // model, leaving the MapCanvas/legend connections intact.
    for (auto it = m_layerToCategory.constBegin();
         it != m_layerToCategory.constEnd(); ++it) {
        OpenSWMMVisLayer *layer = it.key();
        if (!layer) continue;
        QObject::disconnect(layer, &OpenSWMMVisLayer::opacityChanged,    this, nullptr);
        QObject::disconnect(layer, &OpenSWMMVisLayer::visibilityChanged, this, nullptr);
        QObject::disconnect(layer, &OpenSWMMVisLayer::nameChanged,       this, nullptr);
        QObject::connect(layer, &OpenSWMMVisLayer::opacityChanged, this,
                         [this, layer]() { onLayerDataChanged(layer); });
        QObject::connect(layer, &OpenSWMMVisLayer::visibilityChanged, this,
                         [this, layer]() { onLayerDataChanged(layer); });
        QObject::connect(layer, &OpenSWMMVisLayer::nameChanged, this,
                         [this, layer]() { onLayerDataChanged(layer); });
    }

    // Slice BI-MK.LT — populate kind sub-rows for any SWMMModelLayer in tree.
    rebuildKindRows();

    // Populate sublayer sub-rows for every ISublayerHost layer that doesn't
    // already carry kind rows — currently SWMMResultsLayer, SWMM2DResultsLayer,
    // and SWMM2DMeshLayer. The kind-vs-sublayer mutual exclusion still skips
    // SWMMModelLayer here (kinds take over its sub-row surface).
    rebuildSublayerRows();
}

int LayerTreeModel::categoryOf(OpenSWMMVisLayer *layer) const
{
    return m_layerToCategory.value(layer, -1);
}

// Slice BI-MK.LT — populate m_kindRowStorage for every SWMMModelLayer that
// currently appears in m_categories. Called by rebuildCategories whenever
// the layer stack changes. The KindRow addresses inside the std::array
// stay stable for the model's lifetime (no reallocation since the array
// is fixed-size and the QHash node is allocated on insert).
void LayerTreeModel::rebuildKindRows()
{
    m_kindRowStorage.clear();
    m_kindRowPtrSet.clear();
    for (const Category &cat : m_categories) {
        for (OpenSWMMVisLayer *layer : cat.layers) {
            // Slice BI-MK.LT — SWMMModelLayer carries 11 kind sub-rows for
            // per-kind static-attribute styling.
            //
            // Slice S3-2026-05-25 (RENDERING_OUTPUT_SUBLAYERS_PLAN.md, user
            // direction): SWMMResultsLayer NO LONGER gets kind rows. Its
            // sublayer host (S2.4) is the user-facing toggle/style surface
            // — toggling Conduit lines / Conduit arrows / Node markers /
            // Subcatchment fill must be possible from the layer tree. The
            // sublayer-row scheme in rebuildSublayerRows() takes over here.
            // The per-kind override slots (OUT.3) remain available
            // programmatically and via the future MapSymbologyDialog; they
            // are no longer exposed as tree rows.
            auto *modelLayer = qobject_cast<SWMMModelLayer *>(layer);
            if (!modelLayer) continue;
            auto &arr = m_kindRowStorage[layer];
            for (int k = 0; k < kKindsPerSwmmModelLayer; ++k) {
                arr[k].layer       = layer;
                arr[k].kindOrdinal = k;
                m_kindRowPtrSet.insert(static_cast<const void *>(&arr[k]));
            }
            // Slice MVC.1 — refresh kind-row icons + labels when the
            // layer's renderer is swapped from outside the tree (the
            // SymbologyDialog, a "Reset Kind" command, or an undo step).
            // Qt 6 asserts on Qt::UniqueConnection with non-PMF slots, so
            // disconnect first to keep repeated rebuilds idempotent.
            if (modelLayer) {
                QObject::disconnect(modelLayer, &SWMMModelLayer::rendererChanged,
                                    this, nullptr);
                QObject::connect(modelLayer, &SWMMModelLayer::rendererChanged,
                                  this, [this, layer]() {
                                      const QModelIndex top    = createIndex(0, 0,
                                          const_cast<void *>(static_cast<const void *>(layer)));
                                      const QModelIndex bottom = createIndex(
                                          kKindsPerSwmmModelLayer - 1, columnCount() - 1,
                                          const_cast<void *>(static_cast<const void *>(layer)));
                                      emit dataChanged(top, bottom);
                                  });
            }
            // (Removed 2026-05-25): the resultsLayer kind-row branch is
            // dropped per the user-direction comment above. SWMMResultsLayer
            // now exposes its 4-element sublayer mix via the sublayer-row
            // path; per-kind styling overrides on output results are still
            // accessible through SWMMResultsLayer::setKindRenderer() but no
            // longer appear as tree rows.
        }
    }
}

// Sublayer rows are exposed for every ISublayerHost layer so users can
// toggle visibility and edit opacity per-sublayer inline (depth ramp,
// velocity vectors, contours, mesh fill / edges / nodes, per-category
// result paint, …) without opening a styling dialog. The Rule List
// remains the primary styling surface for richer edits.
//
// Mutual exclusion with kind-rows is preserved: layers carrying kind
// rows (today only SWMMModelLayer) are skipped here, matching the
// "kind-vs-sublayer interaction" contract documented in
// rebuildKindRows().
void LayerTreeModel::rebuildSublayerRows()
{
    m_sublayerRowStorage.clear();
    m_sublayerRowPtrSet.clear();

    for (const Category &cat : m_categories) {
        for (OpenSWMMVisLayer *layer : cat.layers) {
            // Skip layers that already carry kind sub-rows (SWMMModelLayer),
            // preserving the mutual-exclusion contract.
            if (m_kindRowStorage.contains(layer))
                continue;

            // Every ISublayerHost contributes inline sublayer rows. The
            // LayerOpacityDelegate + generic model paths handle any
            // ISublayer uniformly, so no per-type wiring is required.
            auto *host = dynamic_cast<OpenSWMM::Render::ISublayerHost *>(layer);
            if (!host)
                continue;

            const QList<OpenSWMM::Render::ISublayer *> subs = host->sublayers();
            if (subs.isEmpty())
                continue;

            // Display order = TOP-of-paint-stack first, matching how the
            // tree shows top-of-canvas first for categories and layers.
            // ISublayerHost::sublayers() returns paint order (bottom-up),
            // so we reverse.
            auto &storage = m_sublayerRowStorage[layer];
            storage.reserve(subs.size());
            for (int i = subs.size() - 1; i >= 0; --i) {
                if (!subs[i]) continue;
                SublayerRow row;
                row.layer    = layer;
                row.sublayer = subs[i];
                storage.push_back(row);
                m_sublayerRowPtrSet.insert(
                    static_cast<const void *>(&storage.back()));
            }

            // Live update: re-emit dataChanged when any sublayer of this
            // host invalidates (style edit, opacity change, animation tick).
            // Disconnect-then-connect keeps repeated rebuilds idempotent
            // (Qt 6 asserts on UniqueConnection with non-PMF slots).
            for (auto *sub : subs) {
                if (!sub) continue;
                QObject::disconnect(sub, &OpenSWMM::Render::ISublayer::invalidated,
                                    this, nullptr);
                QObject::connect(sub, &OpenSWMM::Render::ISublayer::invalidated,
                                 this, [this, layer]() {
                    const int catIdx = m_layerToCategory.value(layer, -1);
                    if (catIdx < 0) return;
                    const int layerRow =
                        m_categories[catIdx].layers.indexOf(layer);
                    if (layerRow < 0) return;
                    const QModelIndex catIndex =
                        createIndex(catIdx, 0, static_cast<void *>(nullptr));
                    const QModelIndex layerIdx =
                        index(layerRow, 0, catIndex);
                    const int rows = rowCount(layerIdx);
                    if (rows <= 0) return;
                    const QModelIndex top =
                        index(0, 0, layerIdx);
                    const QModelIndex bottom =
                        index(rows - 1, columnCount() - 1, layerIdx);
                    emit dataChanged(top, bottom);
                });
            }
        }
    }
}

// Helper: find the row index of a SublayerRow within its parent layer's
// vector. Used by the live-update lambda in rebuildSublayerRows() to
// build a QModelIndex when a sublayer fires invalidated().
int LayerTreeModel::sublayerRowIndex(const void *p) const
{
    if (!p) return -1;
    const auto *row = static_cast<const SublayerRow *>(p);
    auto it = m_sublayerRowStorage.constFind(row->layer);
    if (it == m_sublayerRowStorage.constEnd()) return -1;
    const auto &vec = it.value();
    for (size_t i = 0; i < vec.size(); ++i)
        if (&vec[i] == row) return static_cast<int>(i);
    return -1;
}

// ---------------------------------------------------------------------------
// QAbstractItemModel interface
// ---------------------------------------------------------------------------

QModelIndex LayerTreeModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent))
        return {};

    if (!parent.isValid())
    {
        // Top-level: a category row. internalPointer == nullptr distinguishes
        // categories from layers.
        return createIndex(row, column, static_cast<void *>(nullptr));
    }

    if (parent.internalPointer() == nullptr)
    {
        // Parent is a category row → layer row.
        const int catIdx = parent.row();
        if (catIdx < 0 || catIdx >= m_categories.size())
            return {};
        if (row < 0 || row >= m_categories[catIdx].layers.size())
            return {};
        return createIndex(row, column, m_categories[catIdx].layers[row]);
    }

    // Parent is a layer row → check whether this layer has kind sub-rows
    // (SWMMModelLayer does, others don't). If so, return a kind-row index
    // whose internalPointer is the stable address of the per-kind sentinel.
    auto *parentLayer = static_cast<OpenSWMMVisLayer *>(parent.internalPointer());
    auto it = m_kindRowStorage.constFind(parentLayer);
    if (it != m_kindRowStorage.constEnd()) {
        if (row < 0 || row >= kKindsPerSwmmModelLayer) return {};
        return createIndex(row, column,
                           const_cast<void *>(static_cast<const void *>(&it.value()[row])));
    }
    // Slice S3 — fall back to sublayer rows for sublayer-host layers that
    // aren't kind-row eligible (today: SWMM2DResultsLayer).
    auto subIt = m_sublayerRowStorage.constFind(parentLayer);
    if (subIt != m_sublayerRowStorage.constEnd()) {
        const auto &vec = subIt.value();
        if (row < 0 || static_cast<size_t>(row) >= vec.size()) return {};
        return createIndex(row, column,
                           const_cast<void *>(static_cast<const void *>(&vec[row])));
    }
    return {};
}

QModelIndex LayerTreeModel::parent(const QModelIndex &child) const
{
    if (!child.isValid())
        return {};

    void *p = child.internalPointer();
    if (!p)
        return {};   // child is a category — no parent

    // Slice BI-MK.LT — kind row: parent is the layer row.
    if (m_kindRowPtrSet.contains(p)) {
        const KindRow *kr = static_cast<const KindRow *>(p);
        OpenSWMMVisLayer *parentLayer = kr->layer;
        const int catIdx = m_layerToCategory.value(parentLayer, -1);
        if (catIdx < 0) return {};
        const int layerRow = m_categories[catIdx].layers.indexOf(parentLayer);
        if (layerRow < 0) return {};
        return createIndex(layerRow, 0, parentLayer);
    }

    // Slice S3 — sublayer row: parent is the layer row.
    if (m_sublayerRowPtrSet.contains(p)) {
        const SublayerRow *sr = static_cast<const SublayerRow *>(p);
        OpenSWMMVisLayer *parentLayer = sr->layer;
        const int catIdx = m_layerToCategory.value(parentLayer, -1);
        if (catIdx < 0) return {};
        const int layerRow = m_categories[catIdx].layers.indexOf(parentLayer);
        if (layerRow < 0) return {};
        return createIndex(layerRow, 0, parentLayer);
    }

    // Otherwise child is a layer row → parent is the category.
    auto *layer = static_cast<OpenSWMMVisLayer *>(p);
    const int catIdx = m_layerToCategory.value(layer, -1);
    if (catIdx < 0)
        return {};
    return createIndex(catIdx, 0, static_cast<void *>(nullptr));
}

int LayerTreeModel::rowCount(const QModelIndex &parent) const
{
    if (!m_canvas)
        return 0;
    if (!parent.isValid())
        return m_categories.size();
    if (parent.internalPointer() == nullptr)
    {
        // category row → number of layers in this category
        const int catIdx = parent.row();
        return (catIdx >= 0 && catIdx < m_categories.size())
                   ? m_categories[catIdx].layers.size()
                   : 0;
    }
    // Slice BI-MK.LT / S3 — layer row: has kind sub-rows for multi-kind
    // layers OR sublayer sub-rows for sublayer hosts (mutually exclusive
    // per the eligibility rule in rebuildSublayerRows).
    if (!m_kindRowPtrSet.contains(parent.internalPointer())
        && !m_sublayerRowPtrSet.contains(parent.internalPointer())) {
        auto *layer = static_cast<OpenSWMMVisLayer *>(parent.internalPointer());
        if (m_kindRowStorage.contains(layer))
            return kKindsPerSwmmModelLayer;
        auto subIt = m_sublayerRowStorage.constFind(layer);
        if (subIt != m_sublayerRowStorage.constEnd())
            return static_cast<int>(subIt.value().size());
        return 0;   // leaf layer
    }
    return 0;   // kind rows and sublayer rows are leaves
}

int LayerTreeModel::columnCount(const QModelIndex & /*parent*/) const
{
    return 2;  // Column 0: name + visibility; Column 1: opacity
}

QVariant LayerTreeModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};

    void *p = index.internalPointer();

    // ---- Sublayer row (Slice S3) -------------------------------------------
    if (p && m_sublayerRowPtrSet.contains(p)) {
        const SublayerRow *sr = static_cast<const SublayerRow *>(p);
        OpenSWMM::Render::ISublayer *s = sr->sublayer;
        if (!s) return {};
        if (index.column() == 0) {
            switch (role) {
            case Qt::DisplayRole:
                return s->displayName();
            case Qt::CheckStateRole:
                return s->isVisible() ? Qt::Checked : Qt::Unchecked;
            case Qt::DecorationRole:
                // Slice LTR-2026-05-30 — per-class glyph (per-Category for
                // 1D FeatureSublayers, per-Kind for 2D sublayers) instead
                // of the generic Layers icon.
                return QIcon(iconAliasForSublayer(s, sr->layer));
            case Qt::ToolTipRole:
                // The dynamic flag is the perf-relevant attribute users
                // care about — surface it inline.
                return s->isDynamic()
                    ? QStringLiteral("%1 (animated)").arg(s->displayName())
                    : QStringLiteral("%1 (static)").arg(s->displayName());
            }
        } else if (index.column() == 1) {
            switch (role) {
            case Qt::DisplayRole:
                return QStringLiteral("%1%").arg(qRound(s->opacity() * 100.0));
            case Qt::EditRole:
                return s->opacity() * 100.0;
            }
        }
        return {};
    }

    // ---- Kind row (Slice BI-MK.LT + Slice OUT.3) ---------------------------
    if (p && m_kindRowPtrSet.contains(p)) {
        const KindRow *kr = static_cast<const KindRow *>(p);
        auto *swmm    = qobject_cast<SWMMModelLayer   *>(kr->layer);
        auto *results = qobject_cast<SWMMResultsLayer *>(kr->layer);
        if (!swmm && !results) return {};
        const auto cat = static_cast<SWMMModelLayer::Category>(kr->kindOrdinal);
        if (index.column() != 0) return {};
        switch (role) {
        case Qt::DisplayRole: {
            if (swmm) {
                // Label: "<Kind> (count)" for input layer.
                const int total = swmm->categoryCount(cat);
                return QStringLiteral("%1 (%2)")
                    .arg(SWMMModelLayer::kindKey(cat))
                    .arg(total);
            }
            // Output layer: just the kind name (counts mirror the model layer).
            return SWMMModelLayer::kindKey(cat);
        }
        case Qt::CheckStateRole:
            if (swmm) return swmm->categoryCheckState(cat);
            return Qt::Checked;   // Output kind rows: no per-kind toggle v1
        case Qt::DecorationRole:
            // Per-kind icon. The SWMM instance (model) layer's kind rows use
            // the same per-object-type glyphs as the 1D results sublayers
            // (iconAliasForSwmmCategory), instead of the generic Layers icon.
            // SWMMModelLayer::Category aliases OpenSWMMVis::SwmmCategory, so
            // the ordinal maps straight through. Output kind rows keep Chart.
            return QIcon(swmm
                ? QString::fromLatin1(iconAliasForSwmmCategory(cat))
                : QStringLiteral(":/swmmvis/Chart"));
        case Qt::UserRole:
            return kr->kindOrdinal;
        }
        return {};
    }

    auto *layer = static_cast<OpenSWMMVisLayer *>(p);

    // ---- Category row (layer == nullptr) -----------------------------------
    if (!layer)
    {
        const int catIdx = index.row();
        if (catIdx < 0 || catIdx >= m_categories.size())
            return {};
        const Category &c = m_categories[catIdx];
        if (index.column() == 0)
        {
            switch (role)
            {
            case Qt::DisplayRole:
                return QStringLiteral("%1 (%2)").arg(c.name).arg(c.layers.size());
            case Qt::DecorationRole:
                return QIcon(c.iconAlias);
            case Qt::FontRole:
            {
                QFont f;
                f.setBold(true);
                return f;
            }
            }
        }
        return {};
    }

    // ---- Layer row ---------------------------------------------------------
    if (index.column() == 0)
    {
        switch (role)
        {
        case Qt::DisplayRole:
        case Qt::EditRole:
            return layer->name();
        case Qt::CheckStateRole:
            return layer->isVisible() ? Qt::Checked : Qt::Unchecked;
        case Qt::DecorationRole:
        {
            // Per-layer-type icon from the Qt resource bundle. Aliases live in
            // resources/swmmvis.qrc under the :/swmmvis/ prefix.
            switch (layer->layerType())
            {
            case OpenSWMMVisLayer::SWMMModelLayer:
                return QIcon(QStringLiteral(":/swmmvis/Layers"));
            case OpenSWMMVisLayer::SWMMResultsLayer:
                return QIcon(QStringLiteral(":/swmmvis/Chart"));
            case OpenSWMMVisLayer::SWMMVectorLayer:
            case OpenSWMMVisLayer::SWMMGISLayer:
                return QIcon(QStringLiteral(":/swmmvis/AddVector"));
            case OpenSWMMVisLayer::SWMMRasterLayer:
                return QIcon(QStringLiteral(":/swmmvis/AddRaster"));
            case OpenSWMMVisLayer::SWMMImageryLayer:
            case OpenSWMMVisLayer::SWMMWMSLayer:
                return QIcon(QStringLiteral(":/swmmvis/AddWMS"));
            case OpenSWMMVisLayer::SWMMWMTSLayer:
                return QIcon(QStringLiteral(":/swmmvis/AddBasemap"));
            case OpenSWMMVisLayer::SWMMTabularDataLayer:
            case OpenSWMMVisLayer::SWMMTabularyTimeSeriesLayer:
                return QIcon(QStringLiteral(":/swmmvis/TableView"));
            case OpenSWMMVisLayer::SWMMSubProjectLayer:
                return QIcon(QStringLiteral(":/swmmvis/Layers"));
            // Slice LTR-2026-05-30 — mesh / 2D-results glyph matches the
            // Generate Mesh toolbar icon, making the 2D surface immediately
            // distinguishable from generic vector / raster layers.
            case OpenSWMMVisLayer::SWMM2DMeshLayer:
            case OpenSWMMVisLayer::SWMM2DResultsLayer:
                return QIcon(QStringLiteral(":/swmmvis/CreateMesh"));
            default:
                return QIcon(QStringLiteral(":/swmmvis/Layers"));
            }
        }
        case Qt::UserRole:
            return QVariant::fromValue(layer);
        }
    }
    else if (index.column() == 1)
    {
        switch (role)
        {
        case Qt::DisplayRole:
        case Qt::EditRole:
            return QString::number(qRound(layer->opacity() * 100)) + QLatin1Char('%');
        case Qt::UserRole:
            return layer->opacity();
        }
    }

    return {};
}

bool LayerTreeModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid())
        return false;

    void *p = index.internalPointer();

    // Slice S3 — sublayer-row check-state toggles per-sublayer visibility;
    // column 1 edits per-sublayer opacity.
    if (p && m_sublayerRowPtrSet.contains(p)) {
        const SublayerRow *sr = static_cast<const SublayerRow *>(p);
        OpenSWMM::Render::ISublayer *s = sr->sublayer;
        if (!s) return false;
        if (index.column() == 0 && role == Qt::CheckStateRole) {
            s->setVisible(value.toInt() == Qt::Checked);
            emit dataChanged(index, index, {Qt::CheckStateRole});
            return true;
        }
        if (index.column() == 1 && (role == Qt::EditRole || role == Qt::UserRole)) {
            bool ok = false;
            double op = value.toDouble(&ok);
            if (!ok) op = value.toString().remove('%').trimmed().toDouble(&ok);
            if (ok) {
                s->setOpacity(op / 100.0);
                emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
                return true;
            }
        }
        return false;
    }

    // Slice BI-MK.LT — kind-row check-state toggles per-kind visibility.
    if (p && m_kindRowPtrSet.contains(p)) {
        if (index.column() != 0 || role != Qt::CheckStateRole) return false;
        const KindRow *kr = static_cast<const KindRow *>(p);
        auto *swmm = qobject_cast<SWMMModelLayer *>(kr->layer);
        if (!swmm) return false;
        const auto cat = static_cast<SWMMModelLayer::Category>(kr->kindOrdinal);
        swmm->setCategoryVisible(cat, value.toInt() == Qt::Checked);
        emit dataChanged(index, index, {Qt::CheckStateRole});
        return true;
    }

    auto *layer = static_cast<OpenSWMMVisLayer *>(p);
    if (!layer)
        return false;   // category rows are not editable

    if (index.column() == 0 && role == Qt::CheckStateRole)
    {
        layer->setVisible(value.toInt() == Qt::Checked);
        emit dataChanged(index, index, {Qt::CheckStateRole});
        return true;
    }

    if (index.column() == 1 && (role == Qt::EditRole || role == Qt::UserRole))
    {
        bool ok = false;
        double opacity = value.toDouble(&ok);
        if (!ok)
            opacity = value.toString().remove('%').trimmed().toDouble(&ok);
        if (ok)
        {
            layer->setOpacity(opacity / 100.0);
            emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
            return true;
        }
    }

    return false;
}

Qt::ItemFlags LayerTreeModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;

    void *p = index.internalPointer();

    // Slice S3 — sublayer sub-row: column 0 checkable (visibility), column 1
    // editable (opacity), enabled + selectable.
    // Slice GUI-2026-05-30 §2 — col 0 also drag-enabled (sublayer reorder
    // within the same host); the row is drop-enabled so other sublayers
    // can land between them.
    if (p && m_sublayerRowPtrSet.contains(p)) {
        Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable
                        | Qt::ItemIsDropEnabled;
        if (index.column() == 0) f |= Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled;
        if (index.column() == 1) f |= Qt::ItemIsEditable;
        return f;
    }

    // Slice BI-MK.LT — kind sub-row: checkable + enabled + selectable, no drag.
    // Slice OUT.3 — only SWMMModelLayer kind rows are checkable (per-kind
    // visibility); output-layer kind rows are display-only for v1.
    if (p && m_kindRowPtrSet.contains(p)) {
        const KindRow *kr = static_cast<const KindRow *>(p);
        const bool isModel = qobject_cast<SWMMModelLayer *>(kr->layer) != nullptr;
        Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        if (isModel && index.column() == 0)
            f |= Qt::ItemIsUserCheckable;
        return f;
    }

    auto *layer = static_cast<OpenSWMMVisLayer *>(p);

    // Category row: enabled + selectable + draggable (so the user can reorder
    // entire type groups) + drop-enabled (so other categories can land here).
    if (!layer)
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable
             | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;

    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() == 0)
        f |= Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled;
    if (index.column() == 1)
        f |= Qt::ItemIsEditable;
    return f;
}

QVariant LayerTreeModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};

    switch (section)
    {
    case 0: return tr("Layer");
    case 1: return tr("Opacity");
    default: return {};
    }
}

// ---------------------------------------------------------------------------
// Drag-and-drop
// ---------------------------------------------------------------------------

QStringList LayerTreeModel::mimeTypes() const
{
    return {QStringLiteral("application/x-layerrow"),
            QStringLiteral("application/x-layercategory"),
            QStringLiteral("application/x-sublayerrow")};
}

Qt::DropActions LayerTreeModel::supportedDropActions() const
{
    // Reordering only — no copy semantics on the layer tree.
    return Qt::MoveAction;
}

QMimeData *LayerTreeModel::mimeData(const QModelIndexList &indexes) const
{
    if (indexes.isEmpty())
        return nullptr;

    const QModelIndex first = indexes.first();
    void *p = first.internalPointer();
    auto *mime = new QMimeData;
    QByteArray encoded;
    QDataStream stream(&encoded, QIODevice::WriteOnly);

    // Slice GUI-2026-05-30 §2 — sublayer-row drag.  Encode the host layer
    // pointer + the sublayer's position within sublayers().
    if (p && m_sublayerRowPtrSet.contains(p)) {
        const SublayerRow *sr = static_cast<const SublayerRow *>(p);
        if (!sr->layer || !sr->sublayer) { delete mime; return nullptr; }
        auto *host = dynamic_cast<OpenSWMM::Render::ISublayerHost *>(sr->layer);
        if (!host)             { delete mime; return nullptr; }
        const auto subs = host->sublayers();
        int idx = subs.indexOf(sr->sublayer);
        if (idx < 0)           { delete mime; return nullptr; }
        stream << reinterpret_cast<quintptr>(sr->layer) << qint32(idx);
        mime->setData(QStringLiteral("application/x-sublayerrow"), encoded);
        return mime;
    }

    auto *layer = static_cast<OpenSWMMVisLayer *>(p);

    if (!layer) {
        // Category row drag — encode the CategoryId stored in m_categoryDisplayOrder
        // at this display position.
        const int displayPos = first.row();
        if (displayPos < 0 || displayPos >= m_categoryDisplayOrder.size())
        { delete mime; return nullptr; }
        stream << qint32(m_categoryDisplayOrder[displayPos]);
        mime->setData(QStringLiteral("application/x-layercategory"), encoded);
        return mime;
    }

    // Layer row drag
    stream << reinterpret_cast<quintptr>(layer);
    mime->setData(QStringLiteral("application/x-layerrow"), encoded);
    return mime;
}

bool LayerTreeModel::canDropMimeData(const QMimeData *data, Qt::DropAction action,
                                     int row, int column,
                                     const QModelIndex &parent) const
{
    Q_UNUSED(action) Q_UNUSED(column) Q_UNUSED(row)

    // Category drop: accept between category rows (parent invalid) or onto a
    // category row — NOT onto a layer row.
    if (data->hasFormat(QStringLiteral("application/x-layercategory"))) {
        if (!parent.isValid()) return true;                         // between categories
        if (parent.internalPointer() == nullptr) return true;      // on a category
        return false;                                               // on a layer
    }

    // Layer drop: accept onto any category row (same or different).
    if (data->hasFormat(QStringLiteral("application/x-layerrow"))) {
        if (!parent.isValid()) return false;
        if (parent.internalPointer() != nullptr) return false;  // parent is a layer
        return true;
    }

    // Sublayer drop (Slice GUI-2026-05-30 §2): accept only on a sublayer
    // row whose host layer matches the drag source.  This prevents
    // cross-host drops which have no defined semantics.
    if (data->hasFormat(QStringLiteral("application/x-sublayerrow"))) {
        if (!parent.isValid()) return false;
        void *pp = parent.internalPointer();
        if (!pp || !m_sublayerRowPtrSet.contains(pp)) return false;

        QByteArray buf = data->data(QStringLiteral("application/x-sublayerrow"));
        QDataStream ds(&buf, QIODevice::ReadOnly);
        quintptr hostPtr = 0; qint32 ignoredIdx = 0;
        ds >> hostPtr >> ignoredIdx;
        const SublayerRow *dst = static_cast<const SublayerRow *>(pp);
        return dst && dst->layer
               && reinterpret_cast<quintptr>(dst->layer) == hostPtr;
    }

    return false;
}

void LayerTreeModel::reorderCategories(int srcDisplayPos, int dstDisplayPos)
{
    if (!m_canvas) return;
    if (srcDisplayPos < 0 || srcDisplayPos >= m_categoryDisplayOrder.size()) return;
    if (dstDisplayPos < 0 || dstDisplayPos >= m_categoryDisplayOrder.size()) return;
    if (srcDisplayPos == dstDisplayPos) return;

    QVector<int> newDisplayOrder = m_categoryDisplayOrder;
    newDisplayOrder.move(srcDisplayPos, dstDisplayPos);

    // Canvas order: bottom→top = reverse of display order (display 0 = top = rendered last).
    QList<OpenSWMMVisLayer *> desiredCanvasOrder;
    for (int dp = newDisplayOrder.size() - 1; dp >= 0; --dp) {
        const int catId = newDisplayOrder[dp];
        for (int ci = 0; ci < m_canvas->layerCount(); ++ci) {
            OpenSWMMVisLayer *l = m_canvas->layerAt(ci);
            if (l && int(categoryFor(l->layerType())) == catId)
                desiredCanvasOrder.append(l);
        }
    }
    if (desiredCanvasOrder.count() != m_canvas->layerCount()) return;

    // Update display order before canvas emits layerOrderChanged so
    // rebuildCategories() picks up the new sequence.
    m_categoryDisplayOrder = newDisplayOrder;
    m_canvas->reorderLayers(desiredCanvasOrder);

    // Persist the new group order globally so it sticks across sessions
    // (mirrors how the user expects layer ordering to be remembered).
    QVariantList toSave;
    toSave.reserve(m_categoryDisplayOrder.size());
    for (int catId : m_categoryDisplayOrder)
        toSave.append(catId);
    QSettings().setValue(QStringLiteral("layerTree/categoryDisplayOrder"), toSave);
}

bool LayerTreeModel::dropMimeData(const QMimeData *data, Qt::DropAction action,
                                   int row, int /*column*/,
                                   const QModelIndex &parent)
{
    if (!m_canvas || action != Qt::MoveAction) return false;

    // ── Category drop ─────────────────────────────────────────────────────
    if (data->hasFormat(QStringLiteral("application/x-layercategory"))) {
        QByteArray buf = data->data(QStringLiteral("application/x-layercategory"));
        QDataStream ds(&buf, QIODevice::ReadOnly);
        qint32 srcCatId = -1; ds >> srcCatId;
        if (srcCatId < 0 || srcCatId >= CatCount) return false;

        const int srcDisplayPos = m_categoryDisplayOrder.indexOf(srcCatId);
        if (srcDisplayPos < 0) return false;

        int dstDisplayPos;
        if (!parent.isValid())
            dstDisplayPos = (row < 0) ? m_categories.size() : row;
        else
            dstDisplayPos = parent.row();

        if (dstDisplayPos > srcDisplayPos) --dstDisplayPos;
        if (dstDisplayPos == srcDisplayPos) return false;
        dstDisplayPos = qBound(0, dstDisplayPos, m_categoryDisplayOrder.size() - 1);

        reorderCategories(srcDisplayPos, dstDisplayPos);
        return true;
    }

    // ── Sublayer drop (Slice GUI-2026-05-30 §2) ──────────────────────────
    if (data->hasFormat(QStringLiteral("application/x-sublayerrow"))) {
        if (!parent.isValid()) return false;
        void *pp = parent.internalPointer();
        if (!pp || !m_sublayerRowPtrSet.contains(pp)) return false;
        const SublayerRow *dst = static_cast<const SublayerRow *>(pp);
        if (!dst || !dst->layer) return false;

        QByteArray buf = data->data(QStringLiteral("application/x-sublayerrow"));
        QDataStream ds(&buf, QIODevice::ReadOnly);
        quintptr hostPtr = 0; qint32 srcIdx = -1;
        ds >> hostPtr >> srcIdx;
        if (reinterpret_cast<quintptr>(dst->layer) != hostPtr || srcIdx < 0)
            return false;

        auto *host = dynamic_cast<OpenSWMM::Render::ISublayerHost *>(dst->layer);
        if (!host) return false;
        const auto subs = host->sublayers();
        int dstIdx = subs.indexOf(dst->sublayer);
        if (dstIdx < 0) return false;
        if (host->moveSublayer(srcIdx, dstIdx)) {
            // Full storage rebuild — see notifyHostSubOrderChanged().
            beginResetModel();
            rebuildKindRows();
            rebuildSublayerRows();
            endResetModel();
        }
        return true;
    }

    // ── Layer drop ────────────────────────────────────────────────────────
    if (!data->hasFormat(QStringLiteral("application/x-layerrow"))
        || !parent.isValid() || parent.internalPointer() != nullptr)
        return false;

    QByteArray encoded = data->data(QStringLiteral("application/x-layerrow"));
    QDataStream stream(&encoded, QIODevice::ReadOnly);
    quintptr layerPtr = 0; stream >> layerPtr;
    auto *srcLayer = reinterpret_cast<OpenSWMMVisLayer *>(layerPtr);
    if (!srcLayer) return false;

    const int srcCatIdx = m_layerToCategory.value(srcLayer, -1);
    const int dstCatIdx = parent.row();
    if (dstCatIdx < 0 || dstCatIdx >= m_categories.size()) return false;

    const Category &dstCat = m_categories[dstCatIdx];

    // Helper: find canvas index of a layer.
    auto canvasIndexOf = [this](OpenSWMMVisLayer *l) {
        for (int i = 0; i < m_canvas->layerCount(); ++i)
            if (m_canvas->layerAt(i) == l) return i;
        return -1;
    };

    if (srcCatIdx == dstCatIdx) {
        // Within-category reorder (original behaviour).
        const Category &srcCat = m_categories[srcCatIdx];
        int srcPos = srcCat.layers.indexOf(srcLayer);
        if (srcPos < 0) return false;
        int dstPos = (row < 0) ? dstCat.layers.size() : row;
        if (dstPos > srcPos) --dstPos;
        if (dstPos == srcPos) return false;

        const int srcCanvas = canvasIndexOf(srcLayer);
        const int dstCanvas = canvasIndexOf(
            dstCat.layers[qMin(dstPos, dstCat.layers.size() - 1)]);
        if (srcCanvas < 0 || dstCanvas < 0) return false;
        m_canvas->moveLayer(srcCanvas, dstCanvas);
        return true;
    }

    // Cross-category: move srcLayer to the position in dstCat.
    int dstPos = (row < 0) ? qMax(0, dstCat.layers.size() - 1)
                           : qMin(row, qMax(0, dstCat.layers.size() - 1));
    const int srcCanvas = canvasIndexOf(srcLayer);
    int dstCanvas = -1;
    if (!dstCat.layers.isEmpty()) {
        dstCanvas = canvasIndexOf(dstCat.layers[dstPos]);
    } else {
        // Target category is empty — find a suitable insertion canvas index
        // from neighbouring categories.  Fall back to index 0.
        dstCanvas = 0;
    }
    if (srcCanvas < 0 || dstCanvas < 0) return false;
    m_canvas->moveLayer(srcCanvas, dstCanvas);
    return true;
}

OpenSWMMVisLayer *LayerTreeModel::layerForIndex(const QModelIndex &index) const
{
    if (!index.isValid())
        return nullptr;
    void *p = index.internalPointer();
    if (!p) return nullptr;
    // Slice BI-MK.LT — kind sub-row: return its parent layer.
    if (m_kindRowPtrSet.contains(p))
        return static_cast<const KindRow *>(p)->layer;
    // Slice S3 — sublayer sub-row: return its parent layer.
    if (m_sublayerRowPtrSet.contains(p))
        return static_cast<const SublayerRow *>(p)->layer;
    return static_cast<OpenSWMMVisLayer *>(p);
}

bool LayerTreeModel::isCategoryIndex(const QModelIndex &index) const
{
    return index.isValid() && index.internalPointer() == nullptr;
}

// Slice BI-MK.LT — kind-row accessors used by LayerTreePanel's context menu.

bool LayerTreeModel::isKindIndex(const QModelIndex &index) const
{
    return index.isValid()
        && index.internalPointer() != nullptr
        && m_kindRowPtrSet.contains(index.internalPointer());
}

OpenSWMMVisLayer *LayerTreeModel::kindParentLayer(const QModelIndex &index) const
{
    if (!isKindIndex(index)) return nullptr;
    return static_cast<const KindRow *>(index.internalPointer())->layer;
}

int LayerTreeModel::kindOrdinal(const QModelIndex &index) const
{
    if (!isKindIndex(index)) return -1;
    return static_cast<const KindRow *>(index.internalPointer())->kindOrdinal;
}

// Slice S3 — sublayer-row accessors mirror the kind-row helpers.

bool LayerTreeModel::isSublayerIndex(const QModelIndex &index) const
{
    return index.isValid()
        && index.internalPointer() != nullptr
        && m_sublayerRowPtrSet.contains(index.internalPointer());
}

OpenSWMMVisLayer *LayerTreeModel::sublayerParentLayer(const QModelIndex &index) const
{
    if (!isSublayerIndex(index)) return nullptr;
    return static_cast<const SublayerRow *>(index.internalPointer())->layer;
}

OpenSWMM::Render::ISublayer *
LayerTreeModel::sublayerForIndex(const QModelIndex &index) const
{
    if (!isSublayerIndex(index)) return nullptr;
    return static_cast<const SublayerRow *>(index.internalPointer())->sublayer;
}

// ---------------------------------------------------------------------------
// Canvas signal handlers — categorisation invalidates on every layer mutation,
// so a full reset (cheap: fewer than ~20 layers in practice) is the simplest
// correct thing to do.
// ---------------------------------------------------------------------------

void LayerTreeModel::onLayerAdded(OpenSWMMVisLayer *)
{
    beginResetModel();
    rebuildCategories();
    endResetModel();
}

void LayerTreeModel::onLayerRemoved(OpenSWMMVisLayer *)
{
    beginResetModel();
    rebuildCategories();
    endResetModel();
}

void LayerTreeModel::onLayerOrderChanged()
{
    beginResetModel();
    rebuildCategories();
    endResetModel();
}

void LayerTreeModel::onLayerDataChanged(OpenSWMMVisLayer *layer)
{
    if (!layer)
        return;
    const int catIdx = m_layerToCategory.value(layer, -1);
    if (catIdx < 0) return;
    const int row = m_categories[catIdx].layers.indexOf(layer);
    if (row < 0) return;
    const QModelIndex parentIdx = createIndex(catIdx, 0, static_cast<void *>(nullptr));
    const QModelIndex tl = index(row, 0,                  parentIdx);
    const QModelIndex br = index(row, columnCount() - 1,  parentIdx);
    emit dataChanged(tl, br);
}

void LayerTreeModel::notifyHostSubOrderChanged()
{
    // Sublayer / kind sub-row order is computed lazily from the host's
    // sublayers() / kindPaintOrder() at rebuild time.  A full reset is the
    // simplest correctness-preserving update: it invalidates every
    // QModelIndex internalPointer (the SublayerRow / KindRow structs are
    // re-allocated), avoiding any chance of a stale pointer dereference.
    beginResetModel();
    rebuildKindRows();
    rebuildSublayerRows();
    endResetModel();
}

// ===========================================================================
// LayerTreePanel
// ===========================================================================

LayerTreePanel::LayerTreePanel(MapCanvas *canvas, QWidget *parent)
    : QWidget(parent),
      m_canvas(canvas)
{
    setupUi();
}

LayerTreePanel::~LayerTreePanel() = default;

// ---------------------------------------------------------------------------

OpenSWMMVisLayer *LayerTreePanel::selectedLayer() const
{
    return m_model->layerForIndex(toSourceIndex(m_treeView->currentIndex()));
}

QModelIndex LayerTreePanel::toSourceIndex(const QModelIndex &proxyIdx) const
{
    if (!proxyIdx.isValid()) return {};
    return m_proxy ? m_proxy->mapToSource(proxyIdx) : proxyIdx;
}

void LayerTreePanel::setCanvas(MapCanvas *canvas)
{
    if (m_canvas == canvas)
        return;
    m_canvas = canvas;
    m_model->setCanvas(canvas);
}

// ---------------------------------------------------------------------------

void LayerTreePanel::setupUi()
{
    auto *vlay = new QVBoxLayout(this);
    vlay->setContentsMargins(2, 2, 2, 2);
    vlay->setSpacing(2);

    // Search box — filters the tree by layer name. Categories with no
    // matching layers are hidden via QSortFilterProxyModel's recursive mode.
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(tr("Filter layers…"));
    m_searchEdit->setClearButtonEnabled(true);
    vlay->addWidget(m_searchEdit);

    // Tree view — categorised by layer type. No add-layer toolbar here; those
    // actions live on the main application toolbar (actionAddVectorData etc.).
    // Per-layer ops are reachable via the right-click context menu.
    m_treeView = new QTreeView(this);
    m_treeView->setDragEnabled(true);
    m_treeView->setAcceptDrops(true);
    m_treeView->setDropIndicatorShown(true);
    m_treeView->setDragDropMode(QAbstractItemView::InternalMove);
    m_treeView->setSelectionMode(QAbstractItemView::SingleSelection);
    // Slice O: double-click on col 0 zooms to the layer; col 1 opens the
    // opacity editor (gated by SelectedClicked so the user has to first
    // select the row, then click col 1 to begin editing — single clicks
    // elsewhere never start an edit).
    m_treeView->setEditTriggers(QAbstractItemView::SelectedClicked
                                | QAbstractItemView::EditKeyPressed);
    m_treeView->setItemDelegateForColumn(1, new LayerOpacityDelegate(m_treeView));
    m_treeView->setAlternatingRowColors(true);
    m_treeView->setHeaderHidden(false);
    m_treeView->setRootIsDecorated(true);          // show category expand chevrons
    m_treeView->setExpandsOnDoubleClick(false);    // double-click = zoom-to-layer
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    vlay->addWidget(m_treeView, 1);

    // Model + proxy
    m_model = new LayerTreeModel(m_canvas, this);
    m_proxy = new QSortFilterProxyModel(this);
    m_proxy->setSourceModel(m_model);
    m_proxy->setRecursiveFilteringEnabled(true);
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxy->setFilterKeyColumn(0);
    m_treeView->setModel(m_proxy);
    m_treeView->setColumnWidth(0, 220);
    m_treeView->setColumnWidth(1, 60);
    m_treeView->expandAll();

    // Re-expand on every reset so newly-categorised layers (and the result
    // of filter changes) are visible without manual expand.
    connect(m_model, &QAbstractItemModel::modelReset,
            m_treeView, &QTreeView::expandAll);
    connect(m_proxy, &QAbstractItemModel::modelReset,
            m_treeView, &QTreeView::expandAll);
    connect(m_proxy, &QAbstractItemModel::layoutChanged,
            m_treeView, &QTreeView::expandAll);

    // Selection + interactions
    connect(m_treeView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &LayerTreePanel::onSelectionChanged);
    connect(m_treeView, &QTreeView::doubleClicked,
            this, &LayerTreePanel::onLayerDoubleClicked);
    connect(m_treeView, &QTreeView::customContextMenuRequested,
            this, &LayerTreePanel::onContextMenuRequested);
    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &LayerTreePanel::onSearchTextChanged);
}

// ---------------------------------------------------------------------------
// Slots — manage layers
// ---------------------------------------------------------------------------

void LayerTreePanel::onRemoveSelectedLayer()
{
    if (!m_canvas) return;
    OpenSWMMVisLayer *sel = selectedLayer();
    if (!sel)
        return;

    for (int i = 0; i < m_canvas->layerCount(); ++i)
    {
        if (m_canvas->layerAt(i) == sel)
        {
            OpenSWMMVisLayer *taken = m_canvas->takeLayer(i);
            taken->deleteLater();
            return;
        }
    }
}

void LayerTreePanel::onZoomToSelectedLayer()
{
    if (!m_canvas) return;
    OpenSWMMVisLayer *sel = selectedLayer();
    if (!sel)
        return;

    const MapExtent ext = m_canvas->layerExtentInCanvasCRS(sel);
    if (ext.isValid())
        m_canvas->setExtent(ext);
}

void LayerTreePanel::onMoveLayerUp()
{
    if (!m_canvas) return;
    OpenSWMMVisLayer *sel = selectedLayer();
    if (!sel) return;       // category row — nothing to move

    int canvasIdx = -1;
    for (int i = 0; i < m_canvas->layerCount(); ++i)
        if (m_canvas->layerAt(i) == sel) { canvasIdx = i; break; }
    if (canvasIdx < 0) return;
    const int targetIdx = canvasIdx + 1;   // "up" in display = higher canvas index
    if (targetIdx >= m_canvas->layerCount()) return;
    m_canvas->moveLayer(canvasIdx, targetIdx);
}

void LayerTreePanel::onMoveLayerDown()
{
    if (!m_canvas) return;
    OpenSWMMVisLayer *sel = selectedLayer();
    if (!sel) return;       // category row — nothing to move

    int canvasIdx = -1;
    for (int i = 0; i < m_canvas->layerCount(); ++i)
        if (m_canvas->layerAt(i) == sel) { canvasIdx = i; break; }
    if (canvasIdx < 0) return;
    const int targetIdx = canvasIdx - 1;   // "down" in display = lower canvas index
    if (targetIdx < 0) return;
    m_canvas->moveLayer(canvasIdx, targetIdx);
}

void LayerTreePanel::onSelectionChanged()
{
    emit layerSelected(selectedLayer());
}

void LayerTreePanel::onLayerDoubleClicked(const QModelIndex &index)
{
    // Slice O: zoom to the layer's extent rather than opening the
    // Properties dialog.  Skip when the user double-clicked the opacity
    // column — they're targeting the cell, not the row.
    if (index.column() == 1)
        return;
    OpenSWMMVisLayer *layer = m_model->layerForIndex(toSourceIndex(index));
    if (layer)
        onZoomToSelectedLayer();
}

void LayerTreePanel::onMoveCategoryUp()
{
    if (!m_canvas) return;
    const QModelIndex idx = toSourceIndex(m_treeView->currentIndex());
    if (!m_model->isCategoryIndex(idx)) return;
    const int displayPos = idx.row();
    if (displayPos <= 0) return;
    m_model->reorderCategories(displayPos, displayPos - 1);
}

void LayerTreePanel::onMoveCategoryDown()
{
    if (!m_canvas) return;
    const QModelIndex idx = toSourceIndex(m_treeView->currentIndex());
    if (!m_model->isCategoryIndex(idx)) return;
    const int displayPos = idx.row();
    const int catCount = m_model->rowCount({});
    if (displayPos >= catCount - 1) return;
    m_model->reorderCategories(displayPos, displayPos + 1);
}

void LayerTreePanel::onContextMenuRequested(const QPoint &pos)
{
    const QModelIndex proxyIdx = m_treeView->indexAt(pos);
    const QModelIndex idx      = toSourceIndex(proxyIdx);
    if (!idx.isValid())
        return;
    m_treeView->setCurrentIndex(proxyIdx);

    // ── Category row ────────────────────────────────────────────────────
    if (m_model->isCategoryIndex(idx))
    {
        const int displayPos = idx.row();
        const int catCount   = m_model->rowCount({});

        QMenu menu(this);
        QStyle *s = QApplication::style();
        QAction *actUp   = menu.addAction(s->standardIcon(QStyle::SP_ArrowUp),
                                          tr("Move Category Up"));
        QAction *actDown = menu.addAction(s->standardIcon(QStyle::SP_ArrowDown),
                                          tr("Move Category Down"));
        actUp  ->setEnabled(displayPos > 0);
        actDown->setEnabled(displayPos < catCount - 1);

        QAction *picked = menu.exec(m_treeView->viewport()->mapToGlobal(pos));
        if (!picked) return;
        if      (picked == actUp)   onMoveCategoryUp();
        else if (picked == actDown) onMoveCategoryDown();
        return;
    }

    // ── Sublayer sub-row (Slice S3 — RENDERING_OUTPUT_SUBLAYERS_PLAN.md §4.1) ──
    if (m_model->isSublayerIndex(idx))
    {
        OpenSWMM::Render::ISublayer *sub = m_model->sublayerForIndex(idx);
        if (!sub) return;
        OpenSWMMVisLayer *parentLayer = m_model->sublayerParentLayer(idx);
        auto *host = dynamic_cast<OpenSWMM::Render::ISublayerHost *>(parentLayer);

        QMenu subMenu(this);
        QStyle *ss = QApplication::style();

        // Slice U-10 — align with the layer-row / kind-row menus: the
        // first entry always opens the unified LayerStyleDialog, scoped
        // to whatever the user clicked. Sublayer rows route to the
        // matching sub-tab via the sublayer's id.
        QAction *actEditStyle = subMenu.addAction(
            ss->standardIcon(QStyle::SP_FileDialogDetailedView),
            tr("Properties…"));
        actEditStyle->setEnabled(sub->style() != nullptr);
        subMenu.addSeparator();
        QAction *actToggle = subMenu.addAction(
            sub->isVisible() ? tr("Hide %1").arg(sub->displayName())
                             : tr("Show %1").arg(sub->displayName()));
        subMenu.addSeparator();

        // Slice GUI-2026-05-30 §2 — sublayer reorder.  Up moves toward
        // the top of the paint stack (higher index); Down moves the
        // opposite way.  Disabled at the boundaries.
        QAction *actUp = subMenu.addAction(
            ss->standardIcon(QStyle::SP_ArrowUp),   tr("Move Up"));
        QAction *actDown = subMenu.addAction(
            ss->standardIcon(QStyle::SP_ArrowDown), tr("Move Down"));
        int curPos = -1, nSubs = 0;
        if (host) {
            const auto subs = host->sublayers();
            nSubs  = subs.size();
            curPos = subs.indexOf(sub);
        }
        actUp  ->setEnabled(host && curPos >= 0 && curPos < nSubs - 1);
        actDown->setEnabled(host && curPos >  0);

        QAction *picked = subMenu.exec(m_treeView->viewport()->mapToGlobal(pos));
        if (!picked) return;
        if (picked == actToggle) {
            sub->setVisible(!sub->isVisible());
        } else if (picked == actEditStyle) {
            // Slice U-10 — route to the unified LayerStyleDialog focused
            // on this sublayer's tab via routingId=sub->id().
            if (parentLayer) {
                auto *dlg = new openswmmvis::ui::LayerStyleDialog(
                    parentLayer, sub->id(), this);
                dlg->setAttribute(Qt::WA_DeleteOnClose);
                dlg->show();
            }
        } else if (host && (picked == actUp || picked == actDown)) {
            const int target = curPos + (picked == actUp ? +1 : -1);
            if (host->moveSublayer(curPos, target)) {
                // Rebuild model storage so subsequent QModelIndexes stay
                // valid and the view re-renders in the new paint order.
                m_model->notifyHostSubOrderChanged();
            }
        }
        return;
    }

    // ── Kind sub-row (Slice BI-MK.LT + Slice OUT.3) ───────────────────────
    if (m_model->isKindIndex(idx))
    {
        OpenSWMMVisLayer *parentLayer = m_model->kindParentLayer(idx);
        auto *swmm    = qobject_cast<SWMMModelLayer   *>(parentLayer);
        auto *results = qobject_cast<SWMMResultsLayer *>(parentLayer);
        if (!swmm && !results) return;
        const int kindOrd = m_model->kindOrdinal(idx);
        const auto cat    = static_cast<SWMMModelLayer::Category>(kindOrd);
        const QString kindLabel = SWMMModelLayer::kindKey(cat);
        // (`isLinkKind` was used by the dropped flow-arrow + Style submenu;
        //  preserved here as a no-op reference in case future menu items
        //  need it — silenced via Q_UNUSED below.)
        Q_UNUSED(cat);

        // Slice X.7 — kind-row menu shrinks to the canonical short set:
        //   ☑ Show / Hide <kind>
        //   ──────────
        //   Properties…   (LayerStyleDialog focused on this kind)
        //   Plot timeseries ▸ <object list>
        // The Style ▸ Single / Graduated / Categorized submenu is dropped
        // entirely — the renderer-class swap now lives inside the
        // Symbology tab (§X.3.2).
        QMenu kindMenu(this);
        QStyle *ks = QApplication::style();

        // Show / Hide (only meaningful for model layer; result-layer kinds
        // don't have per-category visibility today).
        QAction *actToggleK = nullptr;
        if (swmm) {
            const Qt::CheckState st = swmm->categoryCheckState(cat);
            actToggleK = kindMenu.addAction(
                st == Qt::Unchecked ? tr("Show %1").arg(kindLabel)
                                    : tr("Hide %1").arg(kindLabel));
            kindMenu.addSeparator();
        }

        QAction *actPropsK = kindMenu.addAction(
            ks->standardIcon(QStyle::SP_FileDialogInfoView),
            tr("Properties…"));

        // X.7 — unused renderer-mode / arrow / reset actions retained as
        // nullptrs so the pickedK dispatcher below keeps compiling.
        QAction *actStyleSingle      = nullptr;
        QAction *actStyleGraduated   = nullptr;
        QAction *actStyleCategorized = nullptr;
        QAction *actArrows           = nullptr;
        QAction *actReset            = nullptr;
        QAction *actZoomK            = nullptr;

        // Slice PT.1 — Plot timeseries submenu. Available on both
        // SWMMModelLayer and SWMMResultsLayer kind rows: the .out file
        // (or live simulation) is what drives the chart, but the model
        // layer has the object name index pre-simulation so users can
        // queue plots even before running.
        QMenu *plotSubmenu = nullptr;
        QList<QAction *> plotObjActs;
        const int featureCount = swmm ? swmm->categoryCount(cat) : 0;
        if (swmm && featureCount > 0
            && (cat == SWMMModelLayer::CatJunctions
                || cat == SWMMModelLayer::CatOutfalls
                || cat == SWMMModelLayer::CatStorage
                || cat == SWMMModelLayer::CatDividers
                || cat == SWMMModelLayer::CatConduits
                || cat == SWMMModelLayer::CatPumps
                || cat == SWMMModelLayer::CatOrifices
                || cat == SWMMModelLayer::CatWeirs
                || cat == SWMMModelLayer::CatOutlets
                || cat == SWMMModelLayer::CatSubcatchments)) {
            kindMenu.addSeparator();
            plotSubmenu = kindMenu.addMenu(
                QIcon(QStringLiteral(":/swmmvis/Chart")),
                tr("Plot timeseries…"));
            // Cap to first 50 to keep the menu reasonable on large
            // models. Users can use the Object Browser for full lists.
            const int cap = std::min(featureCount, 50);
            plotObjActs.reserve(cap);
            for (int i = 0; i < cap; ++i) {
                const QString name = swmm->objectNameAt(cat, i);
                if (name.isEmpty()) continue;
                plotObjActs.append(plotSubmenu->addAction(name));
            }
            if (featureCount > cap) {
                plotSubmenu->addSeparator();
                QAction *more = plotSubmenu->addAction(
                    tr("… +%1 more (use Object Browser)").arg(featureCount - cap));
                more->setEnabled(false);
            }
        }

        // Slice X.7 — Reset-to-defaults moves inside the Symbology tab's
        // per-kind editor (a small button next to the renderer-mode combo).
        // The kind-row menu stays tight: Show/Hide + Properties + Plot.
        QAction *pickedK = kindMenu.exec(m_treeView->viewport()->mapToGlobal(pos));
        if (!pickedK) return;

        // Use parentLayer (works for both SWMMModelLayer and
        // SWMMResultsLayer) when emitting the style signal so the
        // downstream slot dispatches based on the runtime type.
        if (pickedK == actPropsK) {
            // Slice U-10 — kind-row "Properties…" routes to the unified
            // LayerStyleDialog focused on this kind's adapter. Routing
            // ids match the convention used by SWMMModelLayer/Results
            // styleSubjects(): "model.<kind>" or "results.<kind>".
            const QString prefix = swmm ? QStringLiteral("model.")
                                         : QStringLiteral("results.");
            const QString suffix = [cat]() -> QString {
                switch (cat) {
                    case SWMMModelLayer::CatJunctions:     return QStringLiteral("junctions");
                    case SWMMModelLayer::CatOutfalls:      return QStringLiteral("outfalls");
                    case SWMMModelLayer::CatStorage:       return QStringLiteral("storage");
                    case SWMMModelLayer::CatDividers:      return QStringLiteral("dividers");
                    case SWMMModelLayer::CatConduits:      return QStringLiteral("conduits");
                    case SWMMModelLayer::CatPumps:         return QStringLiteral("pumps");
                    case SWMMModelLayer::CatOrifices:      return QStringLiteral("orifices");
                    case SWMMModelLayer::CatWeirs:         return QStringLiteral("weirs");
                    case SWMMModelLayer::CatOutlets:       return QStringLiteral("outlets");
                    case SWMMModelLayer::CatSubcatchments: return QStringLiteral("subcatchments");
                    case SWMMModelLayer::CatRainGages:     return QStringLiteral("raingages");
                    default:                               return QString();
                }
            }();
            auto *dlg = new openswmmvis::ui::LayerStyleDialog(
                parentLayer, prefix + suffix, this);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            dlg->show();
        } else if (actToggleK && pickedK == actToggleK) {
            // Only set on model layer (we only built the action there).
            if (swmm) {
                const Qt::CheckState st = swmm->categoryCheckState(cat);
                swmm->setCategoryVisible(cat, st == Qt::Unchecked);
            }
        } else if (pickedK == actStyleSingle) {
            emit layerKindStyleRequested(parentLayer, kindOrd, QStringLiteral("single"));
        } else if (pickedK == actStyleGraduated) {
            emit layerKindStyleRequested(parentLayer, kindOrd, QStringLiteral("graduated"));
        } else if (pickedK == actStyleCategorized) {
            emit layerKindStyleRequested(parentLayer, kindOrd, QStringLiteral("categorized"));
        } else if (actArrows && pickedK == actArrows) {
            swmm->setLinkArrowsEnabled(cat, !swmm->linkArrowsEnabled(cat));
        } else if (pickedK == actReset) {
            if (swmm) swmm->resetKindRendererToDefaults(cat);
            else if (results) results->resetKindRendererToDefaults(cat);
        } else if (plotSubmenu && plotObjActs.contains(pickedK)) {
            // Slice PT.1 — Plot timeseries… → emit so SWMMVis pops the
            // AttributePickerMenu and opens the Comparison Plot Dialog.
            emit plotKindObjectRequested(kindOrd, pickedK->text());
        }
        return;
    }

    // ── Layer row ────────────────────────────────────────────────────────
    //
    // Slice X.7 — canonical layer-row context menu.  Same set of actions
    // appears for every layer type; entries that don't apply to the
    // current layer are shown disabled-but-visible so muscle memory
    // remains consistent across types.  Order matches QGIS / ArcGIS Pro.
    OpenSWMMVisLayer *layer = m_model->layerForIndex(idx);
    if (!layer)
        return;

    QMenu menu(this);
    QStyle *s = QApplication::style();

    // Layer-type checks for the disabled-but-visible policy.
    const int   ltype       = layer->layerType();
    const bool  isVector    = (ltype == OpenSWMMVisLayer::SWMMVectorLayer
                            || ltype == OpenSWMMVisLayer::SWMMGISLayer);
    const bool  isResults   = (qobject_cast<SWMMResultsLayer *>(layer) != nullptr);
    const bool  hasAttrTable = isVector
                            || ltype == OpenSWMMVisLayer::SWMMModelLayer
                            || isResults;

    // Group 1 — navigation
    QAction *actZoom = menu.addAction(QIcon(QStringLiteral(":/swmmvis/Extent")),
                                      tr("Zoom to Layer"));
    QAction *actOverview = menu.addAction(tr("Show in Overview"));
    actOverview->setEnabled(false);                 // pending feature
    menu.addSeparator();

    // Group 2 — data inspection
    QAction *actAttrTable = menu.addAction(tr("Open Attribute Table"));
    actAttrTable->setEnabled(false);                // pending feature
    Q_UNUSED(hasAttrTable);
    QAction *actFeatureCount = menu.addAction(tr("Show Feature Count"));
    actFeatureCount->setEnabled(false);             // pending feature
    auto *results2D = qobject_cast<SWMM2DResultsLayer *>(layer);
    const bool is2DResults = (results2D != nullptr);
    QAction *actPlotTS = nullptr;
    if (isResults) {
        actPlotTS = menu.addAction(QIcon(QStringLiteral(":/swmmvis/Chart")),
                                    tr("Plot Time Series…"));
    }
    // "Set as Active Results Layer" — makes this the layer every analysis /
    // visualization tool targets. Checkable; checked when it is already active.
    QAction *actSetActive = nullptr;
    if (isResults || is2DResults) {
        actSetActive = menu.addAction(tr("Set as Active Results Layer"));
        actSetActive->setCheckable(true);
        const bool isActive =
            (isResults  && qobject_cast<SWMMResultsLayer *>(layer) == m_activeResults1D) ||
            (is2DResults && results2D == m_activeResults2D);
        actSetActive->setChecked(isActive);
    }
    menu.addSeparator();

    // Group 3 — order
    QAction *actMoveTop = menu.addAction(s->standardIcon(QStyle::SP_TitleBarShadeButton),
                                          tr("Move to Top"));
    actMoveTop->setEnabled(false);                  // pending feature
    QAction *actUp   = menu.addAction(s->standardIcon(QStyle::SP_ArrowUp),
                                      tr("Move Up"));
    QAction *actDown = menu.addAction(s->standardIcon(QStyle::SP_ArrowDown),
                                      tr("Move Down"));
    QAction *actMoveBottom = menu.addAction(s->standardIcon(QStyle::SP_TitleBarUnshadeButton),
                                             tr("Move to Bottom"));
    actMoveBottom->setEnabled(false);               // pending feature
    menu.addSeparator();

    // Group 4 — lifecycle
    QAction *actRename = menu.addAction(tr("Rename Layer"));
    actRename->setEnabled(false);                   // pending feature
    QAction *actDuplicate = menu.addAction(tr("Duplicate Layer"));
    actDuplicate->setEnabled(false);                // pending feature
    QAction *actRemove = menu.addAction(QIcon(QStringLiteral(":/swmmvis/Clear")),
                                        tr("Remove Layer"));
    menu.addSeparator();

    // Group 5 — query
    QAction *actFilter = menu.addAction(tr("Filter…"));
    actFilter->setEnabled(false);                   // pending feature
    QAction *actScaleVis = menu.addAction(tr("Set Layer Scale Visibility…"));
    actScaleVis->setEnabled(false);                 // pending feature
    menu.addSeparator();

    // Group 6 — visibility toggle (kept here near the bottom per QGIS)
    QAction *actToggle = menu.addAction(layer->isVisible()
                                            ? tr("Hide Layer")
                                            : tr("Show Layer"));
    menu.addSeparator();

    // Group 7 — styling (always the last entries — `Properties…` is the
    // primary action and Styles ▸ is a placeholder for future copy/paste).
    QAction *actProps = menu.addAction(s->standardIcon(QStyle::SP_FileDialogInfoView),
                                       tr("Properties…"));
    QMenu *stylesMenu = menu.addMenu(QIcon(QStringLiteral(":/swmmvis/Style")),
                                      tr("Styles"));
    QAction *actEditSymbology = stylesMenu->addAction(tr("Edit Symbology…"));
    QAction *actCopyStyle = stylesMenu->addAction(tr("Copy Style"));
    actCopyStyle->setEnabled(false);                // pending feature
    QAction *actPasteStyle = stylesMenu->addAction(tr("Paste Style"));
    actPasteStyle->setEnabled(false);               // pending feature

    // Move enable/disable based on canvas position.
    int canvasIdx = -1;
    if (m_canvas)
        for (int i = 0; i < m_canvas->layerCount(); ++i)
            if (m_canvas->layerAt(i) == layer) { canvasIdx = i; break; }
    actUp->setEnabled  (m_canvas && canvasIdx >= 0 && canvasIdx < m_canvas->layerCount() - 1);
    actDown->setEnabled(m_canvas && canvasIdx > 0);

    QAction *picked = menu.exec(m_treeView->viewport()->mapToGlobal(pos));
    if (!picked) return;
    if      (picked == actZoom)        onZoomToSelectedLayer();
    else if (picked == actProps)       emit layerPropertiesRequested(layer);
    else if (picked == actEditSymbology) emit layerPropertiesRequested(layer);   // same dialog, Symbology tab focused
    else if (actPlotTS && picked == actPlotTS)
        emit plotTimeSeriesFromOutputLayerRequested(qobject_cast<SWMMResultsLayer *>(layer));
    else if (actSetActive && picked == actSetActive) {
        if (auto *r1d = qobject_cast<SWMMResultsLayer *>(layer))
            emit setActiveResultsLayerRequested(r1d);
        else if (auto *r2d = qobject_cast<SWMM2DResultsLayer *>(layer))
            emit setActive2DResultsLayerRequested(r2d);
    }
    else if (picked == actUp)          onMoveLayerUp();
    else if (picked == actDown)        onMoveLayerDown();
    else if (picked == actToggle)      layer->setVisible(!layer->isVisible());
    else if (picked == actRemove) onRemoveSelectedLayer();
}

void LayerTreePanel::setActiveResultsLayer(SWMMResultsLayer *layer)
{
    m_activeResults1D = layer;   // check-state only; menu reads it on next open
}

void LayerTreePanel::setActive2DResultsLayer(SWMM2DResultsLayer *layer)
{
    m_activeResults2D = layer;
}

void LayerTreePanel::onSearchTextChanged(const QString &text)
{
    if (!m_proxy) return;
    m_proxy->setFilterFixedString(text);
    m_treeView->expandAll();
}
