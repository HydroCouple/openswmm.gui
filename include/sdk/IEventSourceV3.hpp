/**
 * @file IEventSourceV3.hpp
 * @brief Versioned model, layer and attached-results observations and copied view metadata.
 * @details Optional SDK 1.4 capability discovered on ExtensionHost::lifetime().
 * All calls use the GUI thread. Values own metadata, never native objects or data
 * arrays. V1/V2 interfaces and layouts are unchanged; this contract has its own IID.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include "IEventSourceV2.hpp"
#include <QDateTime>
#include <QStringList>

/** @brief Frozen discovery identity for SDK 1.4 model/layer/results observations. */
#define OPENSWMM_EVENT_SOURCE_V3_IID "io.opensimulator.openswmm.IEventSource/3.0"

namespace OpenSWMM::Sdk
{
/** @brief Stable invalidation reasons, without geometry, selection or result arrays. */
enum class ViewEventKind : quint32 {
    ModelGeometryChanged = 1,    ///< Geometry/topology may have changed; rebuilds are broad.
    ModelOptionsChanged = 2,     ///< Accepted native option keys, or empty for broad invalidation.
    ObjectRenamed = 3,           ///< Exact native namespace and old/new name where available.
    AttributeValuesChanged = 4,  ///< Native/computed values need to be queried again.
    AttributeSchemaChanged = 5,  ///< Computed/native field descriptors need to be queried again.
    ModelSelectionChanged = 6,   ///< Canonical shared authored-model selection changed.
    LayerAdded = 7,              ///< A new runtime attachment is present in the layer snapshot.
    LayerRemoved = 8,            ///< Historical detach; the attachment no longer resolves.
    LayerMetadataChanged = 9,    ///< Name, visibility, opacity or layer discriminator changed.
    LayerOrderChanged = 10,      ///< Top-level or sublayer ordering changed.
    LayerStyleChanged = 11,      ///< Authored renderer/style configuration changed.
    LayerSchemaChanged = 12,     ///< Feature field descriptors changed.
    LayerFeaturesChanged = 13,   ///< Feature content/geometry changed; query the source again.
    LayerSelectionChanged = 14,  ///< A vector layer's selected feature identities changed.
    LayerFilterChanged = 15,     ///< The applied vector attribute filter changed.
    LayerCrsChanged = 16,        ///< Layer coordinate-reference metadata changed.
    LayerSourceChanged = 17,     ///< Layer source metadata/loading state changed.
    ResultsSourceChanged = 18,   ///< Attached reader/source epoch, availability or count changed.
    ResultsActiveChanged = 19,   ///< Session-wide active 1D/2D choice changed, including detachment.
    ResultsVariableChanged = 20, ///< Selected result variable/channel changed.
    ResultsTimeChanged = 21, ///< Published/displayed cursor changed, not merely a frame request.
    ProjectUnitsChanged = 22 ///< Project display units changed; file-native units are separate.
};

/** @brief Copied metadata for one unambiguous current canvas attachment. */
struct LayerViewState {
    QUuid layerSessionId; ///< Runtime attachment UUID, replaced after detach/reattach.
    QString layerId; ///< Native persisted ID; it may change on restore or repeat across layers.
    QString title;   ///< Current display name.
    QString type;    ///< Native type caption; use capability flags for commands.
    QString crsWkt;  ///< Layer CRS, possibly empty.
    bool visible = false;      ///< Local layer visibility.
    double opacity = 1.;       ///< Local opacity in [0,1].
    bool vector = false;       ///< The attachment supports feature selection and filtering.
    QString filter;            ///< Current applied vector attribute filter, empty without one.
    qint64 featureCount = -1;  ///< Available source count, or -1 for non-vector/unknown.
    qint64 selectionCount = 0; ///< Selected FID count; identities use a paged query.
};

/** @brief Metadata availability of the currently attached results source. */
enum class ResultAvailability : quint32 {
    Empty = 0,   ///< No adopted reader/source.
    Loading = 1, ///< A new attached source is being prepared.
    Ready = 2,   ///< Reader/source metadata is adopted; zero published periods is permitted.
    Failed = 3   ///< Latest preparation failed and no usable reader/source remains.
};

/** @brief One current rendering channel's selected variable and native unit metadata. */
struct ResultVariableState {
    QString channelId;   ///< Empty for the 1D variable; semantic sublayer ID for 2D channels.
    QString variableKey; ///< Stable native variable key, not a translated label.
    QString units; ///< Source/descriptor units, possibly empty; never inferred from project units.
};

/** @brief Copied state of an attached results layer, separate from run-attempt telemetry. */
struct ResultsViewState {
    QUuid layerSessionId;         ///< Owning canvas attachment identity, matching LayerViewState.
    QString layerId;              ///< Native persisted layer ID.
    quint64 sourceGeneration = 0; ///< Monotonic replacement/open/close epoch within this layer.
    QString dimension;            ///< "1d" or "2d".
    QString path;                 ///< Adopted reader/source path when known.
    ResultAvailability availability = ResultAvailability::Empty; ///< Current source state.
    QString error;       ///< Latest preparation error; a failure can leave an older reader usable.
    bool active = false; ///< This session's active source in its dimension.
    bool live = false;   ///< Source is still receiving periods; false after native finalization.
    qint64 periodCount = 0;               ///< Published source period count.
    qint64 currentIndex = -1;             ///< Displayed frame index, or -1 with no adopted frame.
    QDateTime currentDateTime;            ///< Displayed frame time, invalid when unavailable.
    QList<ResultVariableState> variables; ///< Current semantic rendering channels.
};

/** @brief Pure copied current metadata at the shared registry watermark.
 * @details Subscribe before the initial query and retain that initial cutoff.
 * This is not geometry/results data, a historical event log or an engine lease. */
struct ViewSnapshot {
    quint64 sequence = 0;            ///< Same application registry watermark used by V2 snapshots.
    ModelSessionState session;       ///< Null sessionId means unknown/retired session.
    QList<LayerViewState> layers;    ///< Current attachments in native bottom-to-top order.
    QList<ResultsViewState> results; ///< Attached result sources, including unavailable sources.
    QString projectUnits;       ///< Project display-unit key, separate from native result units.
    qint64 selectedObjects = 0; ///< Count in canonical cross-view selection, including nonspatial refs.
};

/** @brief Owning deferred observation identifying state to query again.
 * @details Ordinary invalidations can coalesce by reason/attachment; exact renames
 * and detach records preserve their captured identities. Empty object/key details
 * mean broad invalidation, including native-edit commit and snapshot undo.
 * Recheck session/load generation before acting. A detach record is history and
 * does not grant access to a removed layer. */
struct ViewEvent {
    ViewEventKind kind = ViewEventKind::ModelGeometryChanged; ///< Invalidation reason.
    quint64 sequence = 0;      ///< Shared V2/V3 registry publication sequence.
    ModelSessionState session; ///< Copied open-lifetime/revision metadata.
    QUuid layerSessionId;      ///< Affected attachment, null for session-wide changes.
    QString layerId; ///< Copied native ID, which alone is not an unambiguous command target.
    quint64 sourceGeneration = 0; ///< Affected result epoch, zero for other domains.
    QString objectNamespace;      ///< "node", "link", "subcatchment" or "gage", when known.
    QString previousName;         ///< Exact prior name only for a precise native rename.
    QString objectName; ///< Exact current object name when supplied by the native operation.
    QStringList keys;   ///< Bounded option keys or channel IDs; empty means broad invalidation.
    QUuid
        origin; ///< Caller token for synchronous view-command effects; null for native/async work.
};

/** @brief Registration-time reason, session and runtime-attachment selection. */
struct ViewEventFilter {
    QList<ViewEventKind> kinds; ///< Empty accepts every V3 reason.
    QUuid sessionId;            ///< Null accepts all model sessions.
    QUuid layerSessionId;       ///< Null accepts all attachments and session-wide changes.
    /** @brief Match a copied observation without consulting native state.
     * @param[in] event Copied observation.
     * @return True when all specified selectors match. */
    bool accepts(const ViewEvent &event) const
    {
        return (kinds.isEmpty() || kinds.contains(event.kind)) &&
               (sessionId.isNull() || sessionId == event.session.sessionId) &&
               (layerSessionId.isNull() || layerSessionId == event.layerSessionId);
    }
};

/** @brief Frozen optional GUI-thread view metadata and deferred domain observation service. */
class IEventSourceV3
{
public:
    /** @brief Destroy an implementation; consumers borrow the host-owned capability. */
    virtual ~IEventSourceV3() = default;
    /** @brief Copy one session's current tracked view state, without side effects.
     * @param[in] sessionId Per-open identity obtained from IEventSourceV2.
     * @return Metadata, or a null-session snapshot when unavailable/off-thread/stopping. */
    virtual ViewSnapshot viewSnapshot(const QUuid &sessionId) const = 0;
    /** @brief Observe future queued invalidations on the GUI thread.
     * @param[in] context Required GUI-thread lifetime guard.
     * @param[in] filter Copied reason/session/attachment selection.
     * @param[in] callback Short managed observer; exceptions are isolated.
     * @return Disconnectable connection, invalid for an unavailable registration.
     * @note No replay. Subscribe then query the initial snapshot; a current-state
     * view can discard observations at/below its initial watermark. Close/reload,
     * receiver destruction, disconnect and host stop suppress obsolete live work. */
    virtual QMetaObject::Connection
    observeViews(QObject *context, const ViewEventFilter &filter,
                 std::function<void(const ViewEvent &)> callback) = 0;
};
}
Q_DECLARE_INTERFACE(OpenSWMM::Sdk::IEventSourceV3, OPENSWMM_EVENT_SOURCE_V3_IID)
Q_DECLARE_METATYPE(OpenSWMM::Sdk::ViewEvent)
Q_DECLARE_METATYPE(OpenSWMM::Sdk::ViewSnapshot)
Q_DECLARE_METATYPE(OpenSWMM::Sdk::ViewEventKind)
