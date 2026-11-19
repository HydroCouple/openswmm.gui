/**
 * @file      test_featurefielddelegate.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     FeatureFieldDelegate — one editor per feature column
 *         (workplans/FEATURE_LAYER_ROLES_AND_FIELDS_PLAN_2026-09-30.md R3).
 *
 * Leaf test: the delegate against a QStandardItemModel, with the role
 * registry supplying the fields. The edits-push-one-undo-command half of R3
 * needs a FeatureLayer and a MapCanvas and lives in test_featurelayer_editors.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "feature/featureroles.h"
#include "ui/panels/featurefielddelegate.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLocale>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QStyleOptionViewItem>
#include <QTableView>
#include <QTest>
#include <QWidget>

#include <memory>

using namespace openswmmvis::feature;
using openswmmvis::ui::FeatureFieldDelegate;

namespace
{

/** @brief Copy a role field definition for a delegate fixture.
 * @param[in] role Feature role whose metadata are searched.
 * @param[in] name Required null-terminated Latin-1 field name.
 * @return Matching FieldDef by value, or a default definition when absent. */
FieldDef roleField(FeatureLayerRole role, const char *name)
{
    const FieldDef *f = featureRoleSpec(role).field(QLatin1String(name));
    return f ? *f : FieldDef{};
}

/*! A one-column model holding \p values. */
/** @brief Build a uniquely owned one-column editable string model.
 * @param[in] values Initial strings in row order.
 * @return New model with one row per value assigned through EditRole. */
std::unique_ptr<QStandardItemModel> columnModel(const QStringList &values)
{
    auto m = std::make_unique<QStandardItemModel>(values.size(), 1);
    for (int r = 0; r < values.size(); ++r)
        m->setData(m->index(r, 0), values.at(r), Qt::EditRole);
    return m;
}

/*! The delegate's initStyleOption is protected; this exposes it. */
class ProbeDelegate : public FeatureFieldDelegate
{
public:
    using FeatureFieldDelegate::FeatureFieldDelegate;
    /** @brief Expose the protected delegate style initialization for inspection.
     * @param[in] index Model cell whose display style is requested.
     * @return Independent initialized style option; model ownership stays with the caller. */
    QStyleOptionViewItem styleFor(const QModelIndex &index) const
    {
        QStyleOptionViewItem opt;
        initStyleOption(&opt, index);
        return opt;
    }
};

} // namespace

class TestFeatureFieldDelegate : public QObject
{
    Q_OBJECT

private slots:
    /** @brief Verify a required fixed-choice delegate displays a friendly label while committing the canonical boundary token. */
    void fixedChoiceStoresTheTokenAndShowsTheLabel()
    {
        FeatureFieldDelegate d(roleField(FeatureLayerRole::BoundaryCondition, "bc_type"));
        auto model = columnModel({QStringLiteral("TS_STAGE")});
        const QModelIndex idx = model->index(0, 0);

        QCOMPARE(d.displayText(idx.data(), QLocale()),
                 QStringLiteral("Specified Stage (Timeseries)"));

        QWidget host;
        std::unique_ptr<QWidget> editor(d.createEditor(&host, QStyleOptionViewItem(), idx));
        auto *combo = qobject_cast<QComboBox *>(editor.get());
        QVERIFY(combo);
        QVERIFY(!combo->isEditable());
        QCOMPARE(combo->count(), 7); // bc_type is required: no "(none)"
        d.setEditorData(combo, idx);
        QCOMPARE(combo->currentData().toString(), QStringLiteral("TS_STAGE"));

        combo->setCurrentIndex(combo->findData(QStringLiteral("RATING_CURVE")));
        d.setModelData(combo, model.get(), idx);
        QCOMPARE(idx.data(Qt::EditRole).toString(), QStringLiteral("RATING_CURVE"));
        QVERIFY(!d.isOutOfList(idx.data()));
    }

