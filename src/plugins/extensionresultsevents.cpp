/**
 * @file extensionresultsevents.cpp
 * @brief Canonical attached-results metadata, semantic observations and validated view commands.
 * @details Tracks native reader epochs and published cursors once per model window.
 * Snapshots copy metadata without geometry/data reads. Detached layers lose their runtime
 * attachment identity; source adoption, selection and displayed frames remain separate.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "plugins/extensionviewaccess.h"
#include "layers/swmmresultslayer.h"
#include "layers/swmm2dresultslayer.h"
#include "map/mapcanvas.h"
#include "render/isublayer.h"
#include "render/sublayerstyle.h"
#include "render/sublayers/resultscalarsublayer.h"
#include "swmmvisprojectwindow.h"
#include <QHash>
#include <QMetaProperty>
#include <QVariant>
#include <algorithm>
#include <utility>

using namespace OpenSWMM::Sdk;
namespace openswmmvis
{
namespace
{
/** @brief Stable native 1D variable key and corresponding enum for validated commands. */
struct NamedVariable {
    const char *key;          ///< Case-sensitive native attribute key, never a translated caption.
    SWMMResultVariable value; ///< Native layer-level rendering variable.
};
/** @brief Frozen native keys already used by the result layer's renderer configuration. */
constexpr NamedVariable variables1D[] = {
    {"NodeDepth", SWMMResultVariable::NodeDepth},
    {"NodeHead", SWMMResultVariable::NodeHead},
    {"NodeVolume", SWMMResultVariable::NodeVolume},
    {"NodeInflow", SWMMResultVariable::NodeInflow},
    {"NodeOverflow", SWMMResultVariable::NodeOverflow},
    {"NodeLateralInflow", SWMMResultVariable::NodeLateralInflow},
    {"LinkFlow", SWMMResultVariable::LinkFlow},
    {"LinkDepth", SWMMResultVariable::LinkDepth},
    {"LinkVelocity", SWMMResultVariable::LinkVelocity},
    {"LinkCapacity", SWMMResultVariable::LinkCapacity},
    {"SubcatchRunoff", SWMMResultVariable::SubcatchRunoff},
    {"SubcatchInfiltration", SWMMResultVariable::SubcatchInfiltration},
    {"SubcatchEvaporation", SWMMResultVariable::SubcatchEvaporation},
    {"SubcatchSnowDepth", SWMMResultVariable::SubcatchSnowDepth},
    {"SubcatchRainfall", SWMMResultVariable::SubcatchRainfall},
    {"SubcatchGwFlow", SWMMResultVariable::SubcatchGwFlow},
    {"SubcatchGwElevation", SWMMResultVariable::SubcatchGwElevation},
    {"SubcatchSoilMoisture", SWMMResultVariable::SubcatchSoilMoisture}};

/** @brief Describe only known native output units, without relabeling them as project units.
 * @param[in] variable Selected 1D variable.
 * @param[in] flowUnits File-native SWMM code, negative when no reader exists.
 * @return Native unit key or empty when not established here. */
QString units1D(SWMMResultVariable variable, int flowUnits)
{
    if (flowUnits < 0 || flowUnits > 5)
        return {};
    const bool metric = flowUnits >= 3;
    switch (variable) {
    case SWMMResultVariable::NodeDepth:
    case SWMMResultVariable::NodeHead:
    case SWMMResultVariable::LinkDepth:
    case SWMMResultVariable::SubcatchGwElevation:
        return metric ? QStringLiteral("m") : QStringLiteral("ft");
    case SWMMResultVariable::NodeVolume:
        return metric ? QStringLiteral("m³") : QStringLiteral("ft³");
    case SWMMResultVariable::LinkVelocity:
        return metric ? QStringLiteral("m/s") : QStringLiteral("ft/s");
    case SWMMResultVariable::NodeInflow:
    case SWMMResultVariable::NodeOverflow:
    case SWMMResultVariable::NodeLateralInflow:
    case SWMMResultVariable::LinkFlow:
    case SWMMResultVariable::SubcatchRunoff:
    case SWMMResultVariable::SubcatchGwFlow: {
        static const char *labels[] = {"CFS", "GPM", "MGD", "CMS", "LPS", "MLD"};
        return QString::fromLatin1(labels[flowUnits]);
    }
    case SWMMResultVariable::LinkCapacity:
    case SWMMResultVariable::SubcatchSoilMoisture:
        return QStringLiteral("1");
    default:
        return {};
    }
}

