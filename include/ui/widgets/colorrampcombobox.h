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

    /*! Selects the first item whose ramp matches `name` (case-insensitive,
     *  matched against built-in display names and against user-ramp
     *  labels). No-op when no item matches. */
    void setCurrentRampByName(const QString &name);

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
};

#endif // OPENSWMMVIS_UI_WIDGETS_COLORRAMPCOMBOBOX_H
