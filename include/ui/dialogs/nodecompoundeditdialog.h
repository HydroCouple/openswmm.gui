/*!
 * \file   nodecompoundeditdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice DB.2 — One modal dialog with a stacked-widget body, one page
 * per compound node attribute (Inflows / DWF / RDII / Treatment).
 *
 * **Engine-API status (2026-05-22)** — only RDII has the full per-entry
 * round-trip (`swmm_rdii_get` exists; this dialog iterates all entries
 * filtering by `node_idx`). Inflows and DWF have `_add` + `_count` only
 * — the dialog surfaces the count and an `Add…` form (the engine
 * accepts new rows but the GUI can't read existing ones back). Treatment
 * has no engine API at all and shows an explanatory placeholder. As soon
 * as the AG.0 batch (`swmm_inflow_get` / `swmm_dwf_get` / per-entry
 * setters + removes; see `openswmm.engine/docs/AG_GUI_API_REQUEST.md`)
 * lands, the same dialog gains full read + edit + remove for all
 * four kinds without any GUI scaffolding change.
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

    NodeCompoundEditRef m_ref;

    // RDII page — fully functional via `swmm_rdii_*`.
    QTableWidget   *m_rdiiTable     = nullptr;
    QComboBox      *m_rdiiUhCombo   = nullptr;
    QDoubleSpinBox *m_rdiiAreaSpin  = nullptr;
    QLabel         *m_rdiiSummary   = nullptr;

    // Inflows page — Add-only until engine lands per-entry getter.
    QLabel         *m_inflowsSummary    = nullptr;
    QLineEdit      *m_inflowsConstEdit  = nullptr;
    QComboBox      *m_inflowsTypeCombo  = nullptr;
    QLineEdit      *m_inflowsTsEdit     = nullptr;
    QDoubleSpinBox *m_inflowsBaseSpin   = nullptr;
    QDoubleSpinBox *m_inflowsMFactSpin  = nullptr;
    QDoubleSpinBox *m_inflowsSFactSpin  = nullptr;
    QLineEdit      *m_inflowsPatEdit    = nullptr;

    // DWF page — Add-only until engine lands per-entry getter.
    QLabel         *m_dwfSummary       = nullptr;
    QLineEdit      *m_dwfConstEdit     = nullptr;
    QDoubleSpinBox *m_dwfAvgSpin       = nullptr;
    QLineEdit      *m_dwfPat1Edit      = nullptr;
    QLineEdit      *m_dwfPat2Edit      = nullptr;
    QLineEdit      *m_dwfPat3Edit      = nullptr;
    QLineEdit      *m_dwfPat4Edit      = nullptr;

    // Treatment — engine API entirely missing today.
    QLabel         *m_treatmentNotice  = nullptr;

    QStackedWidget   *m_stack   = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
};

#endif // NODECOMPOUNDEDITDIALOG_H