/** @brief Compare current semantic rendering channels without data-array access.
 * @param[in] a,b Copied metadata lists in native sublayer order.
 * @return True when all channel identities, keys and native units match. */
bool sameVariables(const QList<ResultVariableState> &a, const QList<ResultVariableState> &b)
{
    if (a.size() != b.size())
        return false;
    for (qsizetype i = 0; i < a.size(); ++i)
        if (a[i].channelId != b[i].channelId || a[i].variableKey != b[i].variableKey ||
            a[i].units != b[i].units)
            return false;
    return true;
}

/** @brief Compare adopted source metadata independently of cursor/channel selection.
 * @param[in] a,b Copied reader/source states.
 * @return True when availability, generation, path, diagnostic and available count match. */
bool sameSource(const ResultsViewState &a, const ResultsViewState &b)
{
    return a.sourceGeneration == b.sourceGeneration && a.path == b.path &&
           a.availability == b.availability && a.error == b.error &&
           a.periodCount == b.periodCount && a.live == b.live;
}

/** @brief Copy 2D style-owned channel selection without source discovery or frame reads.
 * @param[in] layer Live 2D results layer.
 * @return Current attribute-bearing rendering channels, including unresolved scalar keys. */
QList<ResultVariableState> variables2D(SWMM2DResultsLayer *layer)
{
    QList<ResultVariableState> result;
    for (auto *part : layer->sublayers()) {
        auto *style = part->style();
        if (!style || !style->property("attribute").isValid())
            continue;
        ResultVariableState row{part->id(), style->property("attribute").toString(), {}};
        if (auto *scalar = qobject_cast<OpenSWMM::Render::ResultScalarSublayer *>(part)) {
            const auto &descriptor = scalar->descriptor();
            if (descriptor.key() == row.variableKey && descriptor.unitsKnown)
                row.units = descriptor.units;
        } else if (row.variableKey == QStringLiteral("depth") ||
                   row.variableKey == QStringLiteral("head")) {
            row.units = QStringLiteral("m");
        } else if (row.variableKey == QStringLiteral("vmag") ||
                   row.variableKey == QStringLiteral("vx") ||
                   row.variableKey == QStringLiteral("vy")) {
            row.units = QStringLiteral("m/s");
        }
        result.append(row);
    }
    return result;
}
}

/** @brief Private attached-reader records; QObject connection handles have explicit ownership. */
struct ExtensionResultsBridge::Data {
    /** @brief One attached native results object and its last diff baseline. */
    struct Record {
        QPointer<OpenSWMMVisLayer> layer; ///< Borrowed layer, independent of canvas ownership.
        ResultsViewState state;           ///< Previous copied semantic state for no-op suppression.
        QList<QMetaObject::Connection> connections; ///< Managed reader/cursor watchers.
        QList<QMetaObject::Connection> styles;      ///< Managed attribute-channel watchers.
    };
    ExtensionResultsBridge *context;       ///< Owning connection lifetime, never adopted.
    QPointer<SWMMVisProjectWindow> window; ///< Native session/controller owner.
    std::function<QUuid(OpenSWMMVisLayer *)> identity; ///< Lookup-only layer attachment resolver.
    ViewEventSink changed; ///< Canonical coordinator sink, no plugin callbacks here.
    QHash<OpenSWMMVisLayer *, Record> records; ///< Attached results records by native pointer.

