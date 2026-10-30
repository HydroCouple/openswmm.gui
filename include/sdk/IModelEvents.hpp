/**
 * @file IModelEvents.hpp
 * @brief Versioned, typed observations of GUI extension host state.
 * @details This optional interface is discovered through ExtensionHost::lifetime()
 * with qobject_cast. Values describe the existing SDK event coverage, including
 * its synchronous delivery and lifecycle limitations. They do not imply model
 * readiness, committed transactions, state revisions or replay. The interface,
 * enumerator values and payload layouts are frozen for this IID.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once

#include <QList>
#include <QMetaObject>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QUuid>
#include <functional>
#include <utility>
#include <variant>

/** @brief Frozen Qt interface identity for the first typed host event service. */
#define OPENSWMM_MODEL_EVENTS_IID "io.opensimulator.openswmm.IEventSource/1.0"

namespace OpenSWMM::Sdk
{
/** @brief Discoverable interface IID, equivalent to OPENSWMM_MODEL_EVENTS_IID. */
inline constexpr auto MODEL_EVENTS_IID = OPENSWMM_MODEL_EVENTS_IID;

/** @brief Existing host observations with explicit, stable numeric identities.
 * @note Discovery and closing describe facade discovery and the beginning of
 * closure, respectively. They are not loaded/ready or post-destruction events. */
enum class EventKind : quint32 {
    InstanceDiscovered =
        1,               ///< First discovery of a model facade, possibly before loading completes.
    InstanceClosing = 2, ///< Model closure has begun; treat the model as unavailable.
    InstanceActivated =
        3, ///< MDI activation resolved to a model, or a null UUID for another document.
    SelectionChanged = 4, ///< Shared authored-model selection changed.
    ModelEdited = 5,      ///< The native modelEdited observation was emitted.
    AttributesChanged =
        6, ///< The native attributeChanged observation identified an object or broad invalidation.
    ViewportChanged = 7, ///< Visible canvas extent or CRS changed, including resizing.
    LayersChanged =
        8, ///< Canvas layer membership changed; the affected layer identity is unspecified.
    BoundaryChanged = 9, ///< A watched vector boundary source changed in place.
    ActiveResultsChanged =
        10,               ///< Active 1D/2D results source changed, without a playback guarantee.
    RunsChanged = 11,     ///< Simulation Status metadata or row membership changed.
    SettingsChanged = 12, ///< The owning extension wrote or removed one settings key/group.
    ThemeChanged = 13,    ///< The main-window palette changed.
    JobProgress = 14,     ///< The owning extension reported background-job progress.
    JobFinished = 15      ///< The owning extension's background worker completed.
};

/** @brief Model discovery, closing, activation, selection, edit or attribute observation. */
struct ModelEvent {
    QUuid instanceId; ///< Persistent model UUID; null activation means no active model.
    QString
        objectName; ///< Native object identifier for AttributesChanged, possibly empty for broad invalidation; never a field key.
};

/** @brief Visible-area, canvas membership or watched vector-boundary observation. */
struct SpatialEvent {
    QUuid instanceId; ///< Persistent model UUID containing the canvas/source.
    QString layerId; ///< Watched source for BoundaryChanged; empty for viewport/membership changes.
};

/** @brief Active result-source observation without result-reader ownership. */
struct ResultsEvent {
    QUuid instanceId; ///< Persistent model UUID whose active 1D/2D source changed.
};

/** @brief Simulation Status invalidation; query the run registry for current rows.
 * @note No individual run or attempt identity is claimed by this version. */
struct RunsEvent {
};

/** @brief One owning extension's settings invalidation. */
struct SettingsEvent {
    QString key; ///< Extension-local key or removed group, without another plugin's prefix.
};

/** @brief Palette invalidation; query current host theme tokens. */
struct ThemeEvent {
};

/** @brief Copied progress from one of the owning extension's background jobs. */
struct JobProgressEvent {
    QString title;        ///< Human-readable job title, not a unique operation identity.
    double progress = 0.; ///< Normalized completion fraction in the range [0,1].
    QString text;         ///< Optional short progress-stage description.
};

/** @brief Copied completion status from one of the owning extension's background jobs. */
struct JobFinishedEvent {
    QString title; ///< Human-readable job title, not a unique operation identity.
    bool ok =
        false; ///< False for failure or cancellation; these outcomes are not distinguished here.
    QString error; ///< Failure detail; cancellation may produce an empty string.
};

/** @brief Closed, owning payload alternatives for the frozen IModelEvents event service. */
using EventPayload = std::variant<ModelEvent, SpatialEvent, ResultsEvent, RunsEvent, SettingsEvent,
                                  ThemeEvent, JobProgressEvent, JobFinishedEvent>;

/** @brief One typed observation published by an extension's host facade.
 * @details The host supplies the payload alternative corresponding to kind.
 * Sequence numbers identify publications within that host facade only. A
 * synchronous observer may reenter the application, so nested callbacks can
 * receive a later publication before an earlier emission reaches every listener.
 * A sequence is not a model revision, snapshot boundary or global ordering token.
 * Copy this value to retain it beyond the callback; it contains no raw objects. */
struct Event {
    EventKind kind = EventKind::
        InstanceDiscovered; ///< Case-sensitive legacy observations have typed identities here.
    quint64 sequence =
        0; ///< Host-local publication identity; zero is an unpublished default value.
    EventPayload payload; ///< Owning domain value; its alternative is fixed by kind.

