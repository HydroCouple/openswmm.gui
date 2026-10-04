/*!
 * \file   featurefieldeditor.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "ui/dialogs/featurefieldeditor.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QTableWidget>
#include <QVBoxLayout>

using namespace openswmmvis::feature;

namespace openswmmvis::ui {

namespace {

/*! Type-combo payload for "Choice": a Text column with a Fixed list. */
constexpr int kChoiceType = 100;

enum ChoiceColumn { ColValue = 0, ColLabel = 1 };

}   // namespace

FeatureFieldEditor::FeatureFieldEditor(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Column"));
    setObjectName(QStringLiteral("FeatureFieldEditor"));

    auto *root = new QVBoxLayout(this);
    auto *form = new QFormLayout();

    m_name = new QLineEdit(this);
    m_name->setObjectName(QStringLiteral("featureFieldName"));
    m_name->setPlaceholderText(tr("letters, digits and _"));
    form->addRow(tr("&Name:"), m_name);

    m_type = new QComboBox(this);
    m_type->setObjectName(QStringLiteral("featureFieldType"));
    for (auto t : {FieldType::Text, FieldType::Integer, FieldType::Real, FieldType::Boolean})
        m_type->addItem(fieldTypeLabel(t), static_cast<int>(t));
    m_type->addItem(tr("Choice"), kChoiceType);
    m_type->setToolTip(tr("Choice: a text column whose values come from the list "
                          "below, edited with a dropdown everywhere — also in QGIS."));
    form->addRow(tr("&Type:"), m_type);

    auto *defaultRow = new QWidget(this);
    auto *defaultLay = new QHBoxLayout(defaultRow);
    defaultLay->setContentsMargins(0, 0, 0, 0);
    m_default = new QLineEdit(defaultRow);
    m_default->setObjectName(QStringLiteral("featureFieldDefault"));
    m_default->setPlaceholderText(tr("(none)"));
    m_defaultPick = new QComboBox(defaultRow);
    m_defaultPick->setObjectName(QStringLiteral("featureFieldDefaultChoice"));
    defaultLay->addWidget(m_default);
    defaultLay->addWidget(m_defaultPick);
    form->addRow(tr("&Default:"), defaultRow);

    m_description = new QLineEdit(this);
    m_description->setObjectName(QStringLiteral("featureFieldDescription"));
    form->addRow(tr("D&escription:"), m_description);
    root->addLayout(form);

    m_choiceBox = new QWidget(this);
    auto *choiceLay = new QVBoxLayout(m_choiceBox);
    choiceLay->setContentsMargins(0, 0, 0, 0);
    choiceLay->addWidget(new QLabel(tr("Values (the value is stored; the label is shown):"),
                                    m_choiceBox));
    m_choices = new QTableWidget(0, 2, m_choiceBox);
    m_choices->setObjectName(QStringLiteral("featureFieldChoices"));
    m_choices->setHorizontalHeaderLabels({tr("Value"), tr("Label")});
    m_choices->horizontalHeader()->setStretchLastSection(false);
    m_choices->verticalHeader()->setVisible(false);
    m_choices->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_choices->setSelectionMode(QAbstractItemView::SingleSelection);
    m_choices->setMinimumHeight(120);
    choiceLay->addWidget(m_choices);
    auto *btns = new QHBoxLayout();
    auto *add    = new QPushButton(tr("Add"), m_choiceBox);
    auto *remove = new QPushButton(tr("Remove"), m_choiceBox);
    auto *up     = new QPushButton(tr("Up"), m_choiceBox);
    auto *down   = new QPushButton(tr("Down"), m_choiceBox);
    add->setObjectName(QStringLiteral("featureFieldAddChoice"));
    for (QPushButton *b : {add, remove, up, down}) btns->addWidget(b);
    btns->addStretch();
    choiceLay->addLayout(btns);
    root->addWidget(m_choiceBox);

    m_hint = new QLabel(this);
    m_hint->setWordWrap(true);
    m_hint->setEnabled(false);
    root->addWidget(m_hint);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    root->addWidget(box);

    connect(box, &QDialogButtonBox::accepted, this, &FeatureFieldEditor::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_type, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { onTypeChanged(); });
    connect(add, &QPushButton::clicked, this, &FeatureFieldEditor::onAddChoice);
    connect(remove, &QPushButton::clicked, this, &FeatureFieldEditor::onRemoveChoice);
    connect(up, &QPushButton::clicked, this, [this] { onMoveChoice(-1); });
    connect(down, &QPushButton::clicked, this, [this] { onMoveChoice(+1); });
    connect(m_choices, &QTableWidget::cellChanged,
            this, [this](int, int) { refreshDefaultChoices(); });

    onTypeChanged();
    resize(440, 420);
}

