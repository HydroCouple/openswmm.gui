/**
 * @file IModelInstance.hpp
 * @brief Public access to one loaded model, selection, snapshots and edit scopes.
 * @details Facades are host-owned and become inert after the model window closes. Engine handles are borrowed only while a lease or edit scope is alive. GUI-thread access is required.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include <QObject>
#include <QUuid>
#include <QVariantMap>
#include "IEngineLease.hpp"
#include "IEditScope.hpp"
#include "IAttributeProvider.hpp"
#include "IComputedAttributeProvider.hpp"
#include <memory>

namespace OpenSWMM::Sdk
{
class IProject;
/** Read/edit facade for one loaded model; becomes inert when its window closes. */
class IModelInstance
{
public:
    /** @brief Destroy a facade; extensions borrow the host-owned instance. */
    virtual ~IModelInstance() = default;
    /** @brief Identify the model across project save/restore.
     * @return Persistent instance UUID, retained by the facade after closure. */
    virtual QUuid id() const = 0;
    /** @brief Read the current window caption. @return Empty after window destruction. */
    virtual QString title() const = 0;
    /** @brief Read the current model input path. @return Empty after window destruction. */
    virtual QString inpPath() const = 0;
    /** @brief Read the selected simulation engine version. @return Empty after closure. */
    virtual QString engineVersion() const = 0;
    /** @brief Obtain the model window as a lifetime signal context.
     * @return Borrowed QObject or null; observe destroyed() rather than retaining an unguarded pointer. */
    virtual QObject *lifetime() const = 0;
    /** @brief Access the containing project facade. @return Borrowed host-owned project. */
    virtual IProject *project() const = 0;
    /** @brief Test model availability. @return False once close begins or no engine is loaded. */
    virtual bool isOpen() const = 0;
    /** Selection entries use {type: engine object kind, name: object ID}. */
    /** @brief Copy the cross-view selection shared by the map, tables and property panels.
     * @return Entries with integer type (SelectionManager object kind) and string name; empty when closed. */
    virtual QList<QVariantMap> selection() const = 0;
    /** @brief Replace the cross-view selection using stable object identities.
     * @param[in] objects Entries containing integer type and string name; an empty list clears selection.
     * @note GUI thread only. No model data is changed. Closed instances ignore the request. */
    virtual void select(const QList<QVariantMap> &objects) = 0;
    /** @brief Write a reviewable INP snapshot and its companions under the project directory.
     * @param[out] error Optional failure detail, unchanged on success.
     * @return Absolute snapshot path, or empty for a closed model or failed write. Unsaved models
     * use the reviewable user-data directory under unsaved/instance-UUID/.
     * @note The caller must not concurrently mutate the engine. Snapshot files remain reviewable. */
    virtual QString snapshot(QString *error = nullptr) const = 0;
    /** @brief Read the same model attributes used by identify and property views.
     * @param[in] category One of node, link, subcatchment or gage.
     * @param[in] name Exact model object ID in that namespace.
     * @return Owning attribute map in the GUI's existing field/unit conventions; empty if unavailable. */
    virtual QVariantMap attributes(const QString &category, const QString &name) const = 0;
    /** @brief Access the layer's canonical attribute field metadata.
     * @return Borrowed provider for the live model, or null after closure; never delete it. */
    virtual IAttributeProvider *attributeProvider() const = 0;
    /** @brief Copy metadata for loaded 1D and 2D result layers.
     * @return Maps with id, title, kind, resultsPath, active and periodCount; 1D entries also
     * contain reportPath, start and end. Read payloads with the matching engine file readers. */
    virtual QList<QVariantMap> results() const = 0;
    /** @brief Acquire shared read access or exclusive write access to the live engine.
     * @param[in] kind Requested access level.
     * @param[out] error Optional conflict reason naming an exclusive extension holder.
     * @return Caller-owned RAII lease, or null while a run/conflicting lease holds the model.
     * @note Release on the GUI thread. Raw writes belong in nativeEdit() so GUI caches and undo stay synchronized. */
    virtual std::unique_ptr<IEngineLease> acquireLease(LeaseKind kind,
                                                       QString *error = nullptr) = 0;
    /** @brief Begin a snapshot-backed native engine transaction under an exclusive lease.
     * @param[in] label Undo-menu description of the complete operation.
     * @param[out] error Optional lease or snapshot failure detail.
     * @return Caller-owned scope, or null without changing the model.
     * @note Commit once; destruction otherwise rolls back. Do not process nested GUI events inside a scope. */
    virtual std::unique_ptr<INativeEditScope> nativeEdit(const QString &label,
                                                         QString *error = nullptr) = 0;
    /** @brief Begin one undoable transaction through the existing GUI edit paths.
     * @param[in] label Undo-menu description of the complete batch.
     * @param[out] error Optional lease or snapshot failure detail.
     * @return Caller-owned scope; null for unavailable access or snapshot failure. */
    virtual std::unique_ptr<IEditScope> edit(const QString &label, QString *error = nullptr) = 0;
    /** @brief Register read-only model attributes shared by identify, tables and symbology.
     * @param[in] id Extension-local ASCII identifier, using letters, digits, underscores or hyphens.
     * @param[in] provider Borrowed implementation, kept alive until unregistered or guard destruction.
     * @param[in] context Required GUI-thread QObject lifetime guard; never transferred to the host.
     * @param[out] error Optional invalid metadata, duplicate identity or closed-model detail.
     * @return True after registering all field metadata atomically; false leaves existing fields intact.
     * @note Registration is per model and scoped to the owning extension. No engine data is written.
     * Available since SDK 1.1. */
    virtual bool registerAttributeProvider(const QString &id, IComputedAttributeProvider *provider,
                                           QObject *context, QString *error = nullptr) = 0;
    /** @brief Remove this extension's provider and refresh its model views.
     * @param[in] id Extension-local provider identity; an unknown identity is ignored.
     * @note GUI thread only, safe after model closure. Available since SDK 1.1. */
    virtual void unregisterAttributeProvider(const QString &id) = 0;
    /** @brief Refresh derived values after an extension changes its calculation inputs.
     * @param[in] id Registered extension-local provider identity.
     * @note Invalidates table values and classification caches without changing model data or
     * field metadata. Unknown identities are ignored. Available since SDK 1.1. */
    virtual void refreshAttributeProvider(const QString &id) = 0;
};
}