    /** @brief Resolve the model identity carried by the typed domain value.
     * @return Model UUID for model/spatial/results payloads; null for global
     * events and no-active-model activation. This is not a session generation. */
    [[nodiscard]] QUuid instanceId() const
    {
        if (const auto *value = std::get_if<ModelEvent>(&payload))
            return value->instanceId;
        if (const auto *value = std::get_if<SpatialEvent>(&payload))
            return value->instanceId;
        if (const auto *value = std::get_if<ResultsEvent>(&payload))
            return value->instanceId;
        return {};
    }
};

/** @brief Immutable-at-registration selection of event kinds and a model identity.
 * @details An empty kind list accepts every known kind. A null instanceId accepts
 * every model and global observation. A specific UUID excludes global events
 * and null-UUID activation events. Layer membership observations do not carry
 * a layer ID, so IModelEvents deliberately has no layer-identity filter. */
struct EventFilter {
    QList<EventKind> kinds; ///< Accepted kinds, or empty for every IModelEvents kind.
    QUuid instanceId;       ///< Accepted model UUID, or null for every model/global event.

    /** @brief Test a typed value against this filter without consulting host state.
     * @param[in] event Owning event value to inspect; never modified.
     * @return True when both the kind and optional model identity match. */
    [[nodiscard]] bool accepts(const Event &event) const
    {
        return (kinds.isEmpty() || kinds.contains(event.kind)) &&
               (instanceId.isNull() || instanceId == event.instanceId());
    }
};

/** @brief Optional, frozen interface for receiver-scoped typed GUI observations.
 * @details Discover with qobject_cast<IModelEvents *>(host->lifetime()).
 * A null result means the capability is unavailable. The host owns this
 * interface; never delete, reparent or retain it after its QObject lifetime.
 * Commands continue through the public model/project/UI/application services. */
class IModelEvents
{
public:
    /** @brief Destroy an implementation; plugins borrow the host-owned service. */
    virtual ~IModelEvents() = default;

