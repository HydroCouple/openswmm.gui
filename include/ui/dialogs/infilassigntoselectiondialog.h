/**
 * @file      infilassigntoselectiondialog.h
 * @brief     "Assign Infiltration to Selection…" — GUI plan
 *            `workplans/INTEGRATED2D_GW_GUI_PLAN_2026-08-15.md` §3.5(4), phase GG0c.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * "Assign Infiltration to Selection…" — GUI plan
 * `workplans/INTEGRATED2D_GW_GUI_PLAN_2026-08-15.md` §3.5(4), phase GG0c.
 *
 * The mesh-editing toolbar's cell editor prescribes ONE parameter to a
 * selection. Infiltration is not one parameter: it is a method plus up to five
 * positional values plus a destination, and the values only mean anything
 * together. This dialog is the whole-row form, applied to the cells the user
 * picked on the map with MapToolPick2DCells.
 *
 * Two write targets, and the difference matters (engine D-I3):
 *
 *  - **Per-cell overrides** — mesh::pushCellInfilEdit, one `[2D_INFILTRATION]`
 *    row per cell. The command snapshots each cell's PROVENANCE, so undoing an
 *    assignment made over an inheriting cell restores INHERITANCE rather than
 *    a materialised copy carrying identical numbers.
 *  - **Region tag** — mesh::pushInfilDefaultsEdit, one
 *    `[2D_INFILTRATION_DEFAULTS]` row. Offered only when every selected cell
 *    shares one non-empty tag, because that is the only case where "these
 *    cells" and "this region" mean the same thing. This is the in-map route to
 *    editing a default instead of minting overrides, so a later region-level
 *    edit still reaches the cells.
 *
 * Parameter fields are masked by the chosen method through
 * mesh::infilUsesParam(). Every destination is selectable and carries a
 * mesh::infilDestHint() tooltip — the engine routes all three, and whether
 * `AQUIFER_2D` works depends on the open model carrying a `[2D_AQUIFER]`
 * rather than on the release, so the engine judges it at resolve. Same two
 * rules the attribute table and the region-defaults table follow.
 *
 * Singleton-raise per `[[feedback_mvc_synchronized_uis]]`: showFor() keeps a
 * `static QPointer` so the toolbar action and the menu mirror raise one window
 * rather than stacking two views over the same selection.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_INFILASSIGNTOSELECTIONDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_INFILASSIGNTOSELECTIONDIALOG_H

#include "mesh/meshinfil.h"

#include <QDialog>
#include <QPointer>
#include <QString>
#include <QVector>

class MapCanvas;
class SelectionManager;
class SWMM2DMeshLayer;

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QRadioButton;

namespace openswmmvis::ui
{

class InfilAssignToSelectionDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief Create or rebind and raise the session's guarded infiltration-assignment dialog.
     * @param[in] meshLayer Borrowed mesh target; null returns without opening a dialog.
     * @param[in] canvas Borrowed optional canvas used by mesh commands for undo; never
     * reparented or owned.
     * @param[in] selection Borrowed optional selection bus; null gives an empty selection and
     * disables Apply.
     * @param[in] depthUnitLabel Copied display label for length-valued parameter fields; does
     * not convert numeric values.
     * @param[in] parent Optional Qt owner used only on initial creation; raising an existing
     * instance does not change its parent.
     * @details GUI-thread operation. New instances delete themselves on close; QPointer clears
     * the singleton. An existing instance disconnects the old selection before binding the new
     * project.
     *
     * @details Create-or-raise the one instance, bound to \p meshLayer.
     *
     *  Re-binds an existing instance to the (possibly different) layer /
     *  canvas / selection before raising it, so switching project windows
     *  cannot leave the dialog writing into the previous project's mesh.
     */
    static void showFor(SWMM2DMeshLayer *meshLayer, MapCanvas *canvas, SelectionManager *selection,
                        const QString &depthUnitLabel, QWidget *parent);

    /**
     * @brief Build a modal whole-row infiltration editor bound to guarded project objects.
     * @param[in] meshLayer Borrowed nullable target mesh; guarded so destruction disables useful
     * selection/apply operations.
     * @param[in] canvas Borrowed nullable canvas supplying the undo destination to command
     * helpers.
     * @param[in] selection Borrowed nullable selection bus; selection changes refresh the form.
     * @param[in] depthUnitLabel Copied presentation label, without numerical unit conversion.
     * @param[in] parent Optional Qt parent owning the dialog and its child controls.
     * @details Creates controls and initially seeds homogeneous selected values. All widget use
     * and mesh edits run on the GUI thread; this constructor does not itself enable
     * delete-on-close.
     */
    InfilAssignToSelectionDialog(SWMM2DMeshLayer *meshLayer, MapCanvas *canvas,
                                 SelectionManager *selection, const QString &depthUnitLabel,
                                 QWidget *parent = nullptr);

    /**
     * @brief Reconnect the editor to a different project and refresh selection-dependent fields.
     * @param[in] meshLayer New borrowed nullable mesh stored through QPointer.
     * @param[in] canvas New borrowed nullable undo canvas stored through QPointer.
     * @param[in] selection New borrowed nullable bus; old selection-change connection is removed
     * before reconnecting.
     * @param[in] depthUnitLabel New copied field-label text; values are not converted.
     * @details Refresh resets method-used controls to registry defaults before homogeneous
     * resolved cell values are restored; heterogeneous selection can retain the remaining draft
     * values.
     *
     * @details Point the dialog at another project's mesh / canvas / selection.
     */
    void rebind(SWMM2DMeshLayer *meshLayer, MapCanvas *canvas, SelectionManager *selection,
                const QString &depthUnitLabel);

