/*!
 * \file   sizechanneleditor.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Editor for a results kind's size channel (node area / link width
 *         by result value), bound to the kind's sublayer style.
 *
 *         Edits write straight to the style (setSizeChannel / setWidthChannel);
 *         the editor re-seeds on the style's styleChanged, so a Cancel
 *         rollback or an edit from another view keeps it in sync.
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

namespace OpenSWMM::Render {
class FeatureSublayerStyle;
}

namespace openswmmvis::ui {

class SizeChannelEditor : public QWidget
{
    Q_OBJECT
public:
    /*! \p style must be a Point or Line FeatureSublayerStyle; \p fields are
     *  the kind's attributes (only numeric, per-frame ones are offered). */
    SizeChannelEditor(OpenSWMM::Render::FeatureSublayerStyle *style,
                      const QVector<OpenSWMM::Render::AttributeField> &fields,
                      QWidget *parent = nullptr);

    void refreshFromModel();

private:
    [[nodiscard]] OpenSWMM::Render::SizeChannel channel() const;
    void push();
    void updateEnabled();

    QPointer<OpenSWMM::Render::FeatureSublayerStyle> m_style;
    bool m_point = false;

    QCheckBox      *m_enabled   = nullptr;
    QComboBox      *m_attribute = nullptr;
    QComboBox      *m_transform = nullptr;
    QComboBox      *m_range     = nullptr;
    QDoubleSpinBox *m_userMin   = nullptr;
    QDoubleSpinBox *m_userMax   = nullptr;
    QCheckBox      *m_zeroBased = nullptr;
    QDoubleSpinBox *m_minPx     = nullptr;
    QDoubleSpinBox *m_maxPx     = nullptr;
    QLabel         *m_hint      = nullptr;
    bool m_suppress = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_EDITORS_SIZECHANNELEDITOR_H
