/*!
 * \file   nodecompoundeditdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice DB.2 — One modal dialog with a stacked-widget body, one page
 * per compound node attribute (Inflows / DWF / RDII / Treatment).
 *
 * **Engine-API status (2026-05-24)** — all four pages have full per-entry
 * round-trip. Inflows uses `swmm_ext_inflow_get/add/remove`, DWF uses
 * `swmm_dwf_get/add/remove`, RDII uses `swmm_rdii_get/add/remove`, and
 * Treatment uses `swmm_treatment_get/set/clear` indexed by
 * (node_idx, pollut_idx). Each page shows existing per-node entries in
 * a table with Add and Remove buttons; per-entry edits are
 * remove-and-readd via the table selection + form below.
 */

#ifndef NODECOMPOUNDEDITDIALOG_H
#define NODECOMPOUNDEDITDIALOG_H

#include <QDialog>

#include "ui/properties/nodecompoundeditref.h"

class QStackedWidget;
class QTableWidget;
class QLabel;
class QLineEdit;
class QComboBox;
class QDoubleSpinBox;
class QDialogButtonBox;
class QPushButton;
class LabeledPickerCombo;

class NodeCompoundEditDialog : public QDialog
{
    Q_OBJECT

public:
    explicit NodeCompoundEditDialog(NodeCompoundEditRef ref,
                                    QWidget *parent = nullptr);

    /*! Refreshed summary string after the dialog closes — the cell's
     *  property adapter reads this back to update the row text without
     *  requerying the engine. Equal to the constructor's
     *  `ref.summary` if no mutation succeeded. */
    [[nodiscard]] QString updatedSummary() const { return m_ref.summary; }

private:
    void buildInflowsPage();
    void buildDwfPage();
    void buildRdiiPage();
    void buildTreatmentPage();

    /*! Re-read the engine state for the active page and refresh the
     *  table + summary line. Call after any successful Add / Remove. */
    void refreshActivePage();

    /*! Look up the bound node's index on the engine. Returns -1 if the
     *  node has been removed since the dialog opened. */
    [[nodiscard]] int nodeIdx() const;

    // DB.4 — combo population helpers. Each clears + repopulates the
    // picker with the engine's current set of data objects.
    void populateConstituentCombo(QComboBox *c);
    void populateTimeSeriesCombo(LabeledPickerCombo *p);
    void populatePatternCombo(LabeledPickerCombo *p);
    void populateUhGroupCombo(LabeledPickerCombo *p);

    /*! Open the full complex editor for the given data category. If
     *  \p currentName is empty the editor opens in CreateNew mode; if it
     *  names an existing data object, the editor opens in Edit mode
     *  pre-selecting that object. Returns the name of the object
     *  selected when the editor closes — used to refresh the picker
     *  combo. Categories without a complex editor surface the gap-
     *  tooltip info box. */
    QString launchObjectEditor(int dataCategory, const QString &currentName);

    /*! Wire a picker's "..." button so it opens the right complex
     *  editor (Pattern / Time Series / UH group). After the editor
     *  closes the combo is repopulated and the worked-on item is
     *  re-selected. */
    void wirePicker(LabeledPickerCombo *picker, int dataCategory,
                     void (NodeCompoundEditDialog::*repopulate)(LabeledPickerCombo*));

    /*! Apply the FLOW/MASS rule to the inflow Type combo based on the
     *  currently-selected constituent. MASS is pollutant-only — disable
     *  it and downshift the selection when FLOW is chosen. */
    void updateInflowsMassEnabled();

    NodeCompoundEditRef m_ref;

    // RDII page — `swmm_rdii_*`.
    QTableWidget       *m_rdiiTable     = nullptr;
    QPushButton        *m_rdiiRemoveBtn = nullptr;
    LabeledPickerCombo *m_rdiiUhPicker  = nullptr;  // DB.4f — combo + "..." button
    QDoubleSpinBox     *m_rdiiAreaSpin  = nullptr;
    QLabel             *m_rdiiSummary   = nullptr;

    // Inflows page — `swmm_ext_inflow_*`.
    QLabel             *m_inflowsSummary    = nullptr;
    QTableWidget       *m_inflowsTable      = nullptr;
    QPushButton        *m_inflowsRemoveBtn  = nullptr;
    QComboBox          *m_inflowsConstCombo = nullptr;  // DB.4d — FLOW + pollutants
    QComboBox          *m_inflowsTypeCombo  = nullptr;  // MASS disabled when FLOW
    LabeledPickerCombo *m_inflowsTsPicker   = nullptr;  // time series + "..."
    QDoubleSpinBox     *m_inflowsBaseSpin   = nullptr;
    QDoubleSpinBox     *m_inflowsMFactSpin  = nullptr;
    QDoubleSpinBox     *m_inflowsSFactSpin  = nullptr;
    LabeledPickerCombo *m_inflowsPatPicker  = nullptr;  // pattern + "..."

    // DWF page — `swmm_dwf_*`.
    QLabel             *m_dwfSummary       = nullptr;
    QTableWidget       *m_dwfTable         = nullptr;
    QPushButton        *m_dwfRemoveBtn     = nullptr;
    QComboBox          *m_dwfConstCombo    = nullptr;  // DB.4e
    QDoubleSpinBox     *m_dwfAvgSpin       = nullptr;
    LabeledPickerCombo *m_dwfPat1Picker    = nullptr;  // Monthly + "..."
    LabeledPickerCombo *m_dwfPat2Picker    = nullptr;  // Daily   + "..."
    LabeledPickerCombo *m_dwfPat3Picker    = nullptr;  // Hourly  + "..."
    LabeledPickerCombo *m_dwfPat4Picker    = nullptr;  // Weekend + "..."

    // Treatment — per-(node, pollutant) removal expression, edited
    // inline in a 2-col table (Pollutant | Expression). Writes route
    // through `swmm_treatment_set` / `swmm_treatment_clear` as the
    // user commits the Expression cell.
    QLabel         *m_treatmentSummary = nullptr;
    QTableWidget   *m_treatmentTable   = nullptr;
    QLabel         *m_treatmentBanner  = nullptr;   // live validator verdict
    // Re-entrancy guard: applying engine state to the table fires
    // QTableWidget::itemChanged for every cell, which would otherwise
    // bounce right back into the commit handler. Set to true while
    // populating; the cell-changed slot bails when set.
    bool            m_treatmentSuppressCommit = false;

    QStackedWidget   *m_stack   = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
};

#endif // NODECOMPOUNDEDITDIALOG_H
