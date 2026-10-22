/**
 * @file      subcatchcompoundeditdialog.h
 * @brief     compound editor for subcatchment attributes that don't fit a single property row:
 *            land-use coverage, LID usage and initial loadings (groundwater exchange lives in
 *            GroundwaterExchangeDialog).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Phase 3 — compound editor for subcatchment attributes that don't fit a
 * single property row: land-use coverage, LID usage and initial loadings
 * (groundwater exchange lives in GroundwaterExchangeDialog). Mirror of
 * NodeCompoundEditDialog: one QStackedWidget page per Kind, apply-as-you-go
 * engine writes, summary tracked in m_ref.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef SUBCATCHCOMPOUNDEDITDIALOG_H
#define SUBCATCHCOMPOUNDEDITDIALOG_H

#include <QDialog>

#include "ui/properties/subcatchcompoundeditref.h"

class QStackedWidget;
class QTableWidget;
class QLabel;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QDialogButtonBox;
class QPushButton;

class SubcatchCompoundEditDialog : public QDialog
{
    Q_OBJECT
public:
    /**
     * @param[in] ref Moved identity with borrowed raw engine/layer, subcatchment name and valid
     *   Kind enum. Use LandUse/LidUsage/Loadings here; Groundwater belongs to its own editor,
     *   invalid Kind can index title array out of bounds.
     * @param[in] parent Optional QWidget owner; caller manages parentless dialog and modal
     *   invocation if needed.
     * @brief Build all three compound pages and read selected active page without source writes on
     *   construction.
     * @note Subsequent cell/Add/Remove edits call C API immediately, without undo commands or
     *   dialog-level layer markEdited/attribute notifications. Close/reject does not undo writes.
     *   Layer destruction/engineAboutToClose invalidates, geometryChanged verifies current name;
     *   unbound-layer callers invalidate BEFORE engine closure and serialize GUI-thread access. No
     *   source-change synchronization beyond those checks.
     */
    explicit SubcatchCompoundEditDialog(SubcatchCompoundEditRef ref, QWidget *parent = nullptr);

    /**
     * @return Copied cached active-page summary updated by constructor/explicit refresh, including
     *   removed-subcatch label, not fresh engine query or mutation-success flag. No source
     *   ownership/dirty-state update.
     */
    [[nodiscard]] QString updatedSummary() const { return m_ref.summary; }

public slots:
    /*! Disable editing before the project engine or owning layer is released.
     * @brief Null borrowed handle/layer, disable form and reject without destroying engine or
     *   reversing previous mutations.
     * @note Call before untracked handle release; leaves cached summary/tables intact. No
     *   replacement-engine binding.
 */
    void invalidateContext();

private slots:
    /**
     * @brief Resolve bound subcatchment name and invalidate if absent/null handle. Rename is
     *   treated as old-name removal, not followed; no live model identity/state refresh or engine
     *   writes.
     */
    void checkContext();

private:
    /**
     * @brief Create owned two-column full land-use matrix with immediate coverage-percent commit
     *   and guarded readback.
     * @note Edit resolves land use by current displayed name and current subcatch index, accepts
     *   finite numeric [0,100] PERCENT, calls swmm_subcatch_set_coverage and refreshes even after
     *   rejection. Missing name/target returns without write. No enforcement of total <= 100;
     *   footer warns when summed readbacks exceed 100+1e-9. Zero counts unassigned. No undo or
     *   dialog-level dirty signal.
     */
    void buildLandUsePage();
    /**
     * @brief Build readonly usage list plus Add form for direct appended LID usage and selected-row
     *   removal.
     * @note Control must resolve by current engine name. Number 1..1e6 units; area per unit and top
     *   width 0..1e12/four decimals in project ft2/m2 and ft/m respectively, not acres/ha; initial
     *   saturation and impervious inflow 0..100 PERCENT/two decimals. No automatic unit conversion,
     *   coverage check, duplicate replacement or editing/hydrating existing row into Add form.
     * @note Remove uses stored global usage index and errors warn; successful single API writes
     *   refresh. Does not expose pervious inflow/drain/report fields or create
     *   undo/transaction/model notification; Close retains changes.
     */
    void buildLidUsagePage();
    /**
     * @brief Build owned pollutant initial-buildup matrix with immediate nonnegative numeric commit
     *   and suppression during refresh.
     * @note Resolve current pollutant by name, require finite value >= 0 then call
     *   swmm_subcatch_set_initial_loading; errors warn and refresh to authoritative value. Raw
     *   initial mass per unit area, not runoff concentration; no unit conversion or magnitude cap.
     *   Only pollutant rows offered, not reaction species supported elsewhere by API. No undo/dirty
     *   signal, zero counts unassigned in summary.
     */
    void buildLoadingsPage();
    /**
     * @brief Requery bound source and replace selected page/read summary without writing engine.
     * @note Missing subcatch updates summary only, old table remains. Coverage/loadings enumerate
     *   every current land use/pollutant, skip failed reads and use displayNumber under commit
     *   suppression; footer counts strictly positive values, coverage sum warning advisory. No
     *   automatic invocation merely because external catalogs change.
     * @note LID control choices populated only while combo EMPTY, otherwise retain old catalog.
     *   Usage rows scan global API list, filter source, store global index for removal and display
     *   six significant digits; extra pervious/treatment flags read but not shown. Refresh resets
     *   usage selection/Remove state, not Add form. Groundwater kind has no refresh branch here.
     */
    void refreshActivePage();

    /*! Stack page for a Kind (Groundwater has no page here — it opens
     *  GroundwaterExchangeDialog instead).
     * @param[in] kind Raw enum selector interpreted without engine lookup.
     * @return LandUse 0, LidUsage 1, Loadings 2; Groundwater/unknown fall back 0 though constructor
     *   requires valid title enum. No groundwater editor construction or model mutation.
 */
    [[nodiscard]] static int pageIndexFor(SubcatchCompoundEditRef::Kind kind);

    /**
     * @return Current zero-based engine subcatchment index by copied name, -1 for null
     *   handle/empty/absent. Borrowed nonnull handle must remain live; no engine identity,
     *   editable-lifecycle or unit checks.
     */
    [[nodiscard]] int subIdx() const; // swmm_subcatch_index, or -1

    SubcatchCompoundEditRef m_ref;

    // Land-use page — iteration 4: editable full matrix (one row per
    // defined land use, editable percent column, live sum footer).
    QLabel *m_luSummary = nullptr;
    QTableWidget *m_luTable = nullptr;
    bool m_luRefreshing = false;

    // Loadings page (iteration 4) — [LOADINGS] initial buildup per pollutant.
    QLabel *m_loadSummary = nullptr;
    QTableWidget *m_loadTable = nullptr;
    bool m_loadRefreshing = false;

    // LID usage page
    QLabel *m_lidSummary = nullptr;
    QTableWidget *m_lidTable = nullptr;
    QPushButton *m_lidRemoveBtn = nullptr;
    QComboBox *m_lidCombo = nullptr;
    QSpinBox *m_lidNumber = nullptr;
    QDoubleSpinBox *m_lidArea = nullptr;
    QDoubleSpinBox *m_lidWidth = nullptr;
    QDoubleSpinBox *m_lidInitSat = nullptr;
    QDoubleSpinBox *m_lidFromImp = nullptr;

    QStackedWidget *m_stack = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
};

#endif // SUBCATCHCOMPOUNDEDITDIALOG_H
