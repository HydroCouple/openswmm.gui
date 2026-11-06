/**
 * @file      comprehensiveeditorregistry.h
 * @brief     Single source of truth mapping each non-spatial data category to its comprehensive
 *            MVC editor.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice BM.0-Browse-Edit — Single source of truth mapping each non-spatial
 * data category to its comprehensive MVC editor. Consumed by three surfaces
 * that all need the same (has-editor? / gap-tooltip / open-create-new)
 * answer:
 *
 *   1. `ObjectBrowserPanel`'s category-header "Add New…" context-menu action.
 *   2. `DataObjectPickerEditor`'s "…" browse button on attribute-panel rows.
 *   3. A future attribute-panel row right-click "Edit…" menu (Slice
 *      BM.0-Browse-Edit step 5; not yet wired).
 *
 * Replacing the duplicated switch statements in `ObjectBrowserPanel` and
 * `DataObjectPickerEditor` with one registry lookup means each future
 * editor slice (BO/BP/BQ/BR) lights up all three surfaces by adding a
 * single `registerEditor(...)` call rather than touching N call sites.
 *
 * Lifetime: the registry is a process-wide singleton. Editor entries are
 * layer-agnostic — the `OpenCreateFn` receives a `SWMMModelLayer*` per
 * invocation, never captures one. This survives project switches without
 * re-registration.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef COMPREHENSIVEEDITORREGISTRY_H
#define COMPREHENSIVEEDITORREGISTRY_H

#include "layers/swmmmodellayer.h"

#include <QHash>
#include <QString>
#include <functional>

class QUndoStack;
class QWidget;

/*!
 * \class ComprehensiveEditorRegistry
 * \brief Singleton mapping `SWMMModelLayer::DataCategory` → comprehensive
 *        editor metadata + create-new dispatch.
 */
class ComprehensiveEditorRegistry
{
public:
    /*! Invoked to open the editor in create-new mode.
     *  \param layer      Active project layer (non-null; entry is skipped if null).
     *  \param undoStack  Optional canvas undo stack; nullptr is acceptable
     *                    (editors that take an undo stack can pass it
     *                    through; those that don't ignore the argument).
     *  \param parent     QWidget parent for the spawned dialog. */
    using OpenCreateFn =
        std::function<void(SWMMModelLayer *layer, QUndoStack *undoStack, QWidget *parent)>;

    /*! Invoked to open the editor with the object named \p name selected
     *  (Object Browser leaf Edit… / double-click, Properties "Open in…",
     *  Attribute Table row "Edit in…"). */
    using OpenForObjectFn = std::function<void(SWMMModelLayer *layer, QUndoStack *undoStack,
                                               const QString &name, QWidget *parent)>;

    struct Entry {
        QString editorTitle;        ///< User-visible name, e.g. "Curve Editor".
        QString gapSliceLabel;      ///< Populated only when openCreateNew is null.
        OpenCreateFn openCreateNew; ///< Null iff editor is not yet shipped.
        /*! Opens the editor in review/browse mode — no object is created;
         *  the user picks from the editor's own list and creates via its
         *  Add/New button. Same signature as `openCreateNew`. Null iff the
         *  editor is not yet shipped. */
        OpenCreateFn openBrowse;
        /*! Opens the editor on an existing object. Null iff the editor is
         *  not yet shipped. */
        OpenForObjectFn openForObject;
    };

    /**
     * @return Borrowed process-static registry, populated on first call with 13 nonspatial
     *   categories. Never free.
     * @note GUI-thread only; initialization flag set before population permits reentry to observe
     *   PARTIAL registry. No lock/thread-safe mutation or retranslation after language change.
     *   Static callbacks may keep dialog instances across projects until destroyed; registry
     *   entries themselves store no layer.
     */
    static ComprehensiveEditorRegistry &instance();

    /*! Insert or replace the entry for \p cat. Last-write-wins.
     * @param[in] cat Category integer key; unknown enum values allowed.
     * @param[in] entry Metadata/callables moved into hash; replaces previous value.
     * @note No validation, signal or existing dialog close. Invalidates borrowed Entry
     *   pointers/references under hash mutation; caller synchronizes all lookup/dispatch. Callback
     *   captures are caller-owned lifetime responsibility.
     */
    void registerEditor(SWMMModelLayer::DataCategory cat, Entry entry);

    /*! Returns nullptr when \p cat is unregistered.
     * @param[in] cat Category key, unknown returns null.
     * @return Borrowed hash entry or null. Valid only until any registry mutation/destruction; do
     *   not retain across callbacks that can register entries. No lazy creation or ownership
     *   transfer.
     */
    [[nodiscard]] const Entry *find(SWMMModelLayer::DataCategory cat) const noexcept;

    /*! True iff the entry for \p cat has a non-null `openCreateNew`.
     * @param[in] cat Category key.
     * @return Entry exists and openCreateNew callable nonempty; does not prove browse/object
     *   callback exists, active layer available, or dialog creation succeeds.
     */
    [[nodiscard]] bool hasEditor(SWMMModelLayer::DataCategory cat) const noexcept;

    /*! Tooltip for the disabled action on a gap category. Empty when
     *  `hasEditor` returns true or \p cat is unregistered.
     * @param[in] cat Category key.
     * @return Copied gapSliceLabel only if entry exists and create callback absent; empty
     *   otherwise. No dynamic translation or availability check.
     */
    [[nodiscard]] QString gapTooltip(SWMMModelLayer::DataCategory cat) const;

    /*! User-visible editor title, or empty when unregistered.
     * @param[in] cat Category key.
     * @return Copied stored title, or empty for unknown category; no active-layer lookup.
     */
    [[nodiscard]] QString editorTitle(SWMMModelLayer::DataCategory cat) const;

    /*! Opens the \p cat editor with \p name selected. Returns false (and
     *  does nothing) when \p cat has no `openForObject` or \p name is empty.
     * @param[in] cat Category dispatch key.
     * @param[in] layer Optional borrowed live layer, null rejects.
     * @param[in] undoStack Optional borrowed stack passed to callback; only supported editors use
     *   it.
     * @param[in] name Exact requested object ID; empty rejects, existence not checked centrally.
     * @param[in] parent Optional QWidget dialog owner passed through; parentless windows require
     *   dialog-specific cleanup.
     * @return False if entry/callback/layer absent or name empty; true once callback invoked, even
     *   if callback silently fails registry/dialog creation or object lookup. Does not mean object
     *   was found or changes saved.
     * @note GUI-thread dispatch can create/show/raise modeless dialogs, initialize registries and
     *   wire close-time engine writes. Callback exceptions propagate. Builtin reuse policy differs
     *   by kind: registry-backed six retarget on registry change, several other static windows
     *   remain bound to original model until destroyed; host manages project/window lifetime.
     */
    bool openForObject(SWMMModelLayer::DataCategory cat, SWMMModelLayer *layer,
                       QUndoStack *undoStack, const QString &name, QWidget *parent) const;

private:
    /**
     * @brief Construct empty hash privately; instance() performs builtin registration separately.
     *   No dialogs or engine access.
     */
    ComprehensiveEditorRegistry() = default;

    QHash<int, Entry> m_entries; ///< keyed by `DataCategory` (cast to int)
};

#endif // COMPREHENSIVEEDITORREGISTRY_H