bool FeatureFieldEditor::isChoice() const
{
    return m_type->currentData().toInt() == kChoiceType;
}

void FeatureFieldEditor::onTypeChanged()
{
    const int t = m_type->currentData().toInt();
    const bool choice = (t == kChoiceType);
    const bool yesNo  = (t == static_cast<int>(FieldType::Boolean));
    m_choiceBox->setVisible(choice);
    m_default->setVisible(!choice && !yesNo);
    m_defaultPick->setVisible(choice || yesNo);
    refreshDefaultChoices();
    m_hint->setText(choice ? tr("Values are stored as typed (keep them short and "
                                "stable); labels can be anything.")
                           : QString());
}

void FeatureFieldEditor::refreshDefaultChoices()
{
    const QVariant keep = m_defaultPick->currentData();
    m_defaultPick->blockSignals(true);
    m_defaultPick->clear();
    m_defaultPick->addItem(tr("(none)"), QVariant());
    if (m_type->currentData().toInt() == static_cast<int>(FieldType::Boolean)) {
        m_defaultPick->addItem(tr("Yes"), true);
        m_defaultPick->addItem(tr("No"), false);
    } else {
        for (const FieldChoice &c : choicesFromTable())
            m_defaultPick->addItem(c.displayLabel(), c.value);
    }
    const int i = m_defaultPick->findData(keep);
    m_defaultPick->setCurrentIndex(i >= 0 ? i : 0);
    m_defaultPick->blockSignals(false);
}

void FeatureFieldEditor::onAddChoice()
{
    const int row = m_choices->rowCount();
    m_choices->insertRow(row);
    m_choices->setItem(row, ColValue, new QTableWidgetItem(QString()));
    m_choices->setItem(row, ColLabel, new QTableWidgetItem(QString()));
    m_choices->setCurrentCell(row, ColValue);
    m_choices->editItem(m_choices->item(row, ColValue));
}

void FeatureFieldEditor::onRemoveChoice()
{
    const int row = m_choices->currentRow();
    if (row >= 0) m_choices->removeRow(row);
    refreshDefaultChoices();
}

void FeatureFieldEditor::onMoveChoice(int delta)
{
    const int row = m_choices->currentRow();
    const int to  = row + delta;
    if (row < 0 || to < 0 || to >= m_choices->rowCount()) return;
    const QSignalBlocker block(m_choices);
    for (int c = 0; c < 2; ++c) {
        QTableWidgetItem *a = m_choices->takeItem(row, c);
        QTableWidgetItem *b = m_choices->takeItem(to, c);
        m_choices->setItem(row, c, b);
        m_choices->setItem(to, c, a);
    }
    m_choices->setCurrentCell(to, 0);
    refreshDefaultChoices();
}

QVector<FieldChoice> FeatureFieldEditor::choicesFromTable() const
{
    QVector<FieldChoice> out;
    for (int r = 0; r < m_choices->rowCount(); ++r) {
        const QTableWidgetItem *v = m_choices->item(r, ColValue);
        const QTableWidgetItem *l = m_choices->item(r, ColLabel);
        FieldChoice c;
        c.value = v ? v->text().trimmed() : QString();
        c.label = l ? l->text().trimmed() : QString();
        if (c.label == c.value) c.label.clear();
        out.append(c);
    }
    return out;
}

