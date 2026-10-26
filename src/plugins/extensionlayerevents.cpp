/**
 * @file extensionlayerevents.cpp
 * @brief Canonical canvas attachment identities, semantic layer observations and view commands.
 * @details One bridge belongs to each model window, regardless of plugin count. Its
 * runtime identities describe canvas membership rather than a mutable persisted ID.
 * Detached layers are disconnected and a later undo/re-add receives a fresh identity.
 * Rendering repaint requests are deliberately not treated as authored style changes.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "plugins/extensionviewaccess.h"
#include "layers/annotationlayer.h"
#include "layers/featurelayer.h"
#include "layers/gisrasterlayer.h"
#include "layers/gisvectorlayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "layers/swmm2dresultslayer.h"
#include "layers/swmmmodellayer.h"
#include "layers/swmmresultslayer.h"
#include "layers/tabulardatalayer.h"
#include "layers/wmslayer.h"
#include "layers/wmtslayer.h"
#include "map/mapcanvas.h"
#include "map/spatialreferencesystem.h"
#include "render/isublayer.h"
#include "render/isublayerhost.h"
#include "render/rulelist.h"
#include "render/sublayerstyle.h"
#include "swmmvisprojectwindow.h"
#include "ui/panels/layertreecategories.h"
#include <QJsonObject>
#include <QVariantList>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <map>
#include <utility>

using namespace OpenSWMM::Sdk;
namespace openswmmvis
{
namespace
{
/** @brief Copy a stable native discriminator caption without exposing a private QObject.
 * @param[in] layer Borrowed current layer.
 * @return Untranslated type caption, or the numeric discriminator for a future type. */
QString typeCaption(const OpenSWMMVisLayer *layer)
{
    static const char *const captions[]{"Default",     "SWMM model", "SWMM results", "GIS",
                                        "Vector",      "Raster",     "Imagery",      "Table",
                                        "Time series", "Subproject", "WMS",          "WMTS",
                                        "2D mesh",     "2D results", "Annotation",   "Feature"};
    const auto type = int(layer->layerType());
    return type >= 0 && type < int(std::size(captions)) ? QString::fromLatin1(captions[type])
                                                        : QString::number(type);
}

/** @brief Copy metadata used to suppress repeated native general-property notifications.
 * @param[in] layer Borrowed attached layer.
 * @return Current persisted identity, title, discriminator, visibility and opacity. */
QVariantList metadata(const OpenSWMMVisLayer *layer)
{
    return {layer->layerId(), layer->name(), int(layer->layerType()), layer->isVisible(),
            layer->opacity()};
}

/** @brief Read applied vector filtering, excluding pending text on an unavailable source.
 * @param[in] layer Borrowed vector layer.
 * @return Stored accepted expression, or empty while closed. */
QString appliedFilter(const GISVectorLayer *layer)
{
    return layer->ogrLayer() ? layer->filterExpression() : QString{};
}

/** @brief Copy an inexpensive source signature without forcing feature scans.
 * @param[in] layer Borrowed attached layer.
 * @return Source/path/availability/count metadata used only for change comparison. */
QVariantList sourceMetadata(OpenSWMMVisLayer *layer)
{
    QVariantList result{layer->sourceDescription()};
    if (auto *vector = qobject_cast<GISVectorLayer *>(layer))
        result << vector->filePath() << bool(vector->ogrLayer())
               << (vector->ogrLayer() ? vector->featureCount() : -1);
    else if (auto *raster = qobject_cast<GISRasterLayer *>(layer))
        result << raster->filePath() << raster->bandCount();
    return result;
}

/** @brief Copy authored sublayer state while excluding per-frame invalidation counters.
 * @param[in] sub Borrowed semantic rendering contribution.
 * @return Display/visibility/opacity/style values, never frame/geometry payloads. */
QVariantList sublayerMetadata(OpenSWMM::Render::ISublayer *sub)
{
    return {sub->id(), sub->displayName(), sub->isVisible(), sub->opacity(),
            sub->style() ? sub->style()->toJson() : QJsonObject{}};
}
}

