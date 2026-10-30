/**
 * @file      colorrampcombobox.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BB-α — QComboBox that previews built-in + custom colour
 *         ramps as horizontal gradient swatches in the dropdown.
 *
 *         Mirrors the QGIS / ArcGIS Pro idiom: each row shows the actual
 *         gradient (not just a name) so the user picks visually. The
 *         trailing "Edit Custom Ramp…" row opens the existing
 *         ColorRampEditorDialog (WIP carved 2026-05-22) to author a new
 *         ramp; on OK the ramp is appended to the user's custom-ramp
 *         library (PreferencesManager → Rendering/CustomRamps) and
 *         selected.
 *
 *         Consumers wire it like any QComboBox:
 *           connect(combo, &ColorRampComboBox::rampChanged, layer,
 *                   [](const RasterColorRamp &r){ layer->setColorRamp(r); });
 *
 *         Cross-slice: GUI_IMPLEMENTATION_PLAN.md §L.BB-α Phase BB-α.2.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_WIDGETS_COLORRAMPCOMBOBOX_H
#define OPENSWMMVIS_UI_WIDGETS_COLORRAMPCOMBOBOX_H

#include "render/colorramp.h"

#include <QComboBox>

class ColorRampComboBox : public QComboBox
{
    Q_OBJECT

public:
    /**
     * @brief Create the gradient-preview combo from built-in and persisted custom ramps.
     * @param[in] parent Optional QWidget owner. Initially selects the first builtin; uses a widget-owned custom delegate.
     */
    explicit ColorRampComboBox(QWidget *parent = nullptr);
    /**
     * @brief Destroy the combo and its owned delegate/model helpers.
     */
    ~ColorRampComboBox() override;

    /*! Currently-selected ramp (built-in or user-custom). Returns
     *  RasterColorRamp::grayscale() when the "Edit Custom Ramp…"
     *  sentinel row is the active selection. */
    [[nodiscard]] RasterColorRamp currentRamp() const;

    /*! True when the current selection is a user-authored (custom) ramp
     *  rather than a built-in. Consumers that persist ramps by NAME must
     *  check this and store the full payload instead — builtin-name lookup
     *  degrades unknown names to grayscale. */
    [[nodiscard]] bool currentIsCustom() const;

    /*! Selects the first item whose ramp matches `name` (case-insensitive,
     *  matched against built-in display names and against user-ramp
     *  labels). No-op when no item matches.
     * @param[in] name Case-insensitive row display label; the first text match wins without checking row kind.
     * @note Programmatic selection updates m_lastRampRow but does not emit rampChanged(), which is driven by activation/editor acceptance.
     */
    void setCurrentRampByName(const QString &name);

    /*! Select the ramp named `name`; when no row carries that name (a
     *  custom ramp authored on another machine, or one since deleted from
     *  the preferences library) a transient "custom" row is inserted from
     *  the supplied payload so the swatch and selection stay truthful.
     * @param[in] name Case-insensitive display label; a matching existing row is reused without replacing its payload.
     * @param[in] ramp Fallback embedded payload used only if no row matches name. A new transient row is not persisted to preferences.
     * @note Programmatic selection does not emit rampChanged().
     */
    void ensureRampSelected(const QString &name, const RasterColorRamp &ramp);

    /*! Pre-populates the combo with built-ins. Custom user ramps loaded
     *  from `PreferencesManager::customColorRamps()` are appended below
     *  a separator.
     * @note Clears/recreates rows and resets selection to the first row. Signals are blocked during reconstruction and explicitly unblocked afterward; an outer blocked state is not preserved. Transient ramps are lost and m_lastRampRow is not recalculated.
     */
    void rebuildItems();

signals:
    /*! Emitted whenever the user picks a ramp from the dropdown. Not
     *  emitted for the "Edit Custom Ramp…" sentinel — for that case
     *  `customRampSaved` fires after the editor dialog returns.
     * @param[in] ramp Activated or newly saved ramp payload. A successful custom editor emits this after customRampSaved().
     */
    void rampChanged(const RasterColorRamp &ramp);

    /*! Emitted after the user saves a new ramp from the editor dialog
     *  (Edit Custom Ramp…). The new ramp is appended to the combo and
     *  also persisted to PreferencesManager.
     * @param[in] name Accepted nonempty custom ramp name saved through PreferencesManager.
     * @param[in] ramp Saved payload, copied into the rebuilt combo.
     */
    void customRampSaved(const QString &name, const RasterColorRamp &ramp);

protected:
    /*! Slice S4 — override the QComboBox default text-only closed state
     *  so the gradient swatch keeps rendering after selection. QGIS /
     *  ArcGIS Pro idiom.
     * @param[in] event Qt paint event, unused; standard combo chrome plus gradient/name are drawn, with text-only sentinel fallback.
     */
    void paintEvent(QPaintEvent *event) override;

private slots:
    /**
     * @brief Handle user row activation, opening the custom editor for its sentinel or forwarding a real ramp.
     * @param[in] index Activated current row; negative/separator rows are ignored.
     */
    void onActivated(int index);

private:
    /**
     * @brief Append all builtin ramp names/payloads in RasterColorRamp catalog order.
     */
    void appendBuiltins();
    /**
     * @brief Append a disabled separator and the persisted custom-ramp map, if nonempty.
     * @note Reads preferences only; caller rebuildItems() controls signal suppression.
     */
    void appendCustoms();
    /**
     * @brief Append a disabled separator then the selectable Edit Custom Ramp action row.
     */
    void appendEditSentinel();
    /**
     * @brief Open the modal ramp editor seeded from the last ramp selection (Viridis fallback).
     * @note Cancel or empty accepted name restores the previous row. Successful acceptance persists the ramp, rebuilds/selects its row and emits customRampSaved() then rampChanged(). Preferences write success is not separately checked here.
     */
    void openEditor();

    /*! Row of the last real ramp selection (builtin/custom). Restored when
     *  the "Edit Custom Ramp…" flow is cancelled — previously the cancel
     *  path reset to row 0, silently switching the user's ramp to
     *  Grayscale. */
    int m_lastRampRow = 0;
};

#endif // OPENSWMMVIS_UI_WIDGETS_COLORRAMPCOMBOBOX_H
