/**
 * @file      classificationeditor.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice US.1 (UNIFIED_STYLING plan S1) — the one classification
 *         editor shared by every classified visual.
 *
 *         Extracted from KindRendererPanel's graduated box so the 1D
 *         per-kind GraduatedRenderer, the 2D depth fill / contour bands /
 *         isolines, and the mesh elevation fill all expose the SAME
 *         ArcGIS-Pro-style controls:
 *
 *           ┌─ Classification ─────────────────────────────┐
 *           │ [○ Continuous  ● Classified]   (optional)    │
 *           │ Attribute: [ depth ▾ ]          (optional)   │
 *           │ Colour ramp: [ ▒▒▒ Viridis ▾ ]  [ ] Invert   │
 *           │ Method: [ Equal interval ▾ ]   Classes: 5    │
 *           │ Range:  [ Fixed over run ▾ ]    (optional)   │
 *           │ [ ] Custom range  Min ____  Max ____ (opt.)  │
 *           │ ┌─────────────────────────────────────────┐ │
 *           │ │ Lower   Upper   Colour   Label          │ │
 *           │ └─────────────────────────────────────────┘ │
 *           │  [Auto-classify from data]                  │
 *           └──────────────────────────────────────────────┘
 *
 *         The editor owns no model state — it talks to an
 *         IClassificationBinding. Every edit calls binding->setScheme(),
 *         which routes through the model's normal change channel so all
 *         views repaint. The host re-shows the editor against the live
 *         model via refresh() (e.g. after a Cancel rollback).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_WIDGETS_CLASSIFICATIONEDITOR_H
#define OPENSWMMVIS_UI_WIDGETS_CLASSIFICATIONEDITOR_H

#include "render/classificationscheme.h"

#include <QWidget>

#include <memory>

class ColorRampComboBox;

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;
class QStandardItem;
class QStandardItemModel;
class QTableView;
class QToolButton;

namespace openswmmvis::ui
{

class IClassificationBinding;

class ClassificationEditor : public QWidget
{
    Q_OBJECT
public:
    /*! \param binding  Adapter onto the classified model. The editor does
     *                  NOT take ownership unless \p ownBinding is true.
     * @brief Build the shared classification controls and hydrate them from an optional binding.
     * @param[in] ownBinding Delete the binding when replaced or this editor is destroyed when true; otherwise caller retains lifetime responsibility.
     * @param[in] parent Optional QWidget owner; all binding reads/writes occur synchronously on the GUI thread.
     */
    explicit ClassificationEditor(IClassificationBinding *binding, bool ownBinding = false,
                                  QWidget *parent = nullptr);
    /**
     * @brief Destroy the editor, deleting its binding only when ownership was accepted.
     */
    ~ClassificationEditor() override;

    /*! Swap the bound model (e.g. when the host re-targets the editor).
     *  Takes ownership when \p ownBinding is true.
     * @param[in] binding New adapter, possibly null; borrowed adapters must outlive use by this editor.
     * @param[in] ownBinding Whether this editor assumes deletion responsibility. Replacing a distinct previously owned adapter deletes it.
     * @note Calls refresh(); null binding leaves current controls visible but future mutation helpers do nothing.
     */
    void setBinding(IClassificationBinding *binding, bool ownBinding = false);

    /*! Re-read every control from the binding's current scheme. Call after
     *  an external change / Cancel rollback.
     * @note Null binding is a no-op. Otherwise clears local errors, replaces displayed attribute/ramp/range/label controls with signals suppressed, applies capability visibility and rebuilds the class table without writing the binding.
     */
    void refresh();

    /*! Invalid visible drafts never reach the live binding. Hosts must refuse
     * Apply/OK until the user corrects them or cancels the containing session.
     * @return True for disabled/unbound editors, otherwise false for a retained break/range/scheme error or live scheme validation error. Does not mutate the model.
     */
    [[nodiscard]] bool hasValidDraft() const;

signals:
    /*! Emitted after any edit has been pushed to the binding — hosts that
     *  show a live legend swatch or derived UI can refresh on this. */
    void edited();

