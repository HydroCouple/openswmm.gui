/**
 * @file      test_userflagsdialog.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Phase 2 of docs/USER_FLAGS_UI_PLAN_2026-06-03.md — UserFlagsDialog
 *         staged-commit semantics against a real engine handle: load,
 *         add, remove (cascades values), redefine (description-only keeps
 *         values; type change clears them), and validation failures.
 *
 *         Confirmations are disabled via setConfirmationsEnabled(false) so
 *         no QMessageBox blocks the offscreen run.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/dialogs/userflagsdialog.h"
#include "ui/models/userflagsmodel.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_model.h>

#include <QComboBox>
#include <QObject>
#include <QTableWidget>
#include <QTest>

#include "dialog_a11y_checks.h"

using openswmmvis::ui::UserFlagsModel;
using FlagType = UserFlagsModel::FlagType;

namespace
{
enum Column { ColName = 0, ColType = 1, ColDescription = 2 };

/**
 *
 * @brief Replace a staged schema row's name item text without applying engine changes.
 * @param t Borrowed non-null table with an existing ColName item.
 * @param row Valid zero-based logical row.
 * @param name Borrowed text copied to the item, without normalization here.
 * Caller owns table lifetime; no null/range validation or retained reference.
 *
 */
void setRowName(QTableWidget *t, int row, const QString &name)
{
    t->item(row, ColName)->setText(name);
}

/**
 *
 * @brief Select the staged schema row's combo entry corresponding to a flag type.
 * @param t Borrowed non-null table containing the type combo.
 * @param row Valid zero-based logical row.
 * @param type Enum converted to combo data; absent value yields index minus one.
 * Asserts combo presence through QtTest and returns from this helper on failure;
 * no apply, engine edit or ownership transfer.
 *
 */
void setRowType(QTableWidget *t, int row, FlagType type)
{
    auto *combo = qobject_cast<QComboBox *>(t->cellWidget(row, ColType));
    QVERIFY(combo);
    combo->setCurrentIndex(combo->findData(static_cast<int>(type)));
}
} // namespace

class TestUserFlagsDialog : public QObject
{
    Q_OBJECT

private slots:

    /**
     *
     * @brief Require existing flag name/description to load and unchanged Apply to report no
     * writes.
     * Runs shared dialog accessibility assertions and disables confirmations; native engine
     * is manually destroyed on success. No flag-value edit or file save.
     *
     */
    void loadsExistingDefs()
    {
        SWMM_Engine eng = swmm_engine_new();
        QVERIFY(eng != nullptr);
        UserFlagsModel model(eng);
        QVERIFY(model.define(QStringLiteral("INSPECTED"), FlagType::Boolean,
                             QStringLiteral("Field inspected?")));

        UserFlagsDialog dlg(&model);
        swmmvis_test::assertDialogA11y(&dlg);
        dlg.setConfirmationsEnabled(false);
        QCOMPARE(dlg.table()->rowCount(), 1);
        QCOMPARE(dlg.table()->item(0, ColName)->text(), QStringLiteral("INSPECTED"));
        QCOMPARE(dlg.table()->item(0, ColDescription)->text(), QStringLiteral("Field inspected?"));
        QVERIFY(!dlg.wroteAnyChanges());

        // Applying an unchanged table writes nothing.
        QVERIFY(dlg.applyChanges());
        QVERIFY(!dlg.wroteAnyChanges());

        swmm_engine_destroy(eng);
    }

