/*!
 * \file   colorrampeditordialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Custom colour-ramp editor (redesigned 2026-08).
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

namespace openswmmvis::ui {

class ColorRampEditorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ColorRampEditorDialog(const RasterColorRamp &initial,
                                   QWidget *parent = nullptr);
    ~ColorRampEditorDialog() override;

    /*! \brief Current edited ramp. */
    [[nodiscard]] RasterColorRamp ramp() const { return m_ramp; }

    /*! Name the ramp will be saved under. Empty when the user left the
     *  name blank (callers treat that as a declined save). */
    [[nodiscard]] QString rampName() const;
    void setRampName(const QString &name);

private slots:
    void onPresetChanged(int index);
    void onInterpChanged(int index);
    void onReverseClicked();
    void onAddStop();
    void onRemoveStop();
    void onStopEdited();
    void onNameEdited(const QString &text);
    void onLoadSaved();
    void onDeleteSaved();

private:
    void buildUi();
    void rebuildStopTable();
    void readStopsFromTable();
    void refreshSavedCombo();
    void refreshOkEnabled();
    void markCustomised();

    RasterColorRamp m_ramp;
    bool m_suppress = false;   //!< guards table-driven re-entrancy

    QLineEdit    *m_nameEdit     = nullptr;
    QLabel       *m_nameWarning  = nullptr;
    QComboBox    *m_presetCombo  = nullptr;
    QComboBox    *m_interpCombo  = nullptr;
    QToolButton  *m_reverseBtn   = nullptr;
    QWidget      *m_preview      = nullptr;   // GradientPreviewWidget
    QTableWidget *m_stopTable    = nullptr;
    QPushButton  *m_addStopBtn   = nullptr;
    QPushButton  *m_removeStopBtn = nullptr;
    QComboBox    *m_savedCombo   = nullptr;
    QPushButton  *m_loadSavedBtn = nullptr;
    QPushButton  *m_deleteSavedBtn = nullptr;
    QPushButton  *m_okBtn        = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_COLORRAMPEDITORDIALOG_H
