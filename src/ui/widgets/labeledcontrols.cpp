/*!
 * \file   labeledcontrols.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/widgets/labeledcontrols.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>

namespace {
// Shared layout: 8 px label-to-control gap, no outer margins so the
// row composes cleanly under QFormLayout::addRow().
QHBoxLayout *makeRowLayout(QWidget *self)
{
    auto *lay = new QHBoxLayout(self);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(8);
    return lay;
}
} // namespace

// ── LabeledLineEdit ─────────────────────────────────────────────────────────

LabeledLineEdit::LabeledLineEdit(const QString &labelText, QWidget *parent)
    : QWidget(parent)
{
    auto *lay = makeRowLayout(this);
    m_label = new QLabel(labelText, this);
    m_edit  = new QLineEdit(this);
    lay->addWidget(m_label);
    lay->addWidget(m_edit, /*stretch=*/1);
    connect(m_edit, &QLineEdit::textChanged,
            this, &LabeledLineEdit::textChanged);
}

QString LabeledLineEdit::text() const           { return m_edit->text(); }
void    LabeledLineEdit::setText(const QString &v) { m_edit->setText(v); }

// ── LabeledDoubleSpin ───────────────────────────────────────────────────────

LabeledDoubleSpin::LabeledDoubleSpin(const QString &labelText,
                                     double         minimum,
                                     double         maximum,
                                     int            decimals,
                                     QWidget       *parent)
    : QWidget(parent)
{
    auto *lay = makeRowLayout(this);
    m_label = new QLabel(labelText, this);
    m_spin  = new QDoubleSpinBox(this);
    m_spin->setRange(minimum, maximum);
    m_spin->setDecimals(decimals);
    lay->addWidget(m_label);
    lay->addWidget(m_spin, /*stretch=*/1);
    connect(m_spin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &LabeledDoubleSpin::valueChanged);
}

double LabeledDoubleSpin::value() const     { return m_spin->value(); }
void   LabeledDoubleSpin::setValue(double v){ m_spin->setValue(v); }

// ── LabeledIntSpin ──────────────────────────────────────────────────────────

LabeledIntSpin::LabeledIntSpin(const QString &labelText,
                               int            minimum,
                               int            maximum,
                               QWidget       *parent)
    : QWidget(parent)
{
    auto *lay = makeRowLayout(this);
    m_label = new QLabel(labelText, this);
    m_spin  = new QSpinBox(this);
    m_spin->setRange(minimum, maximum);
    lay->addWidget(m_label);
    lay->addWidget(m_spin, /*stretch=*/1);
    connect(m_spin, qOverload<int>(&QSpinBox::valueChanged),
            this, &LabeledIntSpin::valueChanged);
}

int  LabeledIntSpin::value() const   { return m_spin->value(); }
void LabeledIntSpin::setValue(int v) { m_spin->setValue(v); }

// ── LabeledCombo ────────────────────────────────────────────────────────────

LabeledCombo::LabeledCombo(const QString &labelText, QWidget *parent)
    : QWidget(parent)
{
    auto *lay = makeRowLayout(this);
    m_label = new QLabel(labelText, this);
    m_combo = new QComboBox(this);
    lay->addWidget(m_label);
    lay->addWidget(m_combo, /*stretch=*/1);
    connect(m_combo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &LabeledCombo::currentIndexChanged);
}

void LabeledCombo::addItem(const QString &text, const QVariant &data)
{
    m_combo->addItem(text, data);
}

int      LabeledCombo::currentIndex() const { return m_combo->currentIndex(); }
QVariant LabeledCombo::currentData()  const { return m_combo->currentData(); }

void LabeledCombo::setCurrentIndex(int idx) { m_combo->setCurrentIndex(idx); }

void LabeledCombo::setCurrentData(const QVariant &data)
{
    const int idx = m_combo->findData(data);
    if (idx >= 0)
        m_combo->setCurrentIndex(idx);
}
