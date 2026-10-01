/*!
 * \file   featurefieldeditor.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
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

namespace openswmmvis::ui {

class FeatureFieldEditor : public QDialog
{
    Q_OBJECT

public:
    explicit FeatureFieldEditor(QWidget *parent = nullptr);

    /*! Names already taken (compared case-insensitively). */
    void setExistingNames(const QStringList &names) { m_existing = names; }
    /*! Pre-fill every control from \p f. */
    void setField(const openswmmvis::feature::FieldDef &f);
    /*! The field as currently entered (name sanitised). */
    [[nodiscard]] openswmmvis::feature::FieldDef field() const;
    /*! Why the current entry cannot be accepted, or empty when it can. */
    [[nodiscard]] QString validationError() const;

    /*!
     * \brief The same check, on a field already built.
     * \param raw  The name as typed, before sanitising.
     */
    [[nodiscard]] static QString validate(const openswmmvis::feature::FieldDef &f,
                                          const QString &raw,
                                          const QStringList &existing);

public slots:
    void accept() override;

private slots:
    void onTypeChanged();
    void onAddChoice();
    void onRemoveChoice();
    void onMoveChoice(int delta);
    void refreshDefaultChoices();

private:
    [[nodiscard]] bool isChoice() const;
    [[nodiscard]] QVector<openswmmvis::feature::FieldChoice> choicesFromTable() const;

    QStringList    m_existing;
    QLineEdit     *m_name        = nullptr;
    QComboBox     *m_type        = nullptr;
    QLineEdit     *m_default     = nullptr;   ///< Text / Integer / Real
    QComboBox     *m_defaultPick = nullptr;   ///< Yes-No / Choice
    QLineEdit     *m_description = nullptr;
    QWidget       *m_choiceBox   = nullptr;
    QTableWidget  *m_choices     = nullptr;
    QLabel        *m_hint        = nullptr;
};

}   // namespace openswmmvis::ui

#endif // FEATUREFIELDEDITOR_H
