/**
 * @file      sizechanneleditor.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Editor for a results kind's size channel (node area / link width
 *         by result value), bound to the kind's sublayer style.
 *
 *         Edits write straight to the style (setSizeChannel / setWidthChannel);
 *         the editor re-seeds on the style's styleChanged, so a Cancel
 *         rollback or an edit from another view keeps it in sync.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_EDITORS_SIZECHANNELEDITOR_H
#define OPENSWMMVIS_UI_DIALOGS_EDITORS_SIZECHANNELEDITOR_H

#include "render/iattributeprovider.h"
#include "render/sizechannel.h"

#include <QPointer>
#include <QVector>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;

namespace OpenSWMM::Render
{
class FeatureSublayerStyle;
}

namespace openswmmvis::ui
{

class SizeChannelEditor : public QWidget
{
    Q_OBJECT
public:
    /**
     * @brief Build a live point-size or line-width channel editor and subscribe to style changes.
     * @param style Optional borrowed style tracked with QPointer; Point/Line styles expose
     * writable channels. Other types display a default channel but receive no channel write;
     * null/destroyed styles make refresh/push no-ops without disabling all existing controls.
     * @param fields Borrowed metadata consumed synchronously; offers dynamic fields except
     * QString-typed fields, not a general numeric-type validator. Copies names/display text
     * into controls and retains no references to the vector.
     * @param parent Optional Qt owner; caller owns a parentless widget.
     * Controls use value bounds plus/minus1e9 at4decimals and0..256pixels at2decimals.
     * Local widget factories return box-owned spins; change callbacks invoke immediate push
     * on GUI thread. No rendering/data sample, undo command or Cancel snapshot here.
     */
    SizeChannelEditor(OpenSWMM::Render::FeatureSublayerStyle *style,
                      const QVector<OpenSWMM::Render::AttributeField> &fields,
                      QWidget *parent = nullptr);

    /**
     * @brief Seed controls from the current channel without writing it or reentering refresh.
     * No-op for missing style or suppression. An unused channel seeds its attribute from
     * current color attribute or first offered field and min/max from positive static
     * size/three-times-size. Preserves an unknown nonempty channel attribute by appending
     * it. Blocks all child signals, applies display rounding/clamping, then updates enabled
     * state. Does not remove previously appended fields or validate range/transform enums.
     */
    void refreshFromModel();

private:
    /**
     * @brief Read the current point-size or line-width channel by value.
     * @return Owned channel from a live Point/Line style, default SizeChannel otherwise;
     * no engine read, style mutation or retained borrowed reference.
     */
    [[nodiscard]] OpenSWMM::Render::SizeChannel channel() const;
    /**
     * @brief Write a complete channel from current controls to a live Point/Line style.
     * No-op during suppression or after style destruction. Sorts min/max value and pixel
     * pairs before storage; original control order is not immediately reseeded. Copies
     * attribute/transform/range/zero-based state and uses zero-valued QVariant conversions
     * for invalid combo selections without separate validation. Suppresses only this editor's
     * refresh while setters notify other observers. No undo, atomic transaction or error result.
     */
    void push();
    /**
     * @brief Enable channel controls when Size by value is checked and custom bounds only in
     * FixedUser mode.
     * Does not inspect style lifetime, field availability, numeric validity or mutate a channel;
     * the enabled checkbox itself remains usable even without a live target.
     */
    void updateEnabled();

    QPointer<OpenSWMM::Render::FeatureSublayerStyle> m_style;
    bool m_point = false;

    QCheckBox *m_enabled = nullptr;
    QComboBox *m_attribute = nullptr;
    QComboBox *m_transform = nullptr;
    QComboBox *m_range = nullptr;
    QDoubleSpinBox *m_userMin = nullptr;
    QDoubleSpinBox *m_userMax = nullptr;
    QCheckBox *m_zeroBased = nullptr;
    QDoubleSpinBox *m_minPx = nullptr;
    QDoubleSpinBox *m_maxPx = nullptr;
    QLabel *m_hint = nullptr;
    bool m_suppress = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_EDITORS_SIZECHANNELEDITOR_H
