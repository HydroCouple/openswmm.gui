/**
 * @file IViewControls.hpp
 * @brief Versioned validated canvas and attached-results commands for extensions.
 * @details Discover on ExtensionHost::lifetime(). GUI-thread commands target a
 * Ready session and its runtime attachment UUID. They use native controllers,
 * return explicit refusal details and suppress no-op writes. Metadata queries
 * remain separate on IViewEvents. Interface layout is frozen for this IID.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include <QList>
#include <QObject>
#include <QString>
#include <QUuid>

/** @brief Frozen discovery identity for validated SDK view commands. */
#define OPENSWMM_VIEW_CONTROLS_IID "io.opensimulator.openswmm.IViewControls/1.0"

namespace OpenSWMM::Sdk
{
/** @brief Supported, typed view commands without exposing private Qt slots. */
enum class ViewCommandKind : quint32 {
    RenameLayer = 1,     ///< text is the new nonempty title.
    SetLayerVisible = 2, ///< enabled is the desired local visibility.
    SetLayerOpacity = 3, ///< number is finite and in [0,1].
    MoveLayer = 4, ///< index is the absolute bottom-to-top canvas index, within its category group.
    RemoveLayer = 5,         ///< Detach through the existing native undo route.
    SetLayerFilter = 6,      ///< text is a validated vector attribute filter, empty clears it.
    SelectLayerFeatures = 7, ///< featureIds replaces the vector selection; nonnegative IDs only.
    ActivateResults = 8,     ///< Select the attached source in its 1D/2D dimension.
    SetResultsTime =
        9, ///< index is a valid published period ordinal; async display can finish later.
    SetResultsVariable = 10 ///< text is a stable variable key; channelId selects the 2D sublayer.
};

/** @brief Owning bounded request for one native view-controller action.
 * @details Only fields documented by kind are interpreted. Selection requests
 * are capped at 5000 IDs. A null origin is permitted; a supplied token tags
 * synchronous effects. Asynchronous source/frame publications have null origin.
 * General layer property/filter/selection commands follow their native direct
 * setter behavior. Move/remove use the native undo stack; no universal undo or
 * durable source-file transaction is implied. */
struct ViewCommand {
    ViewCommandKind kind = ViewCommandKind::SetLayerVisible; ///< Requested operation.
    QUuid sessionId;            ///< Ready open-lifetime identity; never a persistent model UUID.
    quint64 loadGeneration = 0; ///< Exact accepted load generation from the adopted view snapshot.
    QUuid layerSessionId;       ///< Current runtime attachment identity from ViewSnapshot.
    QUuid origin;               ///< Optional caller correlation/feedback-suppression token.
    QString text;               ///< New title, filter or variable key according to kind.
    QString channelId;          ///< 2D semantic result-channel ID; empty for 1D.
    bool enabled = false;       ///< Desired visibility for SetLayerVisible.
    double number = 1.;         ///< Desired opacity for SetLayerOpacity.
    qint64 index = 0;           ///< Layer-stack or result-period ordinal according to kind.
    QList<qint64> featureIds;   ///< Replacement vector selection, capped at 5000 IDs.
};

/** @brief Frozen optional validated controller entry point for view changes. */
class IViewControls
{
public:
    /** @brief Destroy an implementation; consumers borrow the host-owned service. */
    virtual ~IViewControls() = default;
    /** @brief Apply one validated action through the existing native route.
     * @param[in] command Copied request targeting a Ready session/current attachment.
     * @param[out] error Optional explicit refusal detail, cleared on success.
     * @return True for accepted actions and valid no-ops; false after close/reload,
     * invalid values, unsupported layer capabilities or native refusal.
     * @note For 2D seeking, acceptance does not promise that a requested frame is
     * displayed; observe ResultsTimeChanged and query the published cursor. */
    virtual bool executeViewCommand(const ViewCommand &command, QString *error = nullptr) = 0;
    /** @brief Page through selected vector FIDs without returning native objects.
     * @param[in] sessionId Ready per-open identity.
     * @param[in] layerSessionId Current vector attachment UUID.
     * @param[in] offset Nonnegative index into the sorted selection.
     * @param[in] limit Positive page size, capped at 5000.
     * @return Sorted copied IDs, or empty for invalid/unavailable input. */
    virtual QList<qint64> selectedLayerFeatures(const QUuid &sessionId, const QUuid &layerSessionId,
                                                int offset = 0, int limit = 500) const = 0;
};
}
Q_DECLARE_INTERFACE(OpenSWMM::Sdk::IViewControls, OPENSWMM_VIEW_CONTROLS_IID)