    /** @brief Read current native metadata without source discovery or frame data reads.
     * @param[in] base Attached result layer.
     * @return Copied state, with null runtime identity after detachment. */
    ResultsViewState read(OpenSWMMVisLayer *base) const
    {
        ResultsViewState result;
        if (!window || !base)
            return result;
        result.layerSessionId = identity(base);
        if (result.layerSessionId.isNull())
            return result;
        result.layerId = base->layerId();
        if (auto *layer = qobject_cast<SWMMResultsLayer *>(base)) {
            result.dimension = QStringLiteral("1d");
            result.sourceGeneration = layer->sourceGeneration();
            result.path = layer->adoptedSourcePath();
            result.error = layer->sourceError();
            result.availability = layer->sourceLoading()    ? ResultAvailability::Loading
                                  : layer->hasOpenResults() ? ResultAvailability::Ready
                                  : !result.error.isEmpty() ? ResultAvailability::Failed
                                                            : ResultAvailability::Empty;
            result.active = window->activeResultsLayer() == layer;
            result.live = layer->isLive();
            result.periodCount = layer->totalTimeSteps();
            if (layer->hasOpenResults() && result.periodCount > 0) {
                result.currentIndex = layer->currentTimeStep();
                result.currentDateTime = layer->currentDateTime();
            }
            for (const auto &value : variables1D)
                if (value.value == layer->variable()) {
                    result.variables.append({{},
                                             QString::fromLatin1(value.key),
                                             units1D(value.value, layer->flowUnits())});
                    break;
                }
        } else if (auto *layer = qobject_cast<SWMM2DResultsLayer *>(base)) {
            result.dimension = QStringLiteral("2d");
            result.sourceGeneration = layer->sourceRevision();
            const auto *source = layer->source();
            result.path = source ? source->sourcePath() : QString{};
            result.availability = layer->sourceLoading() ? ResultAvailability::Loading
                                  : source               ? ResultAvailability::Ready
                                                         : ResultAvailability::Empty;
            result.active = window->active2DResultsLayer() == layer;
            result.live = source && source->isLive();
            result.periodCount = source ? source->timeCount() : 0;
            result.currentIndex = layer->currentTimeIndex();
            if (source && result.currentIndex >= 0 && result.currentIndex < result.periodCount)
                result.currentDateTime = source->simTimeAt(int(result.currentIndex));
            result.variables = variables2D(layer);
        }
        return result;
    }

    /** @brief Publish a bounded invalidation using copied attachment/source identities.
     * @param[in] kind Semantic domain that changed.
     * @param[in] state Captured current native state. */
    void publish(ViewEventKind kind, const ResultsViewState &state)
    {
        if ((state.layerSessionId.isNull() && kind != ViewEventKind::ResultsActiveChanged) ||
            !window || window->isClosing())
            return;
        ViewEvent event;
        event.kind = kind;
        // Active choice affects the session, including removal of its old attachment.
        // A session-wide invalidation survives detach and coalesces old/new choices.
        if (kind != ViewEventKind::ResultsActiveChanged) {
            event.layerSessionId = state.layerSessionId;
            event.layerId = state.layerId;
            event.sourceGeneration = state.sourceGeneration;
        }
        if (kind == ViewEventKind::ResultsVariableChanged && state.variables.size() <= 64)
            for (const auto &variable : state.variables)
                event.keys.append(variable.channelId);
        changed(std::move(event));
    }

    /** @brief Diff semantic state; repeated count/time/style notifications are inert.
     * @param[in] base Current native layer identity, possibly detached before this signal. */
    void refresh(OpenSWMMVisLayer *base)
    {
        auto it = records.find(base);
        if (it == records.end() || !it->layer || !window)
            return;
        const auto current = read(base);
        const auto previous = it->state;
        it->state = current;
        const QPointer<ExtensionResultsBridge> guard(context);
        if (!sameSource(previous, current))
            publish(ViewEventKind::ResultsSourceChanged, current);
        if (!guard)
            return;
        if (previous.active != current.active)
            publish(ViewEventKind::ResultsActiveChanged, current);
        if (!guard)
            return;
        if (!sameVariables(previous.variables, current.variables))
            publish(ViewEventKind::ResultsVariableChanged, current);
        if (!guard)
            return;
        if (previous.currentIndex != current.currentIndex ||
            previous.currentDateTime != current.currentDateTime)
            publish(ViewEventKind::ResultsTimeChanged, current);
    }

    /** @brief Replace channel watchers after dynamic sublayer membership changes.
     * @param[in] layer Current 2D result layer. */
    void watchStyles(SWMM2DResultsLayer *layer)
    {
        auto it = records.find(layer);
        if (it == records.end())
            return;
        for (const auto &connection : std::as_const(it->styles))
            QObject::disconnect(connection);
        it->styles.clear();
        for (auto *part : layer->sublayers())
            if (auto *style = part->style(); style && style->property("attribute").isValid())
                it->styles.append(QObject::connect(style,
                                                   &OpenSWMM::Render::SublayerStyle::styleChanged,
                                                   context, [this, layer] { refresh(layer); }));
    }