/** @brief Private per-session attachment map and guarded native signal wiring. */
struct ExtensionLayerBridge::Data {
    /** @brief Current membership and its semantic change-comparison caches. */
    struct Entry {
        QPointer<OpenSWMMVisLayer> layer; ///< Borrowed attached native layer.
        QUuid id;                         ///< Fresh identity for precisely this canvas attachment.
        QString nativeId;          ///< Last observed persisted identity for destruction history.
        QVariantList general;      ///< Last observed general-property values.
        QVariantList source;       ///< Last observed source/path/count metadata.
        QString crs;               ///< Last observed coordinate-reference value.
        QString filter;            ///< Last observed accepted filter on an available source.
        QSet<long long> selection; ///< Last observed vector selection for no-op suppression.
        QList<QMetaObject::Connection> connections; ///< Managed membership watcher handles.
        QList<QMetaObject::Connection>
            subConnections; ///< Replaced when sublayer structure changes.
    };
    ExtensionLayerBridge *bridge;          ///< Owning QObject connection context.
    QPointer<SWMMVisProjectWindow> window; ///< Guarded native session owner.
    QPointer<MapCanvas> canvas;            ///< Guarded canvas with externally owned layer pointers.
    ViewEventSink changed;                 ///< Canonical coordinator callback.
    std::map<OpenSWMMVisLayer *, Entry> entries; ///< Current memberships only.
    QList<QUuid> lastOrder;                      ///< Last observed top-level attachment order.

    /** @brief Verify that native state remains usable for an ordinary observation.
     * @return True while the model window/canvas remain available and not closing. */
    bool available() const { return window && canvas && !window->isClosing(); }

    /** @brief Resolve one tracked and currently attached native identity.
     * @param[in] key Non-dereferenced native pointer identity.
     * @return Current entry or null after removal/destruction/window close. */
    Entry *entry(OpenSWMMVisLayer *key)
    {
        const auto it = entries.find(key);
        return available() && it != entries.end() && it->second.layer &&
                       canvas->layers().contains(key)
                   ? &it->second
                   : nullptr;
    }

    /** @brief Send one owning observation after checking current attachment membership.
     * @param[in] key Tracked native identity.
     * @param[in] kind Semantic invalidation reason.
     * @param[in] keys Optional bounded semantic child identities. */
    void publish(OpenSWMMVisLayer *key, ViewEventKind kind, const QStringList &keys = {})
    {
        auto *current = entry(key);
        if (!current)
            return;
        current->nativeId = current->layer->layerId();
        ViewEvent event;
        event.kind = kind;
        event.layerSessionId = current->id;
        event.layerId = current->nativeId;
        event.keys = keys;
        if (changed)
            changed(std::move(event));
    }

    /** @brief Attach one native signal to this membership's managed connection list.
     * @tparam Sender Native QObject type.
     * @tparam Signal Typed native member-signal pointer.
     * @tparam Callback Zero-argument semantic observer.
     * @param[in,out] current Owning membership record.
     * @param[in] sender Borrowed required native signal source.
     * @param[in] signal Typed native signal.
     * @param[in] callback GUI-thread receiver action. */
    template <class Sender, class Signal, class Callback>
    void watch(Entry &current, Sender *sender, Signal signal, Callback callback)
    {
        current.connections.append(QObject::connect(sender, signal, bridge, std::move(callback)));
    }

