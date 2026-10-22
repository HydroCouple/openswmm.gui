/**
 * @file      colorrampeditordialog.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Custom colour-ramp editor (redesigned 2026-08).
 *
 * Authors a stand-alone RasterColorRamp for the user's custom-ramp library
 * (PreferencesManager → Rendering/CustomRamps). Layout:
 *   ┌─ Name: [            ]  (overwrite warning when it collides) ────┐
 *   ├─ Preset ▾ (all builtins)   Interpolation ▾   [Reverse]         ─┤
 *   ├─ [ live gradient preview — repaints on edit and resize ]       ─┤
 *   ├─ Stop table: Position | Colour  (+ Add / Remove stop)          ─┤
 *   ├─ Saved custom ramps: ▾  [Load] [Delete]                        ─┤
 *   └─ OK / Cancel (OK disabled until named and ≥ 2 stops)           ─┘
 *
 * Unlike the previous incarnation, stop POSITIONS are genuinely editable
 * (the old dialog only recoloured evenly spaced samples), presets cover
 * the full builtin catalogue via RasterColorRamp::builtinNames(), naming
 * happens in-dialog (no follow-up QInputDialog), and custom ramps can be
 * deleted (first caller of PreferencesManager::removeCustomColorRamp).
 * The caller reads the result via ramp() / rampName() on accept and does
 * its own saveCustomColorRamp — the dialog persists nothing on OK.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_COLORRAMPEDITORDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_COLORRAMPEDITORDIALOG_H

#include "render/colorramp.h"

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QToolButton;

namespace openswmmvis::ui
{

class ColorRampEditorDialog : public QDialog
{
    Q_OBJECT
public:
    /**
     * @param[in] initial Value-copied ramp; fewer than two stops replaces entire value with
     *   viridis, otherwise raw ordering/metadata retained initially.
     * @param[in] parent Optional QWidget owner; caller manages parentless dialog.
     * @brief Build local ramp/name editor and load saved-ramp names on GUI thread without saving a
     *   new ramp.
     * @note Initial name is blank and OK disabled until named. Slot changes usually replace
     *   stops/interpolation only, preserving other initial ramp metadata. Preferences Delete acts
     *   immediately and survives Cancel; ordinary edits remain local until caller saves returned
     *   result.
     */
    explicit ColorRampEditorDialog(const RasterColorRamp &initial, QWidget *parent = nullptr);
    /**
     * @brief Destroy owned widgets/local ramp; no implicit preference save, undo or rollback of
     *   explicit library deletions.
     */
    ~ColorRampEditorDialog() override;

    /*! \brief Current edited ramp.
     * @return Current owned ramp value copy, without rereading table or checking dialog acceptance.
     *   Stop edits already synchronize it; untouched imported positions retain full original
     *   precision until table is reread. Caller applies/persists only after accepted result.
 */
    [[nodiscard]] RasterColorRamp ramp() const { return m_ramp; }

    /*! Name the ramp will be saved under. Empty when the user left the
     *  name blank (callers treat that as a declined save).
     * @return Current name trimmed at both ends, possibly empty, without checking acceptance,
     *   collision or storage validity. No persistence.
 */
    [[nodiscard]] QString rampName() const;
    /**
     * @param[in] name Raw copied editor text; getter/validation trims it later.
     * @brief Replace name text and allow textChanged to recompute advisory overwrite and OK state.
     *   Does not rename saved library entry or update ramp metadata/save preferences.
     */
    void setRampName(const QString &name);

