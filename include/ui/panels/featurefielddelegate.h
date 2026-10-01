/*!
 * \file   featurefielddelegate.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * The one cell editor for feature-layer columns
 * (workplans/FEATURE_LAYER_ROLES_AND_FIELDS_PLAN_2026-09-30.md §6 "Views", R3).
 *
 * Installed per column by both views of a feature layer — the Features dock's
 * grid and the Attribute Table — so the same column edits the same way in
 * either:
 *   - Fixed choices  → combo showing labels, storing the token;
 *   - Model list     → editable combo of the project's time series / curves;
 *   - Suggested      → editable combo of the values already in the column;
 *   - Real / Integer → spin box, with the length unit as suffix;
 *   - Yes / No       → Yes / No combo;
 *   - Text           → line edit.
 * Display shows a choice's label. A value outside its list (a Fixed value no
 * longer offered, or a series name the project does not define yet) is drawn
 * in the warning colour with a tooltip — flagged, never refused (plan Q4).
 *
 * A VIEW only: setModelData writes through the model, which for both feature
 * views pushes an EditFeatureAttributesCommand — so every edit stays one undo
 * step (CLAUDE.md §5.1).
 *
 * Self-contained (Qt Widgets + the feature registry): it follows
 * EnumDelegate's {label, value} convention but does not derive from it, so it
 * links into a leaf test without attributedelegates.cpp's dialog closure.
 */

#ifndef FEATUREFIELDDELEGATE_H
#define FEATUREFIELDDELEGATE_H

#include "feature/featuretypes.h"

#include <QColor>
#include <QStringList>
#include <QStyledItemDelegate>

#include <functional>

namespace openswmmvis::ui {

class FeatureFieldDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    /*! Supplies the names a ChoiceSource::Model field offers. */
    using ModelNamesProvider =
        std::function<QStringList(openswmmvis::feature::ModelList)>;

    explicit FeatureFieldDelegate(const openswmmvis::feature::FieldDef &field,
                                  QObject *parent = nullptr);

    [[nodiscard]] const openswmmvis::feature::FieldDef &field() const { return m_field; }

    void setModelNamesProvider(ModelNamesProvider provider) { m_modelNames = std::move(provider); }
    /*! Suffix for a FieldUnit::Length spin box, e.g. "m" or "ft". */
    void setLengthUnit(const QString &unit) { m_lengthUnit = unit; }
    /*! Column of the row's `bc_type` in the same model. When set, a BC
     *  parameter the row's type does not read is drawn greyed (§4.7). -1
     *  (the default) disables it. */
    void setBcTypeColumn(int column) { m_bcTypeColumn = column; }
    void setWarningColor(const QColor &c) { m_warning = c; }

    /*! True when \p value is non-empty and not one the field offers: a
     *  Fixed value outside the list, or a Model name the project lacks. */
    [[nodiscard]] bool isOutOfList(const QVariant &value) const;
    /*! The text shown for \p value: a choice's label, Yes / No, or the value. */
    [[nodiscard]] QString textFor(const QVariant &value) const;

    QString displayText(const QVariant &value, const QLocale &locale) const override;
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override;
    void setEditorData(QWidget *editor, const QModelIndex &index) const override;
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override;
    bool helpEvent(QHelpEvent *event, QAbstractItemView *view,
                   const QStyleOptionViewItem &option, const QModelIndex &index) override;

protected:
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override;

private:
    [[nodiscard]] QStringList modelNames() const;
    /*! The distinct non-empty values of \p index's column, sorted. */
    [[nodiscard]] static QStringList columnValues(const QModelIndex &index);
    /*! False when the row's bc_type does not read this column. */
    [[nodiscard]] bool appliesToRow(const QModelIndex &index) const;

    openswmmvis::feature::FieldDef m_field;
    ModelNamesProvider             m_modelNames;
    QString                        m_lengthUnit;
    int                            m_bcTypeColumn = -1;
    QColor                         m_warning = QColor(0xC2, 0x6A, 0x00);
};

}   // namespace openswmmvis::ui

#endif // FEATUREFIELDDELEGATE_H