    /** @brief Subscribe to filtered typed observations on the GUI thread.
     * @param[in] context Required GUI-thread QObject receiver lifetime; destruction disconnects.
     * The receiver must remain on the GUI thread while receiving observations.
     * @param[in] filter Copied selection of kinds and optional persistent model UUID.
     * @param[in] callback Synchronous GUI-thread callback borrowing the Event only for this call.
     * @return Disconnectable Qt connection, or an invalid connection for a null,
     * wrong-thread or stopping receiver/host, off-thread call or empty callback.
     * @note No initial snapshot, replay, readiness or completed-transaction guarantee
     * is provided. Query current state after subscribing and recheck model isOpen().
     * Delivery is suppressed while the receiver is off the GUI thread and after
     * shutdown begins. Observer exceptions are isolated at the host subscription
     * boundary; native faults remain ordinary in-process native faults.
     * Keep handlers short, avoid nested event loops, and defer state-changing
     * commands through IUiHost::postToMainThread(). */
    virtual QMetaObject::Connection subscribe(QObject *context, const EventFilter &filter,
                                              std::function<void(const Event &)> callback) = 0;
};

/** @brief Move-only owner that disconnects a Qt subscription when its scope ends.
 * @details This helper owns the connection handle, not the source or receiver.
 * Source/receiver destruction still follows ordinary Qt lifetime rules. An
 * ordinary queued Qt signal already posted before disconnect may still arrive;
 * this helper does not replace a dispatcher generation/cancellation guard. */
class ScopedEventSubscription
{
public:
    /** @brief Construct an empty subscription owner. */
    ScopedEventSubscription() = default;
    /** @brief Take responsibility for disconnecting a connection.
     * @param[in] connection Qt handle returned by a subscription or connect(). */
    explicit ScopedEventSubscription(QMetaObject::Connection connection)
        : mConnection(std::move(connection))
    {
    }
    /** @brief Disconnect the currently owned connection. */
    ~ScopedEventSubscription() { reset(); }
    /** @brief Forbid duplicated ownership of a disconnect action.
     * @param[in] other Source owner; copy construction is unavailable. */
    ScopedEventSubscription(const ScopedEventSubscription &other) = delete;
    /** @brief Forbid duplicated ownership of a disconnect action.
     * @param[in] other Source owner; copy assignment is unavailable.
     * @return Assignment is unavailable for copies. */
    ScopedEventSubscription &operator=(const ScopedEventSubscription &other) = delete;
    /** @brief Transfer connection ownership, leaving the source empty.
     * @param[in,out] other Owner whose connection is transferred. */
    ScopedEventSubscription(ScopedEventSubscription &&other) noexcept
        : mConnection(std::exchange(other.mConnection, {}))
    {
    }
    /** @brief Disconnect this owner and transfer another connection into it.
     * @param[in,out] other Owner whose connection is transferred.
     * @return This subscription owner. */
    ScopedEventSubscription &operator=(ScopedEventSubscription &&other) noexcept
    {
        if (this != &other)
            reset(std::exchange(other.mConnection, {}));
        return *this;
    }
    /** @brief Disconnect the current handle and optionally take another one.
     * @param[in] connection Replacement connection, or empty to unsubscribe. */
    void reset(QMetaObject::Connection connection = {})
    {
        QObject::disconnect(mConnection);
        mConnection = std::move(connection);
    }
    /** @brief Inspect whether this owner holds a valid Qt connection handle.
     * @return True according to QMetaObject::Connection's validity test;
     * this is not a promise that future matching events will occur. */
    [[nodiscard]] bool isConnected() const { return static_cast<bool>(mConnection); }

private:
    QMetaObject::Connection mConnection; ///< Owned disconnect action, empty after reset/move.
};
}

Q_DECLARE_INTERFACE(OpenSWMM::Sdk::IModelEvents, OPENSWMM_MODEL_EVENTS_IID)
Q_DECLARE_METATYPE(OpenSWMM::Sdk::EventKind)
Q_DECLARE_METATYPE(OpenSWMM::Sdk::ModelEvent)
Q_DECLARE_METATYPE(OpenSWMM::Sdk::SpatialEvent)
Q_DECLARE_METATYPE(OpenSWMM::Sdk::ResultsEvent)
Q_DECLARE_METATYPE(OpenSWMM::Sdk::RunsEvent)
Q_DECLARE_METATYPE(OpenSWMM::Sdk::SettingsEvent)
Q_DECLARE_METATYPE(OpenSWMM::Sdk::ThemeEvent)
Q_DECLARE_METATYPE(OpenSWMM::Sdk::JobProgressEvent)
Q_DECLARE_METATYPE(OpenSWMM::Sdk::JobFinishedEvent)
Q_DECLARE_METATYPE(OpenSWMM::Sdk::Event)