    /** @brief Verify an optional hydrologic-soil-group choice offers none and commits an empty string. */
    void anOptionalChoiceCanBeCleared()
    {
        FeatureFieldDelegate d(roleField(FeatureLayerRole::ParameterZone, "hsg"));
        auto model = columnModel({QStringLiteral("B")});
        const QModelIndex idx = model->index(0, 0);
        QWidget host;
        std::unique_ptr<QWidget> editor(d.createEditor(&host, QStyleOptionViewItem(), idx));
        auto *combo = qobject_cast<QComboBox *>(editor.get());
        QVERIFY(combo);
        QCOMPARE(combo->count(), 8); // (none) + 7 groups
        combo->setCurrentIndex(0);
        d.setModelData(combo, model.get(), idx);
        QCOMPARE(idx.data(Qt::EditRole).toString(), QString());
    }

    /** @brief Verify an obsolete imported choice is highlighted and preserved when its editor opens/closes unchanged. */
    void aValueOutsideTheListIsKeptAndFlagged()
    {
        ProbeDelegate d(roleField(FeatureLayerRole::Region, "cells"));
        d.setWarningColor(QColor(Qt::red));
        auto model = columnModel({QStringLiteral("Mapped")}); // a retired quad mode
        const QModelIndex idx = model->index(0, 0);
        QVERIFY(d.isOutOfList(idx.data()));
        QCOMPARE(d.displayText(idx.data(), QLocale()), QStringLiteral("Mapped"));
        QCOMPARE(d.styleFor(idx).palette.color(QPalette::Text), QColor(Qt::red));

        // Opening and closing the editor must not change the value.
        QWidget host;
        std::unique_ptr<QWidget> editor(d.createEditor(&host, QStyleOptionViewItem(), idx));
        auto *combo = qobject_cast<QComboBox *>(editor.get());
        QVERIFY(combo);
        d.setEditorData(combo, idx);
        d.setModelData(combo, model.get(), idx);
        QCOMPARE(idx.data(Qt::EditRole).toString(), QStringLiteral("Mapped"));
    }

    /** @brief Verify suggested choices deduplicate existing column values and trim a newly typed value. */
    void suggestedOffersTheValuesInTheColumn()
    {
        FeatureFieldDelegate d(roleField(FeatureLayerRole::Region, "tag"));
        auto model = columnModel(
            {QStringLiteral("park"), QStringLiteral("Road"), QStringLiteral("park"), QString()});
        const QModelIndex idx = model->index(3, 0);
        QWidget host;
        std::unique_ptr<QWidget> editor(d.createEditor(&host, QStyleOptionViewItem(), idx));
        auto *combo = qobject_cast<QComboBox *>(editor.get());
        QVERIFY(combo);
        QVERIFY(combo->isEditable());
        QCOMPARE(combo->count(), 2);
        QCOMPARE(combo->itemText(0), QStringLiteral("park"));
        QCOMPARE(combo->itemText(1), QStringLiteral("Road"));
        combo->setCurrentText(QStringLiteral("  wetland ")); // anything may be typed
        d.setModelData(combo, model.get(), idx);
        QCOMPARE(idx.data(Qt::EditRole).toString(), QStringLiteral("wetland"));
        QVERIFY(!d.isOutOfList(idx.data()));
    }

    /** @brief Verify time-series choices come from the project provider, unknown names are flagged case-insensitively, and no-project delegates do not falsely flag names. */
    void modelListsComeFromTheProjectAndUnknownNamesAreFlagged()
    {
        FeatureFieldDelegate d(roleField(FeatureLayerRole::BoundaryCondition, "tseries"));
        ModelList asked = ModelList::None;
        d.setModelNamesProvider([&asked](ModelList l) {
            asked = l;
            return QStringList{QStringLiteral("TS1"), QStringLiteral("Tide")};
        });
        auto model = columnModel({QStringLiteral("TS9")});
        const QModelIndex idx = model->index(0, 0);
        QWidget host;
        std::unique_ptr<QWidget> editor(d.createEditor(&host, QStyleOptionViewItem(), idx));
        auto *combo = qobject_cast<QComboBox *>(editor.get());
        QVERIFY(combo);
        QCOMPARE(asked, ModelList::TimeSeries);
        QVERIFY(combo->isEditable());
        QCOMPARE(combo->count(), 2);
        // Allowed and flagged (plan Q4): the user may draw before defining it.
        QVERIFY(d.isOutOfList(QStringLiteral("TS9")));
        QVERIFY(!d.isOutOfList(QStringLiteral("tide")));

        FeatureFieldDelegate noProject(roleField(FeatureLayerRole::BoundaryCondition, "tseries"));
        // Without a project there is nothing to flag against.
        QVERIFY(!noProject.isOutOfList(QStringLiteral("TS9")));
    }