    /** @brief Rebind authored sublayer observers after structural changes.
     * @param[in] key Tracked native attachment identity.
     * @note Dynamic invalidation alone produces no event unless authored metadata changed. */
    void watchSublayers(OpenSWMMVisLayer *key)
    {
        auto *current = entry(key);
        if (!current)
            return;
        for (const auto &connection : std::as_const(current->subConnections))
            QObject::disconnect(connection);
        current->subConnections.clear();
        auto *host = dynamic_cast<OpenSWMM::Render::ISublayerHost *>(key);
        if (!host)
            return;
        for (auto *sub : host->sublayers()) {
            if (!sub)
                continue;
            const QPointer<OpenSWMM::Render::ISublayer> guard(sub);
            auto previous = std::make_shared<QVariantList>(sublayerMetadata(sub));
            const auto authored = [this, key, guard, previous] {
                if (!guard || !entry(key))
                    return;
                const auto value = sublayerMetadata(guard);
                if (value == *previous)
                    return;
                *previous = value;
                publish(key, ViewEventKind::LayerStyleChanged, {guard->id()});
            };
            current->subConnections.append(
                QObject::connect(sub, &OpenSWMM::Render::ISublayer::invalidated, bridge, authored));
            if (auto *style = sub->style())
                current->subConnections.append(QObject::connect(
                    style, &OpenSWMM::Render::SublayerStyle::styleChanged, bridge, authored));
        }
    }

    /** @brief Copy tracked top-level attachment order without inventing identities.
     * @return Current bottom-to-top runtime identities, excluding unavailable pointers. */
    QList<QUuid> order() const
    {
        QList<QUuid> result;
        if (!canvas)
            return result;
        for (auto *layer : canvas->layers())
            if (const auto it = entries.find(layer); it != entries.end() && it->second.layer)
                result.append(it->second.id);
        return result;
    }