private slots:
    /**
     * @param[in] index Activated combo row; <=0 custom placeholder is no-op. Positive row text is
     *   passed to builtin lookup without extra bounds validation.
     * @brief Replace stops/interpolation with chosen builtin and rebuild table/preview,
     *   synchronizing interpolation combo with signals blocked.
     * @note Preserve other m_ramp metadata and editor name; no library save or overwrite
     *   confirmation.
     */
    void onPresetChanged(int index);
    /**
     * @param[in] index Combo row whose item data is cast directly to RampInterp; invalid data
     *   converts to zero.
     * @brief Store interpolation, mark preset custom and repaint preview.
     * @note No enum validation, stop resampling, table rebuild, save or undo; called directly from
     *   combo change.
     */
    void onInterpChanged(int index);
    /**
     * @brief Reverse current ramp stops and map each position to 1-position, mark custom and
     *   rebuild editors/preview.
     * @note Uses current m_ramp rather than rereading table, preserves colors/interpolation/other
     *   metadata. Raw invalid positions are not independently normalized; no persistence.
     */
    void onReverseClicked();
    /**
     * @brief Read/sort displayed stops, insert midpoint of first widest adjacent gap using
     *   interpolated current color, then stable-sort/rebuild.
     * @note Does not consider gaps to 0/1 outside existing stop span; fewer than two valid table
     *   stops uses position 0.5. Duplicate positions permitted. Reading table first quantizes all
     *   positions to three-decimal editor values. Marks custom, no save.
     */
    void onAddStop();
    /**
     * @brief With selected row and more than two table rows, read/sort stops, remove that NUMERIC
     *   row index from sorted ramp and rebuild.
     * @note Table itself is not reordered after position edits, so after crossing positions the
     *   removed sorted index can differ from visibly selected stop. Missing/too-small selection is
     *   no-op; no endpoint protection, library write or undo.
     */
    void onRemoveStop();
    /**
     * @brief Unless rebuild suppression active, reread/sort all position/color editors into ramp,
     *   mark custom and repaint preview.
     * @note Does not reorder visible table or rebuild controls; all positions become their
     *   displayed three-decimal values. Duplicate stops/colors accepted without endpoint
     *   validation. No preferences write.
     */
    void onStopEdited();
    /**
     * @param[in] text Raw current editor text, trimmed for collision lookup.
     * @brief Show advisory when exact trimmed name already exists in custom library and refresh OK
     *   enable state.
     * @note Collision does not disable OK or overwrite anything here; actual save/overwrite belongs
     *   to caller. Empty name hides warning.
     */
    void onNameEdited(const QString &text);
    /**
     * @brief For a currently existing saved combo name, copy its stops/interpolation into local
     *   ramp, set name and rebuild preview/table.
     * @note Empty/missing entry no-op. Other ramp metadata preserved; interpolation signals
     *   blocked, preset combo not changed to reflect loaded ramp. Loading neither saves current
     *   edits nor persists a new value.
     */
    void onLoadSaved();
    /**
     * @brief Confirm and immediately remove named custom ramp from PreferencesManager, then refresh
     *   saved choices and name warning.
     * @note Empty name/cancel no-op. Deletion persists independently of dialog acceptance, has no
     *   undo and does not change current embedded edited ramp or already embedded layer styles.
     *   Host must preserve dialog lifetime through modal question.
     */
    void onDeleteSaved();

private:
    /**
     * @brief Create owned name, preset/interpolation, local-ramp preview, editable stop controls,
     *   saved-library actions and OK/Cancel.
     * @note OK directly accepts without persistence; current ramp pointer is borrowed by preview
     *   for dialog lifetime. Initial controls alone do not populate stop rows/saved list. No engine
     *   or layer binding.
     */
    void buildUi();
    /**
     * @brief Recreate position/color widgets for each current ramp stop while suppressing edit
     *   callbacks, then repaint and refresh OK.
     * @note Positions display clamped [0,1] with three decimals/0.05 step; m_ramp itself is NOT
     *   rewritten from those rounded/clamped displays until a later read. No sort/dedup/endpoint
     *   validation; clears suppression unconditionally rather than restoring prior state. Table
     *   owns new controls.
     */
    void rebuildStopTable();
    /**
     * @brief Replace only ramp stops with valid position-spin/color-button rows, stable-sorted by
     *   numeric position.
     * @note Missing editor pairs skipped. Reads displayed [0,1]/three-decimal positions without
     *   exact-value hydration; equal positions retain table order and are not merged. Does not
     *   reorder table, repaint, change preset/OK or save by itself.
     */
    void readStopsFromTable();
    /**
     * @brief Reload names from current preference map while blocking combo signals; restore
     *   previous exact name when present, otherwise default first.
     * @note Enables Load/Delete exactly when any name exists; map iteration determines order. No
     *   current ramp/name change, source subscription, library save or error reporting.
     */
    void refreshSavedCombo();
    /**
     * @brief Enable OK exactly for nonempty trimmed name and at least two m_ramp stops.
     * @note Does not validate stop position range/order/distinctness/colors, interpolation enum or
     *   name collision and does not save.
     */
    void refreshOkEnabled();
    /**
     * @brief Select custom preset placeholder with combo signals blocked. Does not change
     *   ramp/name, preview or preferences.
     */
    void markCustomised();

    RasterColorRamp m_ramp;
    bool m_suppress = false; //!< guards table-driven re-entrancy

    QLineEdit *m_nameEdit = nullptr;
    QLabel *m_nameWarning = nullptr;
    QComboBox *m_presetCombo = nullptr;
    QComboBox *m_interpCombo = nullptr;
    QToolButton *m_reverseBtn = nullptr;
    QWidget *m_preview = nullptr; // GradientPreviewWidget
    QTableWidget *m_stopTable = nullptr;
    QPushButton *m_addStopBtn = nullptr;
    QPushButton *m_removeStopBtn = nullptr;
    QComboBox *m_savedCombo = nullptr;
    QPushButton *m_loadSavedBtn = nullptr;
    QPushButton *m_deleteSavedBtn = nullptr;
    QPushButton *m_okBtn = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_COLORRAMPEDITORDIALOG_H
