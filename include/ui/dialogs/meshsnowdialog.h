// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      meshsnowdialog.h
 * @brief     Configuration of snow packs and forcing for mesh cells.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once
#include <QDialog>
#include <QPointer>
#include <QStringList>
#include <array>
#include <openswmm/engine/openswmm_engine.h>
class SWMMModelLayer;
class SWMM2DMeshLayer;
class MapCanvas;
class SelectionManager;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QTableView;
class QTableWidget;
class QPushButton;
namespace openswmmvis::ui
{
class MeshSnowModel;
class MeshSnowDialog final : public QDialog
{
public:
    /**
     * @brief Build a staged mesh-snow editor and load its engine configuration snapshot.
     * @param[in] model Borrowed nullable model, guarded through QPointer; editing requires its captured engine to remain live and unchanged.
     * @param[in] mesh Borrowed nullable mesh, guarded through QPointer; editing requires its captured geometry revision to remain unchanged.
     * @param[in] canvas Borrowed nullable canvas, guarded through QPointer; a live undo stack is required before Apply can be enabled.
     * @param[in] selection Borrowed optional selection bus, guarded through QPointer; selected-cell scope is empty without it.
     * @param[in] parent Optional Qt owner of the dialog and controls.
     * @details Captures engine identity and mesh geometry revision, loads row/default/options
     * text and pack names, and preserves the exact loaded UEB step until the displayed value is
     * changed. Uses GUI-thread widgets and engine access; assignment changes remain local until
     * apply(), whereas the separate snowpack editor edits its shared registry.
     */
    MeshSnowDialog(SWMMModelLayer *, SWMM2DMeshLayer *, MapCanvas *, SelectionManager *,
                   QWidget *parent = nullptr);

private:
    /**
     * @brief Validate staged snow rows and refresh effective-cell previews and Apply state.
     * @details Copies mode/UEB step controls into the draft, preserving an unchanged rounded
     * interval's original precision. Resolves cell override, region and whole-mesh inheritance,
     * displays at most 200 filtered rows and counts assigned/UEB/degree-day cells. Context or
     * model validation errors disable Apply. Global ignore/mode settings affect reported planned
     * activity; eligible coverage/weather are checked at run initialization, not proven here.
     * Does not write engine configuration.
     */
    void preview();
    /**
     * @brief Install a validated staged snow configuration and register undo for changed text.
     * @details Calls preview(), rejects engine/mesh/state changes and refuses a snapshot
     * differing from the loaded configuration. Applies all three sections through MeshSnowModel,
     * rereads actual stored text and pushes a SnowUndo whose first redo is skipped because
     * installation already happened. Failure to reread after writing marks the model dirty but
     * cannot create the undo snapshot. Successful application updates the baseline, clears
     * sampling provenance, marks edited and emits optionsChanged even for unchanged text; errors
     * stay in the status label.
     */
    void apply();
    /**
     * @brief Stage a snowpack row for the chosen cell, tag or whole-mesh scope.
     * @details Requires a currently editable context. Selected-cell scope parses only references
     * matching this mesh's source-path key; tag scope requires a nonempty chosen tag. Upsert
     * operates on local rows and then refreshes preview; no engine write occurs until apply().
     */
    void addAssignment();
    /**
     * @brief Open the shared snowpack registry editor, then reload available pack names.
     * @details Requires a live editable context and registry. Executes a modal child editor
     * whose pack edits are independent of staged mesh assignments, rebuilds the
     * NONE-plus-engine-pack choices, attempts to restore the selected pack text and refreshes
     * the draft preview.
     */
    void editPacks();
    /**
     * @brief Drop the borrowed engine handle and disable assignment controls after closure.
     * @details Disables Apply and row editing and reports that the editor must reopen. Keeps
     * staged values rather than writing or automatically closing the dialog.
     */
    void invalidate();
    /**
     * @brief Preview GIS samples and stage accepted mesh-snow property or pack assignments.
     * @details Opens a modal child workflow over loaded raster/vector sources, requiring
     * explicit mesh/source CRS and a nonempty chosen cell scope. Numeric properties use native
     * model units without automatic physical-unit conversion; categorical classes require a
     * unique existing-pack/NONE lookup and Majority or Centroid sampling. Runs a copied sampling
     * job in QtConcurrent while a nested GUI event loop shows cancellable progress;
     * cancellation/result/context errors prevent staging. Shows at most 500 changed rows, then
     * rechecks context and unchanged draft text before stageSamples() changes local rows and
     * records provenance. Actual engine writes wait for the parent editor's Apply.
     */
    void fillFromLayer();
    /**
     * @brief Check captured model, mesh geometry and pre-initialization editability.
     * @return Empty when guarded model/mesh/canvas and undo stack remain valid, engine identity
     * and geometry revision match, and engine state is OPENED or BUILDING. Otherwise a
     * translated reopen/reset explanation; performs no mutations.
     */
    QString contextError() const;
    QPointer<SWMMModelLayer> model_;
    QPointer<SWMM2DMeshLayer> mesh_;
    QPointer<MapCanvas> canvas_;
    QPointer<SelectionManager> selection_;
    SWMM_Engine engine_ = nullptr;
    quint64 geometry_ = 0;
    MeshSnowModel *rows_ = nullptr;
    QComboBox *mode_ = nullptr, *scope_ = nullptr, *tag_ = nullptr, *pack_ = nullptr;
    QDoubleSpinBox *step_ = nullptr;
    QLabel *status_ = nullptr;
    QTableView *table_ = nullptr;
    QTableWidget *resolved_ = nullptr;
    double loaded_step_ = 0, displayed_step_ = 0;
    QPushButton *apply_ = nullptr;
    std::array<QString, 3> before_;
    QStringList sampledSources_;
};
}
