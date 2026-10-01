/*!
 * \file   featurefielddelegate.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "ui/panels/featurefielddelegate.h"

#include "feature/featureroles.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QSpinBox>
#include <QToolTip>

#include <algorithm>
#include <limits>

using namespace openswmmvis::feature;

namespace openswmmvis::ui {

namespace {

constexpr double kRealLimit = 1.0e12;

/*!
 * A spin box that can be left empty. Enter on an empty box would otherwise
 * make QAbstractSpinBox refill the text with its last value before the
 * view's queued commit reaches setModelData — writing 0 (or a number the
 * user just deleted) into a cell that was meant to stay empty. Tab, a click
 * elsewhere and Esc already commit before that happens.
 */
template <class Spin>
class EmptyableSpin : public Spin
{
public:
    using Spin::Spin;

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
            && this->cleanText().isEmpty()) {
            event->ignore();   // the view commits and closes as usual
            return;
        }
        Spin::keyPressEvent(event);
    }
};

}   // namespace

FeatureFieldDelegate::FeatureFieldDelegate(const FieldDef &field, QObject *parent)
    : QStyledItemDelegate(parent)
    , m_field(field)
{
}

QStringList FeatureFieldDelegate::modelNames() const
{
    return m_modelNames ? m_modelNames(m_field.modelList) : QStringList();
}

