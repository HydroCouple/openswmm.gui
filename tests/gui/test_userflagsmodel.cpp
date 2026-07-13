/*!
 * \file   test_userflagsmodel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Phase 1 of docs/USER_FLAGS_UI_PLAN_2026-06-03.md — UserFlagsModel
 *         define/undefine/value round-trips against a real engine handle,
 *         signal emission, and INP persistence ([USER_FLAGS] /
 *         [USER_FLAG_VALUES]) through swmm_model_write + swmm_engine_open.
 *
 *         The round-trip INP is written to the gui-test data directory
 *         (SWMMVIS_GUI_TEST_DATA) so the file is reviewable after the run,
 *         per the repo guideline on transparent test file IO.
 */
#include "ui/models/userflagsmodel.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_nodes.h>

#include <QDir>
#include <QFile>
#include <QObject>
#include <QSignalSpy>
#include <QTest>

using openswmmvis::ui::UserFlagsModel;
using FlagType = UserFlagsModel::FlagType;

class TestUserFlagsModel : public QObject
{
    Q_OBJECT

private slots:

    void defineUndefineEnumerate()
    {
        SWMM_Engine eng = swmm_engine_new();
        QVERIFY(eng != nullptr);
        UserFlagsModel model(eng);
        QSignalSpy defsSpy(&model, &UserFlagsModel::defsChanged);

        QVERIFY(model.define(QStringLiteral("inspected"), FlagType::Boolean,
                             QStringLiteral("Field inspected?")));
        QVERIFY(model.define(QStringLiteral("PRIORITY"), FlagType::Integer,
                             QStringLiteral("Maintenance priority")));
        QVERIFY(model.define(QStringLiteral("ASSET_ID"), FlagType::String,
                             QString()));
        QCOMPARE(defsSpy.count(), 3);

        const auto &defs = model.defs();
        QCOMPARE(defs.size(), 3);
        QCOMPARE(defs[0].name, QStringLiteral("INSPECTED"));  // uppercased
        QCOMPARE(defs[0].type, FlagType::Boolean);
        QCOMPARE(defs[0].description, QStringLiteral("Field inspected?"));
        QCOMPARE(defs[1].name, QStringLiteral("PRIORITY"));
        QVERIFY(model.isDefined(QStringLiteral("inspected")));
        QVERIFY(!model.isDefined(QStringLiteral("NOPE")));

        // Undefine drops the definition and its values; cache refreshes.
        QVERIFY(model.setValue(QStringLiteral("NODE"), QStringLiteral("J1"),
                               QStringLiteral("INSPECTED"),
                               QStringLiteral("YES")));
        QVERIFY(model.undefine(QStringLiteral("INSPECTED")));
        QCOMPARE(defsSpy.count(), 4);
        QCOMPARE(model.defs().size(), 2);
        bool found = true;
        model.value(QStringLiteral("NODE"), QStringLiteral("J1"),
                    QStringLiteral("INSPECTED"), &found);
        QVERIFY(!found);

        // Undefining an unknown flag fails with an error message, no signal.
        QString err;
        QVERIFY(!model.undefine(QStringLiteral("NOPE"), &err));
        QVERIFY(!err.isEmpty());
        QCOMPARE(defsSpy.count(), 4);

        swmm_engine_destroy(eng);
    }

    void valueSetGetClear()
    {
        SWMM_Engine eng = swmm_engine_new();
        QVERIFY(eng != nullptr);
        UserFlagsModel model(eng);
        QSignalSpy valueSpy(&model, &UserFlagsModel::valueChanged);

        QVERIFY(model.define(QStringLiteral("ROUGHNESS_ADJ"), FlagType::Real,
                             QString()));

        // Unset reads as empty + found == false.
        bool found = true;
        QCOMPARE(model.value(QStringLiteral("LINK"), QStringLiteral("C1"),
                             QStringLiteral("ROUGHNESS_ADJ"), &found),
                 QString());
        QVERIFY(!found);

        QVERIFY(model.setValue(QStringLiteral("LINK"), QStringLiteral("C1"),
                               QStringLiteral("ROUGHNESS_ADJ"),
                               QStringLiteral("1.05")));
        QCOMPARE(valueSpy.count(), 1);
        QCOMPARE(valueSpy.last().at(2).toString(),
                 QStringLiteral("ROUGHNESS_ADJ"));
        QCOMPARE(model.value(QStringLiteral("LINK"), QStringLiteral("C1"),
                             QStringLiteral("ROUGHNESS_ADJ"), &found),
                 QStringLiteral("1.05"));
        QVERIFY(found);

        // Type validation: a non-numeric value for a REAL flag is rejected
        // and emits nothing.
        QString err;
        QVERIFY(!model.setValue(QStringLiteral("LINK"), QStringLiteral("C1"),
                                QStringLiteral("ROUGHNESS_ADJ"),
                                QStringLiteral("abc"), &err));
        QVERIFY(!err.isEmpty());
        QCOMPARE(valueSpy.count(), 1);

        // Setting on an undefined flag is rejected.
        QVERIFY(!model.setValue(QStringLiteral("LINK"), QStringLiteral("C1"),
                                QStringLiteral("UNDEFINED"),
                                QStringLiteral("1")));

        // Clear marks unset and notifies; idempotent.
        QVERIFY(model.clearValue(QStringLiteral("LINK"), QStringLiteral("C1"),
                                 QStringLiteral("ROUGHNESS_ADJ")));
        QCOMPARE(valueSpy.count(), 2);
        model.value(QStringLiteral("LINK"), QStringLiteral("C1"),
                    QStringLiteral("ROUGHNESS_ADJ"), &found);
        QVERIFY(!found);
        QVERIFY(model.clearValue(QStringLiteral("LINK"), QStringLiteral("C1"),
                                 QStringLiteral("ROUGHNESS_ADJ")));

        swmm_engine_destroy(eng);
    }

