/*!
 * \file   colorrampcombobox.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BB-α — QComboBox that previews built-in + custom colour
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
 */

#ifndef OPENSWMMVIS_UI_WIDGETS_COLORRAMPCOMBOBOX_H
#define OPENSWMMVIS_UI_WIDGETS_COLORRAMPCOMBOBOX_H

#include "render/colorramp.h"

#include <QComboBox>

class ColorRampComboBox : public QComboBox
{
    Q_OBJECT

public:
    explicit ColorRampComboBox(QWidget *parent = nullptr);
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
     *  labels). No-op when no item matches. */
    void setCurrentRampByName(const QString &name);

    /*! Select the ramp named `name`; when no row carries that name (a
     *  custom ramp authored on another machine, or one since deleted from
     *  the preferences library) a transient "custom" row is inserted from
     *  the supplied payload so the swatch and selection stay truthful. */
    void ensureRampSelected(const QString &name, const RasterColorRamp &ramp);

    /*! Pre-populates the combo with built-ins. Custom user ramps loaded
     *  from `PreferencesManager::customColorRamps()` are appended below
     *  a separator. */
    void rebuildItems();

signals:
    /*! Emitted whenever the user picks a ramp from the dropdown. Not
     *  emitted for the "Edit Custom Ramp…" sentinel — for that case
     *  `customRampSaved` fires after the editor dialog returns. */
    void rampChanged(const RasterColorRamp &ramp);

    /*! Emitted after the user saves a new ramp from the editor dialog
     *  (Edit Custom Ramp…). The new ramp is appended to the combo and
     *  also persisted to PreferencesManager. */
    void customRampSaved(const QString &name, const RasterColorRamp &ramp);

protected:
    /*! Slice S4 — override the QComboBox default text-only closed state
     *  so the gradient swatch keeps rendering after selection. QGIS /
     *  ArcGIS Pro idiom. */
    void paintEvent(QPaintEvent *event) override;

private slots:
    void onActivated(int index);

private:
    void appendBuiltins();
    void appendCustoms();
    void appendEditSentinel();
    void openEditor();

    /*! Row of the last real ramp selection (builtin/custom). Restored when
     *  the "Edit Custom Ramp…" flow is cancelled — previously the cancel
     *  path reset to row 0, silently switching the user's ramp to
     *  Grayscale. */
    int m_lastRampRow = 0;
};

#endif // OPENSWMMVIS_UI_WIDGETS_COLORRAMPCOMBOBOX_H
