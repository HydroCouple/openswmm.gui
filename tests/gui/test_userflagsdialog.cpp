/*!
 * \file   test_userflagsdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Phase 2 of docs/USER_FLAGS_UI_PLAN_2026-06-03.md — UserFlagsDialog
 *         staged-commit semantics against a real engine handle: load,
 *         add, remove (cascades values), redefine (description-only keeps
 *         values; type change clears them), and validation failures.
 *
 *         Confirmations are disabled via setConfirmationsEnabled(false) so
 *         no QMessageBox blocks the offscreen run.
 */
#include "ui/dialogs/userflagsdialog.h"
#include "ui/models/userflagsmodel.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_model.h>

#include <QComboBox>
#include <QObject>
#include <QTableWidget>
#include <QTest>

using openswmmvis::ui::UserFlagsModel;
using FlagType = UserFlagsModel::FlagType;

namespace {
enum Column { ColName = 0, ColType = 1, ColDescription = 2 };

void setRowName(QTableWidget *t, int row, const QString &name)
{
    t->item(row, ColName)->setText(name);
}

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

    void loadsExistingDefs()
    {
        SWMM_Engine eng = swmm_engine_new();
        QVERIFY(eng != nullptr);
        UserFlagsModel model(eng);
        QVERIFY(model.define(QStringLiteral("INSPECTED"), FlagType::Boolean,
                             QStringLiteral("Field inspected?")));

        UserFlagsDialog dlg(&model);
        dlg.setConfirmationsEnabled(false);
        QCOMPARE(dlg.table()->rowCount(), 1);
        QCOMPARE(dlg.table()->item(0, ColName)->text(),
                 QStringLiteral("INSPECTED"));
        QCOMPARE(dlg.table()->item(0, ColDescription)->text(),
                 QStringLiteral("Field inspected?"));
        QVERIFY(!dlg.wroteAnyChanges());

        // Applying an unchanged table writes nothing.
        QVERIFY(dlg.applyChanges());
        QVERIFY(!dlg.wroteAnyChanges());

        swmm_engine_destroy(eng);
    }

    void addAndRemoveRoundTrip()
    {
        SWMM_Engine eng = swmm_engine_new();
        QVERIFY(eng != nullptr);
        UserFlagsModel model(eng);
        QVERIFY(model.define(QStringLiteral("DOOMED"), FlagType::Integer,
                             QString()));
        QVERIFY(model.setValue(QStringLiteral("NODE"), QStringLiteral("J1"),
                               QStringLiteral("DOOMED"), QStringLiteral("3")));

        UserFlagsDialog dlg(&model);
        dlg.setConfirmationsEnabled(false);

        // Stage: drop DOOMED, add ASSET_ID (name normalised to uppercase).
        dlg.table()->removeRow(0);
        dlg.addRow(QStringLiteral("asset_id"), FlagType::String,
                   QStringLiteral("External AM id"));
        QVERIFY(dlg.applyChanges());
        QVERIFY(dlg.wroteAnyChanges());

        const auto &defs = model.defs();
        QCOMPARE(defs.size(), 1);
        QCOMPARE(defs[0].name, QStringLiteral("ASSET_ID"));
        QCOMPARE(defs[0].type, FlagType::String);

        // DOOMED's per-object value cascaded away with the undefine.
        bool found = true;
        model.value(QStringLiteral("NODE"), QStringLiteral("J1"),
                    QStringLiteral("DOOMED"), &found);
        QVERIFY(!found);

        swmm_engine_destroy(eng);
    }

    void redefineSemantics()
    {
        SWMM_Engine eng = swmm_engine_new();
        QVERIFY(eng != nullptr);
        UserFlagsModel model(eng);
        QVERIFY(model.define(QStringLiteral("PRIORITY"), FlagType::Integer,
                             QStringLiteral("old")));
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
        model.value(QStringLiteral("NODE"), QStringLiteral("J1"),
                    QStringLiteral("PRIORITY"), &found);
        QVERIFY(!found);

        swmm_engine_destroy(eng);
    }

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
