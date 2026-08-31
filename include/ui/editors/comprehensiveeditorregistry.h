/*!
 * \file   comprehensiveeditorregistry.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
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
    using OpenCreateFn = std::function<void(SWMMModelLayer *layer,
                                            QUndoStack    *undoStack,
                                            QWidget       *parent)>;

    struct Entry {
        QString      editorTitle;     ///< User-visible name, e.g. "Curve Editor".
        QString      gapSliceLabel;   ///< Populated only when openCreateNew is null.
        OpenCreateFn openCreateNew;   ///< Null iff editor is not yet shipped.
        /*! Opens the editor in review/browse mode — no object is created;
         *  the user picks from the editor's own list and creates via its
         *  Add/New button. Same signature as `openCreateNew`. Null iff the
         *  editor is not yet shipped. */
        OpenCreateFn openBrowse;
    };

    static ComprehensiveEditorRegistry &instance();

    /*! Insert or replace the entry for \p cat. Last-write-wins. */
    void registerEditor(SWMMModelLayer::DataCategory cat, Entry entry);

    /*! Returns nullptr when \p cat is unregistered. */
    [[nodiscard]] const Entry *find(SWMMModelLayer::DataCategory cat) const noexcept;

    /*! True iff the entry for \p cat has a non-null `openCreateNew`. */
    [[nodiscard]] bool hasEditor(SWMMModelLayer::DataCategory cat) const noexcept;

    /*! Tooltip for the disabled action on a gap category. Empty when
     *  `hasEditor` returns true or \p cat is unregistered. */
    [[nodiscard]] QString gapTooltip(SWMMModelLayer::DataCategory cat) const;

    /*! User-visible editor title, or empty when unregistered. */
    [[nodiscard]] QString editorTitle(SWMMModelLayer::DataCategory cat) const;

private:
    ComprehensiveEditorRegistry() = default;

    QHash<int, Entry> m_entries;   ///< keyed by `DataCategory` (cast to int)
};

#endif // COMPREHENSIVEEDITORREGISTRY_H
