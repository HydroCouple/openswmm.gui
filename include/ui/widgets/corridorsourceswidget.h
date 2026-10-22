/**
 * @file      corridorsourceswidget.h
 * @brief     Editing of selected-feature recipes for mesh corridor generation.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMMVIS_CORRIDORSOURCESWIDGET_H
#define OPENSWMMVIS_CORRIDORSOURCESWIDGET_H

#include "mesh/corridorsource.h"
#include <QPointer>
#include <QWidget>

class GISVectorLayer;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

// Authors selected-feature recipes only. Geometry is read by the pipeline worker.
class CorridorSourcesWidget : public QWidget
{
    Q_OBJECT
public:
    /**
     * @brief Create the selected-feature corridor recipe editor without reading feature geometries.
     * @param[in] parent Optional QWidget owner; GIS layer/schema access and edits occur on the GUI thread.
     */
    explicit CorridorSourcesWidget(QWidget *parent = nullptr);
    /**
     * @brief Replace available borrowed GIS layers and reconnect selection/name/destruction notifications.
     * @param[in] layers Null/duplicate pointers are ignored and remaining pointers are guarded by QPointer. Prior current selection is retained when possible.
     * @note Does not alter stored recipes; a missing map layer remains represented in the source table.
     */
    void setLayers(const QList<GISVectorLayer *> &layers);
    /**
     * @brief Replace the captured recipe table from stored metadata.
     * @param[in] sources Value records copied without complete validation; table edit signals are blocked and sourcesChanged() is not emitted.
     */
    void setSources(const QVector<mesh::CorridorSource> &sources);
    /**
     * @brief Validate visible recipe edits and collect a complete source snapshot.
     * @param[out] out Required destination, assigned only after every row validates.
     * @param[out] error Optional first diagnostic; cleared on success.
     * @return True clears guidance; false leaves out unchanged and focuses/reveals the offending control/table row.
     * @note Width/along spacing use mesh CRS units, width is positive finite, across is a positive int, and along is finite nonnegative. Bank pairs require two distinct nonnegative IDs and no width field. Numeric width-field availability is checked only for a matching loaded layer; geometry/file validation is deferred to generation. Changed width fields clear the captured geometry digest in the returned copy.
     */
    bool sources(QVector<mesh::CorridorSource> *out, QString *error = nullptr) const;
signals:
    /**
     * @brief Announce a user table edit, accepted selected-feature addition or removal; programmatic setSources() does not emit.
     */
    void sourcesChanged();

private:
    /**
     * @brief Resolve the current combo payload into the guarded layer list.
     * @return Borrowed live layer or null for an invalid/missing selection.
     */
    GISVectorLayer *currentLayer() const;
    /**
     * @brief Match a stored recipe to currently available path/sublayer spellings.
     * @param[in] source Stored source metadata.
     * @return First borrowed exact path and OGR-layer-name match, or null; no canonical/symlink matching occurs.
     */
    GISVectorLayer *matchingLayer(const mesh::CorridorSource &source) const;
    /**
     * @brief Rebuild layer choices with signals blocked, preserve the current pointer when possible and refresh fields/selection/source labels.
     */
    void refreshLayers();
    /**
     * @brief Rebuild numeric width-field choices from schema, retaining prior selection when possible.
     * @note Corridor-role FeatureLayer defaults to its standard width field when no prior matching field exists; does not fetch geometries.
     */
    void refreshFields();
    /**
     * @brief Enable width controls for centreline uniform/field modes, disable them for bank pairs and refresh along-spacing help.
     */
    void syncSourceMode();
    /**
     * @brief Display the current selected-feature count and bank-pair/empty-selection guidance without capturing new IDs.
     */
    void updateSelectionMessage();
    /**
     * @brief Refresh captured path/sublayer labels and mark recipes whose layer is no longer on the map.
     * @note Updates only display/tooltip/accessibility roles with table signals blocked.
     */
    void updateSourceLabels();
    /**
     * @brief Append a captured recipe and its editable metadata cells with table signals blocked.
     * @param[in] source Source snapshot copied into storage; identity/bank-width columns are made read-only. No complete validation or sourcesChanged() emission occurs here.
     */
    void appendSource(const mesh::CorridorSource &source);
    /**
     * @brief Validate the current selection/configuration, capture sorted feature IDs and source CRS, append the recipe and emit sourcesChanged().
     * @note Requires source CRS, selected IDs (exactly two for banks), a width choice, positive across count and nonnegative spacing. Failure displays/focuses a diagnostic; no geometry is read.
     */
    void addSelection();
    /**
     * @brief Remove selected recipe rows in descending order and emit sourcesChanged() only when rows were removed.
     */
    void removeSelection();
    /**
     * @brief Display a validation error and reveal/focus its optional offending control.
     * @param[in] message Diagnostic shown in the widget.
     * @param[in] field Optional control; ancestors' tab/scroll state may change to reveal it.
     * @param[out] error Optional diagnostic copy.
     * @return Always false; logically const but changes presentation/focus.
     */
    bool fail(const QString &message, QWidget *field, QString *error) const;
    /**
     * @brief Select and reveal a recipe cell with a one-based row diagnostic.
     * @param[in] row Existing zero-based table row.
     * @param[in] column Existing table column.
     * @param[in] message Row-specific reason.
     * @param[out] error Optional diagnostic copy.
     * @return Always false; table current cell and focus/scroll may change.
     */
    bool failRow(int row, int column, const QString &message, QString *error) const;

    QVector<QPointer<GISVectorLayer>> m_layers;
    QVector<QMetaObject::Connection> m_layerConnections;
    QVector<mesh::CorridorSource> m_sources;
    QComboBox *m_sourceMode = nullptr;
    QComboBox *m_layer = nullptr;
    QComboBox *m_widthMode = nullptr;
    QComboBox *m_widthField = nullptr;
    QLineEdit *m_width = nullptr;
    QLineEdit *m_across = nullptr;
    QLineEdit *m_along = nullptr;
    QLineEdit *m_tag = nullptr;
    QLabel *m_selection = nullptr;
    QLabel *m_message = nullptr;
    QPushButton *m_add = nullptr;
    QPushButton *m_remove = nullptr;
    QTableWidget *m_table = nullptr;
};
#endif
