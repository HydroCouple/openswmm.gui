/**
 * @file      propertyeditorregistry.h
 * @brief     Slice BM Phase 6.3.2 — `PropertyEditorRegistry`.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice BM Phase 6.3.2 — `PropertyEditorRegistry`.
 *
 * Maps an "editor kind" string → factory callable that produces an
 * `IPropertyEditor`. Process-wide singleton (matches `UnitSystem` and
 * `FileFilterRegistry` patterns already used by the GUI).
 *
 * Concrete editor slices (BN/BO/BP/BQ/BR/BS/BT) call `registerFactory()`
 * once at startup — typically from a small `register_<slice>_editors()`
 * free function pulled in by `main.cpp`. `PropertiesPanel` consults the
 * registry on every selection change via `editorFor(ref, kind)`. A null
 * factory result means "no concrete editor today" and the legacy
 * property-tree path stays in charge — the registry never replaces
 * existing behaviour silently.
 *
 * Thread-safety: registration happens at startup; lookup is read-only
 * after that. No mutex.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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
 * explicit at the call site (PropertiesPanel holds the editor for the
 * lifetime of the current selection).
 */
class PropertyEditorRegistry
{
public:
    using Factory = std::function<std::unique_ptr<IPropertyEditor>()>;

    /*! \brief Access the singleton.
     * @return Borrowed process-lifetime registry; no engine or editor instance is owned by this reference.
     */
    static PropertyEditorRegistry &instance();

    /*!
     * \brief Register a factory for an editor kind. Replacing an existing
     *        registration issues a `qWarning` and accepts
     *        the new one (deterministic for unit tests; concrete editor
     *        slices should not double-register).
     * @param[in] kind Nonempty, case-sensitive editor kind; empty names are warned and ignored.
     * @param[in] factory Owning callable moved into the registry; empty callables are warned and ignored.
     * @details Registration is unsynchronized and must not overlap lookup; an existing name is overwritten with a warning.
     */
    void registerFactory(const QString &kind, Factory factory);

    /*! \brief True when at least one factory is registered for `kind`.
     * @param[in] kind Exact registered key.
     * @return True when a factory is stored; does not invoke it or guarantee a non-null editor.
     */
    [[nodiscard]] bool hasFactory(const QString &kind) const;

    /*!
     * \brief Build a fresh editor instance for `kind`, or nullptr if
     *        no factory is registered. Callers are responsible for
     *        wiring the editor to a concrete `SWMMObjectRef` via
     *        `IPropertyEditor::editorForObject`.
     * @param[in] kind Exact registered key.
     * @return Fresh caller-owned editor, or nullptr for a missing registration or null factory result. Factory exceptions propagate.
     */
    [[nodiscard]] std::unique_ptr<IPropertyEditor> create(const QString &kind) const;

    /*! \brief Drop every registration. Test-only helper.
     * @details Destroys stored callable objects; previously created editor instances remain owned by their callers. Must not overlap lookups.
     */
    void clear();

private:
    /**
     * @brief Construct an empty dispatch table for the singleton.
     */
    PropertyEditorRegistry() = default;
    /**
     * @brief Disable copying of the singleton registry; the reference argument has no callable runtime operation.
     */
    PropertyEditorRegistry(const PropertyEditorRegistry &) = delete;
    /**
     * @brief Disable copy assignment so registry ownership remains process-wide; the reference argument is intentionally unavailable.
     */
    PropertyEditorRegistry &operator=(const PropertyEditorRegistry &) = delete;

    QHash<QString, Factory> m_factories;
};

#endif // PROPERTYEDITORREGISTRY_H
