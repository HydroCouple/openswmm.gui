/*!
 * \file   infilassigntoselectiondialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
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
 * mesh::infilUsesParam(), and destinations the engine does not accept in this
 * release are shown disabled per mesh::infilDestSupported() — the same two
 * rules the attribute table and the region-defaults table already follow.
 *
 * Singleton-raise per `[[feedback_mvc_synchronized_uis]]`: showFor() keeps a
 * `static QPointer` so the toolbar action and the menu mirror raise one window
 * rather than stacking two views over the same selection.
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

namespace openswmmvis::ui {

class InfilAssignToSelectionDialog : public QDialog
{
    Q_OBJECT

public:
    /*! \brief Create-or-raise the one instance, bound to \p meshLayer.
     *
     *  Re-binds an existing instance to the (possibly different) layer /
     *  canvas / selection before raising it, so switching project windows
     *  cannot leave the dialog writing into the previous project's mesh. */
    static void showFor(SWMM2DMeshLayer  *meshLayer,
                        MapCanvas        *canvas,
                        SelectionManager *selection,
                        const QString    &depthUnitLabel,
                        QWidget          *parent);

    InfilAssignToSelectionDialog(SWMM2DMeshLayer  *meshLayer,
                                 MapCanvas        *canvas,
                                 SelectionManager *selection,
                                 const QString    &depthUnitLabel,
                                 QWidget          *parent = nullptr);

    /*! \brief Point the dialog at another project's mesh / canvas / selection. */
    void rebind(SWMM2DMeshLayer  *meshLayer,
                MapCanvas        *canvas,
                SelectionManager *selection,
                const QString    &depthUnitLabel);

private slots:
    void onMethodChanged();
    void onApply();
    void onSelectionChanged();

private:
    void buildUi();

    /*! \brief Selected MeshCell triangle indices, filtered to the bound
     *  layer's mesh::MeshObjectRef::layerKey — the same filter
     *  MeshEditingToolbar::onSelectionChanged() applies, so a project with two
     *  meshes cannot write one mesh's selection into the other. Sorted, so the
     *  undo entry is reproducible. */
    [[nodiscard]] QVector<int> selectedCells() const;

    /*! \brief The tag every selected cell carries, or an empty string when the
     *  selection is empty, untagged, or spans more than one tag. */
    [[nodiscard]] QString commonTag() const;

    /*! \brief The form's current contents as one mesh::InfilRow. Slots the
     *  method does not use stay NaN. */
    [[nodiscard]] mesh::InfilRow currentRow() const;

    [[nodiscard]] mesh::InfilMethod currentMethod() const;

    /*! \brief Re-title / re-range / show-hide the five parameter rows for the
     *  chosen method, seeding each from mesh::cellParamSpecs(). */
    void refreshParamFields();

    /*! \brief Refresh the "N cells" caption, the region-tag radio's label and
     *  availability, and the Apply button. */
    void refreshSelectionState();

    QPointer<SWMM2DMeshLayer>  m_mesh;
    QPointer<MapCanvas>        m_canvas;
    QPointer<SelectionManager> m_selection;
    QString                    m_depthUnitLabel;

    QLabel         *m_selectionLbl = nullptr;
    QComboBox      *m_methodCombo  = nullptr;
    QLabel         *m_paramLabels[mesh::kInfilMaxParams]  = {};
    QDoubleSpinBox *m_paramSpins[mesh::kInfilMaxParams]   = {};
    QComboBox      *m_destCombo    = nullptr;
    QRadioButton   *m_writeCells   = nullptr;
    QRadioButton   *m_writeTag     = nullptr;
    QLabel         *m_statusLbl    = nullptr;
    QPushButton    *m_applyBtn     = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_INFILASSIGNTOSELECTIONDIALOG_H