void FeatureFieldEditor::setField(const FieldDef &f)
{
    m_name->setText(f.name);
    const bool choice = f.choiceSource == ChoiceSource::Fixed;
    m_type->setCurrentIndex(m_type->findData(choice ? kChoiceType : static_cast<int>(f.type)));
    m_description->setText(f.description);
    {
        const QSignalBlocker block(m_choices);
        m_choices->setRowCount(0);
        for (const FieldChoice &c : f.choices) {
            const int row = m_choices->rowCount();
            m_choices->insertRow(row);
            m_choices->setItem(row, ColValue, new QTableWidgetItem(c.value));
            m_choices->setItem(row, ColLabel, new QTableWidgetItem(c.label));
        }
    }
    onTypeChanged();
    if (choice || f.type == FieldType::Boolean) {
        const int i = f.defaultValue.isValid() ? m_defaultPick->findData(f.defaultValue) : 0;
        m_defaultPick->setCurrentIndex(i >= 0 ? i : 0);
    } else {
        m_default->setText(f.defaultValue.isValid() ? f.defaultValue.toString() : QString());
    }
}

FieldDef FeatureFieldEditor::field() const
{
    FieldDef f;
    f.name        = sanitizeFieldName(m_name->text());
    f.description = m_description->text().trimmed();
    const int t   = m_type->currentData().toInt();
    if (t == kChoiceType) {
        f.type         = FieldType::Text;
        f.choiceSource = ChoiceSource::Fixed;
        f.choices      = choicesFromTable();
        const QVariant d = m_defaultPick->currentData();
        if (d.isValid() && !d.toString().isEmpty()) f.defaultValue = d.toString();
    } else if (t == static_cast<int>(FieldType::Boolean)) {
        f.type = FieldType::Boolean;
        const QVariant d = m_defaultPick->currentData();
        if (d.isValid()) f.defaultValue = d.toBool();
    } else {
        f.type = static_cast<FieldType>(t);
        const QString d = m_default->text().trimmed();
        if (!d.isEmpty()) f.defaultValue = d;   // validated, then coerced below
        if (f.defaultValue.isValid() && f.type != FieldType::Text)
            f.defaultValue = coerceToFieldType(f.defaultValue, f.type);
    }
    return f;
}

QString FeatureFieldEditor::validate(const FieldDef &f, const QString &raw,
                                     const QStringList &existing)
{
    if (raw.trimmed().isEmpty())
        return tr("Give the column a name.");
    if (f.name.isEmpty())
        return tr("\"%1\" cannot be used as a column name. Use letters, digits "
                  "and underscores.").arg(raw.trimmed());
    for (const QString &e : existing)
        if (e.compare(f.name, Qt::CaseInsensitive) == 0)
            return tr("There is already a column named \"%1\".").arg(f.name);
    if (f.choiceSource == ChoiceSource::Fixed) {
        if (f.choices.isEmpty())
            return tr("A Choice column needs at least one value.");
        QSet<QString> seen;
        for (const FieldChoice &c : f.choices) {
            if (c.value.isEmpty()) return tr("Every value needs a value text.");
            if (seen.contains(c.value))
                return tr("The value \"%1\" is listed twice.").arg(c.value);
            seen.insert(c.value);
        }
    }
    return {};
}

QString FeatureFieldEditor::validationError() const
{
    const FieldDef f = field();
    const QString err = validate(f, m_name->text(), m_existing);
    if (!err.isEmpty()) return err;
    // A typed numeric default that does not parse would silently become 0.
    const int t = m_type->currentData().toInt();
    const QString d = m_default->text().trimmed();
    if (!d.isEmpty() && (t == static_cast<int>(FieldType::Integer)
                         || t == static_cast<int>(FieldType::Real))) {
        bool ok = false;
        if (t == static_cast<int>(FieldType::Integer)) d.toInt(&ok);
        else                                           d.toDouble(&ok);
        if (!ok) return tr("The default \"%1\" is not a number.").arg(d);
    }
    return {};
}

void FeatureFieldEditor::accept()
{
    const QString err = validationError();
    if (!err.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), err);
        return;
    }
    QDialog::accept();
}

}   // namespace openswmmvis::ui