private slots:
    /**
     * @brief Submit the selected continuous/classified mode through validated mutation and update control visibility.
     */
    void onModeChanged();
    /**
     * @brief Submit an attribute identity to the binding, refresh all controls and emit edited().
     * @param[in] row Attribute combo row whose item data contains the identity; caller supplies a current row. Suppressed/unbound updates are ignored.
     */
    void onAttributeChanged(int row);
    /**
     * @brief Submit the selected built-in ramp name or embed a selected custom ramp payload.
     * @param[in] ramp Chosen ramp value; the current combo name/custom flag determines which scheme setter is used.
     */
    void onRampChanged(const RasterColorRamp &ramp);
    /**
     * @brief Submit a ramp-direction change through the scheme validation path.
     * @param[in] on True reverses colour-ramp sampling.
     */
    void onInvertToggled(bool on);
    /**
     * @brief Submit the selected bin method, seeding manual breaks from current data when entering empty Manual mode.
     * @param[in] row Current method combo row. A rejected scheme restores the combo to the live method; leaving Manual clears stale break errors.
     */
    void onMethodChanged(int row);
    /**
     * @brief Submit the requested class count through scheme validation.
     * @param[in] n Count supplied by the UI spin box, normally 1–64.
     */
    void onClassCountChanged(int n);
    /**
     * @brief Submit range-mode selection and synchronize useCustomRange with FixedUser.
     * @param[in] row Current range-mode combo row. Leaving FixedUser clears a retained range error; fixed mode captures precise displayed bounds.
     */
    void onRangeModeChanged(int row);
    /**
     * @brief Enable/disable explicit bounds through validation and update range-control visibility.
     * @param[in] on True captures the precise displayed min/max; false clears any retained range error.
     */
    void onCustomRangeToggled(bool on);
    /**
     * @brief Validate the visible precise range before submitting its bounds/custom-range policy.
     * @note Invalid bounds remain as an error draft and do not reach the binding. Bound values retain the selected attribute's units; range-mode bindings use explicit bounds only in FixedUser mode.
     */
    void onCustomRangeEdited();
    /**
     * @brief Submit decimal-place versus significant-figure label formatting.
     * @param[in] row Current format combo row containing the scheme enum value.
     */
    void onLabelFormatChanged(int row);
    /**
     * @brief Submit class-edge/legend label precision through scheme validation.
     * @param[in] digits UI digit count, normally 0–9.
     */
    void onLabelPrecisionChanged(int digits);
    /**
     * @brief Invoke binding auto-classification for a valid unsuppressed draft, rebuild the table and emit edited().
     * @note No-op with a missing binding or invalid draft; the binding owns data-driven classification semantics.
     */
    void onAutoClassify();
    /**
     * @brief Submit edited class colour/label or validated strictly increasing interior upper bounds.
     * @param[in] item Changed table item; null/suppressed/unbound updates are ignored.
     * @note Interior breaks must be finite, exceed the previous bound and stay below range maximum; invalid drafts are retained locally. Valid break edits switch to Manual. Untouched formatted bounds reuse stored full-precision values.
     */
    void onTableItemChanged(QStandardItem *item);

private:
    /**
     * @brief Create owned classification/range/label controls, class table, error status and live edit connections.
     * @note This is construction-time setup; it does not hydrate or write a binding.
     */
    void buildUi();
    /**
     * @brief Recreate class rows from binding-computed edges or scheme/sample-derived edges with edit signals suppressed.
     * @note Continuous mode yields an empty table. Lower/final upper bounds are read-only; interior uppers preserve full precision separately from formatted text. Refreshes status without changing the binding.
     */
    void rebuildTable();
    /**
     * @brief Show controls supported by the binding and current continuous/classified/range mode.
     * @note Range-mode capability takes precedence over the custom-range checkbox idiom. No binding is a no-op.
     */
    void applyVisibility();
    /**
     * @brief Display the highest-priority local/live validation error or constant/no-sample/unavailable-ramp advisory.
     * @note Theme error styling is applied only for validation errors; diagnostics do not mutate the scheme.
     */
    void updateStatus();
    QString m_breakError;
    QString m_rangeError;
    QString m_schemeError;
    QLabel *m_status = nullptr;
    /*! Read the editor's binding scheme, mutate via \p fn, push back.
     * @param[in] fn Callable mutating a copied ClassificationScheme.
     * @note No-op during suppression or without a binding. Validates the candidate against binding data bounds and blocks writes while any retained error remains. Changed valid schemes call setScheme(), rebuild the table and emit edited(); equal valid candidates only rebuild.
     */
    template <class Fn> void mutateScheme(Fn fn);

    IClassificationBinding *m_binding = nullptr;
    bool m_ownBinding = false;

    // Controls
    QWidget *m_modeRow = nullptr;
    QComboBox *m_modeCombo = nullptr;
    QWidget *m_attrRow = nullptr;
    QComboBox *m_attrCombo = nullptr;
    ColorRampComboBox *m_rampCombo = nullptr;
    QCheckBox *m_invertCheck = nullptr;
    QComboBox *m_methodCombo = nullptr;
    QSpinBox *m_countSpin = nullptr;
    QLabel *m_methodLabel = nullptr;
    QLabel *m_countLabel = nullptr;
    QWidget *m_rangeModeRow = nullptr;
    QComboBox *m_rangeModeCombo = nullptr;
    QWidget *m_customRangeRow = nullptr;
    QCheckBox *m_customRangeCheck = nullptr;
    QDoubleSpinBox *m_rangeMinSpin = nullptr;
    QDoubleSpinBox *m_rangeMaxSpin = nullptr;
    QWidget *m_labelFormatRow = nullptr;
    QComboBox *m_labelFormatCombo = nullptr;
    QSpinBox *m_labelPrecisionSpin = nullptr;
    QToolButton *m_autoBtn = nullptr;
    QTableView *m_table = nullptr;
    QStandardItemModel *m_tableModel = nullptr;

    bool m_suppress = false; // re-entrancy guard during refresh()
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_CLASSIFICATIONEDITOR_H
