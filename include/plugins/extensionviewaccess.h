/**
 * @file extensionviewaccess.h
 * @brief Canonical per-window native model, layer and result bridges for SDK view services.
 * @details These private controllers are owned by the native window, independently
 * of plugin count. Public queries copy registry values and never create trackers.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include "sdk/IViewEvents.hpp"
#include "sdk/IViewControls.hpp"
#include <QObject>
#include <QPointer>
#include <memory>

class SWMMVisProjectWindow;
class OpenSWMMVisLayer;

namespace openswmmvis
{
class ExtensionStateRegistry;
/** @brief Native metadata change callback, consumed by the canonical window coordinator. */
using ViewEventSink = std::function<void(OpenSWMM::Sdk::ViewEvent)>;

/** @brief Per-session layer membership identities, semantic watchers and validated commands. */
class ExtensionLayerBridge final : public QObject
{
public:
    /** @brief Attach watchers to the current canvas and future membership changes.
     * @param[in] window Native owner and canvas source.
     * @param[in] changed Copied metadata-change sink; no callback is required during construction. */
    ExtensionLayerBridge(SWMMVisProjectWindow *window, ViewEventSink changed);
    /** @brief Disconnect per-membership watchers before releasing private state. */
    ~ExtensionLayerBridge() override;
    /** @brief Read native layer metadata outside public getters.
     * @return Ordered copied current attachment metadata. */
    QList<OpenSWMM::Sdk::LayerViewState> snapshot() const;
    /** @brief Resolve an already tracked native pointer to its runtime attachment.
     * @param[in] layer Borrowed native pointer; never adopted.
     * @return Current attachment UUID, or null when detached. */
    QUuid identity(OpenSWMMVisLayer *layer) const;
    /** @brief Resolve a current runtime attachment for private controller use.
     * @param[in] id Runtime attachment identity.
     * @return Borrowed native layer, or null after detachment/destruction. */
    OpenSWMMVisLayer *resolve(const QUuid &id) const;
    /** @brief Dispatch a validated layer action using its native controller.
     * @param[in] command Request; session/availability validated by the coordinator.
     * @param[out] error Optional refusal detail, cleared on success.
     * @return True for accepted layer actions/no-ops, false for unsupported/invalid input. */
    bool execute(const OpenSWMM::Sdk::ViewCommand &command, QString *error);
    /** @brief Copy a sorted bounded page of current vector selection identities.
     * @param[in] id Runtime attachment UUID.
     * @param[in] offset Nonnegative sorted-result offset.
     * @param[in] limit Positive requested count, capped at 5000.
     * @return Copied FIDs or empty for unavailable/non-vector/invalid input. */
    QList<qint64> selectedFeatures(const QUuid &id, int offset, int limit) const;

private:
    struct Data;                 ///< Native membership records and managed watcher connections.
    std::unique_ptr<Data> mData; ///< Owning private implementation.
};

/** @brief Per-session attached result lifecycle and published cursor/variable metadata. */
class ExtensionResultsBridge final : public QObject
{
public:
    /** @brief Watch attached sources and the window's active source choices.
     * @param[in] window Native owner and results controller.
     * @param[in] identity Lookup-only current layer-attachment resolver.
     * @param[in] changed Copied result observation sink. */
    ExtensionResultsBridge(SWMMVisProjectWindow *window,
                           std::function<QUuid(OpenSWMMVisLayer *)> identity,
                           ViewEventSink changed);
    /** @brief Disconnect result/source/style watchers. */
    ~ExtensionResultsBridge() override;
    /** @brief Read copied native reader/source/channel state outside public getters.
     * @return Current attached results metadata. */
    QList<OpenSWMM::Sdk::ResultsViewState> snapshot() const;
    /** @brief Execute a results controller action against a current attachment.
     * @param[in] command Validated session request.
     * @param[in] layer Borrowed resolved native layer.
     * @param[out] error Optional refusal detail, cleared on success.
     * @return True for an accepted action/no-op; async seek need not yet be displayed. */
    bool execute(const OpenSWMM::Sdk::ViewCommand &command, OpenSWMMVisLayer *layer,
                 QString *error);

private:
    struct Data;                 ///< Managed attached-reader and semantic variable watcher state.
    std::unique_ptr<Data> mData; ///< Owning private implementation.
};

/** @brief Connect native model/selection/unit observations once per window.
 * @param[in] window Borrowed native owner.
 * @param[in] context Required connection lifetime guard, owned by the window.
 * @param[in] changed Copied bounded model observation sink. */
void installExtensionModelViewEvents(SWMMVisProjectWindow *window, QObject *context,
                                     ViewEventSink changed);
/** @brief Copy project display units and canonical selection count into view metadata.
 * @param[in] window Borrowed native source.
 * @param[in,out] snapshot Destination; layer/results metadata remains untouched. */
void populateExtensionModelViewState(SWMMVisProjectWindow *window,
                                     OpenSWMM::Sdk::ViewSnapshot &snapshot);

/** @brief Canonical native coordinator publishing into the application-owned registry. */
class ExtensionViewAccess final : public QObject
{
    Q_OBJECT
public:
    /** @brief Construct once after native session registration; owned by the window.
     * @param[in] window Required registered native owner. */
    explicit ExtensionViewAccess(SWMMVisProjectWindow *window);
    /** @brief Destroy native bridges before the window's QObject children. */
    ~ExtensionViewAccess() override;
    /** @brief Find an already installed coordinator without creating state.
     * @param[in] window Borrowed native window.
     * @return Borrowed coordinator or null during teardown. */
    static ExtensionViewAccess *find(SWMMVisProjectWindow *window);
    /** @brief Refresh final restored metadata before native readiness is committed. */
    void refresh();
    /** @brief Validate session generation/readiness then execute a typed view command.
     * @param[in] command Copied command using runtime attachment identity.
     * @param[out] error Optional refusal detail.
     * @return True for accepted/no-op, false for unavailable or invalid input. */
    bool execute(const OpenSWMM::Sdk::ViewCommand &command, QString *error);
    /** @brief Read one bounded page of current layer selection.
     * @param[in] id Runtime attachment identity.
     * @param[in] offset Sorted selection offset.
     * @param[in] limit Requested page count.
     * @return Copied sorted FIDs. */
    QList<qint64> selectedFeatures(const QUuid &id, int offset, int limit) const;
    /** @brief Resolve a tracked native layer to its current runtime attachment.
     * @param[in] layer Borrowed native pointer; never adopted.
     * @return Attachment UUID, or null when detached or during teardown. */
    QUuid identity(OpenSWMMVisLayer *layer) const;

private:
    QPointer<SWMMVisProjectWindow> mWindow;           ///< Guarded native owner.
    QPointer<ExtensionStateRegistry> mRegistry;       ///< Shared authoritative registry.
    std::unique_ptr<ExtensionLayerBridge> mLayers;    ///< Canonical layer tracker.
    std::unique_ptr<ExtensionResultsBridge> mResults; ///< Canonical results tracker.
    QUuid mOrigin; ///< Synchronous command effect token, restored after nested commands.
    bool mInitializing = true; ///< Suppress callbacks until both native trackers exist.
    /** @brief Copy native metadata at an explicit mutation/restoration boundary.
     * @return Owning view state; no public getter calls this method. */
    OpenSWMM::Sdk::ViewSnapshot capture() const;
    /** @brief Publish one native semantic invalidation with current copied metadata.
     * @param[in] event Copied reason/details; registry assigns session/revision/sequence. */
    void record(OpenSWMM::Sdk::ViewEvent event);
};
}
