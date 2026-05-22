/*!
 * \file   propertyeditorregistry.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice BM Phase 6.3.2 — `PropertyEditorRegistry`.
 *
 * Maps an "editor kind" string → factory callable that produces an
 * `IPropertyEditor`. Process-wide singleton (matches `UnitSystem` and
 * `FileFilterRegistry` patterns already used by the GUI).
 *
 * Concrete editor slices (BN/BO/BP/BQ/BR/BS/BT) call `registerFactory()`
 * once at startup — typically from a small `register_<slice>_editors()`
 * free function pulled in by `main.cpp`. `AttributePanel` consults the
 * registry on every selection change via `editorFor(ref, kind)`. A null
 * factory result means "no concrete editor today" and the legacy
 * property-tree path stays in charge — the registry never replaces
 * existing behaviour silently.
 *
 * Thread-safety: registration happens at startup; lookup is read-only
 * after that. No mutex.
 */
#ifndef PROPERTYEDITORREGISTRY_H
#define PROPERTYEDITORREGISTRY_H

#include <QHash>
#include <QString>
#include <functional>
#include <memory>

#include "ipropertyeditor.h"

/*!
 * \class PropertyEditorRegistry
 * \brief Process-wide dispatch table from editor kind → factory.
 *
 * Construction is private; callers go through `instance()`. The factory
 * signature returns `std::unique_ptr<IPropertyEditor>` so ownership is
 * explicit at the call site (AttributePanel holds the editor for the
 * lifetime of the current selection).
 */
class PropertyEditorRegistry
{
public:
    using Factory = std::function<std::unique_ptr<IPropertyEditor>()>;

    /*! \brief Access the singleton. */
    static PropertyEditorRegistry &instance();

    /*!
     * \brief Register a factory for an editor kind. Idempotent on the
     *        same (kind, factory) pair; replacing an existing factory
     *        with a different callable issues a `qWarning` and accepts
     *        the new one (deterministic for unit tests; concrete editor
     *        slices should not double-register).
     */
    void registerFactory(const QString &kind, Factory factory);

    /*! \brief True when at least one factory is registered for `kind`. */
    [[nodiscard]] bool hasFactory(const QString &kind) const;

    /*!
     * \brief Build a fresh editor instance for `kind`, or nullptr if
     *        no factory is registered. Callers are responsible for
     *        wiring the editor to a concrete `SWMMObjectRef` via
     *        `IPropertyEditor::editorForObject`.
     */
    [[nodiscard]] std::unique_ptr<IPropertyEditor> create(const QString &kind) const;

    /*! \brief Drop every registration. Test-only helper. */
    void clear();

private:
    PropertyEditorRegistry() = default;
    PropertyEditorRegistry(const PropertyEditorRegistry &)            = delete;
    PropertyEditorRegistry &operator=(const PropertyEditorRegistry &) = delete;

    QHash<QString, Factory> m_factories;
};

#endif // PROPERTYEDITORREGISTRY_H
