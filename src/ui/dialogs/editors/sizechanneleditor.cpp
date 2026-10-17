/*!
 * \file   sizechanneleditor.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/editors/sizechanneleditor.h"

#include "render/sublayers/feature/featuresublayerstyle.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>

namespace openswmmvis::ui {

using OpenSWMM::Render::LineFeatureSublayerStyle;
using OpenSWMM::Render::PointFeatureSublayerStyle;
using OpenSWMM::Render::RangeMode;
using OpenSWMM::Render::SizeChannel;

SizeChannelEditor::SizeChannelEditor(OpenSWMM::Render::FeatureSublayerStyle *style,
                                     const QVector<OpenSWMM::Render::AttributeField> &fields,
                                     QWidget *parent)
    : QWidget(parent), m_style(style)
{
    m_point = qobject_cast<PointFeatureSublayerStyle *>(style) != nullptr;

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *box = new QGroupBox(m_point ? tr("Size scale — node area")
                                      : tr("Size scale — line width"), this);
    outer->addWidget(box);
    outer->addStretch();
    auto *form = new QFormLayout(box);

    m_enabled = new QCheckBox(tr("Size by value"), box);
    m_enabled->setObjectName(QStringLiteral("sizeChannelEnabled"));
    form->addRow(m_enabled);

    m_attribute = new QComboBox(box);
    m_attribute->setObjectName(QStringLiteral("sizeChannelAttribute"));
    for (const auto &f : fields) {
        // The channel samples per-frame result vectors.
        if (f.type == QMetaType::QString || !f.isDynamic)
            continue;
        m_attribute->addItem(f.displayName.isEmpty() ? f.name : f.displayName, f.name);
    }
    form->addRow(tr("Size &by:"), m_attribute);

    m_transform = new QComboBox(box);
    m_transform->addItem(tr("Linear"), int(SizeChannel::Transform::Linear));
    m_transform->addItem(tr("Square root"), int(SizeChannel::Transform::Sqrt));
    m_transform->addItem(tr("Logarithmic (log 1+value)"), int(SizeChannel::Transform::Log));
    form->addRow(tr("&Scale:"), m_transform);

    m_range = new QComboBox(box);
    m_range->setObjectName(QStringLiteral("sizeChannelRange"));
    m_range->addItem(tr("Whole run"), int(RangeMode::FixedOverRun));
    m_range->addItem(tr("Each frame"), int(RangeMode::PerFrameAutoStretch));
    m_range->addItem(tr("Custom"), int(RangeMode::FixedUser));
    auto makeValue = [box] {
        auto *s = new QDoubleSpinBox(box);
        // A finite range keeps the box readable: a ±max-double range sizes
        // it for a 300-digit number.
        s->setRange(-1e9, 1e9);
        s->setDecimals(4);
        s->setKeyboardTracking(false);
        return s;
    };
    m_userMin = makeValue();
    m_userMax = makeValue();
    m_zeroBased = new QCheckBox(tr("Start at zero"), box);
    m_zeroBased->setToolTip(tr("Size from zero rather than from the smallest value, so "
                               "sizes stay proportional to the value."));
    auto *rangeRow = new QWidget(box);
    {
        auto *h = new QHBoxLayout(rangeRow);
        h->setContentsMargins(0, 0, 0, 0);
        h->addWidget(m_range);
        h->addWidget(new QLabel(tr("min"), rangeRow));
        h->addWidget(m_userMin);
        h->addWidget(new QLabel(tr("max"), rangeRow));
        h->addWidget(m_userMax);
        h->addStretch();
    }
    form->addRow(tr("Value &range:"), rangeRow);
    form->addRow(QString(), m_zeroBased);

    auto makePx = [box] {
        auto *s = new QDoubleSpinBox(box);
        s->setRange(0.0, 256.0);
        s->setDecimals(2);
        s->setSingleStep(0.5);
        s->setSuffix(QStringLiteral(" px"));
        s->setKeyboardTracking(false);
        return s;
    };
    m_minPx = makePx();
    m_maxPx = makePx();
    auto *pxRow = new QWidget(box);
    {
        auto *h = new QHBoxLayout(pxRow);
        h->setContentsMargins(0, 0, 0, 0);
        h->addWidget(new QLabel(tr("min"), pxRow));
        h->addWidget(m_minPx);
        h->addWidget(new QLabel(QStringLiteral("–"), pxRow));
        h->addWidget(new QLabel(tr("max"), pxRow));
        h->addWidget(m_maxPx);
        h->addStretch();
    }
    form->addRow(m_point ? tr("&Diameter:") : tr("&Width:"), pxRow);

    m_hint = new QLabel(box);
    m_hint->setWordWrap(true);
    m_hint->setText(m_point
        ? tr("Marker area follows the selected value scale, independently of the "
             "colour renderer.")
        : tr("Line width follows the selected value scale, independently of the "
             "colour renderer."));
    form->addRow(m_hint);

    auto pushLater = [this] { push(); };
    connect(m_enabled, &QCheckBox::toggled, this, pushLater);
    connect(m_zeroBased, &QCheckBox::toggled, this, pushLater);
    for (QComboBox *c : {m_attribute, m_transform, m_range})
        connect(c, qOverload<int>(&QComboBox::currentIndexChanged), this, pushLater);
    for (QDoubleSpinBox *s : {m_userMin, m_userMax, m_minPx, m_maxPx})
        connect(s, qOverload<double>(&QDoubleSpinBox::valueChanged), this, pushLater);

    if (m_style)
        connect(m_style, &OpenSWMM::Render::SublayerStyle::styleChanged,
                this, &SizeChannelEditor::refreshFromModel);
    refreshFromModel();
}

SizeChannel SizeChannelEditor::channel() const
{
    if (auto *ps = qobject_cast<PointFeatureSublayerStyle *>(m_style.data()))
        return ps->sizeChannel();
    if (auto *ls = qobject_cast<LineFeatureSublayerStyle *>(m_style.data()))
        return ls->widthChannel();
    return {};
}

void SizeChannelEditor::refreshFromModel()
{
    if (!m_style || m_suppress) return;
    SizeChannel ch = channel();
    // Q4 — a channel never switched on starts from the kind's colour
    // attribute and its static size → 3× that.
    if (!ch.enabled && ch.attribute.isEmpty()) {
        const int colourRow = m_attribute->findData(m_style->attribute(), Qt::UserRole,
                                                    Qt::MatchFixedString);
        if (colourRow >= 0)
            ch.attribute = m_attribute->itemData(colourRow).toString();
        else if (m_attribute->count() > 0)
            ch.attribute = m_attribute->itemData(0).toString();
        double base = 0.0;
        if (auto *ps = qobject_cast<PointFeatureSublayerStyle *>(m_style.data()))
            base = ps->markerSizePx();
        else if (auto *ls = qobject_cast<LineFeatureSublayerStyle *>(m_style.data()))
            base = ls->lineWidthPx();
        if (base > 0.0) {
            ch.minPx = base;
            ch.maxPx = 3.0 * base;
        }
    }

    m_suppress = true;
    const QSignalBlocker b0(m_enabled), b1(m_attribute), b2(m_transform), b3(m_range),
        b4(m_userMin), b5(m_userMax), b6(m_zeroBased), b7(m_minPx), b8(m_maxPx);
    m_enabled->setChecked(ch.enabled);
    int row = m_attribute->findData(ch.attribute);
    if (row < 0 && !ch.attribute.isEmpty()) {
        m_attribute->addItem(ch.attribute, ch.attribute);
        row = m_attribute->count() - 1;
    }
    m_attribute->setCurrentIndex(row);
    m_transform->setCurrentIndex(m_transform->findData(int(ch.transform)));
    m_range->setCurrentIndex(m_range->findData(int(ch.rangeMode)));
    m_userMin->setValue(ch.userMin);
    m_userMax->setValue(ch.userMax);
    m_zeroBased->setChecked(ch.zeroBased);
    m_minPx->setValue(ch.minPx);
    m_maxPx->setValue(ch.maxPx);
    m_suppress = false;
    updateEnabled();
}

void SizeChannelEditor::updateEnabled()
{
    const bool on = m_enabled->isChecked();
    const bool custom = m_range->currentData().toInt() == int(RangeMode::FixedUser);
    for (QWidget *w : std::initializer_list<QWidget *>{m_attribute, m_transform, m_range,
                                                       m_zeroBased, m_minPx, m_maxPx})
        w->setEnabled(on);
    m_userMin->setEnabled(on && custom);
    m_userMax->setEnabled(on && custom);
}

void SizeChannelEditor::push()
{
    if (m_suppress || !m_style) return;
    updateEnabled();
    SizeChannel ch = channel();
    ch.enabled   = m_enabled->isChecked();
    ch.attribute = m_attribute->currentData().toString();
    ch.transform = static_cast<SizeChannel::Transform>(m_transform->currentData().toInt());
    ch.rangeMode = static_cast<RangeMode>(m_range->currentData().toInt());
    ch.userMin   = std::min(m_userMin->value(), m_userMax->value());
    ch.userMax   = std::max(m_userMin->value(), m_userMax->value());
    ch.zeroBased = m_zeroBased->isChecked();
    ch.minPx     = std::min(m_minPx->value(), m_maxPx->value());
    ch.maxPx     = std::max(m_minPx->value(), m_maxPx->value());
    m_suppress = true;   // the style's styleChanged would re-seed mid-edit
    if (auto *ps = qobject_cast<PointFeatureSublayerStyle *>(m_style.data()))
        ps->setSizeChannel(ch);
    else if (auto *ls = qobject_cast<LineFeatureSublayerStyle *>(m_style.data()))
        ls->setWidthChannel(ch);
    m_suppress = false;
}

} // namespace openswmmvis::ui
