/**
 * @file      labelconfigeditor.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Reusable full-fidelity editor for a LabelConfig value.
 *
 *         LAYER_STYLING_LABELING_PLAN_2026-08-16 — the per-sublayer label
 *         boxes (kind tree panel, feature style editors) previously exposed
 *         only enabled / expression / colour, silently dropping the other
 *         ~12 LabelConfig fields the paint paths honour (font, halo,
 *         placement, scale window, background, priority). This widget edits
 *         the complete value so every surface offers the same controls.
 *
 *         Value-in / value-out: the caller seeds it with setConfig() and
 *         receives the whole edited LabelConfig via configChanged(). No
 *         layer/style coupling.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_WIDGETS_LABELCONFIGEDITOR_H
#define OPENSWMMVIS_UI_WIDGETS_LABELCONFIGEDITOR_H

#include "render/iattributeprovider.h"
#include "render/labelconfig.h"

#include <QVector>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFontComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QToolButton;

namespace openswmmvis::ui
{

class ColorButton;

class LabelConfigEditor : public QWidget
{
    Q_OBJECT
public:
    /**
     * @brief Build value-based label controls and connect edits to full-config notifications.
     * @param[in] parent Optional QWidget owner. Hosts should call setConfig() before displaying model values; this widget owns no layer.
     */
    explicit LabelConfigEditor(QWidget *parent = nullptr);

    /*! Seed every control from \p cfg without emitting configChanged.
     * @param[in] cfg Label config copied into the draft; Qt control ranges/precision apply to displayed values. No layer mutation occurs.
     */
    void setConfig(const OpenSWMM::Render::LabelConfig &cfg);
    /**
     * @brief Read a complete value composed from current controls and untouched fields of the seeded draft.
     * @return Label config with font points, halo/background pixels, scale denominators (zero unbounded), decimals -1 for auto or 0–10, and current field/expression strings. No validation occurs.
     */
    [[nodiscard]] OpenSWMM::Render::LabelConfig config() const;

    /*!
     * \brief The layer's themeable attributes for this sublayer's category.
     *
     *        Drives the Field and Priority-field pickers (which are combos,
     *        not free text — a mistyped field name silently produces blank
     *        labels) and the Expression builder's field list. Priority only
     *        offers numeric fields, since it is sorted on. Both combos stay
     *        editable so a name this layer no longer advertises — a legacy
     *        project, a renamed result variable — survives a round trip
     *        instead of being erased on open.
     *
     *        Pass an empty list when the host implements no
     *        IAttributeProvider; the combos then hold whatever the config
     *        carries and behave like the old free-text fields.
     * @param[in] fields Attribute metadata copied into pickers/expression tokens. Priority filtering excludes QString fields; other types remain available.
     * @note Refills pickers with signals blocked, preserving their existing key/text and emits no configChanged().
     */
    void setAvailableFields(const QVector<OpenSWMM::Render::AttributeField> &fields);

signals:
    /*! Emitted after any user edit, carrying the full edited value.
     * @param[in] cfg Complete current draft emitted after an unsuppressed edit; no direct layer write is performed.
     */
    void configChanged(const OpenSWMM::Render::LabelConfig &cfg);

private:
    /**
     * @brief Rebuild the held config from controls and emit configChanged(), unless hydration suppression is active.
     */
    void emitChanged();
    /*! Refills a field picker, preserving whatever text it already holds.
     * @param[in,out] combo Required editable field picker to refill; its signals are blocked during replacement.
     * @param[in] numericOnly Exclude fields whose QMetaType is QString; does not require a stricter numeric metatype.
     * @note Adds an empty identity row and restores the prior field key/text even if no longer advertised.
     */
    void populateFieldCombo(QComboBox *combo, bool numericOnly) const;
    /*! Opens the expression builder seeded with the current expression.
     * @note Cancel leaves text unchanged; acceptance copies the builder expression and emits the full updated draft through emitChanged().
     */
    void openExpressionBuilder();

    OpenSWMM::Render::LabelConfig m_cfg;
    QVector<OpenSWMM::Render::AttributeField> m_fields;
    bool m_suppress = false;

    QCheckBox *m_enabledChk = nullptr;
    QComboBox *m_fieldCombo = nullptr;
    QLineEdit *m_exprEdit = nullptr;
    QToolButton *m_exprBuildBtn = nullptr;
    QSpinBox *m_decimalsSpin = nullptr; // feedback C4 — Auto / 0–10
    QFontComboBox *m_fontCombo = nullptr;
    QDoubleSpinBox *m_fontSizeSpin = nullptr;
    QToolButton *m_boldBtn = nullptr;
    QToolButton *m_italicBtn = nullptr;
    ColorButton *m_colorBtn = nullptr;
    QCheckBox *m_haloChk = nullptr;
    ColorButton *m_haloColorBtn = nullptr;
    QDoubleSpinBox *m_haloRadSpin = nullptr;
    QComboBox *m_placementCombo = nullptr;
    QDoubleSpinBox *m_minScaleSpin = nullptr;
    QDoubleSpinBox *m_maxScaleSpin = nullptr;
    QCheckBox *m_bgChk = nullptr;
    ColorButton *m_bgColorBtn = nullptr;
    QDoubleSpinBox *m_bgPadSpin = nullptr;
    QDoubleSpinBox *m_bgRadiusSpin = nullptr;
    QComboBox *m_priorityCombo = nullptr;
    QLabel *m_hintLabel = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_LABELCONFIGEDITOR_H
