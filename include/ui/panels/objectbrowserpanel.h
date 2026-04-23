/*!
 * \file   objectbrowserpanel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license MIT
 *
 * Phase 0 — Object Browser dock. Tree view of every SWMM object in the
 * active project's model, grouped by type (Junctions, Conduits, …).
 * Two-way bound to the per-project SelectionManager.
 */
#ifndef OBJECTBROWSERPANEL_H
#define OBJECTBROWSERPANEL_H

#include "selection/selectionmanager.h"

#include <QHash>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QWidget>

class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;
class SWMMModelLayer;

/*!
 * \class ObjectBrowserPanel
 * \brief Embeddable object-navigation widget. Mirrors the legacy Delphi
 *        browser's per-type collapsible groups.
 *
 * The panel is canvas-agnostic: it binds to a `(SWMMModelLayer*,
 * SelectionManager*)` pair on every MDI tab switch via setProject().
 * Pass nulls to detach (empty tree, no signal traffic).
 */
class ObjectBrowserPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ObjectBrowserPanel(QWidget *parent = nullptr);
    ~ObjectBrowserPanel() override;

    /*!
     * \brief Bind the panel to a project's model layer + selection bus.
     * \param layer    SWMM model layer for the active project (or nullptr).
     * \param selMgr   SelectionManager owned by the same project (or nullptr).
     */
    void setProject(SWMMModelLayer *layer, SelectionManager *selMgr);

    /*!
     * \brief Repopulate the tree from the bound layer's engine.
     *        Call after the model is loaded.
     */
    void refresh();

signals:
    /*! \brief Emitted when the user picks "Plot Time Series" from the
     *         right-click menu. The receiver locates the project's
     *         results layer (.out file) and opens TimeSeriesPlotDialog. */
    void plotTimeSeriesRequested(const SWMMObjectRef &object);

private slots:
    void onTreeSelectionChanged();
    void onSearchTextChanged(const QString &text);
    void onSelectionManagerChanged(const QSet<SWMMObjectRef> &current,
                                   const QSet<SWMMObjectRef> &added,
                                   const QSet<SWMMObjectRef> &removed);
    void onContextMenuRequested(const QPoint &pos);

private:
    void buildUi();
    void clearTree();

    /*! Look up the leaf item for a given (type, name) — null if missing. */
    QTreeWidgetItem *itemFor(const SWMMObjectRef &ref) const;

    QLineEdit                      *m_searchEdit = nullptr;
    QTreeWidget                    *m_tree       = nullptr;
    QPointer<SWMMModelLayer>        m_layer;
    QPointer<SelectionManager>      m_selMgr;

    /*! Quick lookup: (type, name) → leaf item. Repopulated on refresh(). */
    QHash<SWMMObjectRef, QTreeWidgetItem *> m_index;

    /*! Reentrancy guard — true while we're applying SelectionManager
     *  changes to the tree (don't bounce them back). */
    bool                            m_applyingFromBus = false;
};

#endif // OBJECTBROWSERPANEL_H