    /** @brief Verify length editors display a feet suffix without converting values while dimensionless slope editors remain unsuffixed. */
    void lengthFieldsShowTheUnit()
    {
        FeatureFieldDelegate d(roleField(FeatureLayerRole::Region, "h"));
        d.setLengthUnit(QStringLiteral("ft"));
        auto model = columnModel({QStringLiteral("2.5")});
        const QModelIndex idx = model->index(0, 0);
        QWidget host;
        std::unique_ptr<QWidget> editor(d.createEditor(&host, QStyleOptionViewItem(), idx));
        auto *spin = qobject_cast<QDoubleSpinBox *>(editor.get());
        QVERIFY(spin);
        QCOMPARE(spin->suffix(), QStringLiteral(" ft"));
        d.setEditorData(spin, idx);
        QCOMPARE(spin->value(), 2.5);
        spin->setValue(4.0);
        d.setModelData(spin, model.get(), idx);
        QCOMPARE(idx.data(Qt::EditRole).toDouble(), 4.0);

        FeatureFieldDelegate plain(roleField(FeatureLayerRole::BoundaryCondition, "slope"));
        plain.setLengthUnit(QStringLiteral("ft"));
        std::unique_ptr<QWidget> e2(plain.createEditor(&host, QStyleOptionViewItem(), idx));
        auto *s2 = qobject_cast<QDoubleSpinBox *>(e2.get());
        QVERIFY(s2);
        QVERIFY(s2->suffix().isEmpty());
    }

    /** @brief Verify boundary head text is disabled for wall rows and normal for specified-stage rows. */
    void bcParametersTheTypeDoesNotReadAreGreyed()
    {
        ProbeDelegate head(roleField(FeatureLayerRole::BoundaryCondition, "head"));
        head.setBcTypeColumn(0);
        QStandardItemModel model(2, 2);
        model.setData(model.index(0, 0), QStringLiteral("WALL"));
        model.setData(model.index(1, 0), QStringLiteral("SPECIFIED_STAGE"));
        model.setData(model.index(0, 1), 0.0);
        model.setData(model.index(1, 1), 3.0);
        const QStyleOptionViewItem wall = head.styleFor(model.index(0, 1));
        const QStyleOptionViewItem stage = head.styleFor(model.index(1, 1));
        QCOMPARE(wall.palette.color(QPalette::Text),
                 wall.palette.color(QPalette::Disabled, QPalette::Text));
        QCOMPARE(stage.palette.color(QPalette::Text),
                 QStyleOptionViewItem().palette.color(QPalette::Text));
    }

    /** @brief Verify unchanged empty choice, numeric, suggested and Boolean editors emit no writes.
     * Checks required-choice none and blank numeric text, then verifies an explicit numeric edit commits. */
    void anEmptyCellStaysEmptyWhenTheEditorOpensAndCloses()
    {
        QWidget host;
        auto model = columnModel({QString()});
        const QModelIndex idx = model->index(0, 0);
        QSignalSpy writes(model.get(), &QAbstractItemModel::dataChanged);

        // A required choice: "(none)" is offered for the empty cell only.
        FeatureFieldDelegate bc(roleField(FeatureLayerRole::BoundaryCondition, "bc_type"));
        std::unique_ptr<QWidget> e1(bc.createEditor(&host, QStyleOptionViewItem(), idx));
        auto *combo = qobject_cast<QComboBox *>(e1.get());
        QVERIFY(combo);
        QCOMPARE(combo->count(), 8);
        bc.setEditorData(combo, idx);
        QCOMPARE(combo->currentIndex(), 0);
        bc.setModelData(combo, model.get(), idx);

        // A length: no text, no 0 written.
        FeatureFieldDelegate h(roleField(FeatureLayerRole::Region, "h"));
        std::unique_ptr<QWidget> e2(h.createEditor(&host, QStyleOptionViewItem(), idx));
        auto *spin = qobject_cast<QDoubleSpinBox *>(e2.get());
        QVERIFY(spin);
        h.setEditorData(spin, idx);
        QVERIFY(spin->cleanText().isEmpty());
        h.setModelData(spin, model.get(), idx);

        // A suggested tag.
        FeatureFieldDelegate tag(roleField(FeatureLayerRole::Region, "tag"));
        std::unique_ptr<QWidget> e3(tag.createEditor(&host, QStyleOptionViewItem(), idx));
        tag.setEditorData(e3.get(), idx);
        tag.setModelData(e3.get(), model.get(), idx);

        // Yes / No.
        FieldDef yn;
        yn.name = QStringLiteral("active");
        yn.type = FieldType::Boolean;
        FeatureFieldDelegate b(yn);
        std::unique_ptr<QWidget> e4(b.createEditor(&host, QStyleOptionViewItem(), idx));
        auto *ynCombo = qobject_cast<QComboBox *>(e4.get());
        QVERIFY(ynCombo);
        QCOMPARE(ynCombo->count(), 3);
        b.setEditorData(ynCombo, idx);
        b.setModelData(ynCombo, model.get(), idx);

        QCOMPARE(writes.count(), 0);
        QCOMPARE(idx.data(Qt::EditRole).toString(), QString());

        // Typing a number still writes it.
        h.setEditorData(spin, idx);
        spin->setValue(3.0);
        h.setModelData(spin, model.get(), idx);
        QCOMPARE(idx.data(Qt::EditRole).toDouble(), 3.0);
    }