    /** @brief Wire a new attachment, optionally reporting its committed insertion.
     * @param[in] layer Borrowed newly attached native layer.
     * @param[in] announce False while constructing the initial tracker. */
    void attach(OpenSWMMVisLayer *layer, bool announce)
    {
        if (!available() || !layer || entries.contains(layer) || !canvas->layers().contains(layer))
            return;
        auto &current = entries[layer];
        current.layer = layer;
        current.id = QUuid::createUuid();
        current.nativeId = layer->layerId();
        current.general = metadata(layer);
        current.source = sourceMetadata(layer);
        current.crs = layer->srs() ? layer->srs()->toWkt() : QString{};
        auto *vector = qobject_cast<GISVectorLayer *>(layer);
        if (vector) {
            current.filter = appliedFilter(vector);
            current.selection = vector->selectedFeatureIds();
        }
        const auto general = [this, layer] {
            auto *item = entry(layer);
            if (!item)
                return;
            const auto value = metadata(layer);
            if (value == item->general)
                return;
            item->general = value;
            publish(layer, ViewEventKind::LayerMetadataChanged);
        };
        watch(current, layer, &OpenSWMMVisLayer::nameChanged, general);
        watch(current, layer, &OpenSWMMVisLayer::visibilityChanged, general);
        watch(current, layer, &OpenSWMMVisLayer::opacityChanged, general);
        watch(current, layer, &OpenSWMMVisLayer::layerTypeChanged, general);
        watch(current, layer, &OpenSWMMVisLayer::srsChanged, [this, layer] {
            auto *item = entry(layer);
            if (!item)
                return;
            const auto value = layer->srs() ? layer->srs()->toWkt() : QString{};
            if (value == item->crs)
                return;
            item->crs = value;
            publish(layer, ViewEventKind::LayerCrsChanged);
        });
        const auto style = [this, layer] { publish(layer, ViewEventKind::LayerStyleChanged); };
        watch(current, layer, &OpenSWMMVisLayer::labelConfigChanged, style);
        watch(current, layer, &OpenSWMMVisLayer::temporalSpecChanged, style);
        watch(current, layer, &OpenSWMMVisLayer::maskSpecChanged, style);
        watch(current, layer, &OpenSWMMVisLayer::auxStorageSpecChanged, style);
        watch(current, layer, &OpenSWMMVisLayer::joinsChanged, style);
        watch(current, layer, &OpenSWMMVisLayer::diagramSpecChanged, style);
        watch(current, layer, &OpenSWMMVisLayer::childrenChanged, [this, layer] {
            watchSublayers(layer);
            publish(layer, ViewEventKind::LayerStyleChanged);
        });
        if (auto *rules = layer->ruleList()) {
            watch(current, rules, &OpenSWMM::Render::RuleList::ruleChanged, style);
            watch(current, rules, &OpenSWMM::Render::RuleList::ruleListChanged, style);
            watch(current, rules, &OpenSWMM::Render::RuleList::activeIndexChanged, style);
        }
        const auto source = [this, layer] {
            auto *item = entry(layer);
            if (!item)
                return;
            const auto value = sourceMetadata(layer);
            if (value == item->source)
                return;
            item->source = value;
            publish(layer, ViewEventKind::LayerSourceChanged);
        };
        // An adoption attempt can fail without changing retained path/count text.
        // Its explicit completion still invalidates attached-source availability.
        const auto sourceSettled = [this, layer] {
            auto *item = entry(layer);
            if (!item)
                return;
            item->source = sourceMetadata(layer);
            publish(layer, ViewEventKind::LayerSourceChanged);
        };
        if (vector) {
            watch(current, vector, &GISVectorLayer::selectionChanged, [this, vector] {
                auto *item = entry(vector);
                if (!item)
                    return;
                const auto value = vector->selectedFeatureIds();
                if (value == item->selection)
                    return;
                item->selection = value;
                publish(vector, ViewEventKind::LayerSelectionChanged);
            });
            watch(current, vector, &GISVectorLayer::filterExpressionChanged, [this, vector] {
                auto *item = entry(vector);
                if (!item)
                    return;
                const auto value = appliedFilter(vector);
                if (value == item->filter)
                    return;
                item->filter = value;
                publish(vector, ViewEventKind::LayerFilterChanged);
            });
            watch(current, vector, &GISVectorLayer::filePathChanged, source);
            watch(current, vector, &GISVectorLayer::layerNameChanged, source);
            watch(current, vector, &GISVectorLayer::featureCountChanged, source);
            watch(current, vector, &GISVectorLayer::openFinished, sourceSettled);
            watch(current, vector, &GISVectorLayer::rendererChanged, style);
            watch(current, vector, &GISVectorLayer::symbolChanged, style);
        }
        if (auto *feature = qobject_cast<FeatureLayer *>(layer)) {
            watch(current, feature, &FeatureLayer::schemaChanged,
                  [this, layer] { publish(layer, ViewEventKind::LayerSchemaChanged); });
            watch(current, feature, &FeatureLayer::featuresChanged,
                  [this, layer] { publish(layer, ViewEventKind::LayerFeaturesChanged); });
            watch(current, feature, &FeatureLayer::roleChanged, [this, layer] {
                publish(layer, ViewEventKind::LayerMetadataChanged, {QStringLiteral("role")});
            });
            watch(current, feature, &FeatureLayer::zPolicyChanged, [this, layer] {
                publish(layer, ViewEventKind::LayerMetadataChanged, {QStringLiteral("zPolicy")});
            });
        }
        if (auto *raster = qobject_cast<GISRasterLayer *>(layer)) {
            watch(current, raster, &GISRasterLayer::filePathChanged, source);
            watch(current, raster, &GISRasterLayer::openFinished, sourceSettled);
            watch(current, raster, &GISRasterLayer::rasterRendererChanged, style);
            watch(current, raster, &GISRasterLayer::renderBandChanged, style);
            watch(current, raster, &GISRasterLayer::noDataValueChanged, style);
        }
        if (auto *model = qobject_cast<SWMMModelLayer *>(layer)) {
            watch(current, model, &SWMMModelLayer::rendererChanged, style);
            watch(current, model, &SWMMModelLayer::presentationChanged, style);
            watch(current, model, &SWMMModelLayer::categoryOrderChanged,
                  [this, layer] { publish(layer, ViewEventKind::LayerOrderChanged); });
        }
        if (auto *mesh = qobject_cast<SWMM2DMeshLayer *>(layer)) {
            watch(current, mesh, &SWMM2DMeshLayer::rendererChanged, style);
            watch(current, mesh, &SWMM2DMeshLayer::meshEditsChanged,
                  [this, layer] { publish(layer, ViewEventKind::LayerFeaturesChanged); });
            watch(current, mesh, &SWMM2DMeshLayer::attributeChanged,
                  [this, layer] { publish(layer, ViewEventKind::LayerFeaturesChanged); });
        }
        if (auto *results = qobject_cast<SWMMResultsLayer *>(layer)) {
            watch(current, results, &SWMMResultsLayer::rendererChanged, style);
            watch(current, results, &SWMMResultsLayer::profileStyleChanged, style);
            watch(current, results, &SWMMResultsLayer::profileLineColorChanged, style);
            watch(current, results, &SWMMResultsLayer::scenarioNameChanged, source);
        }
        if (auto *results = qobject_cast<SWMM2DResultsLayer *>(layer))
            watch(current, results, &SWMM2DResultsLayer::rendererChanged, style);
        if (auto *text = qobject_cast<OpenSWMMVisAnnotationLayer *>(layer)) {
            watch(current, text, &OpenSWMMVisAnnotationLayer::annotationsChanged,
                  [this, layer] { publish(layer, ViewEventKind::LayerFeaturesChanged); });
            watch(current, text, &OpenSWMMVisAnnotationLayer::annotationsChanged, style);
        }
        if (auto *table = qobject_cast<TabularDataLayer *>(layer)) {
            watch(current, table, &TabularDataLayer::dataLoaded,
                  [this, layer] { publish(layer, ViewEventKind::LayerSchemaChanged); });
            watch(current, table, &TabularDataLayer::dataLoaded,
                  [this, layer] { publish(layer, ViewEventKind::LayerFeaturesChanged); });
        }
        if (auto *wms = qobject_cast<WMSLayer *>(layer)) {
            watch(current, wms, &WMSLayer::capabilitiesFetched, sourceSettled);
            watch(current, wms, &WMSLayer::capabilitiesError, sourceSettled);
            watch(current, wms, &WMSLayer::activeLayerNameChanged, sourceSettled);
            watch(current, wms, &WMSLayer::wmsVersionChanged, sourceSettled);
            watch(current, wms, &WMSLayer::activeStyleChanged, style);
            watch(current, wms, &WMSLayer::imageFormatChanged, style);
            watch(current, wms, &WMSLayer::transparentChanged, style);
            watch(current, wms, &WMSLayer::basemapRenderParamsChanged, style);
        }
        if (auto *wmts = qobject_cast<WMTSLayer *>(layer)) {
            watch(current, wmts, &WMTSLayer::capabilitiesFetched, sourceSettled);
            watch(current, wmts, &WMTSLayer::capabilitiesError, sourceSettled);
            watch(current, wmts, &WMTSLayer::activeLayerIdChanged, sourceSettled);
            watch(current, wmts, &WMTSLayer::activeTileMatrixSetChanged, sourceSettled);
            watch(current, wmts, &WMTSLayer::activeStyleChanged, style);
            watch(current, wmts, &WMTSLayer::imageFormatChanged, style);
            watch(current, wmts, &WMTSLayer::basemapRenderParamsChanged, style);
        }
        watch(current, layer, &QObject::destroyed, [this, layer] { detach(layer); });
        watchSublayers(layer);
        lastOrder = order();
        if (announce)
            publish(layer, ViewEventKind::LayerAdded);
    }

