/*!
 * \file   attributedelegates.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/panels/attributedelegates.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>

namespace openswmmvis {

// ---------------------------------------------------------------------------
// NumericDelegate
// ---------------------------------------------------------------------------

NumericDelegate::NumericDelegate(QObject *parent, double minimum,
                                  double maximum, int decimals)
    : QStyledItemDelegate(parent)
    , m_min(minimum)
    , m_max(maximum)
    , m_decimals(decimals)
{
}

QWidget *NumericDelegate::createEditor(QWidget *parent,
                                        const QStyleOptionViewItem & /*opt*/,
                                        const QModelIndex & /*index*/) const
{
    auto *spin = new QDoubleSpinBox(parent);
    spin->setDecimals(m_decimals);
    spin->setRange(m_min, m_max);
    spin->setSingleStep(std::pow(10.0, -m_decimals + 1));
    spin->setKeyboardTracking(false);
    return spin;
}

void NumericDelegate::setEditorData(QWidget *editor,
                                     const QModelIndex &index) const
{
    auto *spin = qobject_cast<QDoubleSpinBox *>(editor);
    if (!spin) return;
    bool ok = false;
    const double v = index.data(Qt::EditRole).toDouble(&ok);
    spin->setValue(ok ? v : 0.0);
}

void NumericDelegate::setModelData(QWidget *editor, QAbstractItemModel *model,
                                    const QModelIndex &index) const
{
    auto *spin = qobject_cast<QDoubleSpinBox *>(editor);
    if (!spin) return;
    spin->interpretText();  // flush any pending typed value
    model->setData(index, spin->value(), Qt::EditRole);
}

// ---------------------------------------------------------------------------
// IntegerDelegate
// ---------------------------------------------------------------------------

IntegerDelegate::IntegerDelegate(QObject *parent, int minimum, int maximum)
    : QStyledItemDelegate(parent)
    , m_min(minimum)
    , m_max(maximum)
{
}

QWidget *IntegerDelegate::createEditor(QWidget *parent,
                                        const QStyleOptionViewItem & /*opt*/,
                                        const QModelIndex & /*index*/) const
{
    auto *spin = new QSpinBox(parent);
    spin->setRange(m_min, m_max);
    spin->setKeyboardTracking(false);
    return spin;
}

void IntegerDelegate::setEditorData(QWidget *editor,
                                     const QModelIndex &index) const
{
    auto *spin = qobject_cast<QSpinBox *>(editor);
    if (!spin) return;
    bool ok = false;
    const int v = index.data(Qt::EditRole).toInt(&ok);
    spin->setValue(ok ? v : 0);
}

void IntegerDelegate::setModelData(QWidget *editor, QAbstractItemModel *model,
                                    const QModelIndex &index) const
{
    auto *spin = qobject_cast<QSpinBox *>(editor);
    if (!spin) return;
    spin->interpretText();
    model->setData(index, spin->value(), Qt::EditRole);
}

// ---------------------------------------------------------------------------
// EnumDelegate
// ---------------------------------------------------------------------------

EnumDelegate::EnumDelegate(QObject *parent, QVariantList values)
    : QStyledItemDelegate(parent)
    , m_values(std::move(values))
{
}

QWidget *EnumDelegate::createEditor(QWidget *parent,
                                     const QStyleOptionViewItem & /*opt*/,
                                     const QModelIndex & /*index*/) const
{
    auto *combo = new QComboBox(parent);
    for (const QVariant &v : m_values) {
        const QVariantList pair = v.toList();
        if (pair.size() != 2) continue;
        combo->addItem(pair[0].toString(), pair[1]);
    }
    return combo;
}

void EnumDelegate::setEditorData(QWidget *editor,
                                  const QModelIndex &index) const
{
    auto *combo = qobject_cast<QComboBox *>(editor);
    if (!combo) return;
    const QVariant data = index.data(Qt::EditRole);
    const int idx = combo->findData(data);
    combo->setCurrentIndex(idx >= 0 ? idx : 0);
}

void EnumDelegate::setModelData(QWidget *editor, QAbstractItemModel *model,
                                 const QModelIndex &index) const
{
    auto *combo = qobject_cast<QComboBox *>(editor);
    if (!combo) return;
    model->setData(index, combo->currentData(), Qt::EditRole);
}

QVariantList EnumDelegate::makePairs(const QStringList &labels,
                                      const QVariantList &data)
{
    QVariantList out;
    const int n = std::min(labels.size(), data.size());
    for (int i = 0; i < n; ++i) {
        QVariantList pair;
        pair << labels[i] << data[i];
        out << QVariant(pair);
    }
    return out;
}

} // namespace openswmmvis