QStringList FeatureFieldDelegate::columnValues(const QModelIndex &index)
{
    QStringList out;
    const QAbstractItemModel *model = index.model();
    if (!model) return out;
    for (int r = 0; r < model->rowCount(index.parent()); ++r) {
        const QString v = model->index(r, index.column(), index.parent())
                              .data(Qt::EditRole).toString().trimmed();
        if (!v.isEmpty() && !out.contains(v)) out << v;
    }
    std::sort(out.begin(), out.end(), [](const QString &a, const QString &b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    return out;
}

bool FeatureFieldDelegate::isOutOfList(const QVariant &value) const
{
    const QString v = value.toString().trimmed();
    if (v.isEmpty()) return false;
    switch (m_field.choiceSource) {
    case ChoiceSource::Fixed:
        return !m_field.choices.isEmpty() && m_field.choiceIndex(v) < 0;
    case ChoiceSource::Model: {
        // No provider (no project open) means nothing to check against.
        if (!m_modelNames) return false;
        return !modelNames().contains(v, Qt::CaseInsensitive);
    }
    case ChoiceSource::None:
    case ChoiceSource::Suggested:
        break;
    }
    return false;
}

QString FeatureFieldDelegate::textFor(const QVariant &value) const
{
    if (!value.isValid() || value.isNull()) return QString();
    if (m_field.type == FieldType::Boolean)
        return value.toBool() ? tr("Yes") : tr("No");
    if (m_field.choiceSource == ChoiceSource::Fixed)
        return m_field.choiceLabel(value.toString());
    return value.toString();
}

QString FeatureFieldDelegate::displayText(const QVariant &value, const QLocale &locale) const
{
    if (m_field.type == FieldType::Boolean || m_field.choiceSource == ChoiceSource::Fixed)
        return textFor(value);
    return QStyledItemDelegate::displayText(value, locale);
}

bool FeatureFieldDelegate::appliesToRow(const QModelIndex &index) const
{
    if (m_bcTypeColumn < 0 || !index.isValid() || index.column() == m_bcTypeColumn)
        return true;
    const QString bcType = index.sibling(index.row(), m_bcTypeColumn)
                               .data(Qt::EditRole).toString();
    return bcFieldApplies(bcType, m_field.name);
}

void FeatureFieldDelegate::initStyleOption(QStyleOptionViewItem *option,
                                           const QModelIndex &index) const
{
    QStyledItemDelegate::initStyleOption(option, index);
    if (!option) return;
    if (isOutOfList(index.data(Qt::EditRole))) {
        option->palette.setColor(QPalette::Text, m_warning);
        option->palette.setColor(QPalette::HighlightedText, m_warning);
        option->font.setItalic(true);
    } else if (!appliesToRow(index)) {
        option->palette.setColor(QPalette::Text,
                                 option->palette.color(QPalette::Disabled, QPalette::Text));
    }
}

bool FeatureFieldDelegate::helpEvent(QHelpEvent *event, QAbstractItemView *view,
                                     const QStyleOptionViewItem &option,
                                     const QModelIndex &index)
{
    if (event && event->type() == QEvent::ToolTip && index.isValid()) {
        const QVariant v = index.data(Qt::EditRole);
        QString tip;
        if (isOutOfList(v)) {
            tip = m_field.choiceSource == ChoiceSource::Model
                      ? tr("\"%1\" is not defined in the project yet.").arg(v.toString())
                      : tr("\"%1\" is not one of this column's values.").arg(v.toString());
        } else if (!appliesToRow(index)) {
            tip = tr("Not used by this boundary-condition type.");
        }
        if (!tip.isEmpty()) {
            QToolTip::showText(event->globalPos(), tip, view);
            return true;
        }
    }
    return QStyledItemDelegate::helpEvent(event, view, option, index);
}

QWidget *FeatureFieldDelegate::createEditor(QWidget *parent,
                                            const QStyleOptionViewItem &option,
                                            const QModelIndex &index) const
{
    const QVariant value = index.data(Qt::EditRole);
    const QString current = value.toString();
    // An empty cell stays empty when the editor is opened and closed: every
    // editor below can show "no value" for it (and only then for a required
    // field).
    const bool empty = !isNonEmptyValue(value);

    if (m_field.type == FieldType::Boolean) {
        auto *combo = new QComboBox(parent);
        combo->addItem(tr("Yes"), true);
        combo->addItem(tr("No"), false);
        if (empty) combo->addItem(tr("(none)"), QVariant());
        return combo;
    }

    switch (m_field.choiceSource) {
    case ChoiceSource::Fixed: {
        auto *combo = new QComboBox(parent);
        if (!m_field.required || empty) combo->addItem(tr("(none)"), QString());
        for (const FieldChoice &c : m_field.choices)
            combo->addItem(c.displayLabel(), c.value);
        // Never drop a value silently: one outside the list stays selectable
        // as itself, so opening and closing the editor changes nothing.
        if (!current.isEmpty() && m_field.choiceIndex(current) < 0)
            combo->addItem(tr("%1 (not in list)").arg(current), current);
        return combo;
    }
    case ChoiceSource::Model:
    case ChoiceSource::Suggested: {
        auto *combo = new QComboBox(parent);
        combo->setEditable(true);
        combo->setInsertPolicy(QComboBox::NoInsert);
        combo->addItems(m_field.choiceSource == ChoiceSource::Model
                            ? modelNames() : columnValues(index));
        return combo;
    }
    case ChoiceSource::None:
        break;
    }

    if (m_field.type == FieldType::Real) {
        auto *spin = new EmptyableSpin<QDoubleSpinBox>(parent);
        spin->setRange(-kRealLimit, kRealLimit);
        spin->setDecimals(6);
        if (m_field.unit == FieldUnit::Length && !m_lengthUnit.isEmpty())
            spin->setSuffix(QLatin1Char(' ') + m_lengthUnit);
        return spin;
    }
    if (m_field.type == FieldType::Integer) {
        auto *spin = new EmptyableSpin<QSpinBox>(parent);
        spin->setRange(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
        return spin;
    }
    return QStyledItemDelegate::createEditor(parent, option, index);
}

void FeatureFieldDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
{
    const QVariant v = index.data(Qt::EditRole);
    const bool empty = !isNonEmptyValue(v);
    if (auto *combo = qobject_cast<QComboBox *>(editor)) {
        if (m_field.type == FieldType::Boolean) {
            combo->setCurrentIndex(empty && combo->count() > 2 ? 2 : (v.toBool() ? 0 : 1));
        } else if (combo->isEditable()) {
            combo->setCurrentText(v.toString());
        } else {
            const int i = combo->findData(v.toString());
            combo->setCurrentIndex(i >= 0 ? i : 0);
        }
        return;
    }
    if (auto *spin = qobject_cast<QDoubleSpinBox *>(editor)) {
        spin->setValue(v.toDouble());
        if (empty) spin->clear();   // no text: setModelData leaves the cell alone
        return;
    }
    if (auto *spin = qobject_cast<QSpinBox *>(editor)) {
        spin->setValue(v.toInt());
        if (empty) spin->clear();
        return;
    }
    QStyledItemDelegate::setEditorData(editor, index);
}

void FeatureFieldDelegate::setModelData(QWidget *editor, QAbstractItemModel *model,
                                        const QModelIndex &index) const
{
    if (auto *combo = qobject_cast<QComboBox *>(editor)) {
        QVariant value;
        if (m_field.type == FieldType::Boolean) {
            if (!combo->currentData().isValid()) return;   // "(none)": still empty
            value = combo->currentData().toBool();
        } else {
            value = combo->isEditable() ? QVariant(combo->currentText().trimmed())
                                        : QVariant(combo->currentData().toString());
            // Empty in, empty out: no write (and no undo step) for a cell
            // that was empty and still is.
            if (value.toString().isEmpty() && !isNonEmptyValue(index.data(Qt::EditRole)))
                return;
        }
        model->setData(index, value, Qt::EditRole);
        return;
    }
    // A spin box left without text (an empty cell nobody typed into, or a
    // number deleted) changes nothing rather than writing 0.
    if (auto *spin = qobject_cast<QDoubleSpinBox *>(editor)) {
        if (spin->cleanText().isEmpty()) return;
        spin->interpretText();
        model->setData(index, spin->value(), Qt::EditRole);
        return;
    }
    if (auto *spin = qobject_cast<QSpinBox *>(editor)) {
        if (spin->cleanText().isEmpty()) return;
        spin->interpretText();
        model->setData(index, spin->value(), Qt::EditRole);
        return;
    }
    QStyledItemDelegate::setModelData(editor, model, index);
}

}   // namespace openswmmvis::ui