private slots:
    /**
     * @brief Reset visible parameter controls to the selected method's registry defaults.
     * @details Changes only the form; no mesh command runs until Apply.
     */
    void onMethodChanged();
    /**
     * @brief Apply the current whole row to selected cells or their common region default.
     * @details Rechecks mesh lifetime, keyed valid cell selection and common-tag availability.
     * Invalid context reports status without writing. Region assignment can affect inheriting
     * cells outside the selection; per-cell assignment snapshots provenance so undo restores
     * inheritance. Commands use the canvas undo route when available, otherwise the command
     * helper's direct route. Used values are passed in their registry/model units without
     * conversion; unused slots remain NaN. Reports changed/no-op counts and keeps the dialog
     * open; engine destination prerequisites are resolved later.
     */
    void onApply();
    /**
     * @brief Refresh selected-cell count, target availability and homogeneous form values.
     * @details Responds to the bound bus on the GUI thread; does not change mesh data.
     */
    void onSelectionChanged();

private:
    /**
     * @brief Create owned method/parameter/destination controls and command connections once.
     * @details Offers all destination types with engine-prerequisite hints, creates cell/default
     * target choices and initializes registry-based fields. Requires the dialog's child-control
     * pointers not to have been populated by a previous call.
     */
    void buildUi();

    /**
     * @brief Collect deterministic valid triangle-cell indices from the bound mesh's selection.
     * @return Ascending indices parsed from MeshCell references whose layer key matches the
     * target source path and whose index is in the current triangle array. Returns empty when
     * mesh/selection is absent; other meshes and invalid references are ignored.
     *
     * @details Selected MeshCell triangle indices, filtered to the bound
     *  layer's mesh::MeshObjectRef::layerKey — the same filter
     *  MeshEditingToolbar::onSelectionChanged() applies, so a project with two
     *  meshes cannot write one mesh's selection into the other. Sorted, so the
     *  undo entry is reproducible.
     */
    [[nodiscard]] QVector<int> selectedCells() const;

    /**
     * @brief Find a single nonempty tag shared by all currently selected target cells.
     * @return Copied tag, or empty for absent mesh, empty selection, an untagged cell or mixed
     * tags; does not require every cell in the region to be selected.
     *
     * @details The tag every selected cell carries, or an empty string when the
     *  selection is empty, untagged, or spans more than one tag.
     */
    [[nodiscard]] QString commonTag() const;

    /**
     * @brief Read the initialized form as a complete infiltration row without applying it.
     * @return Value copy of method, destination and used spin-box values in registry/model
     * units; unused positional slots retain NaN, serialized as unset. No engine prerequisite
     * validation occurs.
     *
     * @details The form's current contents as one mesh::InfilRow. Slots the
     *  method does not use stay NaN.
     */
    [[nodiscard]] mesh::InfilRow currentRow() const;

    /**
     * @brief Decode the initialized method combo's current user-data value.
     * @return InfilMethod enum cast from currentData().toInt(); no additional range validation
     * is performed.
     */
    [[nodiscard]] mesh::InfilMethod currentMethod() const;

    /**
     * @brief Show method-used slots and reseed recognized fields from the shared parameter
     * registry.
     * @details Updates names, display unit labels, bounds, decimals, steps and tooltips with
     * spin signals blocked. Recognized used slots reset to defaults; unused slots hide and
     * unrecognized specs leave existing numerical configuration alone.
     *
     * @details Re-title / re-range / show-hide the five parameter rows for the
     *  chosen method, seeding each from mesh::cellParamSpecs().
     */
    void refreshParamFields();

    /**
     * @brief Update selected count, region target and Apply availability, seeding uniform
     * resolved rows.
     * @details A common nonempty tag enables region defaults even if other cells in that region
     * are not selected. For homogeneous resolved rows, sets method/destination and finite used
     * parameters with signals blocked; unset values keep registry defaults. Apply requires a
     * live mesh and a nonempty filtered selection.
     *
     * @details Refresh the "N cells" caption, the region-tag radio's label and
     *  availability, and the Apply button.
     */
    void refreshSelectionState();

    QPointer<SWMM2DMeshLayer> m_mesh;
    QPointer<MapCanvas> m_canvas;
    QPointer<SelectionManager> m_selection;
    QString m_depthUnitLabel;

    QLabel *m_selectionLbl = nullptr;
    QComboBox *m_methodCombo = nullptr;
    QLabel *m_paramLabels[mesh::kInfilMaxParams] = {};
    QDoubleSpinBox *m_paramSpins[mesh::kInfilMaxParams] = {};
    QComboBox *m_destCombo = nullptr;
    QRadioButton *m_writeCells = nullptr;
    QRadioButton *m_writeTag = nullptr;
    QLabel *m_statusLbl = nullptr;
    QPushButton *m_applyBtn = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_INFILASSIGNTOSELECTIONDIALOG_H
