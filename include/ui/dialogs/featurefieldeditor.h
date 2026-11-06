/**
 * @file      featurefieldeditor.h
 * @brief     The one small editor for a custom feature-layer column
 *            (workplans/FEATURE_LAYER_ROLES_AND_FIELDS_PLAN_2026-09-30.md §5, R4).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * The one small editor for a custom feature-layer column
 * (workplans/FEATURE_LAYER_ROLES_AND_FIELDS_PLAN_2026-09-30.md §5, R4).
 *
 * Name, type (Text / Integer / Real / Yes-No / Choice), default and
 * description; for Choice, a value / label list with Add, Remove and
 * reorder. Used by the New Feature Layer dialog's "Add custom field…" and by
 * the Features dock's "Add column…", replacing the two QInputDialogs.
 *
 * A Choice column is a Text column whose values are tokens from the list
 * (ChoiceSource::Fixed); the store keeps the list as a GeoPackage value list,
 * so QGIS shows the same dropdown.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef FEATUREFIELDEDITOR_H
#define FEATUREFIELDEDITOR_H

#include "feature/featuretypes.h"

#include <QDialog>
#include <QStringList>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QWidget;

namespace openswmmvis::ui
{

class FeatureFieldEditor : public QDialog
{
    Q_OBJECT

public:
    /**
     * @param[in] parent Optional QWidget owner; dialog owns all controls, no layer/model ownership.
     * @brief Build empty custom-column editor, default Text type, no existing-name list/default or
     *   choices. GUI-thread only, no schema/store write. Accepted FieldDef must be applied by
     *   caller.
     */
    explicit FeatureFieldEditor(QWidget *parent = nullptr);

    /*! Names already taken (compared case-insensitively).
     * @param[in] names Copied raw names compared case-insensitively against sanitized candidate
     *   name, including unchecked/excluded rows when caller supplies them.
     * @note Store only, no validation UI/repaint/acceptance or conflict-exemption for previously
     *   edited name. Caller excludes original name when editing existing field if desired.
     */
    void setExistingNames(const QStringList &names) { m_existing = names; }
    /*! Pre-fill every control from \p f.
     * @param[in] f Borrowed field copied into supported controls; Fixed source selects Choice,
     *   other sources treated plain type.
     * @note Replace name/description/choice rows/default. Unknown numeric type may leave combo
     *   unselected. Suggested/Model source, required/unit and other metadata not preserved by
     *   field() reconstruction; this editor targets custom fields, not arbitrary lossless metadata
     *   editing. Invalid/unknown choice default falls to none; no existing-name update or source
     *   write.
     */
    void setField(const openswmmvis::feature::FieldDef &f);
    /*! The field as currently entered (name sanitised).
     * @return New FieldDef from current controls: name sanitizeFieldName, trimmed description/text
     *   default, choice tokens/labels trimmed, Fixed stored as Text, Boolean typed. Numeric
     *   nonempty default coerced now and can become zero if invalid, so caller must validate first.
     *   Absent default invalid QVariant; other unrepresented metadata defaults from FieldDef. Does
     *   not enforce validity or mutate controls/schema.
     */
    [[nodiscard]] openswmmvis::feature::FieldDef field() const;
    /*! Why the current entry cannot be accepted, or empty when it can.
     * @return First static name/list error, otherwise integer default toInt or real toDouble
     *   failure, otherwise empty. Numeric parsing checks original text before coerced field()
     *   value; no explicit finite/range constraint or choice-default membership check. No
     *   dialog/source mutation.
     */
    [[nodiscard]] QString validationError() const;

    /*!
     * \brief The same check, on a field already built.
     * \param raw  The name as typed, before sanitising.
     * @param[in] f Already-built candidate, name assumed sanitized by caller; this helper only
     *   tests name emptiness, not sanitization equality or type validity.
     * @param[in] existing Raw existing names, case-insensitive collision check against f.name; no
     *   trim/canonicalization.
     * @return Localized first error for raw blank/name empty/collision, Fixed empty list/empty
     *   token/EXACT case-sensitive duplicate token; empty otherwise. Does not validate field type,
     *   required/unit metadata, default conversion/membership or labels, and does not modify
     *   candidate. Whitespace-only choice token can pass direct call unless caller trimmed it
     *   first.
     */
    [[nodiscard]] static QString validate(const openswmmvis::feature::FieldDef &f,
                                          const QString &raw, const QStringList &existing);

public slots:
    /**
     * @brief Validate controls, show warning and keep dialog open on error, otherwise
     *   QDialog::accept.
     * @note No schema/layer/GeoPackage write, undo or normalized control replacement; caller reads
     *   field() after acceptance. Modal warning requires host preserve dialog lifetime.
     */
    void accept() override;

private slots:
    /**
     * @brief Toggle choice list and text-vs-combo default editor, refresh default options and hint
     *   from current type.
     * @note Preserve hidden text/choice rows rather than converting values; combo default preserved
     *   by QVariant data if still found. No field/source write or automatic validation.
     */
    void onTypeChanged();
    /**
     * @brief Append empty token/label row, make current and start token editor.
     * @note Default-choice refresh may fire during item insertion; new blank token remains until
     *   edited, validate prevents acceptance. No source mutation or initial default selection
     *   guarantee.
     */
    void onAddChoice();
    /**
     * @brief Remove current choice row when present, always rebuild default choices.
     * @note Deleted default falls to none, no confirmation/undo; purely unsaved dialog state.
     */
    void onRemoveChoice();
    /**
     * @param[in] delta Raw integer row displacement, normally -1/+1; zero swaps with itself, larger
     *   value exchanges two rows rather than shifting intermediate rows.
     * @note Invalid current/target row no-op. Swap token and label items under signal block, choose
     *   destination then rebuild default combo preserving token. No source write/undo or list
     *   validation.
     */
    void onMoveChoice(int delta);
    /**
     * @brief Rebuild none plus Boolean Yes/No or current trimmed choice token/label list; preserve
     *   first findData match or none.
     * @note Block combo signals then reset blocked=false, not caller prior state. Does not remove
     *   invalid/duplicate tokens, validate schema or change numeric text default. No source write.
     */
    void refreshDefaultChoices();

private:
    /**
     * @return True iff current type payload equals local Choice sentinel 100, not whether arbitrary
     *   supplied FieldDef has choices. No mutation.
     */
    [[nodiscard]] bool isChoice() const;
    /**
     * @return New ordered list from EVERY row, empty items included. Trim token/label and clear
     *   redundant label equal to token; empty label later displays token. No deduplication,
     *   type/default validation or source mutation.
     */
    [[nodiscard]] QVector<openswmmvis::feature::FieldChoice> choicesFromTable() const;

    QStringList m_existing;
    QLineEdit *m_name = nullptr;
    QComboBox *m_type = nullptr;
    QLineEdit *m_default = nullptr;     ///< Text / Integer / Real
    QComboBox *m_defaultPick = nullptr; ///< Yes-No / Choice
    QLineEdit *m_description = nullptr;
    QWidget *m_choiceBox = nullptr;
    QTableWidget *m_choices = nullptr;
    QLabel *m_hint = nullptr;
};

} // namespace openswmmvis::ui

#endif // FEATUREFIELDEDITOR_H
