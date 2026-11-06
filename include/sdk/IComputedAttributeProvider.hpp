/**
 * @file IComputedAttributeProvider.hpp
 * @brief Read-only model attributes contributed by an independently built extension.
 * @details Register a provider on IModelInstance with a QObject lifetime guard. The host prefixes
 * field keys, merges values into normal identify/table/rendering reads, and removes contributions
 * when their guard, model or extension is destroyed. Providers run on the GUI thread.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include "IAttributeProvider.hpp"
#include <QVariantMap>

namespace OpenSWMM::Sdk
{
/** @brief Borrowed provider of derived attributes on authored model objects. */
class IComputedAttributeProvider
{
public:
    /** @brief Destroy an unregistered provider, or first destroy its registration lifetime guard. */
    virtual ~IComputedAttributeProvider() = default;
    /** @brief Describe this provider's fields for one spatial category.
     * @param[in] category Model object category; return empty for unsupported categories.
     * @return Owning descriptors, captured at registration. Names are local ASCII identifiers
     * beginning with a letter and containing letters, digits, underscores or hyphens.
     * @note These are static model fields: isDynamic must be false. Unregister and register again
     * to change metadata. The host exposes keys as ext.pluginId.providerId.fieldName. */
    virtual QVector<OpenSWMM::Render::AttributeField>
    fields(OpenSWMMVis::SwmmCategory category) const = 0;
    /** @brief Evaluate one derived field without mutating the model.
     * @param[in] category Category corresponding to a registered descriptor.
     * @param[in] name Exact model object identity in that category.
     * @param[in] field Local field name supplied by fields(), without the host prefix.
     * @param[in] attributes Borrowed base attribute snapshot, excluding extension-derived values;
     * copy individual values when retaining them beyond the call.
     * @return Value convertible to the descriptor type; invalid QVariant denotes an absent value.
     * @note Keep evaluation short. Use attributes for ordinary inputs; recursive model reads omit
     * derived values to prevent cycles. No worker access, edit scopes or nested event loops. */
    virtual QVariant value(OpenSWMMVis::SwmmCategory category, const QString &name,
                           const QString &field, const QVariantMap &attributes) const = 0;
};
}
