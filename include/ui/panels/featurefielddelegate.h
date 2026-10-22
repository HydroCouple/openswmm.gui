/**
 * @file      featurefielddelegate.h
 * @brief     The one cell editor for feature-layer columns
 *            (workplans/FEATURE_LAYER_ROLES_AND_FIELDS_PLAN_2026-09-30.md §6 "Views", R3).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef FEATUREFIELDDELEGATE_H
#define FEATUREFIELDDELEGATE_H

#include "feature/featuretypes.h"

#include <QColor>
#include <QStringList>
#include <QStyledItemDelegate>

#include <functional>

namespace openswmmvis::ui
{

class FeatureFieldDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    /*! Supplies the names a ChoiceSource::Model field offers. */
    using ModelNamesProvider = std::function<QStringList(openswmmvis::feature::ModelList)>;

    /**
     * @param[in] field FieldDef schema copied, including choices/unit/type; later source-schema
     *   changes do not update delegate.
     * @param[in] parent Optional QObject owner.
     * @brief Create GUI-thread field editor delegate with no model-name callback, empty length
     *   suffix, BC gating disabled and default warning colour. No editor/model mutation.
     */
    explicit FeatureFieldDelegate(const openswmmvis::feature::FieldDef &field,
                                  QObject *parent = nullptr);

    /**
     * @return Borrowed immutable schema snapshot, valid until delegate destruction, not live
     *   registry reference.
     */
    [[nodiscard]] const openswmmvis::feature::FieldDef &field() const { return m_field; }

    /**
     * @param[in] provider Moved optional synchronous GUI-thread callback taking ModelList and
     *   returning raw names. Empty disables project-name warning.
     * @note Store only, no repaint/editor refresh; called repeatedly during paint/help/edit
     *   creation rather than cached. Host preserves captured context lifetime, no registry
     *   ownership/reentry protection.
     */
    void setModelNamesProvider(ModelNamesProvider provider) { m_modelNames = std::move(provider); }
    /*! Suffix for a FieldUnit::Length spin box, e.g. "m" or "ft".
     * @param[in] unit Copied raw display suffix, e.g. m or ft, empty omits; applied to future REAL
     *   length editors only.
     * @note No conversion of model values, refresh of existing editor or repaint signal. Integer
     *   fields do not get suffix in current createEditor.
     */
    void setLengthUnit(const QString &unit) { m_lengthUnit = unit; }
    /*! Column of the row's `bc_type` in the same model. When set, a BC
     *  parameter the row's type does not read is drawn greyed (§4.7). -1
     *  (the default) disables it.
     * @param[in] column Logical sibling column containing bc_type EditRole, negative disables
     *   applicability check; upper bound not validated.
     * @note Store only; GREY display/tooltip feedback does not prevent editor creation/model commit
     *   for unused parameter. No automatic repaint or flags modification.
     */
    void setBcTypeColumn(int column) { m_bcTypeColumn = column; }
    /**
     * @param[in] c Copied warning QColor including invalid/alpha, no validation.
     * @note Future style options use it, no repaint request, source change or active editor update.
     */
    void setWarningColor(const QColor &c) { m_warning = c; }

    /*! True when \p value is non-empty and not one the field offers: a
     *  Fixed value outside the list, or a Model name the project lacks.
     * @param[in] value Candidate converted to trimmed string; empty never out-of-list.
     * @return Fixed nonempty choices: choiceIndex miss; Model with callback: case-insensitive
     *   current-name-list miss; no callback/None/Suggested/empty Fixed list false. Advisory only,
     *   never refuses edit. May synchronously invoke external callback; no cache/mutation.
     */
    [[nodiscard]] bool isOutOfList(const QVariant &value) const;
    /*! The text shown for \p value: a choice's label, Yes / No, or the value.
     * @param[in] value Raw field QVariant, null/invalid gives empty.
     * @return Boolean translated Yes/No, Fixed choice display label (token fallback), other
     *   value.toString. No numeric locale formatting/unit suffix or model query.
     */
    [[nodiscard]] QString textFor(const QVariant &value) const;

    /**
     * @param[in] value Raw field value.
     * @param[in] locale Used by styled base for nonBoolean/nonFixed fields, ignored by textFor
     *   branches.
     * @return Boolean/Fixed textFor, otherwise base display formatting. No warning/context lookup
     *   or mutation.
     */
    QString displayText(const QVariant &value, const QLocale &locale) const override;
    /**
     * @param[in] parent Optional owner of returned editor, view manages lifetime.
     * @param[in] option Forwarded only to base fallback editor.
     * @param[in] index Existing current model cell; EditRole seed determines empty preservation,
     *   Suggested scans siblings.
     * @return Parent-owned editor: Boolean Yes/No plus none ONLY if initially empty; Fixed choices
     *   plus optional/initial-empty none and unknown-current fallback; Model/Suggested editable
     *   no-insert list; Real EmptyableSpin ±1e12/6 decimals with optional length suffix; Integer
     *   full int32 range; other Qt base editor.
     * @note Model choices callback/suggested scan synchronous. Does not reject out-of-list
     *   values/unused BC fields or set model flags. No schema min/max/required validation beyond
     *   editor presentation; true acceptance/undo belongs to model. Newly opened empty numeric cell
     *   can stay empty.
     */
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override;
    /**
     * @param[in] editor Borrowed combo/spin or base-supported editor.
     * @param[in] index Source cell EditRole, expected stable valid model.
     * @note Boolean empty selects none if offered, fixed unknown preserves appended token or row 0
     *   fallback, editable combo raw text. Numeric sets converted value then clears if
     *   !isNonEmptyValue; no signal blocker or conversion-success guard. Other widget base
     *   fallback. Source untouched, null semantics via schema helper.
     */
    void setEditorData(QWidget *editor, const QModelIndex &index) const override;
    /**
     * @param[in] editor Borrowed matching editor.
     * @param[in] model Required mutable model when a write/base commit occurs.
     * @param[in] index Current destination, also reread EditRole to avoid empty→empty write.
     * @note Boolean none skips write; fixed/editable choice submits token or trimmed text,
     *   empty→empty skips. Numeric CLEAN TEXT EMPTY skips write even when clearing previously
     *   nonempty cell, otherwise interpretText+spin value. Base fallback for others. Ignores model
     *   setData result, no local undo/error dialog or BC applicability enforcement. Delegates
     *   writes use EditRole; model owns command/validation.
     */
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override;
    /**
     * @param[in] event Optional help event; tooltip branch inspects only nonnull matching type.
     * @param[in] view Borrowed host view, used as tooltip owner/forwarded to base.
     * @param[in] option Borrowed style option passed to base if no custom tooltip.
     * @param[in] index Cell supplying EditRole and optional bc_type sibling.
     * @return True after showing out-of-list/unused-BC explanation, otherwise styled base result.
     *   Out-of-list warning takes precedence over unused-field note. May invoke names callback, no
     *   source mutation or edit rejection.
     */
    bool helpEvent(QHelpEvent *event, QAbstractItemView *view, const QStyleOptionViewItem &option,
                   const QModelIndex &index) override;

