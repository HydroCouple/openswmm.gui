/*!
 * \file   attributepanel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \version
 * \description
 * \license
 * \copyright
 * \date 2026
 */

#ifndef ATTRIBUTEPANEL_H
#define ATTRIBUTEPANEL_H

#include <QDockWidget>
#include <QList>
#include <QVariantMap>

class QTreeView;
class QComboBox;
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

/*!
 * \class AttributePanel
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
class AttributePanel : public QDockWidget
{
    Q_OBJECT

public:

    explicit AttributePanel(QWidget *parent = nullptr);
    ~AttributePanel() override;

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

private:
    void setupUi();

    QTreeView                *m_treeView       = nullptr;
    QComboBox                *m_layerCombo     = nullptr;
    QAbstractItemModel       *m_model          = nullptr;
    QAbstractItemDelegate    *m_delegate       = nullptr;

    QList<IdentifyResult>     m_lastResults;

    // Slice Z.5.3 / AG.3 — engine binding for typed adapters.
    SWMMModelLayer              *m_swmmLayer     = nullptr;
    SWMMNodePropertyAdapter     *m_nodeAdapter   = nullptr;
    SWMMLinkPropertyAdapter     *m_linkAdapter   = nullptr;
    SWMMSubcatchPropertyAdapter *m_subcatchAdapter = nullptr;
    // Slice DA.2 — single cached adapter for the 14 non-spatial Data
    // Object kinds. Replaced on every selection; the previous instance
    // is `deleteLater()`d so the QPropertyModel stops referencing it
    // before the new one binds.
    SWMMDataObjectPropertyAdapter *m_dataAdapter = nullptr;

    bool m_suppressEditForward = false; ///< Set during onObjectEditedExternally to break loop.
};

#endif // ATTRIBUTEPANEL_H