    /** @brief Attach one results layer once, optionally seeding a future-membership observation.
     * @param[in] base Borrowed current canvas member.
     * @param[in] announce False while initial construction backfills state. */
    void attach(OpenSWMMVisLayer *base, bool announce)
    {
        if (!base || records.contains(base) ||
            (!qobject_cast<SWMMResultsLayer *>(base) && !qobject_cast<SWMM2DResultsLayer *>(base)))
            return;
        Record record;
        record.layer = base;
        record.state = read(base);
        records.insert(base, record);
        auto &connections = records[base].connections;
        const auto update = [this, base] { refresh(base); };
        connections.append(
            QObject::connect(base, &QObject::destroyed, context, [this, base] { detach(base); }));
        if (auto *layer = qobject_cast<SWMMResultsLayer *>(base)) {
            connections.append(
                QObject::connect(layer, &SWMMResultsLayer::sourceStateChanged, context, update));
            connections.append(
                QObject::connect(layer, &SWMMResultsLayer::totalTimeStepsChanged, context, update));
            connections.append(
                QObject::connect(layer, &SWMMResultsLayer::resultsFinalized, context, update));
            connections.append(QObject::connect(layer, &SWMMResultsLayer::currentTimeStepChanged,
                                                context, update));
            connections.append(QObject::connect(layer, &SWMMResultsLayer::currentDateTimeChanged,
                                                context, update));
            connections.append(
                QObject::connect(layer, &SWMMResultsLayer::variableChanged, context, update));
        } else if (auto *layer = qobject_cast<SWMM2DResultsLayer *>(base)) {
            connections.append(
                QObject::connect(layer, &SWMM2DResultsLayer::sourceChanged, context, update));
            connections.append(
                QObject::connect(layer, &SWMM2DResultsLayer::timeRangeChanged, context, update));
            connections.append(
                QObject::connect(layer, &SWMM2DResultsLayer::currentTimeChanged, context, update));
            connections.append(QObject::connect(layer, &SWMM2DResultsLayer::currentDateTimeChanged,
                                                context, update));
            connections.append(QObject::connect(layer, &SWMM2DResultsLayer::resultSublayersChanged,
                                                context, [this, layer] {
                                                    watchStyles(layer);
                                                    refresh(layer);
                                                }));
            watchStyles(layer);
        }
        if (announce) {
            const auto current = read(base);
            const QPointer<ExtensionResultsBridge> guard(context);
            publish(ViewEventKind::ResultsSourceChanged, current);
            if (guard && current.active)
                publish(ViewEventKind::ResultsActiveChanged, current);
        }
    }

    /** @brief Disconnect every membership watcher before removing its diff baseline.
     * @param[in] base Borrowed pointer identity; never dereferenced here. */
    void detach(OpenSWMMVisLayer *base)
    {
        const auto record = records.take(base);
        for (const auto &connection : record.connections)
            QObject::disconnect(connection);
        for (const auto &connection : record.styles)
            QObject::disconnect(connection);
        if (record.state.active)
            publish(ViewEventKind::ResultsActiveChanged, {});
    }
};

ExtensionResultsBridge::ExtensionResultsBridge(SWMMVisProjectWindow *window,
                                               std::function<QUuid(OpenSWMMVisLayer *)> identity,
                                               ViewEventSink changed)
    : QObject(window),
      mData(std::make_unique<Data>(Data{this, window, std::move(identity), std::move(changed), {}}))
{
    auto *canvas = window->canvas();
    for (auto *layer : canvas->layers())
        mData->attach(layer, false);
    connect(canvas, &MapCanvas::layerAdded, this,
            [this](OpenSWMMVisLayer *layer) { mData->attach(layer, true); });
    connect(canvas, &MapCanvas::layerRemoved, this,
            [this](OpenSWMMVisLayer *layer) { mData->detach(layer); });
    const auto activeChanged = [this] {
        const auto layers = mData->records.keys();
        for (auto *layer : layers)
            mData->refresh(layer);
    };
    connect(window, &SWMMVisProjectWindow::activeResultsLayerChanged, this, activeChanged);
    connect(window, &SWMMVisProjectWindow::active2DResultsLayerChanged, this, activeChanged);
}

