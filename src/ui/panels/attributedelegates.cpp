/*!
 * \file   attributedelegates.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/panels/attributedelegates.h"

#include "ui/properties/linkcompoundeditbutton.h"
#include "ui/properties/linkcompoundeditref.h"
#include "ui/properties/nodecompoundeditbutton.h"
#include "ui/properties/nodecompoundeditref.h"
// ATTRIBUTE_EDITOR_WIRING follow-up (2026-06-04) — the same Compound
// delegate also serves DataObjectRef cells (pump curve picker) and
// UserFlagsEditRef cells (per-object User Flags), reusing the exact
// editor widgets the Property Browser registers for those metatypes.
#include "ui/properties/dataobjectpickereditor.h"
#include "ui/properties/dataobjectref.h"
#include "ui/properties/userflagseditbutton.h"
#include "ui/properties/subcatchcompoundeditbutton.h"
#include "ui/properties/subcatchcompoundeditref.h"
#include "ui/properties/userflagseditref.h"

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

// ---------------------------------------------------------------------------
// CompoundEditDelegate
// ---------------------------------------------------------------------------

CompoundEditDelegate::CompoundEditDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

QString CompoundEditDelegate::displayText(const QVariant &value,
                                            const QLocale & /*locale*/) const
{
    // §S.SC.1.c — Same delegate handles both node-side and link-side
    // compound cells. Dispatch on userType so each variant renders its
    // summary correctly without losing the "%1 — Edit…" affordance.
    if (value.userType() == qMetaTypeId<NodeCompoundEditRef>()) {
        const auto ref = value.value<NodeCompoundEditRef>();
        return ref.summary.isEmpty()
                   ? tr("Edit…")
                   : tr("%1 — Edit…").arg(ref.summary);
    }
    if (value.userType() == qMetaTypeId<LinkCompoundEditRef>()) {
        const auto ref = value.value<LinkCompoundEditRef>();
        return ref.summary.isEmpty()
                   ? tr("Edit…")
                   : tr("%1 — Edit…").arg(ref.summary);
    }
    if (value.userType() == qMetaTypeId<DataObjectRef>()) {
        const auto ref = value.value<DataObjectRef>();
        return ref.currentName.isEmpty() ? tr("(unassigned)")
                                         : ref.currentName;
    }
    if (value.userType() == qMetaTypeId<UserFlagsEditRef>()) {
        const auto ref = value.value<UserFlagsEditRef>();
        return ref.summary.isEmpty()
                   ? tr("Edit…")
                   : tr("%1 — Edit…").arg(ref.summary);
    }
    if (value.userType() == qMetaTypeId<SubcatchCompoundEditRef>()) {
        const auto ref = value.value<SubcatchCompoundEditRef>();
        return ref.summary.isEmpty()
                   ? tr("Edit…")
                   : tr("%1 — Edit…").arg(ref.summary);
    }
    return value.toString();
}

QWidget *CompoundEditDelegate::createEditor(QWidget *parent,
                                              const QStyleOptionViewItem & /*opt*/,
                                              const QModelIndex &index) const
{
    // §S.SC.1.c — The cell value's type drives which button widget
    // we build. The button's dialog is the source of truth for the
    // compound edit; the delegate only marshals values in/out.
    const QVariant v = index.data(Qt::EditRole);
    if (v.userType() == qMetaTypeId<LinkCompoundEditRef>())
        return new LinkCompoundEditButton(parent);
    if (v.userType() == qMetaTypeId<DataObjectRef>())
        return new DataObjectPickerEditor(parent);
    if (v.userType() == qMetaTypeId<UserFlagsEditRef>())
        return new UserFlagsEditButton(parent);
    if (v.userType() == qMetaTypeId<SubcatchCompoundEditRef>())
        return new SubcatchCompoundEditButton(parent);
    return new NodeCompoundEditButton(parent);
}

void CompoundEditDelegate::setEditorData(QWidget *editor,
                                           const QModelIndex &index) const
{
    const QVariant v = index.data(Qt::EditRole);
    if (auto *nb = qobject_cast<NodeCompoundEditButton *>(editor)) {
        if (v.userType() == qMetaTypeId<NodeCompoundEditRef>())
            nb->setValue(v.value<NodeCompoundEditRef>());
        return;
    }
    if (auto *lb = qobject_cast<LinkCompoundEditButton *>(editor)) {
        if (v.userType() == qMetaTypeId<LinkCompoundEditRef>())
            lb->setValue(v.value<LinkCompoundEditRef>());
        return;
    }
    if (auto *pe = qobject_cast<DataObjectPickerEditor *>(editor)) {
        if (v.userType() == qMetaTypeId<DataObjectRef>())
            pe->setValue(v.value<DataObjectRef>());
        return;
    }
    if (auto *fb = qobject_cast<UserFlagsEditButton *>(editor)) {
        if (v.userType() == qMetaTypeId<UserFlagsEditRef>())
            fb->setValue(v.value<UserFlagsEditRef>());
        return;
    }
    if (auto *sb = qobject_cast<SubcatchCompoundEditButton *>(editor)) {
        if (v.userType() == qMetaTypeId<SubcatchCompoundEditRef>())
            sb->setValue(v.value<SubcatchCompoundEditRef>());
        return;
    }
}

void CompoundEditDelegate::setModelData(QWidget *editor,
                                          QAbstractItemModel *model,
                                          const QModelIndex &index) const
{
    // After the dialog closes, the button holds a refreshed ref with
    // an updated summary. The model's commitValueDirect() path uses
    // this only as a trigger to invalidate the row cache + emit
    // objectEdited; the actual engine writes happened inside the
    // dialog as the user committed each entry.
    if (auto *nb = qobject_cast<NodeCompoundEditButton *>(editor)) {
        model->setData(index, QVariant::fromValue(nb->value()), Qt::EditRole);
        return;
    }
    if (auto *lb = qobject_cast<LinkCompoundEditButton *>(editor)) {
        model->setData(index, QVariant::fromValue(lb->value()), Qt::EditRole);
        return;
    }
    // DataObjectRef (pump curve) is the exception: the picker carries
    // no setter callback, so this setData round-trip is what triggers
    // the engine write in commitValueDirect.
    if (auto *pe = qobject_cast<DataObjectPickerEditor *>(editor)) {
        model->setData(index, QVariant::fromValue(pe->value()), Qt::EditRole);
        return;
    }
    if (auto *fb = qobject_cast<UserFlagsEditButton *>(editor)) {
        model->setData(index, QVariant::fromValue(fb->value()), Qt::EditRole);
        return;
    }
    if (auto *sb = qobject_cast<SubcatchCompoundEditButton *>(editor)) {
        model->setData(index, QVariant::fromValue(sb->value()), Qt::EditRole);
        return;
    }
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