    /** @brief Disconnect and retire a membership before reporting historical removal.
     * @param[in] layer Non-dereferenced native identity, possibly already destroyed. */
    void detach(OpenSWMMVisLayer *layer)
    {
        const auto found = entries.find(layer);
        if (found == entries.end())
            return;
        ViewEvent event;
        event.kind = ViewEventKind::LayerRemoved;
        event.layerSessionId = found->second.id;
        event.layerId =
            found->second.layer ? found->second.layer->layerId() : found->second.nativeId;
        for (const auto &connection : std::as_const(found->second.connections))
            QObject::disconnect(connection);
        for (const auto &connection : std::as_const(found->second.subConnections))
            QObject::disconnect(connection);
        entries.erase(found);
        lastOrder = order();
        if (available() && changed)
            changed(std::move(event));
    }
};

ExtensionLayerBridge::ExtensionLayerBridge(SWMMVisProjectWindow *window, ViewEventSink changed)
    : QObject(window), mData(std::make_unique<Data>())
{
    mData->bridge = this;
    mData->window = window;
    mData->canvas = window ? window->canvas() : nullptr;
    mData->changed = std::move(changed);
    if (!mData->available())
        return;
    for (auto *layer : mData->canvas->layers())
        mData->attach(layer, false);
    connect(mData->canvas, &MapCanvas::layerAdded, this,
            [this](OpenSWMMVisLayer *layer) { mData->attach(layer, true); });
    connect(mData->canvas, &MapCanvas::layerRemoved, this,
            [this](OpenSWMMVisLayer *layer) { mData->detach(layer); });
    connect(mData->canvas, &MapCanvas::layerOrderChanged, this, [this] {
        const auto order = mData->order();
        if (!mData->available() || order == mData->lastOrder)
            return;
        mData->lastOrder = order;
        ViewEvent event;
        event.kind = ViewEventKind::LayerOrderChanged;
        if (mData->changed)
            mData->changed(std::move(event));
    });
    connect(mData->canvas, &MapCanvas::sublayerOrderChanged, this, [this](OpenSWMMVisLayer *layer) {
        mData->publish(layer, ViewEventKind::LayerOrderChanged);
    });
}