    void inpRoundTrip()
    {
        // Output goes to the reviewable gui-test data directory.
        const QString dataDir =
            qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
        const QString inpPath =
            QDir(dataDir).filePath(QStringLiteral("userflags_roundtrip_out.inp"));

        // Build: minimal topology + four typed flags + per-object values.
        SWMM_Engine eng = swmm_engine_new();
        QVERIFY(eng != nullptr);
        QVERIFY(swmm_node_add(eng, "J1", SWMM_NODE_JUNCTION) == SWMM_OK);
        QVERIFY(swmm_node_add(eng, "O1", SWMM_NODE_OUTFALL) == SWMM_OK);

        {
            UserFlagsModel model(eng);
            QVERIFY(model.define(QStringLiteral("INSPECTED"),
                                 FlagType::Boolean,
                                 QStringLiteral("Field inspected?")));
            QVERIFY(model.define(QStringLiteral("PRIORITY"),
                                 FlagType::Integer, QString()));
            QVERIFY(model.define(QStringLiteral("ROUGHNESS_ADJ"),
                                 FlagType::Real, QString()));
            QVERIFY(model.define(QStringLiteral("ASSET_ID"),
                                 FlagType::String, QString()));
            QVERIFY(model.setValue(QStringLiteral("NODE"), QStringLiteral("J1"),
                                   QStringLiteral("INSPECTED"),
                                   QStringLiteral("YES")));
            QVERIFY(model.setValue(QStringLiteral("NODE"), QStringLiteral("J1"),
                                   QStringLiteral("PRIORITY"),
                                   QStringLiteral("-5")));
            QVERIFY(model.setValue(QStringLiteral("LINK"),
                                   QStringLiteral("C_MAIN"),
                                   QStringLiteral("ROUGHNESS_ADJ"),
                                   QStringLiteral("1.05")));
            QVERIFY(model.setValue(QStringLiteral("LINK"),
                                   QStringLiteral("C_MAIN"),
                                   QStringLiteral("ASSET_ID"),
                                   QStringLiteral("AM 00341")));   // space → quoted in INP
        }

        const QByteArray inpUtf8 = inpPath.toUtf8();
        QVERIFY(swmm_model_write(eng, inpUtf8.constData()) == SWMM_OK);
        swmm_engine_destroy(eng);
        QVERIFY(QFile::exists(inpPath));

        // Reload into a fresh engine and confirm full fidelity.
        //
        // swmm_engine_create() (-> CREATED), NOT swmm_engine_new() (-> BUILDING).
        // swmm_engine_open() hard-rejects any state but CREATED/CLOSED, so
        // new() + open() always fails with SWMM_ERR_WRONG_STATE. The engine built
        // *above* correctly uses swmm_engine_new(), because it is constructed
        // programmatically via swmm_node_add() rather than opened from a file —
        // that is precisely what new() is for.
        SWMM_Engine eng2 = swmm_engine_create();
        QVERIFY(eng2 != nullptr);
        QVERIFY2(swmm_engine_open(eng2, inpUtf8.constData(), nullptr, nullptr,
                                  nullptr) == SWMM_OK,
                 swmm_get_last_error_msg(eng2));
        {
            UserFlagsModel model(eng2);
            const auto &defs = model.defs();
            QCOMPARE(defs.size(), 4);
            QCOMPARE(defs[0].name, QStringLiteral("INSPECTED"));
            QCOMPARE(defs[0].type, FlagType::Boolean);
            QCOMPARE(defs[0].description, QStringLiteral("Field inspected?"));
            QCOMPARE(defs[2].type, FlagType::Real);

            bool found = false;
            QCOMPARE(model.value(QStringLiteral("NODE"), QStringLiteral("J1"),
                                 QStringLiteral("INSPECTED"), &found),
                     QStringLiteral("YES"));
            QVERIFY(found);
            QCOMPARE(model.value(QStringLiteral("NODE"), QStringLiteral("J1"),
                                 QStringLiteral("PRIORITY"), nullptr),
                     QStringLiteral("-5"));
            QCOMPARE(model.value(QStringLiteral("LINK"),
                                 QStringLiteral("C_MAIN"),
                                 QStringLiteral("ROUGHNESS_ADJ"), nullptr),
                     QStringLiteral("1.05"));
            QCOMPARE(model.value(QStringLiteral("LINK"),
                                 QStringLiteral("C_MAIN"),
                                 QStringLiteral("ASSET_ID"), nullptr),
                     QStringLiteral("AM 00341"));  // quotes stripped on parse
        }
        swmm_engine_destroy(eng2);
    }

    void closedEngineIsSafe()
    {
        UserFlagsModel model(nullptr);
        QVERIFY(model.defs().isEmpty());
        QString err;
        QVERIFY(!model.define(QStringLiteral("X"), FlagType::Boolean,
                              QString(), &err));
        QVERIFY(!err.isEmpty());
        QVERIFY(!model.setValue(QStringLiteral("NODE"), QStringLiteral("J1"),
                                QStringLiteral("X"), QStringLiteral("YES")));
        QVERIFY(!model.clearValue(QStringLiteral("NODE"), QStringLiteral("J1"),
                                  QStringLiteral("X")));
        bool found = true;
        QCOMPARE(model.value(QStringLiteral("NODE"), QStringLiteral("J1"),
                             QStringLiteral("X"), &found), QString());
        QVERIFY(!found);
    }
};

QTEST_APPLESS_MAIN(TestUserFlagsModel)
#include "test_userflagsmodel.moc"
