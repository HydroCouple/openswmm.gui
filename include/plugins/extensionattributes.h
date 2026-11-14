/**
 * @file extensionattributes.h
 * @brief Private per-layer registry for extension-derived model attributes.
 * @details Entries cache validated descriptors and borrow guarded providers. The GUI reads the
 * same registry for field metadata, identify values and attribute-table columns. No engine or
 * concrete GUI class is exposed through this module. All operations use the layer's GUI thread.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include "sdk/IComputedAttributeProvider.hpp"
#include <QObject>
#include <functional>

namespace openswmmvis
{
/** @brief Atomically register one guarded provider on a layer.
 * @param[in] layer Borrowed target QObject, whose destruction removes its entire registry.
 * @param[in] id Qualified ext.pluginId.providerId namespace.
 * @param[in] provider Borrowed GUI-thread calculation implementation.
 * @param[in] context Required provider lifetime guard on the layer's thread.
 * @param[in] changed GUI-thread notification; true means field schema changed, false values only.
 * @param[out] error Optional validation/exception diagnostic.
 * @return True after validating and installing every descriptor; false leaves registrations intact. */
bool registerExtensionAttributes(QObject *layer, const QString &id,
                                 OpenSWMM::Sdk::IComputedAttributeProvider *provider,
                                 QObject *context, std::function<void(bool)> changed,
                                 QString *error = nullptr);
/** @brief Remove a provider and notify its live layer.
 * @param[in] layer Borrowed target, or null after closure.
 * @param[in] id Fully qualified provider namespace; unknown identities are ignored. */
void unregisterExtensionAttributes(QObject *layer, const QString &id);
/** @brief Invalidate provider values, allowing retry after a calculation exception.
 * @param[in] layer Borrowed target, or null after closure.
 * @param[in] id Fully qualified provider namespace; metadata is unchanged. */
void refreshExtensionAttributes(QObject *layer, const QString &id);
/** @brief Read validated, qualified field descriptors in deterministic provider order.
 * @param[in] layer Borrowed model layer.
 * @param[in] category Requested spatial category.
 * @return Owning descriptors; expired guards contribute no fields. */
QVector<OpenSWMM::Render::AttributeField>
extensionAttributeFields(const QObject *layer, OpenSWMMVis::SwmmCategory category);
/** @brief Merge derived values into a base model-object attribute snapshot.
 * @param[in] layer Borrowed model layer.
 * @param[in] category Exact category of the identified object.
 * @param[in] name Exact object ID in that category.
 * @param[in] base Owning original attributes, never overwritten by a contribution.
 * @return Merged owning map; recursive reads return base only. A throwing provider is disabled
 * until explicitly refreshed, while its metadata remains available. */
QVariantMap extensionAttributeValues(const QObject *layer, OpenSWMMVis::SwmmCategory category,
                                     const QString &name, const QVariantMap &base);
}