protected:
    /**
     * @param[in,out] option Required style option: base called BEFORE local null check, so null is
     *   not a supported safe input.
     * @param[in] index Cell whose EditRole/list membership/applicability affects decoration.
     * @note After base setup, out-of-list sets normal/highlight text warning colour+italic; else
     *   unused BC field sets normal Text disabled colour only. Does not change
     *   flags/editability/value or show tooltip. Callback may be invoked synchronously; caller
     *   requests repaint on configuration changes.
     */
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override;

private:
    /**
     * @return New callback result for stored field ModelList, or empty without callback. Raw
     *   order/duplicates/spelling unchanged; synchronous callback exceptions propagate. No registry
     *   ownership/cache or guaranteed project binding.
     */
    [[nodiscard]] QStringList modelNames() const;
    /*! The distinct non-empty values of \p index's column, sorted.
     * @param[in] index Source index defining model,parent and column; no model gives empty.
     * @return Distinct CASE-SENSITIVE trimmed nonempty EditRole strings across current model
     *   sibling rows, sorted case-insensitively; equal-sort-case ties not promised stable. No proxy
     *   unwrapping, full-store scan, model mutation or caching. Can cost full column scan.
     */
    [[nodiscard]] static QStringList columnValues(const QModelIndex &index);
    /*! False when the row's bc_type does not read this column.
     * @param[in] index Cell; invalid or bc column itself returns true, negative configured BC
     *   column disables gate.
     * @return bcFieldApplies(sibling EditRole string,field.name), otherwise true per bypass cases.
     *   No flags change/validation refusal/model mutation. Missing sibling data follows
     *   bcFieldApplies fallback.
     */
    [[nodiscard]] bool appliesToRow(const QModelIndex &index) const;

    openswmmvis::feature::FieldDef m_field;
    ModelNamesProvider m_modelNames;
    QString m_lengthUnit;
    int m_bcTypeColumn = -1;
    QColor m_warning = QColor(0xC2, 0x6A, 0x00);
};

} // namespace openswmmvis::ui

#endif // FEATUREFIELDDELEGATE_H
