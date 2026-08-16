/*!
 * \file   labelconfigeditor.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Reusable full-fidelity editor for a LabelConfig value.
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
class QToolButton;

namespace openswmmvis::ui {

class ColorButton;

class LabelConfigEditor : public QWidget
{
    Q_OBJECT
public:
    explicit LabelConfigEditor(QWidget *parent = nullptr);

    /*! Seed every control from \p cfg without emitting configChanged. */
    void setConfig(const OpenSWMM::Render::LabelConfig &cfg);
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
     */
    void setAvailableFields(
        const QVector<OpenSWMM::Render::AttributeField> &fields);

signals:
    /*! Emitted after any user edit, carrying the full edited value. */
    void configChanged(const OpenSWMM::Render::LabelConfig &cfg);

private:
    void emitChanged();
    /*! Refills a field picker, preserving whatever text it already holds. */
    void populateFieldCombo(QComboBox *combo, bool numericOnly) const;
    /*! Opens the expression builder seeded with the current expression. */
    void openExpressionBuilder();

    OpenSWMM::Render::LabelConfig m_cfg;
    QVector<OpenSWMM::Render::AttributeField> m_fields;
    bool m_suppress = false;

    QCheckBox      *m_enabledChk   = nullptr;
    QComboBox      *m_fieldCombo   = nullptr;
    QLineEdit      *m_exprEdit     = nullptr;
    QToolButton    *m_exprBuildBtn = nullptr;
    QFontComboBox  *m_fontCombo    = nullptr;
    QDoubleSpinBox *m_fontSizeSpin = nullptr;
    QToolButton    *m_boldBtn      = nullptr;
    QToolButton    *m_italicBtn    = nullptr;
    ColorButton    *m_colorBtn     = nullptr;
    QCheckBox      *m_haloChk      = nullptr;
    ColorButton    *m_haloColorBtn = nullptr;
    QDoubleSpinBox *m_haloRadSpin  = nullptr;
    QComboBox      *m_placementCombo = nullptr;
    QDoubleSpinBox *m_minScaleSpin = nullptr;
    QDoubleSpinBox *m_maxScaleSpin = nullptr;
    QCheckBox      *m_bgChk        = nullptr;
    ColorButton    *m_bgColorBtn   = nullptr;
    QDoubleSpinBox *m_bgPadSpin    = nullptr;
    QComboBox      *m_priorityCombo = nullptr;
    QLabel         *m_hintLabel    = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_LABELCONFIGEDITOR_H
