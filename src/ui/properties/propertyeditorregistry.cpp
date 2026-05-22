/*!
 * \file   propertyeditorregistry.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/properties/propertyeditorregistry.h"

#include <QDebug>

PropertyEditorRegistry &PropertyEditorRegistry::instance()
{
    static PropertyEditorRegistry s;
    return s;
}

void PropertyEditorRegistry::registerFactory(const QString &kind, Factory factory)
{
    if (kind.isEmpty()) {
        qWarning() << "PropertyEditorRegistry::registerFactory: empty kind ignored";
        return;
    }
    if (!factory) {
        qWarning() << "PropertyEditorRegistry::registerFactory: null factory for"
                   << kind;
        return;
    }
    if (m_factories.contains(kind)) {
        // Replacing an existing factory: warn so accidental double-registers
        // surface in CI / dev logs. The new one wins for determinism.
        qWarning() << "PropertyEditorRegistry::registerFactory: replacing existing"
                   << "factory for kind" << kind;
    }
    m_factories.insert(kind, std::move(factory));
}

bool PropertyEditorRegistry::hasFactory(const QString &kind) const
{
    return m_factories.contains(kind);
}

std::unique_ptr<IPropertyEditor>
PropertyEditorRegistry::create(const QString &kind) const
{
    auto it = m_factories.constFind(kind);
    if (it == m_factories.constEnd())
        return nullptr;
    return it.value()();
}

void PropertyEditorRegistry::clear()
{
    m_factories.clear();
}
