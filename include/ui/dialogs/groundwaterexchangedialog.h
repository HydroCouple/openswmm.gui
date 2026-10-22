/**
 * @file      groundwaterexchangedialog.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Groundwater Exchange editor for one subcatchment: the
 *         [GROUNDWATER] receiving node + surface elevation + standard
 *         lateral-flow coefficients (A1 B1 A2 B2 A3 Dsw Egwt) and the two
 *         [GWF] custom expressions (LATERAL / DEEP) with live engine
 *         validation. Replaces the Groundwater page that used to live in
 *         SubcatchCompoundEditDialog (AQUIFER_GROUNDWATER_EXCHANGE_GUI_PLAN
 *         D2): the aquifer itself is assigned on the subcatchment's
 *         Aquifer property row, so this dialog only shows it.
 *
 * Modeless top-level dialog opened from the SubcatchCompoundEditButton
 * (property browser + attribute table) for Kind::Groundwater and from the
 * node-side "Groundwater Sources" row. Apply writes straight to the engine
 * and calls SWMMModelLayer::markEdited() like every other compound editor.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef GROUNDWATEREXCHANGEDIALOG_H
#define GROUNDWATEREXCHANGEDIALOG_H

#include <QDialog>
#include <array>

#include "ui/properties/subcatchcompoundeditref.h"

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QWidget;

namespace openswmmvis::ui
{
class GwfExpressionEdit;
}

class GroundwaterExchangeDialog : public QDialog
{
    Q_OBJECT
public:
    /**
     * @brief Build and load a modeless subcatchment groundwater-exchange editor.
     * @param[in] ref Value-held reference copied/moved into the dialog; engine and optional
     * layer are borrowed, subName identifies the target, and kind is ignored. A live engine must
     * outlast access or be invalidated before closing.
     * @param[in] parent Optional Qt parent owning the dialog and all controls.
     * @details Loads a complete optimistic-concurrency snapshot and connects layer
     * destruction/engine-close notifications to invalidation. Length fields use the engine's
     * user units with the current UnitSystem label (fallback ft); no numerical unit conversion
     * occurs here. Use only on the GUI thread.
     */
    explicit GroundwaterExchangeDialog(SubcatchCompoundEditRef ref, QWidget *parent = nullptr);

    /**
     * @brief Query the current engine-backed groundwater summary for the referenced
     * subcatchment.
     * @return Owned summary string produced by groundwaterSummary() for the current
     * engine/index; this is a fresh query rather than the unsaved form draft.
     *
     * @details Live cell text (groundwaterSummary) — what the opening button shows
     *  after Apply.
     */
    [[nodiscard]] QString updatedSummary() const;

    /**
     * @brief Check loaded-state, aquifer and expression-validation gates for applying the draft.
     * @return True only when a complete snapshot loaded, an aquifer name resolved and both
     * expression validity flags are true. Does not recheck concurrent engine changes or all
     * numeric controls.
     */
    [[nodiscard]] bool canApply() const;

public slots:
    /**
     * @brief Drop borrowed engine/layer access and close the disabled editor.
     * @details Invalidates both expression editors, clears the loaded-state flag, disables
     * controls and rejects the dialog before any later Apply can dereference the closed engine.
     */
    void invalidateContext();

signals:
    /**
     * @brief Notify listeners after the editor reports an engine write as dirty.
     * @details Emitted by notifyEdited() after a changed Apply succeeds, and also when an
     * earlier changed field succeeded before a later write failed. No-op Apply does not emit;
     * receivers should refresh from current engine state rather than assume the whole draft
     * committed.
     */
    void applied();

