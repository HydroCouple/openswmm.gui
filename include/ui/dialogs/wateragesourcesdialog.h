/**
 * @file      wateragesourcesdialog.h
 * @brief     Editor for `[WATER_AGE_SOURCES]` — per-pathway initial water ages
 *        (GUI plan §3.4 / G3g, subplan Y3).
 *
 * @details Water age enters the model at seven source pathways (rainfall,
 *          DWF, groundwater, RDII, external inflow, routing-interface file,
 *          and the initial network state). Each carries a GLOBAL age in
 *          hours, and the DWF / external-inflow pathways additionally take
 *          per-NODE overrides — the scope rule the engine's parser enforces
 *          and `swmm_water_age_set_override` mirrors.
 *
 *          **Negative ages are legal** (engine D-NS1): a negative source
 *          age *extracts* age-volume, making water read younger, clamped so
 *          age never falls below zero. The editor therefore accepts
 *          negatives and says what they mean rather than validating them
 *          away.
 *
 *          The engine handle IS the model (GUI plan §2): edits are read
 *          from and written to `openswmm_water_age.h` directly, with no
 *          intermediate registry. The dialog is deliberately
 *          **dependency-light** — Qt Widgets plus that one engine ABI —
 *          which is what lets a test link and drive it (the
 *          `ClimatologyDialog` precedent; contrast
 *          `SimulationOptionsDialog`, whose link closure no test can
 *          satisfy, `tests/gui/CMakeLists.txt:1996`).
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   Apache-2.0
 */

#ifndef OPENSWMM_UI_DIALOGS_WATERAGESOURCESDIALOG_H
#define OPENSWMM_UI_DIALOGS_WATERAGESOURCESDIALOG_H

#include <openswmm/engine/openswmm_engine.h>

#include <QDialog>

class QTableWidget;
class QLabel;

namespace OpenSWMMVis
{

/*!
 * \brief Non-modal editor for the water-age source table.
 */
class WaterAgeSourcesDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @param[in] engine Optional borrowed engine; caller serializes access and invalidates dialog
     *   BEFORE destroying/replacing handle. No automatic lifetime/state guard.
     * @param[in] parent Optional QWidget owner; caller manages parentless dialog, WA_DeleteOnClose
     *   explicitly false.
     * @brief Build and hydrate seven global pathways plus per-node DWF/external-inflow overrides
     *   without writing engine.
     * @note Nonmodal by default; caller may exec modally. GUI-thread only, no live node/topology
     *   subscription. Null engine leaves default zero globals/empty overrides; apply validation
     *   refuses it.
     */
    explicit WaterAgeSourcesDialog(SWMM_Engine engine, QWidget *parent = nullptr);
    /**
     * @brief Destroy owned controls without closing engine, committing draft or reversing earlier
     *   writes.
     */
    ~WaterAgeSourcesDialog() override = default;

    /*! \brief True once OK has written at least one change to the engine.
     *  \details Lets the caller mark the project dirty without diffing —
     *           and lets a test assert that a no-op OK writes nothing.
     * @return Cumulative true after ANY successful engine write in an onAccept attempt, even if a
     *   later operation fails. Never reset by no-op attempts, reload or invalidation; no live query
     *   or dirty-flag mutation.
 */
    [[nodiscard]] bool wroteAnyChanges() const { return m_wroteAnyChanges; }

