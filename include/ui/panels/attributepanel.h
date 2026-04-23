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

public slots:

    void onIdentifyResult(const QList<IdentifyResult> &results);

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

private slots:
    void onLayerComboIndexChanged(int index);

private:
    void setupUi();

    QTreeView              *m_treeView       = nullptr;
    QComboBox              *m_layerCombo     = nullptr;
    QAbstractItemModel     *m_model          = nullptr;
    QAbstractItemDelegate  *m_delegate       = nullptr;

    QList<IdentifyResult>   m_lastResults;
};

#endif // ATTRIBUTEPANEL_H
