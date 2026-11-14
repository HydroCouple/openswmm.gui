/**
 * @file      kindtreesymbologypanel.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Tree-on-left + editor-on-right Symbology widget for SWMM
 *         multi-kind layers (Slice X.5).
 *
 *         Replaces the nested 11-sub-tab UI from §W with a single panel
 *         where the user picks a kind from the tree, and the right pane
 *         mounts a SymbologyTab focused on that kind's renderer.  Visible
 *         state of each kind shows as a checkbox; current renderer mode
 *         appears as a single-letter badge (S/G/C/R) next to the name.
 *
 *         Tree contents:
 *             Nodes
 *             ├ ☑ Junctions     S
 *             ├ ☑ Outfalls      S
 *             ├ ☑ Storage       S
 *             └ ☑ Dividers      S
 *             Links
 *             ├ ☑ Conduits      G
 *             ├ ☑ Pumps         S
 *             ├ ☑ Orifices      S
 *             ├ ☑ Weirs         S
 *             └ ☑ Outlets       S
 *             Subcatchments ☑   G
 *             Rain gages    ☑   S
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_KINDTREESYMBOLOGYPANEL_H
#define OPENSWMMVIS_UI_DIALOGS_KINDTREESYMBOLOGYPANEL_H

#include "layers/swmm_category.h"

#include <QPointer>
#include <QWidget>

class OpenSWMMVisLayer;

class QStackedWidget;
class QStandardItem;
class QStandardItemModel;
class QTreeView;

namespace openswmmvis::ui
{

class KindTreeSymbologyPanel : public QWidget
{
    Q_OBJECT
public:
    /*! Argument `hostLayer`:  Either a SWMMModelLayer or a SWMMResultsLayer. */
    /**
     * @brief Build a GUI-thread kind tree and mount the initially selected Junctions editor.
     * @param[in,out] hostLayer Nullable borrowed model/results layer, guarded by QPointer; model is
     * the default routing prefix for other/null hosts. Mounted editors retain borrowed
     * styles/adapters, which must outlive their active callbacks. No layer ownership or local undo
     * snapshot.
     * @param[in] parent Optional Qt widget owner.
     * Visibility checkboxes and editor controls write through to the layer; the tree is initially
     * populated once rather than subscribed to every external renderer/visibility change.
     */
    explicit KindTreeSymbologyPanel(OpenSWMMVisLayer *hostLayer, QWidget *parent = nullptr);

    /*! Focus the tree on the kind matching \p routingId
     *  ("model.junctions" / "results.conduits" / …). */
    /**
     * @brief Select the first depth-first tree row with the exact routing ID, invoking normal
     * editor mounting.
     * @param[in] routingId Case-sensitive model.* or results.* ID; empty/unknown IDs leave
     * selection unchanged.
     */
    void focusKind(const QString &routingId);

private slots:
    /**
     * @brief Mount the selected row category or one of the three recognized model subject adapters.
     * Normalizes badge-column clicks to column zero; invalid/group selections are ignored.
     */
    void onTreeSelectionChanged();
    /**
     * @brief Apply a name-column category checkbox to model or results sublayer visibility.
     * @param[in] item Nullable borrowed model item; ignored while hydrating, without category data,
     * or outside column zero. Only Qt::Checked means visible.
     * Model writes are skipped when already equal; results writes require an existing feature
     * sublayer. No undo command is created.
     */
    void onTreeItemChanged(QStandardItem *item);

private:
    /**
     * @brief Replace model-owned tree rows under edit suppression and select the first Junctions
     * row.
     * Adds model-only virtual/inlet subject rows without independent checkboxes; model checks read
     * category state while results checks initially default visible. Initial selection mounts an
     * editor.
     */
    void buildTree();
    /**
     * @brief Defer-delete old stack pages and build a Qt-owned editor for the selected kind.
     * @param[in] cat Category used for renderer/rule, attributes, size/arrow/label channels and
     * visibility context. Callers supply a valid tree category.
     * Model kinds use the complete element-symbol adapter; results refresh their rule mirror first
     * and add applicable channel tabs. Restores the last translated channel-tab title when
     * available. Borrowed layer/styles must survive active editors.
     */
    void mountEditorForCategory(OpenSWMMVis::SwmmCategory cat);
    /*! Mount the SwmmElementSymbolEditor for a category-less subject row
     *  ("model.virtualjunctions" / "model.inletjunctions" /
     *  "model.inletconnectors") — model layers only. */
    /**
     * @brief Replace the page with a registered editor for a category-less model subject.
     * @param[in] routingId Exact adapter routing ID; absent model/adapter leaves the old page
     * intact.
     * Old pages are removed and deleteLater-scheduled; a missing factory yields a label inside a
     * stack-owned scroll area. Adapter ownership stays with the model.
     */
    void mountSubjectEditor(const QString &routingId);
    /**
     * @brief Read the current model/results renderer class badge without transferring ownership.
     * @param[in] cat Kind used to query the layer renderer.
     * @return S for no renderer, the recognized ID badge, or empty for an unrecognized renderer ID.
     */
    [[nodiscard]] QString rendererBadgeFor(OpenSWMMVis::SwmmCategory cat) const;
    /**
     * @brief Combine this layer routing prefix with a recognized category suffix.
     * @param[in] cat Category to encode.
     * @return model.* or results.* ID, or empty for an unsupported category.
     */
    [[nodiscard]] QString routingIdFor(OpenSWMMVis::SwmmCategory cat) const;
    /**
     * @brief Map a category to its stable routing suffix.
     * @param[in] cat One of the eleven supported SWMM categories.
     * @return Lowercase suffix (including raingages), or empty for an unsupported value.
     */
    [[nodiscard]] static QString suffixFor(OpenSWMMVis::SwmmCategory cat);

    QPointer<OpenSWMMVisLayer> m_layer;

    QTreeView *m_tree = nullptr;
    QStandardItemModel *m_model = nullptr;
    QStackedWidget *m_stack = nullptr;

    /*! Routing-id prefix for this layer ("model." / "results."). */
    QString m_routingPrefix;

    /*! Results kinds — title of the last selected style tab (Color / Size /
     *  Arrows / Labels), re-selected when another kind is mounted. */
    QString m_lastKindTab;

    bool m_suppressEdits = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_KINDTREESYMBOLOGYPANEL_H