    /*! \brief Number of engine writes the last OK performed (0 when the
     *         dialog was accepted with no edits). Test-facing.
     * @return Successful engine mutation-call count in most recent onAccept attempt, reset to zero
     *   before validation. Includes partial writes on failure; zero also covers validation/no-op.
     *   Not number of cells changed or a guarantee dialog accepted.
 */
    [[nodiscard]] int lastWriteCount() const { return m_lastWriteCount; }

signals:
    /**
     * @brief Notify a positive successful-write count from an acceptance attempt, including
     *   partially failing attempts that keep dialog open.
     * @note Emitted before possible accept, after marker/count update. Zero-write/no-op/validation
     *   failure emit nothing. Caller marks project dirty/refreshes; no undo command or owned engine
     *   is conveyed.
     */
    void changesApplied();

public slots:
    /**
     * @brief Null the borrowed handle, disable the entire dialog and reject immediately.
     * @note Caller invokes before engine destruction; does not close engine, reset write markers,
     *   roll back changes or rebind to a replacement. WA_DeleteOnClose false preserves instance
     *   until owner destroys it.
     */
    void invalidateEngine();

private slots:
    /**
     * @brief Append a staged DWF row with current node-index/name combo snapshot and zero-hour
     *   spin, then refresh accessibility names.
     * @note Null engine is a no-op. Does not check editable lifecycle, uniqueness or available-node
     *   count; missing IDs get numeric fallback labels but acceptance validation later refuses unavailable
     *   identity. No engine write or topology subscription.
     */
    void onAddOverride();
    /**
     * @brief Remove the current staged override row if any and renumber accessibility descriptions.
     * @note No confirmation, lifecycle/engine check or immediate mutation; actual absent-key
     *   removals happen on accepted draft.
     */
    void onRemoveOverride();
    /**
     * @brief Reset last count, validate all override identities/uniqueness, then attempt sequential
     *   engine diff.
     * @note Validation runs BEFORE global writes/removals. For positive successful-call count,
     *   permanently mark wroteAnyChanges and emit changesApplied even when a later call failed.
     *   Accept only when writeToEngine reported no failure; error keeps staged form open with
     *   inline diagnostic. No rollback/undo or direct project dirty flag.
     */
    void onAccept();

private:
    /**
     * @brief Create owned fixed global pathway table, editable per-node override table and inline
     *   validation/OK/Cancel controls.
     * @note Spins display signed hours in [-1e6, 1e6], three decimals, with negative ages allowed.
     *   Tables stage controls without writing model; initial engine hydration occurs separately.
     *   GUI-thread only.
     */
    void buildUi();
    /**
     * @brief Hydrate global and override hours, preserving exact source values behind
     *   rounded/clamped displays.
     * @note Null engine leaves widgets unchanged. Failed global reads leave prior display;
     *   overrides clear before count query, count failure leaves empty, individual failed reads
     *   skip rows. Each loaded override snapshots current nodes; unsupported/missing source/node
     *   values fall back to first combo option. No marker reset, lifecycle transition or writes.
     */
    void readFromEngine();
    /**
     * @brief Clear previous inline error, refresh accessibility and validate source/node pairs
     *   against current engine.
     * @return False for null engine, missing/unsupported source, invalid node index/name mismatch
     *   or duplicate pair; true otherwise. Empty override table is valid with a nonnull handle.
     * @note Reject first failing row, scroll/select/focus field and enrich its accessibility
     *   description. Node combo text must equal current engine ID, catching rename/index staleness.
     *   Does not validate global/override hour values or editable lifecycle; engine setters handle
     *   those. No mutation before validation success.
     */
    bool validateOverrides();
    /**
     * @brief Rewrite each existing override cell widget name/description using one-based displayed
     *   row and header label.
     * @note Age cells mention signed-hour extraction; source/node cells uniqueness. Requires intact
     *   standard headers, changes accessibility text only, no source writes or numeric changes.
     */
    void refreshOverrideAccessibility();
    /**
     * @brief Apply global hours, removed override keys and changed/new override hours sequentially
     *   through checked engine API calls.
     * @return Number of successful mutation calls before completion/first error; null engine
     *   returns zero.
     * @note Caller must first validate complete table and reset m_writeFailed; this helper assumes
     *   all standard override combos/spins exist. preciseValue preserves hydrated exact hours while
     *   display unchanged; qFuzzyCompare(1+current,1+desired) suppresses small changes. Per-node
     *   keys use source enum and zero-based node index.
     * @note Remove engine keys absent from draft, then reread current override list for every
     *   staged row and set only missing/different values. First failing read/write sets
     *   m_writeFailed, inline diagnostic and field focus, preserving earlier successful changes. No
     *   transaction/rollback, signal, marker update or accept within helper itself.
     */
    int writeToEngine(); ///< returns the number of writes made

    SWMM_Engine m_engine = nullptr;
    QTableWidget *m_globalTable = nullptr;
    QTableWidget *m_overrideTable = nullptr;
    QLabel *m_hintLabel = nullptr;
    QLabel *m_validationError = nullptr;
    bool m_wroteAnyChanges = false;
    bool m_writeFailed = false;
    int m_lastWriteCount = 0;
};

} // namespace OpenSWMMVis

#endif // OPENSWMM_UI_DIALOGS_WATERAGESOURCESDIALOG_H
