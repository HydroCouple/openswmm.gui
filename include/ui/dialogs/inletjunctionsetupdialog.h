/*!
 * \file   inletjunctionsetupdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Floating (non-modal) panel that collects the three things an inlet
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
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_INLETJUNCTIONSETUPDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_INLETJUNCTIONSETUPDIALOG_H

#include <QDialog>
#include <QPointer>
#include <QString>
#include <QVector>

#include <openswmm/engine/openswmm_callbacks.h>   // SWMM_Engine

class QComboBox;
class QDialogButtonBox;
class QLabel;
class LabeledPickerCombo;
class NodePickSession;
class SWMMModelLayer;

namespace openswmmvis::ui {

class InletJunctionSetupDialog : public QDialog
{
    Q_OBJECT

public:
    /*! \param layer        Model layer (engine + inlet registry source; its
     *                      editCanvas() hosts the map pick).
     *  \param excludeNodes Node indices the capture-node list must omit — the
     *                      host conduit's two end nodes for the split flow,
     *                      or the node being promoted for the convert flow.
     *                      Virtual and inlet junctions are excluded already
     *                      (engine rule 627). */
    InletJunctionSetupDialog(SWMMModelLayer *layer,
                             QVector<int> excludeNodes,
                             QWidget *parent = nullptr);
    ~InletJunctionSetupDialog() override;

    /*! Chosen inlet design name. Never empty once the dialog was accepted. */
    [[nodiscard]] QString inletDesign() const;
    /*! Chosen capture (underdrain) node name. Never empty once accepted. */
    [[nodiscard]] QString captureNode() const;
    /*! Chosen placement, as a `SWMM_InletPlacement` ordinal. */
    [[nodiscard]] int placement() const;

    /*! Start picking the capture node on the map (the "…" button's action).
     *  No-op when the layer has no edit canvas. */
    void startCaptureNodePick();
    /*! Stop picking and restore the previous canvas tool. */
    void endCaptureNodePick();
    [[nodiscard]] bool isPickingCaptureNode() const;

private slots:
    /*! Enable OK only once both identity fields are set — the engine rejects
     *  a partial usage row, so an incomplete OK could only fail. */
    void updateOkEnabled();
    /*! Open `InletEditorDialog::pickInlet` filtered to the gutter types and
     *  select the returned design. */
    void onDesignPickerClicked();
    /*! A node was clicked on the map: adopt it if eligible, else explain and
     *  keep picking. */
    void onCaptureNodePicked(SWMMModelLayer *layer, const QString &name, int nodeIdx);

private:
    void refreshDesignItems(const QString &selected = {});
    void refreshCaptureItems();
    void setPickHint(const QString &text);

    SWMMModelLayer     *m_layer = nullptr;
    QVector<int>        m_exclude;

    LabeledPickerCombo *m_designPicker  = nullptr;
    LabeledPickerCombo *m_capturePicker = nullptr;
    QComboBox          *m_placement     = nullptr;
    QLabel             *m_pickHint      = nullptr;
    QDialogButtonBox   *m_buttons       = nullptr;
    QPointer<NodePickSession> m_pick;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_INLETJUNCTIONSETUPDIALOG_H