ExtensionResultsBridge::~ExtensionResultsBridge()
{
    const auto layers = mData->records.keys();
    for (auto *layer : layers)
        mData->detach(layer);
}

QList<ResultsViewState> ExtensionResultsBridge::snapshot() const
{
    QList<ResultsViewState> result;
    if (!mData->window || !mData->window->canvas())
        return result;
    for (auto *layer : mData->window->canvas()->layers())
        if (const auto it = mData->records.constFind(layer);
            it != mData->records.cend() && it->layer) {
            const auto state = mData->read(layer);
            if (!state.layerSessionId.isNull())
                result.append(state);
        }
    return result;
}

bool ExtensionResultsBridge::execute(const ViewCommand &command, OpenSWMMVisLayer *base,
                                     QString *error)
{
    const auto refuse = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (error)
        error->clear();
    if (!mData->window || mData->window->isClosing() || !mData->records.contains(base) ||
        mData->identity(base) != command.layerSessionId)
        return refuse(QStringLiteral("The results attachment is no longer available."));
    auto *one = qobject_cast<SWMMResultsLayer *>(base);
    auto *two = qobject_cast<SWMM2DResultsLayer *>(base);
    if (command.kind == ViewCommandKind::ActivateResults) {
        if (one)
            mData->window->setActiveResultsLayer(one);
        else if (two)
            mData->window->setActive2DResultsLayer(two);
        else
            return refuse(QStringLiteral("This layer is not a results source."));
        return true;
    }
    const auto state = mData->read(base);
    if (state.availability != ResultAvailability::Ready)
        return refuse(QStringLiteral("Wait for an available results source."));
    if (command.kind == ViewCommandKind::SetResultsTime) {
        if (command.index < 0 || command.index >= state.periodCount)
            return refuse(QStringLiteral("The results frame is outside the available range."));
        if (one) {
            if (one->currentTimeStep() != command.index)
                one->setCurrentTimeStep(int(command.index));
        } else if (two) {
            // The native method also withdraws a pending newer seek when the caller
            // deliberately selects the currently displayed frame again.
            two->setCurrentTimeIndex(int(command.index));
        }
        return true;
    }
    if (command.kind != ViewCommandKind::SetResultsVariable)
        return refuse(QStringLiteral("This command does not control result views."));
    if (command.text.isEmpty() || command.text.size() > 512 || command.channelId.size() > 512)
        return refuse(QStringLiteral("Choose a valid bounded result variable key and channel."));
    if (one) {
        if (!command.channelId.isEmpty())
            return refuse(QStringLiteral("The 1D variable uses an empty channel identity."));
        for (const auto &variable : variables1D)
            if (command.text == QLatin1String(variable.key)) {
                if (one->variable() != variable.value)
                    one->setVariable(variable.value);
                return true;
            }
        return refuse(QStringLiteral("The requested 1D result variable is unavailable."));
    }
    for (auto *part : two->sublayers()) {
        if (part->id() != command.channelId)
            continue;
        if (auto *scalar = qobject_cast<OpenSWMM::Render::ResultScalarSublayer *>(part)) {
            bool available = false;
            for (const auto &variable : two->resultVariables())
                available |= variable.key() == command.text;
            if (!available)
                return refuse(QStringLiteral("The semantic result variable is unavailable."));
            if (scalar->variableKey() != command.text)
                scalar->setVariableKey(command.text);
            return true;
        }
        auto *style = part->style();
        const int propertyIndex = style ? style->metaObject()->indexOfProperty("attribute") : -1;
        if (propertyIndex < 0 || !style->metaObject()->property(propertyIndex).isWritable())
            return refuse(QStringLiteral("This result channel has no selectable variable."));
        const QStringList available{QStringLiteral("depth"), QStringLiteral("head"),
                                    QStringLiteral("vmag"), QStringLiteral("vx"),
                                    QStringLiteral("vy")};
        if (!available.contains(command.text))
            return refuse(
                QStringLiteral("The requested hydraulic result variable is unavailable."));
        if (style->property("attribute").toString() != command.text)
            style->setProperty("attribute", command.text);
        return true;
    }
    return refuse(QStringLiteral("The result rendering channel is no longer attached."));
}
}
