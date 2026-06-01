/*!
 * \file   kindtreesymbologypanel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Tree-on-left + editor-on-right Symbology widget for SWMM
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

namespace openswmmvis::ui {

class KindTreeSymbologyPanel : public QWidget
{
    Q_OBJECT
public:
    /*! \param hostLayer  Either a SWMMModelLayer or a SWMMResultsLayer. */
    explicit KindTreeSymbologyPanel(OpenSWMMVisLayer *hostLayer,
                                     QWidget *parent = nullptr);

    /*! Focus the tree on the kind matching \p routingId
     *  ("model.junctions" / "results.conduits" / …). */
    void focusKind(const QString &routingId);

private slots:
    void onTreeSelectionChanged();
    void onTreeItemChanged(QStandardItem *item);

private:
    void buildTree();
    void mountEditorForCategory(OpenSWMMVis::SwmmCategory cat);
    [[nodiscard]] QString rendererBadgeFor(OpenSWMMVis::SwmmCategory cat) const;
    [[nodiscard]] QString routingIdFor(OpenSWMMVis::SwmmCategory cat) const;
    [[nodiscard]] static QString suffixFor(OpenSWMMVis::SwmmCategory cat);

    QPointer<OpenSWMMVisLayer> m_layer;

    QTreeView          *m_tree   = nullptr;
    QStandardItemModel *m_model  = nullptr;
    QStackedWidget     *m_stack  = nullptr;

    /*! Routing-id prefix for this layer ("model." / "results."). */
    QString m_routingPrefix;

    bool m_suppressEdits = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_KINDTREESYMBOLOGYPANEL_H
