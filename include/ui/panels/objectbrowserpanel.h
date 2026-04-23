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
class MapCanvas;
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
     * \brief Bind the panel to a project's model layer + selection bus + canvas.
     * \param layer    SWMM model layer for the active project (or nullptr).
     * \param selMgr   SelectionManager owned by the same project (or nullptr).
     * \param canvas   MapCanvas for zoom-to-object actions (or nullptr to
     *                 disable them).
     */
    void setProject(SWMMModelLayer *layer,
                    SelectionManager *selMgr,
                    MapCanvas *canvas = nullptr);

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

    /*! \brief Emitted when the user toggles a single leaf row's checkbox.
     *         The SWMMVis main window forwards it to the layer's
     *         setObjectVisible(). */
    void objectVisibilityChanged(const SWMMObjectRef &object, bool visible);

    /*! \brief Emitted when the user toggles a group header — the parent
     *         state propagates down to every leaf under the group. The
     *         SWMMVis main window forwards it to the layer's batch
     *         setObjectsVisible(), so a single group-toggle causes one
     *         canvas refresh regardless of how many objects are in the
     *         group. The leaf's check state is NOT propagated back to the
     *         group header: subsequent individual toggles leave the
     *         parent as-is. */
    void objectsVisibilityChanged(const QStringList &names, bool visible);

private slots:
    void onTreeSelectionChanged();
    void onSearchTextChanged(const QString &text);
    void onSelectionManagerChanged(const QSet<SWMMObjectRef> &current,
                                   const QSet<SWMMObjectRef> &added,
                                   const QSet<SWMMObjectRef> &removed);
    void onContextMenuRequested(const QPoint &pos);
    void onItemDoubleClicked(QTreeWidgetItem *item, int column);
    void onItemChanged(QTreeWidgetItem *item, int column);

private:
    void buildUi();
    void clearTree();

    /*! Look up the leaf item for a given (type, name) — null if missing. */
    QTreeWidgetItem *itemFor(const SWMMObjectRef &ref) const;

    /*! Centre the canvas on a single object and zoom to a small buffer
     *  derived from the layer's overall extent. No-op if \p canvas is null
     *  or the object coord can't be resolved via identifyByName. */
    void zoomToObject(const SWMMObjectRef &ref);

    QLineEdit                      *m_searchEdit = nullptr;
    QTreeWidget                    *m_tree       = nullptr;
    QPointer<SWMMModelLayer>        m_layer;
    QPointer<SelectionManager>      m_selMgr;
    QPointer<MapCanvas>             m_canvas;

    /*! Quick lookup: (type, name) → leaf item. Repopulated on refresh(). */
    QHash<SWMMObjectRef, QTreeWidgetItem *> m_index;

    /*! Reentrancy guard — true while we're applying SelectionManager
     *  changes to the tree (don't bounce them back). */
    bool                            m_applyingFromBus = false;

    /*! Reentrancy guard — true while we're seeding leaf / header check
     *  states during a programmatic refresh (don't bounce user-driven
     *  handlers). */
    bool                            m_applyingGroupCheck = false;
};

#endif // OBJECTBROWSERPANEL_H
