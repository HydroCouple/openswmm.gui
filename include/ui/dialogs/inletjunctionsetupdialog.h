/**
 * @file      inletjunctionsetupdialog.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Floating (non-modal) panel that collects the three things an inlet
 *         junction needs BEFORE it is created: an inlet design, a capture
 *         node, and a placement mode.
 *
 * Decision D-G6 (INLET_EDITOR_AND_INLET_JUNCTION_GUI_PLAN_2026-09-05.md §5):
 * ask first rather than insert-then-edit. The engine requires a design and a
 * capture node for the node to be valid, so a half-configured inlet junction
 * would fail validation on save — and the map tool has no way to "hold" an
 * invalid node while the user fills in the property panel.
 *
 * Non-modal (2026-09-10): the capture node can be picked ON THE MAP through
 * the "…" button next to its combo (a NodePickSession swaps in a one-shot
 * pick tool and restores the previous tool afterwards), which an
 * application-modal dialog would block. Callers therefore allocate the
 * dialog on the heap, connect accepted() and show() it; the split / convert
 * runs from that slot, never inside a mouse handler.
 *
 * The same dialog serves the Convert-To flow, which promotes an existing
 * junction instead of splitting a conduit.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_INLETJUNCTIONSETUPDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_INLETJUNCTIONSETUPDIALOG_H

#include <QDialog>
#include <QPointer>
#include <QString>
#include <QVector>

#include <openswmm/engine/openswmm_callbacks.h> // SWMM_Engine

class QComboBox;
class QDialogButtonBox;
class QLabel;
class LabeledPickerCombo;
class NodePickSession;
class SWMMModelLayer;

namespace openswmmvis::ui
{

class InletJunctionSetupDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief Build a nonmodal chooser for a new or converted inlet junction.
     * @param[in] layer Borrowed nullable model supplying engine, inlet registry and map canvas;
     * stored as a raw pointer and must outlive the dialog and its pick callbacks.
     * @param[in] excludeNodes Owned vector of zero-based nodes excluded as capture destinations,
     * moved into the dialog; usually host-link ends or the node being converted.
     * @param[in] parent Optional Qt owner of this floating panel and child controls.
     * @details Initializes design/node choices and placement, excluding virtual/inlet nodes from
     * receiver choices. Creates no node or usage row; the caller connects accepted() to its own
     * mutation. Use on the GUI thread with a live stable model context.
     */
    InletJunctionSetupDialog(SWMMModelLayer *layer, QVector<int> excludeNodes,
                             QWidget *parent = nullptr);
    /**
     * @brief End any active capture-node pick before Qt destroys owned controls.
     * @details Restores the previous map tool through endCaptureNodePick(); never owns the model
     * layer.
     */
    ~InletJunctionSetupDialog() override;

    /**
     * @brief Copy the selected design identity without creating an inlet.
     * @return Current picker text, or empty when its control is absent. OK is enabled only when
     * this and captureNode() are nonempty; programmatic accept() does not itself enforce that
     * gate.
     *
     */
    [[nodiscard]] QString inletDesign() const;
    /**
     * @brief Copy the selected capture-node identity without changing the engine.
     * @return Current picker text, or empty when its control is absent; names come from eligible
     * choices or the map picker.
     *
     */
    [[nodiscard]] QString captureNode() const;
    /**
     * @brief Read the selected placement enum ordinal.
     * @return SWMM_INLET_AUTOMATIC, SWMM_INLET_ON_GRADE or SWMM_INLET_ON_SAG from combo user
     * data; automatic if the control is absent.
     *
     * @details Chosen placement, as a `SWMM_InletPlacement` ordinal.
     */
    [[nodiscard]] int placement() const;

    /**
     * @brief Begin one nonmodal capture-node map-picking session.
     * @details No-ops without a model canvas or while a session is active; otherwise finishes
     * the previous session, creates a dialog-owned NodePickSession and connects
     * completion/cancellation. The session manages temporary map-tool replacement; eligible
     * picks populate only the local form.
     *
     * @details Start picking the capture node on the map (the "…" button's action).
     *  No-op when the layer has no edit canvas.
     */
    void startCaptureNodePick();
    /**
     * @brief Finish the current session, restore its previous map tool and clear the hint.
     * @details Safe when no session exists. Schedules the session for Qt deletion and
     * immediately clears the guarded pointer.
     *
     * @details Stop picking and restore the previous canvas tool.
     */
    void endCaptureNodePick();
    /**
     * @brief Query whether the guarded node-pick session remains active.
     * @return True only for a live session whose isActive() is true; no state changes.
     */
    [[nodiscard]] bool isPickingCaptureNode() const;

private slots:
    /**
     * @brief Enable OK exactly when both picker identity strings are nonempty.
     * @details Returns before button construction; does not independently re-resolve names or
     * validate placement against the engine.
     *
     * @details Enable OK only once both identity fields are set — the engine rejects
     *  a partial usage row, so an incomplete OK could only fail.
     */
    void updateOkEnabled();
    /**
     * @brief Open the model's inlet chooser constrained to street/gutter-compatible designs.
     * @details No-ops without model/registry or an empty returned identity; otherwise reloads
     * design choices and selects the chosen name. The child editor can create/edit registry
     * designs; this panel still creates no inlet junction.
     *
     * @details Open `InletEditorDialog::pickInlet` filtered to the gutter types and
     *  select the returned design.
     */
    void onDesignPickerClicked();
    /**
     * @brief Accept an eligible map-picked receiver and finish the pick session.
     * @param[in] layer Borrowed layer reporting the pick; must equal the bound model to be
     * accepted.
     * @param[in] name Borrowed node ID copied into the capture picker after eligibility checks.
     * @param[in] nodeIdx Valid zero-based index for name in layer's live engine; supplied by
     * NodePickSession, not independently bounds-checked here.
     * @details Rejects another model, excluded host nodes or virtual/inlet junctions with a hint
     * while keeping picking active. An accepted pick refreshes OK, ends the session and raises
     * the panel; no engine mutation occurs.
     *
     * @details A node was clicked on the map: adopt it if eligible, else explain and
     *  keep picking.
     */
    void onCaptureNodePicked(SWMMModelLayer *layer, const QString &name, int nodeIdx);

private:
    /**
     * @brief Reload all nonempty inlet IDs from the bound engine.
     * @param[in] selected Optional identity passed to the picker as the preferred selection;
     * empty leaves selection policy to setItems().
     * @details No-ops without constructed picker, bound layer or engine; the basic list is not
     * itself gutter-type filtered.
     */
    void refreshDesignItems(const QString &selected = {});
    /**
     * @brief Reload eligible capture nodes and preselect the first case-insensitive sorted name.
     * @details Skips excluded indices and nodes reported virtual by the engine, then sets an
     * empty selection if no names remain. No-ops without picker/model/engine. Does not retain
     * pointers returned by engine ID queries.
     */
    void refreshCaptureItems();
    /**
     * @brief Replace the local map-picking hint and toggle its visibility.
     * @param[in] text Copied plain hint text; empty hides the label.
     * @details No-ops before label creation; does not change map tools or model state.
     */
    void setPickHint(const QString &text);

    SWMMModelLayer *m_layer = nullptr;
    QVector<int> m_exclude;

    LabeledPickerCombo *m_designPicker = nullptr;
    LabeledPickerCombo *m_capturePicker = nullptr;
    QComboBox *m_placement = nullptr;
    QLabel *m_pickHint = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
    QPointer<NodePickSession> m_pick;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_INLETJUNCTIONSETUPDIALOG_H