private:
    /**
     * @brief Create owned groundwater controls, expression validation and Apply/Close wiring
     * once.
     * @details Uses current length labels (fallback ft), retains engine-native numerical values
     * and exposes Egwt's missing sentinel through a separate node-invert choice. Requires an
     * uninitialized control tree.
     */
    void buildUi_();
    /**
     * @brief Load aquifer identity, receiver choices and a complete editable snapshot.
     * @details Enables the form only when an aquifer name is available, populates the node list
     * on first load and hydrates numeric/expression controls after a successful readSnapshot().
     * Incomplete reads preserve the draft/error state, disable successful apply through
     * m_loadedOk and report that the editor must be reopened.
     */
    void loadFromEngine_();
    /**
     * @brief Validate the retained draft and write changed groundwater fields sequentially.
     * @details Rejects stale/incomplete snapshots before any write, revalidates receiving-node
     * lookup and both expressions, and requires finite precise numeric values. Writes receiver,
     * eight parameters, lateral expression and deep expression in that order without a
     * transaction or undo command. A later engine error leaves earlier writes installed,
     * marks/notifies them dirty and retains the old concurrency snapshot so retries cannot
     * silently overwrite the partial model. Full success refreshes the snapshot/hydrated values
     * and notifies only if something changed; no-op or pre-write failure leaves the model
     * untouched.
     */
    void apply_();
    /**
     * @brief Enable Apply from canApply() and describe unavailable aquifer/expression state.
     * @details No-ops before the Apply button exists; does not itself validate engine state.
     */
    void updateApplyState_();
    /**
     * @brief Load eight native groundwater values into precision-preserving numeric controls.
     * @param[in] values Ordered surface elevation, A1, B1, A2, B2, A3, Dsw and Egwt.
     * Elevation/depth values are in engine user length units, coefficients/exponents retain the
     * native groundwater formula convention. Egwt=-1e10 selects receiving-node invert instead of
     * a numeric elevation.
     * @details Preserves unrounded loaded numbers through setHydratedValue(); toggles the Egwt
     * control and its display-only special minimum value without writing the engine.
     */
    void hydrateParams_(const std::array<double, 8> &values);
    /**
     * @brief Connect one expression editor to its status label and validity gate.
     * @param[in] edit Non-null dialog-owned editor, captured by the validation callback.
     * @param[in] status Non-null dialog-owned label updated with errors and one-based columns.
     * @param[out] okFlag Non-null pointer to a dialog-member validity flag written on future
     * validation signals; its storage must outlive this connection.
     * @details Captures the original accessible description, appends validation errors when
     * invalid and refreshes Apply state. All controls and flag storage must remain valid for the
     * dialog-lifetime connection.
     */
    void bindExpression_(openswmmvis::ui::GwfExpressionEdit *edit, QLabel *status, bool *okFlag);

    /**
     * @brief Resolve the referenced subcatchment name against the current borrowed engine.
     * @return Zero-based current index, or -1 for null engine, empty name or lookup failure; no
     * index is cached.
     */
    [[nodiscard]] int subIdx() const; // swmm_subcatch_index, or -1

    struct Snapshot {
        int aquifer = -1;
        int node = -1;
        QString aquiferName, nodeName, lateral, deep;
        std::array<double, 8> values{};
        /**
         * @brief Compare all loaded groundwater snapshot fields using default member equality.
         * @return True when aquifer/node indices and names, both expression strings and all
         * eight exact numeric values match the unnamed const Snapshot reference. No tolerance or
         * NaN equivalence is applied.
         */
        bool operator==(const Snapshot &) const = default;
    };
    /**
     * @brief Read complete finite groundwater state for optimistic-concurrency comparison.
     * @param[out] snapshot Destination for aquifer/node indices and names, eight engine-native
     * values and both expressions; can be partially overwritten on failure and must be discarded
     * then.
     * @return True only after every API/ID lookup succeeds, all numeric values are finite and
     * both UTF-8 expressions fit with spare NUL space in buffers grown from 512 bytes up to 1
     * MiB. False for absent target, failed reads/IDs, non-finite values or oversized/truncated
     * expressions; does not mutate engine state.
     */
    bool readSnapshot(Snapshot &snapshot) const;
    /**
     * @brief Display or clear a nonmodal edit error and optionally reveal the offending field.
     * @param[in] message Copied plain-text status; empty hides the error label.
     * @param[in] field Optional borrowed child widget to scroll into view and focus; null
     * changes only the message.
     * @details Requires the error label to exist; does not dismiss the dialog or discard its
     * draft.
     */
    void reportWriteError(const QString &message, QWidget *field = nullptr);
    /**
     * @brief Mark the optional layer dirty, refresh summary and emit applied().
     * @details Invokes markEdited and attributeChanged directly on the borrowed layer when
     * present, then queries updatedSummary(). Called after whole or partial engine writes, never
     * as a rollback.
     */
    void notifyEdited();
    Snapshot m_loaded;
    bool m_loadedOk = false;

    SubcatchCompoundEditRef m_ref;

    QLabel *m_header = nullptr;
    QLabel *m_hint = nullptr;
    QLabel *m_writeError = nullptr;
    QWidget *m_form = nullptr; ///< everything below the header (gated on aquifer)

    QComboBox *m_node = nullptr;
    QDoubleSpinBox *m_surfEl = nullptr;
    QDoubleSpinBox *m_a1 = nullptr;
    QDoubleSpinBox *m_b1 = nullptr;
    QDoubleSpinBox *m_a2 = nullptr;
    QDoubleSpinBox *m_b2 = nullptr;
    QDoubleSpinBox *m_a3 = nullptr;
    QDoubleSpinBox *m_tw = nullptr;       ///< [GROUNDWATER] Dsw (fixed surface-water depth)
    QDoubleSpinBox *m_hstar = nullptr;    ///< [GROUNDWATER] Egwt (threshold water-table elevation)
    QCheckBox *m_egwtUseInvert = nullptr; ///< Egwt `*`: receiving node invert

    openswmmvis::ui::GwfExpressionEdit *m_lateral = nullptr;
    openswmmvis::ui::GwfExpressionEdit *m_deep = nullptr;
    QLabel *m_lateralStatus = nullptr;
    QLabel *m_deepStatus = nullptr;
    bool m_lateralOk = true;
    bool m_deepOk = true;
    bool m_hasAquifer = false;

    QDialogButtonBox *m_buttons = nullptr;
    QPushButton *m_applyBtn = nullptr;
};

#endif // GROUNDWATEREXCHANGEDIALOG_H
