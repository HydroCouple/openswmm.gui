/*!
 * \file   sublayertabhelpers.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Small shared builders for the per-sublayer styling panels
 *         (Swmm2DResultsStylePanel, Swmm2DMeshStylePanel): spin-box
 *         factories with the shared minimum widths, and the "Show <name>"
 *         + opacity header row every tab starts with. Hoisted out of the
 *         results panel's anonymous namespace when the mesh panel became
 *         its structural sibling.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_SUBLAYERTABHELPERS_H
#define OPENSWMMVIS_UI_DIALOGS_SUBLAYERTABHELPERS_H

#include "render/isublayer.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QObject>
#include <QSpinBox>
#include <QString>
#include <QWidget>

namespace openswmmvis::ui {

// Minimum field widths — keeps the QFormLayouts from compressing the
// editors when the dialog is narrow; the per-tab scroll areas pick up the
// slack instead.
inline constexpr int kSpinMinWidthPx  = 110;
inline constexpr int kComboMinWidthPx = 140;

inline QDoubleSpinBox *makeDSpin(QWidget *parent, double lo, double hi, double step,
                                 int decimals, double value,
                                 const QString &suffix = QString())
{
    auto *s = new QDoubleSpinBox(parent);
    s->setRange(lo, hi);
    s->setSingleStep(step);
    s->setDecimals(decimals);
    if (!suffix.isEmpty()) s->setSuffix(suffix);
    s->setValue(value);
    s->setMinimumWidth(kSpinMinWidthPx);
    return s;
}

inline QSpinBox *makeSpin(QWidget *parent, int lo, int hi, int value)
{
    auto *s = new QSpinBox(parent);
    s->setRange(lo, hi);
    s->setValue(value);
    s->setMinimumWidth(kSpinMinWidthPx);
    return s;
}

/*! "Show <name>" checkbox + opacity spin bound to the sublayer's
 *  visibility / opacity — the shared header row of every tab. */
inline QWidget *makeSublayerHeader(QWidget *parent, OpenSWMM::Render::ISublayer *sub,
                                   const QString &showLabel)
{
    auto *row  = new QWidget(parent);
    auto *form = new QFormLayout(row);
    form->setContentsMargins(0, 0, 0, 0);

    auto *show = new QCheckBox(showLabel, row);
    show->setChecked(sub && sub->isVisible());
    form->addRow(QString(), show);

    auto *opacity = makeDSpin(row, 0.0, 1.0, 0.05, 2,
                              sub ? double(sub->opacity()) : 1.0);
    form->addRow(QObject::tr("Opacity:"), opacity);

    if (sub) {
        QObject::connect(show, &QCheckBox::toggled, sub,
                         [sub](bool on) { sub->setVisible(on); });
        QObject::connect(opacity, qOverload<double>(&QDoubleSpinBox::valueChanged),
                         sub, [sub](double a) { sub->setOpacity(a); });
    }
    return row;
}

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_SUBLAYERTABHELPERS_H
