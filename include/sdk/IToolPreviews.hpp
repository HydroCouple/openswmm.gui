/**
 * @file IToolPreviews.hpp
 * @brief Independent transient map previews for tools owned by one GUI extension.
 * @details Discover this optional SDK 1.10 capability on ExtensionHost::lifetime().
 * Every tool/session pair owns a separate preview. The legacy IProject preview
 * remains separate. All calls use the GUI thread and copy GeoJSON values.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QUuid>

/** @brief Frozen Qt discovery identity for independent tool previews. */
#define OPENSWMM_TOOL_PREVIEWS_IID "io.opensimulator.openswmm.IToolPreviews/1.0"

namespace OpenSWMM::Sdk
{
/** @brief Owner-scoped publication and lookup of temporary per-tool features. */
class IToolPreviews
{
public:
    /** @brief Destroy an implementation; consumers borrow the host capability. */
    virtual ~IToolPreviews() = default;
    /** @brief Replace one tool's transient preview in an exact Ready generation.
     * @param[in] sessionId Per-open model identity belonging to this main window.
     * @param[in] loadGeneration Exact accepted model generation.
     * @param[in] toolId Nonempty extension-local tool identity, at most 128 characters.
     * @param[in] collection GeoJSON and previewStyle hints supported by IProject::showPreview().
     * @param[out] error Optional refusal explanation, cleared on entry.
     * @return True after publication or an identical preview. Invalid input preserves
     * the previous display. The calling extension implicitly owns the preview.
     * @note Native temporary layers preserve visibility on replacement, respect manual
     * removal, and are excluded from project persistence and boundary choices. */
    virtual bool showToolPreview(const QUuid &sessionId, quint64 loadGeneration,
                                 const QString &toolId, const QJsonObject &collection,
                                 QString *error = nullptr) = 0;
    /** @brief Clear only this extension's named tool preview in a session.
     * @param[in] sessionId Exact per-open model identity.
     * @param[in] toolId Extension-local tool identity previously published.
     * @note Safe after closure. Other tools, legacy previews and imported layers survive. */
    virtual void clearToolPreview(const QUuid &sessionId, const QString &toolId) = 0;
    /** @brief Resolve one named tool's native preview attachment and source-order FIDs.
     * @param[in] sessionId Exact Ready session identity.
     * @param[in] toolId Extension-local tool identity.
     * @param[out] featureIds Replaced with FIDs in accepted collection feature order.
     * @return Current runtime layer attachment, or null after clear/removal/closure.
     * @note FIDs belong only to the returned attachment and are never persistent keys. */
    virtual QUuid toolPreviewLayer(const QUuid &sessionId, const QString &toolId,
                                   QList<qint64> *featureIds = nullptr) const = 0;
};
}
Q_DECLARE_INTERFACE(OpenSWMM::Sdk::IToolPreviews, OPENSWMM_TOOL_PREVIEWS_IID)
