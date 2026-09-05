/*!
 * \file   inletjunctionsetupdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Small modal that collects the three things an inlet junction needs
 *         BEFORE it is created: an inlet design, a capture node, and a
 *         placement mode.
 *
 * Decision D-G6 (INLET_EDITOR_AND_INLET_JUNCTION_GUI_PLAN_2026-09-05.md §5):
 * ask first rather than insert-then-edit. The engine requires a design and a
 * capture node for the node to be valid, so a half-configured inlet junction
 * would fail validation on save — and the map tool has no way to "hold" an
 * invalid node while the user fills in the property panel.
 *
 * The same dialog serves the Convert-To flow, which promotes an existing
 * junction instead of splitting a conduit.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_INLETJUNCTIONSETUPDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_INLETJUNCTIONSETUPDIALOG_H

#include <QDialog>
#include <QString>
#include <QVector>

#include <openswmm/engine/openswmm_callbacks.h>   // SWMM_Engine

class QComboBox;
class QDialogButtonBox;
class QLabel;
class LabeledPickerCombo;
class SWMMModelLayer;

namespace openswmmvis::ui {

class InletJunctionSetupDialog : public QDialog
{
    Q_OBJECT

public:
    /*! \param layer        Model layer (engine + inlet registry source).
     *  \param excludeNodes Node indices the capture-node list must omit — the
     *                      host conduit's two end nodes for the split flow,
     *                      or the node being promoted for the convert flow.
     *                      Virtual and inlet junctions are excluded already
     *                      (engine rule 627). */
    InletJunctionSetupDialog(SWMMModelLayer *layer,
                             QVector<int> excludeNodes,
                             QWidget *parent = nullptr);

    /*! Chosen inlet design name. Never empty once the dialog was accepted. */
    [[nodiscard]] QString inletDesign() const;
    /*! Chosen capture (underdrain) node name. Never empty once accepted. */
    [[nodiscard]] QString captureNode() const;
    /*! Chosen placement, as a `SWMM_InletPlacement` ordinal. */
    [[nodiscard]] int placement() const;

private slots:
    /*! Enable OK only once both identity fields are set — the engine rejects
     *  a partial usage row, so an incomplete OK could only fail. */
    void updateOkEnabled();
    /*! Open `InletEditorDialog::pickInlet` filtered to the gutter types and
     *  select the returned design. */
    void onDesignPickerClicked();

private:
    void refreshDesignItems(const QString &selected = {});
    void refreshCaptureItems();

    SWMMModelLayer     *m_layer = nullptr;
    QVector<int>        m_exclude;

    LabeledPickerCombo *m_designPicker  = nullptr;
    QComboBox          *m_captureCombo  = nullptr;
    QComboBox          *m_placement     = nullptr;
    QDialogButtonBox   *m_buttons       = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_INLETJUNCTIONSETUPDIALOG_H
