/*!
 * \file   test_userflagvaluesdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Phase 4 of docs/USER_FLAGS_UI_PLAN_2026-06-03.md —
 *         UserFlagValuesDialog staged per-object editing against a real
 *         engine handle: load, set via combo/text, blank clears,
 *         engine type rejection, and summary recomputation
 *         (userFlagsSummaryFor).
 */
#include "ui/dialogs/userflagvaluesdialog.h"
#include "ui/models/userflagsmodel.h"
#include "ui/properties/userflagseditref.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_model.h>

#include <QComboBox>
#include <QObject>
#include <QTableWidget>
#include <QTest>

using openswmmvis::ui::UserFlagsModel;
using FlagType = UserFlagsModel::FlagType;

namespace {
enum Column { ColFlag = 0, ColType = 1, ColValue = 2 };

UserFlagsEditRef makeRef(UserFlagsModel *model, const QString &name)
{
    UserFlagsEditRef ref;
    ref.model      = model;
    ref.objectType = QStringLiteral("NODE");
    ref.objectName = name;
    ref.summary    = userFlagsSummaryFor(model, ref.objectType, name);
    return ref;
}
} // namespace

class TestUserFlagValuesDialog : public QObject
{
    Q_OBJECT

private slots:

    void summaryHelper()
    {
        SWMM_Engine eng = swmm_engine_new();
        QVERIFY(eng != nullptr);
        UserFlagsModel model(eng);

        QCOMPARE(userFlagsSummaryFor(nullptr, QStringLiteral("NODE"),
                                     QStringLiteral("J1")),
                 QObject::tr("(no flags defined)"));
        QCOMPARE(userFlagsSummaryFor(&model, QStringLiteral("NODE"),
                                     QStringLiteral("J1")),
                 QObject::tr("(no flags defined)"));

        QVERIFY(model.define(QStringLiteral("INSPECTED"), FlagType::Boolean,
                             QString()));
        QVERIFY(model.define(QStringLiteral("PRIORITY"), FlagType::Integer,
                             QString()));
        QCOMPARE(userFlagsSummaryFor(&model, QStringLiteral("NODE"),
                                     QStringLiteral("J1")),
                 QObject::tr("(none set)"));

        QVERIFY(model.setValue(QStringLiteral("NODE"), QStringLiteral("J1"),
                               QStringLiteral("INSPECTED"),
                               QStringLiteral("YES")));
        QCOMPARE(userFlagsSummaryFor(&model, QStringLiteral("NODE"),
                                     QStringLiteral("J1")),
                 QObject::tr("%1 of %2 set").arg(1).arg(2));

        swmm_engine_destroy(eng);
    }

    void stagedEditRoundTrip()
    {
        SWMM_Engine eng = swmm_engine_new();
        QVERIFY(eng != nullptr);
        UserFlagsModel model(eng);
        QVERIFY(model.define(QStringLiteral("INSPECTED"), FlagType::Boolean,
                             QStringLiteral("Field inspected?")));
        QVERIFY(model.define(QStringLiteral("PRIORITY"), FlagType::Integer,
                             QString()));
        QVERIFY(model.setValue(QStringLiteral("NODE"), QStringLiteral("J1"),
                               QStringLiteral("PRIORITY"), QStringLiteral("2")));

        UserFlagValuesDialog dlg(makeRef(&model, QStringLiteral("J1")));
        dlg.setConfirmationsEnabled(false);
        QCOMPARE(dlg.table()->rowCount(), 2);
        // Existing value pre-loaded; unset boolean shows the (unset) combo.
        QCOMPARE(dlg.table()->item(1, ColValue)->text(), QStringLiteral("2"));
        auto *combo =
            qobject_cast<QComboBox *>(dlg.table()->cellWidget(0, ColValue));
        QVERIFY(combo);
        QCOMPARE(combo->currentData().toInt(), -1);

        // Stage: set boolean YES, clear the integer.
        combo->setCurrentIndex(combo->findData(1));
        dlg.table()->item(1, ColValue)->setText(QString());
        QVERIFY(dlg.applyChanges());
        QVERIFY(dlg.wroteAnyChanges());

        bool found = false;
        QCOMPARE(model.value(QStringLiteral("NODE"), QStringLiteral("J1"),
                             QStringLiteral("INSPECTED"), &found),
                 QStringLiteral("YES"));
        QVERIFY(found);
        model.value(QStringLiteral("NODE"), QStringLiteral("J1"),
                    QStringLiteral("PRIORITY"), &found);
        QVERIFY(!found);

        QCOMPARE(dlg.updatedSummary(),
                 QObject::tr("%1 of %2 set").arg(1).arg(2));

        swmm_engine_destroy(eng);
    }

    void engineRejectsBadTypedValue()
    {
        SWMM_Engine eng = swmm_engine_new();
        QVERIFY(eng != nullptr);
        UserFlagsModel model(eng);
        QVERIFY(model.define(QStringLiteral("PRIORITY"), FlagType::Integer,
                             QString()));

        UserFlagValuesDialog dlg(makeRef(&model, QStringLiteral("J1")));
        dlg.setConfirmationsEnabled(false);
        dlg.table()->item(0, ColValue)->setText(QStringLiteral("abc"));
        QString err;
        QVERIFY(!dlg.applyChanges(&err));
        QVERIFY(!err.isEmpty());
        QVERIFY(!dlg.wroteAnyChanges());

        bool found = true;
        model.value(QStringLiteral("NODE"), QStringLiteral("J1"),
                    QStringLiteral("PRIORITY"), &found);
        QVERIFY(!found);

        swmm_engine_destroy(eng);
    }

    void noopApplyWritesNothing()
    {
        SWMM_Engine eng = swmm_engine_new();
        QVERIFY(eng != nullptr);
        UserFlagsModel model(eng);
        QVERIFY(model.define(QStringLiteral("ASSET_ID"), FlagType::String,
                             QString()));
        QVERIFY(model.setValue(QStringLiteral("NODE"), QStringLiteral("J1"),
                               QStringLiteral("ASSET_ID"),
                               QStringLiteral("AM-1")));

        UserFlagValuesDialog dlg(makeRef(&model, QStringLiteral("J1")));
        dlg.setConfirmationsEnabled(false);
        QVERIFY(dlg.applyChanges());
        QVERIFY(!dlg.wroteAnyChanges());

        swmm_engine_destroy(eng);
    }
};

QTEST_MAIN(TestUserFlagValuesDialog)
#include "test_userflagvaluesdialog.moc"
