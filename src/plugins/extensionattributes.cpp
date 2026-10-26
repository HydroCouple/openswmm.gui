/**
 * @file extensionattributes.cpp
 * @brief Guarded attribute registration, namespacing and shared derived-value evaluation.
 * @details Descriptors are validated before any registration is published. QObject guards remove
 * callbacks before provider/model destruction. Shared entry snapshots tolerate unregistration
 * during evaluation, and a per-thread recursion guard prevents derived-value dependency cycles.
 * C++ calculation exceptions disable that provider's values until a deliberate refresh.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "plugins/extensionattributes.h"
#include <QDebug>
#include <QHash>
#include <QMap>
#include <QPointer>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSet>
#include <QThread>
#include <exception>
#include <memory>

namespace openswmmvis
{
namespace
{
/** @brief One cached descriptor set and its guarded, borrowed implementation. */
struct AttributeSource {
    QPointer<QObject> context; ///< Required provider lifetime guard.
    OpenSWMM::Sdk::IComputedAttributeProvider *provider =
        nullptr; ///< Borrowed while context lives.
    QMap<int, QVector<OpenSWMM::Render::AttributeField>>
        fields;                        ///< Local keys, grouped by category.
    std::function<void(bool)> changed; ///< Guarded layer refresh callback.
    QMetaObject::Connection destroyed; ///< Disconnect when replacing/removing this entry.
    bool failed = false;               ///< Suppress repeated exceptions until an explicit refresh.
};
/** @brief GUI-thread registry, keyed by the concrete layer's QObject identity.
 * @return Process-lifetime storage; layer destruction removes all associated entries. */
auto &sources()
{
    static QHash<const QObject *, QMap<QString, std::shared_ptr<AttributeSource>>> values;
    return values;
}
}

bool registerExtensionAttributes(QObject *layer, const QString &id,
                                 OpenSWMM::Sdk::IComputedAttributeProvider *provider,
                                 QObject *context, std::function<void(bool)> changed,
                                 QString *error)
{
    const auto refuse = [error](const QString &reason) {
        if (error)
            *error = reason;
        return false;
    };
    if (!layer || !provider || !context || QThread::currentThread() != layer->thread() ||
        context->thread() != layer->thread())
        return refuse("A live model and GUI-thread provider lifetime guard are required.");
    if (id.isEmpty() || sources().value(layer).contains(id))
        return refuse("The attribute provider identity is empty or already registered.");
    auto source = std::make_shared<AttributeSource>();
    source->context = context;
    source->provider = provider;
    source->changed = std::move(changed);
    const QRegularExpression identifier("^[A-Za-z][A-Za-z0-9_-]*$");
    try {
        for (int c = 0; c < OpenSWMMVis::NumCategories; ++c) {
            if (!source->context)
                return refuse("The provider lifetime guard was destroyed during registration.");
            const auto fields = provider->fields(static_cast<OpenSWMMVis::SwmmCategory>(c));
            QSet<QString> seen;
            for (const auto &field : fields) {
                if (!identifier.match(field.name).hasMatch() || seen.contains(field.name) ||
                    !QMetaType(field.type).isValid() || field.isDynamic)
                    return refuse(
                        "Derived fields require unique local identifiers, valid value types and static model metadata.");
                seen.insert(field.name);
            }
            source->fields[c] = fields;
        }
    } catch (const std::exception &exception) {
        return refuse(QString::fromUtf8(exception.what()));
    } catch (...) {
        return refuse("The attribute provider threw while describing its fields.");
    }
    if (!source->context)
        return refuse("The provider lifetime guard was destroyed during registration.");
    if (!sources().contains(layer))
        QObject::connect(layer, &QObject::destroyed, [layer]() { sources().remove(layer); });
    sources()[layer].insert(id, source);
    source->destroyed =
        QObject::connect(context, &QObject::destroyed, layer, [layer, id, source]() {
            if (sources().value(layer).value(id) == source)
                unregisterExtensionAttributes(layer, id);
        });
    if (source->changed)
        source->changed(true);
    return true;
}

void unregisterExtensionAttributes(QObject *layer, const QString &id)
{
    auto it = sources().find(layer);
    if (it == sources().end())
        return;
    const auto source = it->take(id);
    if (!source)
        return;
    QObject::disconnect(source->destroyed);
    if (source->changed)
        source->changed(true);
}

void refreshExtensionAttributes(QObject *layer, const QString &id)
{
    const auto source = sources().value(layer).value(id);
    if (source && source->context) {
        source->failed = false;
        if (source->changed)
            source->changed(false);
    }
}

QVector<OpenSWMM::Render::AttributeField>
extensionAttributeFields(const QObject *layer, OpenSWMMVis::SwmmCategory category)
{
    QVector<OpenSWMM::Render::AttributeField> result;
    const auto entries = sources().value(layer);
    for (auto it = entries.cbegin(); it != entries.cend(); ++it) {
        if (!it.value()->context)
            continue;
        for (auto field : it.value()->fields.value(category)) {
            field.name = it.key() + '.' + field.name;
            result.append(field);
        }
    }
    return result;
}

QVariantMap extensionAttributeValues(const QObject *layer, OpenSWMMVis::SwmmCategory category,
                                     const QString &name, const QVariantMap &base)
{
    static thread_local QSet<const QObject *> evaluating;
    if (evaluating.contains(layer))
        return base;
    evaluating.insert(layer);
    const auto guard = qScopeGuard([layer]() { evaluating.remove(layer); });
    auto result = base;
    const auto entries = sources().value(layer);
    for (auto it = entries.cbegin(); it != entries.cend(); ++it) {
        const auto source = it.value();
        if (source->failed)
            continue;
        QVariantMap computed;
        for (const auto &field : source->fields.value(category)) {
            if (!source->context)
                break;
            try {
                auto value = source->provider->value(category, name, field.name, base);
                const auto key = it.key() + '.' + field.name;
                if (value.isValid() && value.convert(QMetaType(field.type)) &&
                    !result.contains(key))
                    computed.insert(key, value);
            } catch (...) {
                source->failed = true;
                qWarning().noquote() << "Derived attribute provider failed:" << it.key()
                                     << "Refresh the provider to retry.";
                break;
            }
        }
        if (!source->failed && source->context)
            for (auto value = computed.cbegin(); value != computed.cend(); ++value)
                result.insert(value.key(), value.value());
    }
    return result;
}
}
