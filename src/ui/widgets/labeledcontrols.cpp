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
#include <QToolButton>

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

// ── LabeledPickerCombo ──────────────────────────────────────────────────────

LabeledPickerCombo::LabeledPickerCombo(const QString &labelText, QWidget *parent)
    : QWidget(parent)
{
    auto *lay = makeRowLayout(this);
    if (!labelText.isEmpty()) {
        m_label = new QLabel(labelText, this);
        lay->addWidget(m_label);
    }
    m_combo = new QComboBox(this);
    m_combo->setEditable(false);
    m_combo->setSizeAdjustPolicy(QComboBox::AdjustToContentsOnFirstShow);
    lay->addWidget(m_combo, /*stretch=*/1);

    m_btn = new QToolButton(this);
    m_btn->setText(QStringLiteral("…"));
    m_btn->setToolTip(tr("Create a new data object"));
    m_btn->setAutoRaise(false);
    lay->addWidget(m_btn);

    connect(m_btn,   &QToolButton::clicked,
            this,    &LabeledPickerCombo::pickerClicked);
    connect(m_combo, &QComboBox::currentTextChanged,
            this,    &LabeledPickerCombo::currentTextChanged);
}

void LabeledPickerCombo::setItems(const QStringList &items, const QString &selected)
{
    QSignalBlocker block(m_combo);
    m_combo->clear();
    // Always offer an empty entry so the user can clear the selection
    // (most SWMM compound fields are optional — null pattern, null TS).
    m_combo->addItem(tr("(none)"), QString{});
    for (const QString &it : items)
        m_combo->addItem(it, it);
    if (!selected.isEmpty()) {
        const int idx = m_combo->findText(selected);
        if (idx >= 0) m_combo->setCurrentIndex(idx);
    } else {
        m_combo->setCurrentIndex(0);
    }
}

QString LabeledPickerCombo::currentText() const
{
    if (m_combo->currentIndex() == 0) return {};  // "(none)"
    return m_combo->currentText();
}

void LabeledPickerCombo::setCurrentText(const QString &v)
{
    if (v.isEmpty()) {
        m_combo->setCurrentIndex(0);
        return;
    }
    int idx = m_combo->findText(v);
    if (idx < 0) {
        // Caller asked to select a name not in the current list —
        // add it as a fallback so the cell isn't silently blanked.
        m_combo->addItem(v, v);
        idx = m_combo->count() - 1;
    }
    m_combo->setCurrentIndex(idx);
}