ExtensionLayerBridge::~ExtensionLayerBridge()
{
    for (auto &[key, entry] : mData->entries) {
        for (const auto &connection : std::as_const(entry.connections))
            disconnect(connection);
        for (const auto &connection : std::as_const(entry.subConnections))
            disconnect(connection);
    }
}

QList<LayerViewState> ExtensionLayerBridge::snapshot() const
{
    QList<LayerViewState> result;
    if (!mData->available())
        return result;
    QSet<OpenSWMMVisLayer *> seen;
    for (auto *key : mData->canvas->layers()) {
        const auto found = mData->entries.find(key);
        if (found == mData->entries.end() || !found->second.layer || seen.contains(key))
            continue;
        seen.insert(key);
        const auto *layer = found->second.layer.data();
        LayerViewState state;
        state.layerSessionId = found->second.id;
        state.layerId = layer->layerId();
        state.title = layer->name();
        state.type = typeCaption(layer);
        state.crsWkt = layer->srs() ? layer->srs()->toWkt() : QString{};
        state.visible = layer->isVisible();
        state.opacity = layer->opacity();
        if (const auto *vector = qobject_cast<const GISVectorLayer *>(layer)) {
            state.vector = true;
            state.filter = appliedFilter(vector);
            state.featureCount = vector->ogrLayer() ? vector->featureCount() : -1;
            state.selectionCount = vector->selectedFeatureIds().size();
        }
        result.append(std::move(state));
    }
    return result;
}

QUuid ExtensionLayerBridge::identity(OpenSWMMVisLayer *layer) const
{
    const auto *entry = mData->entry(layer);
    return entry ? entry->id : QUuid{};
}

OpenSWMMVisLayer *ExtensionLayerBridge::resolve(const QUuid &id) const
{
    if (id.isNull() || !mData->available())
        return nullptr;
    for (const auto &[key, entry] : mData->entries)
        if (entry.id == id && entry.layer && mData->canvas->layers().contains(key))
            return entry.layer;
    return nullptr;
}

QList<qint64> ExtensionLayerBridge::selectedFeatures(const QUuid &id, int offset, int limit) const
{
    auto *layer = qobject_cast<GISVectorLayer *>(resolve(id));
    if (!layer || offset < 0 || limit <= 0)
        return {};
    QList<qint64> result;
    const auto selected = layer->selectedFeatureIds();
    result.reserve(selected.size());
    for (auto fid : selected)
        result.append(fid);
    std::sort(result.begin(), result.end());
    return result.mid(offset, std::min(limit, 5000));
}

