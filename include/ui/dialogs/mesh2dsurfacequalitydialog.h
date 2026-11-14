/**
 * @file      mesh2dsurfacequalitydialog.h
 * @brief     S7 GUI half (2026-10-30) — the mesh surface-quality row editor: `[2D_COVERAGES]`,
 *            `[2D_LOADINGS]`, `[2D_CURB_LENGTH]` and `[2D_INITIAL_QUALITY]`.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * One tab per section, each a Mesh2DSectionRowModel (MVC, CLAUDE.md §5.1) staged until Apply
 * or OK, which commit every changed section and restore the earlier ones if a later one fails.
 * Rows follow the GLOBAL (*) < TAG < CELL scope model; the spatial assignment of these rows from
 * selections, feature layers or rasters is a separate tool (C8 Phase 3b).
 *
 * The engine refuses these edits once a run has started, so the dialog reads the lifecycle up
 * front and opens read-only with the reason instead of letting Apply fail.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_MESH2DSURFACEQUALITYDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_MESH2DSURFACEQUALITYDIALOG_H

#include <QDialog>
#include <QList>
#include <QPair>

#include <openswmm/engine/openswmm_engine.h>

class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTabWidget;
class QTableView;
class QWidget;
class UnitSystem;

namespace openswmmvis::ui
{

class Mesh2DSectionRowModel;
class Mesh2DBoundaryQualityModel;
class Mesh2DSectionRowEditor;

class Mesh2DSurfaceQualityDialog : public QDialog
{
    Q_OBJECT
public:
    /**
     * @param[in] engine Borrowed engine, valid until invalidateContext() or destruction; null
     * opens an empty read-only dialog.
     * @param[in] parent Optional owner.
     * @param[in] units Borrowed optional unit system for the loading and length labels (SI when
     * null); a unit change invalidates the dialog.
     */
    explicit Mesh2DSurfaceQualityDialog(SWMM_Engine engine, QWidget *parent = nullptr,
                                        const UnitSystem *units = nullptr);

    /**
     * @brief Open the Boundary inflow tab on selected edges (process-boundaries Phase 4d).
     * @details Shows how many of @p edges are exterior, interior or not on the mesh, selects the
     *   rows those edges already have, and offers to set one species on the exterior ones.
     * @param[in] edges (cell, local edge) pairs, typically the map's edge selection.
     */
    void setTargetEdges(const QList<QPair<int, int>> &edges);

public slots:
    /** Drop the borrowed engine and close; earlier Applies stay applied. */
    void invalidateContext();

signals:
    /** Writes are about to start (a later failure may leave them partly applied). */
    void changesMayHaveBeenApplied();
    /** Every changed section was written. */
    void applied();

private:
    bool applyChanges();
    void setEditable(bool on, const QString &whyNot);

    SWMM_Engine m_engine = nullptr;
    bool m_editable = false;
    QLabel *m_banner = nullptr;
    QTabWidget *m_tabs = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
    QList<Mesh2DSectionRowModel *> m_models;
    QList<Mesh2DSectionRowEditor *> m_editors;
    // Boundary inflow tab (Phase 4d).
    Mesh2DBoundaryQualityModel *m_boundary = nullptr;
    QWidget *m_boundaryPage = nullptr;
    QTableView *m_boundaryTable = nullptr;
    QWidget *m_targetBar = nullptr;
    QLabel *m_targetSummary = nullptr;
    QComboBox *m_targetSpecies = nullptr;
    QLineEdit *m_targetValue = nullptr;
    QPushButton *m_targetSet = nullptr;
    QPushButton *m_addRow = nullptr;
    QPushButton *m_removeRow = nullptr;
    QList<QPair<int, int>> m_targetExterior;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_MESH2DSURFACEQUALITYDIALOG_H
