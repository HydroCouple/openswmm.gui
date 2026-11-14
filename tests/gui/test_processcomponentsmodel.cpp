// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>
#include "ui/dialogs/processcomponentsmodel.h"
#include <openswmm/engine/openswmm_process_components.h>

class ProcessComponentsModelTest : public QObject
{
    Q_OBJECT
    SWMM_Engine engine = nullptr;
private slots:
    void init()
    {
        engine = swmm_engine_create();
        QVERIFY(engine);
        const auto input = QStringLiteral(GUI_TEST_DATA_DIR "/typed_selection_fixture.inp").toUtf8();
        QCOMPARE(swmm_engine_open_metadata(engine, input.constData()), SWMM_OK);
    }
    void cleanup() { swmm_engine_destroy(engine); engine = nullptr; }

    void cleanApplyPreservesLongArgumentsAndConfigEditRetainsEveryArgument()
    {
        QCOMPARE(swmm_process_component_register(engine, "receiving", "host.json"), SWMM_OK);
        const auto library = (QString(5000, QLatin1Char('x')) + QStringLiteral("/receiving-λ.so")).toUtf8();
        QCOMPARE(swmm_process_component_argument_set(engine, 0, "library", library.constData()), SWMM_OK);
        QCOMPARE(swmm_process_component_argument_set(engine, 0, "component", "example.receiving"), SWMM_OK);
        QCOMPARE(swmm_process_component_argument_set(engine, 0, "custom", "preserve"), SWMM_OK);
        ProcessComponentsModel model;
        model.load(engine, QStringLiteral("."));
        QVERIFY(!model.isDirty());
        QCOMPARE(model.commit(engine), 0);
        QCOMPARE(model.data(model.index(0, ProcessComponentsModel::ColLibrary), Qt::EditRole).toString(),
                 QString::fromUtf8(library));
        QVERIFY(model.setData(model.index(0, ProcessComponentsModel::ColConfig), QStringLiteral("changed.json")));
        QVERIFY(model.commit(engine) > 0);
        QVERIFY(!model.isDirty());
        QCOMPARE(swmm_process_component_argument_count(engine, 0), 3);
        char key[64]{}, value[64]{};
        QCOMPARE(swmm_process_component_argument_get(engine, 0, 2, key, sizeof(key), value, sizeof(value)), SWMM_OK);
        QCOMPARE(QByteArray(key), QByteArray("custom"));
        QCOMPARE(QByteArray(value), QByteArray("preserve"));
        model.load(engine, QStringLiteral("."));
        QCOMPARE(model.data(model.index(0, ProcessComponentsModel::ColLibrary), Qt::EditRole).toString(),
                 QString::fromUtf8(library));
    }

    void editingLibraryAndTypeUsesTheSameRegistration()
    {
        ProcessComponentsModel model;
        model.load(engine, QStringLiteral("."));
        const int row = model.appendRow(QStringLiteral("river"), QStringLiteral("river.json"));
        QVERIFY(model.setData(model.index(row, ProcessComponentsModel::ColLibrary), QStringLiteral("river.so")));
        QVERIFY(model.setData(model.index(row, ProcessComponentsModel::ColComponent), QStringLiteral("example.river")));
        QVERIFY(model.commit(engine) > 0);
        QCOMPARE(swmm_process_component_count(engine), 1);
        QCOMPARE(swmm_process_component_argument_count(engine, 0), 2);
        QCOMPARE(model.commit(engine), 0);
        QVERIFY(model.setData(model.index(0, ProcessComponentsModel::ColLibrary), QStringLiteral("river-v2.so")));
        QVERIFY(model.commit(engine) > 0);
        QCOMPARE(swmm_process_component_count(engine), 1);
        QCOMPARE(model.data(model.index(0, ProcessComponentsModel::ColComponent), Qt::EditRole).toString(),
                 QStringLiteral("example.river"));
    }

    void duplicateInstancesFailBeforeEngineEdits()
    {
        ProcessComponentsModel model;
        model.load(engine, QStringLiteral("."));
        model.appendRow(QStringLiteral("river"), QStringLiteral("one.json"));
        model.appendRow(QStringLiteral("river"), QStringLiteral("two.json"));
        QCOMPARE(model.commit(engine), -1);
        QCOMPARE(swmm_process_component_count(engine), 0);
        QVERIFY(model.isDirty());
        QVERIFY(!model.errorString().isEmpty());
    }

    void incompleteForeignRegistrationFailsBeforeEngineEdits()
    {
        ProcessComponentsModel model;
        model.load(engine, QStringLiteral("."));
        const int row = model.appendRow(QStringLiteral("river"), QStringLiteral("river.json"));
        QVERIFY(model.setData(model.index(row, ProcessComponentsModel::ColLibrary), QStringLiteral("river.so")));
        QVERIFY(!model.validationError().isEmpty());
        QCOMPARE(model.commit(engine), -1);
        QCOMPARE(swmm_process_component_count(engine), 0);
        QVERIFY(model.setData(model.index(row, ProcessComponentsModel::ColComponent), QStringLiteral("example.river")));
        QVERIFY(model.validationError().isEmpty());
        QVERIFY(model.commit(engine) > 0);
    }
};
QTEST_MAIN(ProcessComponentsModelTest)
#include "test_processcomponentsmodel.moc"