    /** @brief Verify real Enter commits preserve null numeric cells after empty/deleted input and write a typed seven exactly once.
     * Waits up to two seconds for queued editor closure per attempt. */
    void enterInAnEmptyNumberCellWritesNothing()
    {
        // Through a real view and key presses: the commit on Enter is queued
        // behind the spin box's own Enter handling, which used to refill an
        // empty box with its last value (0, or a number just deleted).
        FeatureFieldDelegate d(roleField(FeatureLayerRole::Region, "h"));
        d.setLengthUnit(QStringLiteral("m")); // a suffix keeps the text non-empty
        QStandardItemModel model(1, 1);
        QTableView view;
        view.setModel(&model);
        view.setItemDelegateForColumn(0, &d);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        const QModelIndex idx = model.index(0, 0);
        QSignalSpy writes(&model, &QAbstractItemModel::dataChanged);

        /** @brief Open the captured numeric table cell, type keys and wait for queued Enter commit/closure.
         * Argument `keys`: key sequence sent before Return.
         * Returns: false for a missing numeric editor or closure timeout of two seconds, otherwise true; mutates the captured table/model through its real delegate. */
        const auto typeThenEnter = [&](const QList<Qt::Key> &keys) -> bool {
            view.setCurrentIndex(idx);
            view.edit(idx);
            QWidget *editor = view.indexWidget(idx);
            if (!qobject_cast<QDoubleSpinBox *>(editor))
                return false;
            for (Qt::Key k : keys)
                QTest::keyClick(editor, k);
            QTest::keyClick(editor, Qt::Key_Return);
            // Committed and closed (both queued).
            return QTest::qWaitFor([&] { return !view.indexWidget(idx); }, 2000);
        };

        QVERIFY(typeThenEnter({Qt::Key_Backspace, Qt::Key_Home, Qt::Key_Left}));
        QCOMPARE(writes.count(), 0);
        QVERIFY(typeThenEnter({Qt::Key_5, Qt::Key_Backspace})); // typed, then deleted
        QCOMPARE(writes.count(), 0);
        QVERIFY(!model.data(idx, Qt::EditRole).isValid());

        QVERIFY(typeThenEnter({Qt::Key_7}));
        QCOMPARE(writes.count(), 1);
        QCOMPARE(model.data(idx, Qt::EditRole).toDouble(), 7.0);
    }

    /** @brief Verify Boolean display maps true/false to Yes/No and null to empty. */
    void yesNoColumns()
    {
        FieldDef f;
        f.name = QStringLiteral("active");
        f.type = FieldType::Boolean;
        FeatureFieldDelegate d(f);
        QCOMPARE(d.displayText(1, QLocale()), QStringLiteral("Yes"));
        QCOMPARE(d.displayText(false, QLocale()), QStringLiteral("No"));
        QCOMPARE(d.displayText(QVariant(), QLocale()), QString());
    }
};

QTEST_MAIN(TestFeatureFieldDelegate)
#include "test_featurefielddelegate.moc"
