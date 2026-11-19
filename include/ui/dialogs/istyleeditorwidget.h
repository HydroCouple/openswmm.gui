/**
 * @file      istyleeditorwidget.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     MVC-style editor widget for a single styleable subject.
 *
 *         Slice U-V1. The unified LayerStyleDialog consults a registry to
 *         find a custom editor widget for each ILayerStyleSubject. When a
 *         registered editor exists (e.g. FeatureStyleEditor for a
 *         FeatureSublayerStyle) the dialog embeds it. Otherwise the dialog
 *         falls back to the QPropertyModel-driven generic tree editor.
 *
 *         Custom editors give us QGIS / ArcGIS-style affordances — color
 *         buttons with swatches, marker shape grids, dash style combos,
 *         live preview panels — that QPropertyModel's generic tree can't
 *         match. They bind directly to the propertyObject's Q_PROPERTYs
 *         and listen for the bag's NOTIFY signal so external mutations
 *         (e.g. cancel rollback, .oswp load) refresh the UI.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_ISTYLEEDITORWIDGET_H
#define OPENSWMMVIS_UI_DIALOGS_ISTYLEEDITORWIDGET_H

#include <QWidget>
#include <QString>

#include <functional>
#include <memory>

namespace openswmmvis::ui
{

/*!
 * \brief Custom editor widget for one style subject.
 *
 *        Concrete editors derive from this and accept the property
 *        object pointer in their constructor. They read initial values
 *        in the ctor (or via a refresh() override) and write back to
 *        the property object on every user edit. The dialog's
 *        Apply/Cancel flow is handled at the subject level (via the
 *        ILayerStyleSubject::snapshot/restore round-trip), so editors
 *        only need to perform direct writes — no buffering required.
 */
class IStyleEditorWidget : public QWidget
{
    Q_OBJECT
public:
    using QWidget::QWidget;
    /**
     * @brief Destroy QWidget-owned editor children/connections; default does not restore
     *   model/snapshot or own property object. Caller handles session rollback before source/editor
     *   disposal.
     */
    ~IStyleEditorWidget() override = default;

    /*! Re-read the property object's current values and update the
     *  widget's controls. Called after an external mutation (Cancel
     *  rollback, .oswp load) so the open dialog stays in sync.
     * @brief Derived editor contract: read bound live property state into controls after external
     *   edits/rollback.
     * @note Implementations preserve source lifetime and suppress hydration feedback to avoid
     *   unintended property writes; no base refresh, persistence, undo or automatic NOTIFY wiring
     *   provided. GUI-thread only.
     */
    virtual void refreshFromModel() = 0;
};

/*!
 * \brief Maps a property-object class name to an editor-widget factory.
 *
 *        The registry is a singleton populated at static-init time by
 *        editor source files. Lookup is by QObject::metaObject->className.
 *        Editors can choose to match the most-derived class first; if no
 *        match, the LayerStyleDialog falls back to the QPropertyModel
 *        generic tree.
 */
class StyleEditorRegistry
{
public:
    using Factory = std::function<IStyleEditorWidget *(QObject *propertyObject, QWidget *parent)>;

    /**
     * @return Process-local function-static registry reference, not caller-owned. Initialization
     *   thread-safe by C++, registration/lookup mutable vector not synchronized; intended
     *   startup/GUI-thread use. No factory invoked here.
     */
    static StyleEditorRegistry &instance();

    /*! Register an editor factory for the given className (e.g.
     *  "OpenSWMM::Render::PointFeatureSublayerStyle").
     * @param[in] className Copied exact case-sensitive fully-qualified or unqualified Qt class
     *   name.
     * @param[in] factory Moved callable, required nonempty for matched invocation; no null
     *   validation.
     * @note Append even duplicate/empty keys, never replace; first matching registration wins for
     *   same class depth. May reallocate storage, not thread-safe with lookup/registration. No
     *   factory invocation/source mutation now, captured resources retained until registry
     *   destruction.
     */
    void registerFactory(const QString &className, Factory factory);

    /*! Walk the className then its super-classes looking for a match.
     *  Returns a newly-constructed editor (caller owns) or nullptr when
     *  no factory matches.
     * @param[in] propertyObject Optional borrowed live QObject; null returns null, factory may
     *   retain and require it outlive editor.
     * @param[in] parent Optional QWidget owner forwarded to factory; caller manages returned
     *   parentless editor.
     * @return FIRST factory result (including null) at most-derived matching metaobject depth;
     *   match fully-qualified OR tail case-sensitively in registration order, then superclass. No
     *   fallback to other match/superclass if matching factory returns null. Factory can
     *   allocate/write live style and exceptions propagate; registry performs no rollback/ownership
     *   pin. Empty matching std::function invocation throws. GUI-thread widget factory use.
     */
    [[nodiscard]] IStyleEditorWidget *createEditorFor(QObject *propertyObject,
                                                      QWidget *parent = nullptr) const;

private:
    /**
     * @brief Private default construction with empty factory list, no registration/editor/model
     *   side effects. instance manages lifetime.
     */
    StyleEditorRegistry() = default;

    struct Entry {
        QString className;
        Factory factory;
    };
    std::vector<Entry> m_entries;
};

/*! RAII helper to register a factory at static init. Use the
 *  REGISTER_STYLE_EDITOR(ClassName, FactoryLambda) macro below. */
class StyleEditorRegistrar
{
public:
    /**
     * @param[in] className Copied class key, no uniqueness validation.
     * @param[in] factory Moved callable/captures into global registry, must remain invocable when
     *   matched.
     * @brief Append style editor registration immediately, typically static initialization. No
     *   unregister on registrar destruction or factory execution now; mutable registry access must
     *   be serialized.
     */
    StyleEditorRegistrar(const QString &className, StyleEditorRegistry::Factory factory)
    {
        StyleEditorRegistry::instance().registerFactory(className, std::move(factory));
    }
};

#define REGISTER_STYLE_EDITOR(CLASSNAME, FACTORY_BODY)                                             \
    static const openswmmvis::ui::StyleEditorRegistrar s_register_##CLASSNAME(                     \
        QStringLiteral(#CLASSNAME), FACTORY_BODY);

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_ISTYLEEDITORWIDGET_H
