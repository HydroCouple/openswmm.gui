/*!
 * \file   propertiespanel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \version
 * \description
 * \license
 * \copyright
 * \date 2026
 */

#ifndef PROPERTIESPANEL_H
#define PROPERTIESPANEL_H

#include "layers/swmmmodellayer.h"

#include <QDockWidget>
#include <QList>
#include <QVariantMap>

class QTreeView;
class QComboBox;
class QPushButton;
class QAbstractItemModel;
class QAbstractItemDelegate;
#ifdef HAVE_QPROPERTYMODEL
class QPropertyModel;     // from QPropertyModel library
class QPropertyItemDelegate;
#endif
class OpenSWMMVisLayer;
class SWMMModelLayer;
class SWMMNodePropertyAdapter;
class SWMMLinkPropertyAdapter;
class SWMMSubcatchPropertyAdapter;
class SWMMDataObjectPropertyAdapter;
struct IdentifyResult;

namespace openswmmvis { class OutputStatsRegistry; }    // Slice QA.2

/*!
 * \class PropertiesPanel
 * \brief A dock widget that displays and edits object attributes using QPropertyModel.
 * \details The panel shows:
 *  - The attributes of identified (clicked) features or SWMM elements.
 *  - The properties of the currently selected map layer.
 *
 *  It uses QPropertyModel / QPropertyItemDelegate so any Q_PROPERTY exposed
 *  by a QObject (layer, feature, SWMM element) is automatically displayed
 *  with the correct editor widget (check box, spin box, colour picker, …).
 *
 *  The panel has a layer drop-down so the user can switch between the
 *  properties of any of the layers that returned results in the last
 *  identify operation.
 */
class PropertiesPanel : public QDockWidget
{
    Q_OBJECT

public:

    explicit PropertiesPanel(QWidget *parent = nullptr);
    ~PropertiesPanel() override;

    // ----- Content -------------------------------------------------------

    /*!
     * \brief Switches the panel to show the Q_PROPERTY attributes of \p object.
     * \param object  Any QObject whose Q_PROPERTYs should be displayed.
     *                Pass nullptr to clear the panel.
     * \param title   Optional title shown in the tab or header.
     */
    void showObject(QObject *object, const QString &title = {});

    /*!
     * \brief Shows the attribute results from an identify operation.
     * \param results  One IdentifyResult per layer.
     */
    void showIdentifyResults(const QList<IdentifyResult> &results);

    /*!
     * \brief Shows the editable properties of the given layer.
     */
    void showLayerProperties(OpenSWMMVisLayer *layer);

    /*!
     * \brief Clears the panel.
     */
    void clear();

    /*! Slice Z.5.3 — bind to the active project's model layer so
     *  identify results that resolve to a SWMM node can be shown
     *  via a typed `SWMMNodePropertyAdapter` (editable via existing
     *  QPropertyModel auto-delegates).  Pass nullptr to detach. */
    void setProject(SWMMModelLayer *layer);

    /*! Slice QA.2 — bind the per-project OutputStatsRegistry so the
     *  Stats-source combo above the property tree can populate from
     *  every loaded SWMMResultsLayer. Pass nullptr to hide the combo
     *  (matches today's behaviour for projects with no results loaded
     *  yet). Idempotent — repeated calls re-wire signals safely. */
    void setStatsRegistry(openswmmvis::OutputStatsRegistry *registry);

    /*! Slice DA.2 — show a non-spatial Data Object (curve, time series,
     *  pattern, LID control, pollutant, land use, aquifer, snowpack,
     *  control rule, transect, hydrograph group, street, inlet, or
     *  rain gage) using the typed property adapter for its kind.
     *
     *  Bypasses the identify-result flow; called directly when an
     *  Object Browser leaf is clicked. The kind value is an
     *  `SWMMObjectRef::ObjectType` (cast to int to keep the header
     *  forward-declaration-friendly). */
    void showDataObject(SWMMModelLayer *layer, int objectKind,
                          const QString &name);

public slots:

    void onIdentifyResult(const QList<IdentifyResult> &results);

    /*! Round-4 follow-up 2026-05-12 — refresh the bound adapter if
     *  its object name matches \p name, so a table-side edit is
     *  reflected in the Property Browser.  No-op when no adapter is
     *  bound or the name doesn't match. */
    void onObjectEditedExternally(const QString &name);

    /*! LINK_OFFSETS mode changed: a bound link's offset rows are labelled
     *  "Inlet/Outlet Offset" vs "… Elevation", and the label is captured when
     *  the adapter is bound, so replay the bind. No-op unless a link is shown. */
    void onOffsetModeChanged();

signals:

    /*!
     * \brief Emitted when the user changes a property value in the view.
     * \param object       The source QObject.
     * \param propertyName Q_PROPERTY name.
     * \param oldValue     Previous value.
     * \param newValue     New value.
     */
    void propertyChanged(QObject *object,
                         const QString &propertyName,
                         const QVariant &oldValue,
                         const QVariant &newValue);

    /*! Round-4 follow-up 2026-05-12 — emitted after a property-
     *  browser edit so the Attribute Table dock can mirror the
     *  change.  Wired in `SWMMVis`.  Suppressed when the adapter's
     *  `changed()` was triggered by `onObjectEditedExternally`. */
    void objectEdited(const QString &name);

private slots:
    void onLayerComboIndexChanged(int index);

    /*! Slice BM.0-Browse-Edit (2026-05-25) — right-click on a property
     *  row. For `DataObjectRef` rows: builds "Edit <name> in <editor>…"
     *  routed through ComprehensiveEditorRegistry (greyed-out with gap
     *  tooltip when the editor hasn't shipped yet). For
     *  `NodeCompoundEditRef` rows: single "Edit…" launching
     *  NodeCompoundEditDialog. Otherwise: no menu pops. */
    void onTreeContextMenu(const QPoint &pos);

    /*! 2026-05-29 — Header "Open in <Editor>…" button click. Routes the
     *  active data adapter's category through the shared object-browser
     *  open-for-edit dispatch (`ObjectBrowserPanel::openComprehensiveEditorFor`)
     *  so the attribute-panel button and the object-browser leaf
     *  double-click/right-click share one editor instance. No-op when
     *  no data adapter is bound or its category has no shipped editor. */
    void onOpenInEditorClicked();

private:
    bool eventFilter(QObject *watched, QEvent *event) override;

    void setupUi();

    /*! Slice QA.2 — repopulate the Stats-source combo from the bound
     *  registry's current identities(). Called whenever the registry
     *  emits identitiesChanged() AND when the node adapter changes
     *  (so a node selection refreshes the combo state). Preserves the
     *  current selection by stableId when possible. */
    void refreshStatsSourceCombo();

    /*! Slice QA.2 — push the combo's current selection into the bound
     *  node adapter (idempotent; no-op when the node adapter is null
     *  or the combo's payload is missing). */
    void applyStatsSourceToAdapter();

    QTreeView                *m_treeView       = nullptr;
    QComboBox                *m_layerCombo     = nullptr;
    QComboBox                *m_statsSourceCombo = nullptr;  ///< Slice QA.2
    class QLabel             *m_statsSourceLabel = nullptr;  ///< Slice QA.2
    /*! 2026-05-29 — Header button that opens the active data adapter's
     *  category in its dedicated CRUD editor. Hidden unless a data
     *  adapter is bound to a category with a shipped editor. */
    QPushButton              *m_openEditorButton = nullptr;
    /*! 2026-05-29 — DataCategory of the currently shown data adapter,
     *  or `NumDataCategories` when the panel is not showing a data
     *  object (spatial selection, layer properties, identify, or
     *  cleared). Drives `m_openEditorButton` visibility. */
    SWMMModelLayer::DataCategory m_dataObjectCategory =
        SWMMModelLayer::NumDataCategories;
    QAbstractItemModel       *m_model          = nullptr;
    QAbstractItemDelegate    *m_delegate       = nullptr;

    QList<IdentifyResult>     m_lastResults;

    // Slice Z.5.3 / AG.3 — engine binding for typed adapters.
    SWMMModelLayer              *m_swmmLayer     = nullptr;
    SWMMNodePropertyAdapter     *m_nodeAdapter   = nullptr;
    SWMMLinkPropertyAdapter     *m_linkAdapter   = nullptr;
    SWMMSubcatchPropertyAdapter *m_subcatchAdapter = nullptr;
    // SWMM_NodeType / SWMM_LinkType the bound adapter subclass was built
    // for, so an external type conversion (right-click "Convert To") can
    // detect the kind changed and rebind the correct subclass instead of
    // refreshing a now-wrong one. -1 when no adapter is bound.
    int                          m_nodeAdapterKind = -1;
    int                          m_linkAdapterKind = -1;
    // Slice DA.2 — single cached adapter for the 14 non-spatial Data
    // Object kinds. Replaced on every selection; the previous instance
    // is `deleteLater()`d so the QPropertyModel stops referencing it
    // before the new one binds.
    SWMMDataObjectPropertyAdapter *m_dataAdapter = nullptr;

    bool m_suppressEditForward = false; ///< Set during onObjectEditedExternally to break loop.

    /// Slice QA.2 — bound registry; null when no project is active. The
    /// panel never owns the registry (SWMMVisProjectWindow does).
    openswmmvis::OutputStatsRegistry *m_statsRegistry = nullptr;
};

#endif // PROPERTIESPANEL_H