    /**
     *
     * @brief Require staged removal/addition to normalize the new schema name and cascade deleted
     * flag values.
     * Removes DOOMED, adds asset_id String, applies and checks only ASSET_ID remains plus
     * old NODE/J1 value absence. No file round trip or undo despite the case name.
     *
     */
    void addAndRemoveRoundTrip()
    {
        SWMM_Engine eng = swmm_engine_new();
        QVERIFY(eng != nullptr);
        UserFlagsModel model(eng);
        QVERIFY(model.define(QStringLiteral("DOOMED"), FlagType::Integer, QString()));
        QVERIFY(model.setValue(QStringLiteral("NODE"), QStringLiteral("J1"),
                               QStringLiteral("DOOMED"), QStringLiteral("3")));

        UserFlagsDialog dlg(&model);
        dlg.setConfirmationsEnabled(false);

        // Stage: drop DOOMED, add ASSET_ID (name normalised to uppercase).
        dlg.table()->removeRow(0);
        dlg.addRow(QStringLiteral("asset_id"), FlagType::String, QStringLiteral("External AM id"));
        QVERIFY(dlg.applyChanges());
        QVERIFY(dlg.wroteAnyChanges());

        const auto &defs = model.defs();
        QCOMPARE(defs.size(), 1);
        QCOMPARE(defs[0].name, QStringLiteral("ASSET_ID"));
        QCOMPARE(defs[0].type, FlagType::String);

        // DOOMED's per-object value cascaded away with the undefine.
        bool found = true;
        model.value(QStringLiteral("NODE"), QStringLiteral("J1"), QStringLiteral("DOOMED"), &found);
        QVERIFY(!found);

        swmm_engine_destroy(eng);
    }

    /**
     *
     * @brief Require description-only redefinition to retain integer value and type change to
     * clear it.
     * Uses two successive dialogs without confirmations, checks description/new String type
     * and found flags; no input-file serialization or later row failure rollback test.
     *
     */
    void redefineSemantics()
    {
        SWMM_Engine eng = swmm_engine_new();
        QVERIFY(eng != nullptr);
        UserFlagsModel model(eng);
        QVERIFY(model.define(QStringLiteral("PRIORITY"), FlagType::Integer, QStringLiteral("old")));
        QVERIFY(model.setValue(QStringLiteral("NODE"), QStringLiteral("J1"),
                               QStringLiteral("PRIORITY"), QStringLiteral("2")));

        // Description-only edit keeps assigned values.
        {
            UserFlagsDialog dlg(&model);
            dlg.setConfirmationsEnabled(false);
            dlg.table()->item(0, ColDescription)->setText(QStringLiteral("new"));
            QVERIFY(dlg.applyChanges());
            QVERIFY(dlg.wroteAnyChanges());
        }
        bool found = false;
        QCOMPARE(model.value(QStringLiteral("NODE"), QStringLiteral("J1"),
                             QStringLiteral("PRIORITY"), &found),
                 QStringLiteral("2"));
        QVERIFY(found);
        QCOMPARE(model.defs()[0].description, QStringLiteral("new"));

        // Type change clears the flag's values (undefine + redefine).
        {
            UserFlagsDialog dlg(&model);
            dlg.setConfirmationsEnabled(false);
            setRowType(dlg.table(), 0, FlagType::String);
            QVERIFY(dlg.applyChanges());
        }
        QCOMPARE(model.defs()[0].type, FlagType::String);
        model.value(QStringLiteral("NODE"), QStringLiteral("J1"), QStringLiteral("PRIORITY"),
                    &found);
        QVERIFY(!found);

        swmm_engine_destroy(eng);
    }

    /**
     *
     * @brief Require empty and case-insensitive duplicate staged names to fail with diagnostics
     * before creating definitions.
     * Checks empty model and unchanged-write flag after both refusals; no partially applied
     * multirow engine failure or confirmation dialog is exercised.
     *
     */
    void validationFailures()
    {
        SWMM_Engine eng = swmm_engine_new();
        QVERIFY(eng != nullptr);
        UserFlagsModel model(eng);

        UserFlagsDialog dlg(&model);
        dlg.setConfirmationsEnabled(false);

        // Empty name rejected.
        dlg.addRow(QString(), FlagType::Boolean, QString());
        QString err;
        QVERIFY(!dlg.applyChanges(&err));
        QVERIFY(!err.isEmpty());
        QVERIFY(model.defs().isEmpty());

        // Duplicate names (case-insensitive) rejected.
        setRowName(dlg.table(), 0, QStringLiteral("FLAG_A"));
        dlg.addRow(QStringLiteral("flag_a"), FlagType::Integer, QString());
        err.clear();
        QVERIFY(!dlg.applyChanges(&err));
        QVERIFY(!err.isEmpty());
        QVERIFY(model.defs().isEmpty());
        QVERIFY(!dlg.wroteAnyChanges());

        swmm_engine_destroy(eng);
    }
};

QTEST_MAIN(TestUserFlagsDialog)
#include "test_userflagsdialog.moc"