bool ExtensionLayerBridge::execute(const ViewCommand &command, QString *error)
{
    if (error)
        error->clear();
    const auto refuse = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    const QPointer<ExtensionLayerBridge> owner(this);
    const QPointer<SWMMVisProjectWindow> window(mData->window);
    auto *layer = resolve(command.layerSessionId);
    if (!layer)
        return refuse(tr("The layer attachment is unavailable."));
    auto *canvas = mData->canvas.data();
    switch (command.kind) {
    case ViewCommandKind::RenameLayer:
        if (command.text.trimmed().isEmpty())
            return refuse(tr("A layer title must contain text."));
        if (layer->name() == command.text)
            return true;
        layer->setName(command.text);
        break;
    case ViewCommandKind::SetLayerVisible:
        if (layer->isVisible() == command.enabled)
            return true;
        layer->setVisible(command.enabled);
        break;
    case ViewCommandKind::SetLayerOpacity:
        if (!std::isfinite(command.number) || command.number < 0. || command.number > 1.)
            return refuse(tr("Layer opacity must be a finite value between zero and one."));
        if (qFuzzyCompare(layer->opacity() + 1., command.number + 1.))
            return true;
        layer->setOpacity(command.number);
        break;
    case ViewCommandKind::MoveLayer: {
        const auto layers = canvas->layers();
        if (command.index < 0 || command.index >= layers.size())
            return refuse(tr("The layer destination is outside the canvas stack."));
        const int source = layers.indexOf(layer), target = int(command.index);
        if (source == target)
            return true;
        const auto category = ui::categoryForLayerType(int(layer->layerType()));
        for (int index = std::min(source, target); index <= std::max(source, target); ++index) {
            const auto found = mData->entries.find(layers.at(index));
            if (found == mData->entries.end() || !found->second.layer ||
                ui::categoryForLayerType(int(found->second.layer->layerType())) != category)
                return refuse(tr("Move a layer only within its existing category group."));
        }
        canvas->moveLayer(source, target, true);
        break;
    }
    case ViewCommandKind::RemoveLayer:
        if (layer == window->modelLayer())
            return refuse(tr("The project's editable model layer cannot be removed."));
        if (const auto *mesh = qobject_cast<SWMM2DMeshLayer *>(layer); mesh && mesh->isActiveMesh())
            return refuse(tr("The active authoring mesh cannot be removed through this command."));
        canvas->takeLayer(canvas->layers().indexOf(layer), true);
        break;
    case ViewCommandKind::SetLayerFilter: {
        auto *vector = qobject_cast<GISVectorLayer *>(layer);
        if (!vector)
            return refuse(tr("Attribute filters require an attached vector layer."));
        if (!vector->setValidatedFilterExpression(command.text, error))
            return false;
        break;
    }
    case ViewCommandKind::SelectLayerFeatures: {
        auto *vector = qobject_cast<GISVectorLayer *>(layer);
        if (!vector || !vector->ogrLayer())
            return refuse(tr("Feature selection requires an open attached vector source."));
        if (command.featureIds.size() > 5000 ||
            std::any_of(command.featureIds.cbegin(), command.featureIds.cend(),
                        [](qint64 id) { return id < 0; }))
            return refuse(tr("Select at most 5000 nonnegative feature identities per request."));
        QSet<long long> selected;
        for (const auto fid : command.featureIds)
            selected.insert(fid);
        if (vector->selectedFeatureIds() == selected)
            return true;
        vector->setSelectedFeatureIds(selected);
        break;
    }
    default:
        return refuse(tr("This command is not a supported layer action."));
    }
    if (!owner || !window || window->isClosing())
        return refuse(QStringLiteral("The project closed while applying the layer command."));
    return true;
}
}
