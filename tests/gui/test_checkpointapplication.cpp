/* SPDX-License-Identifier: GPL-3.0-or-later */

/**
 * @file      test_checkpointapplication.cpp
 * @brief     Tests for checkpointapplication.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <QtTest>
#include <QSignalSpy>
#include <QSettings>
#include <QDir>
#include <QFile>
#include <QAction>
#include <QMdiArea>
#include <QTableView>
#include <QPushButton>
#include "simulation/simulationrunner.h"
#include "swmmvis.h"
#include "swmmvisprojectwindow.h"
#include "project/openswmmvisworkspace.h"
#include "simulation/checkpointmodel.h"
#include "simulation/simulationstatusmodel.h"
#include "ui/dialogs/checkpointsdialog.h"
#include <openswmm/engine/openswmm_checkpoint.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_surface_ownership.h>

class TestCheckpointApplication : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName("OpenSWMMCheckpointTest");
        QCoreApplication::setApplicationName("CheckpointApplication");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root());
    }
    void reviewedProjectInspectionInitialRunAndStatusUseOneModel()
    {
#ifndef OPENSWMM_HAS_2D
        QSKIP("Native authored surface profile requires 2D");
#else
        const QDir dir(root());
        const auto input = dir.filePath("reviewed.inp"), cp = dir.filePath("reviewed.chk");
        QFile f(input);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::NewOnly));
        const QByteArray deck = R"([OPTIONS]
FLOW_UNITS CMS
FLOW_ROUTING DYNWAVE
START_DATE 10/12/2026
END_DATE 10/12/2026
END_TIME 00:00:08
ROUTING_STEP .31
VARIABLE_STEP 0
WET_STEP 00:00:01
DRY_STEP 00:00:01
REPORT_STEP 00:00:02
IGNORE_QUALITY YES
IGNORE_SNOWMELT YES
IGNORE_GROUNDWATER YES
[STORAGE]
SINK 0 100 0 FUNCTIONAL 0 0 10 0 0
[RAINGAGES]
R INTENSITY 0:00:01 1 TIMESERIES Rain
[TIMESERIES]
Rain 10/12/2026 00:00:00 8
Rain 10/12/2026 00:00:04 0
[SUBCATCHMENTS]
A R SINK .0002 45 1 .5 0
[SUBAREAS]
A .015 .12 .03 .05 35 PERVIOUS 55
[INFILTRATION]
A 75 5 .3
[EVAPORATION]
CONSTANT 0
[POLYGONS]
A 1 0
A 3 0
A 3 1
A 1 1
[MAP]
DIMENSIONS 0 0 3 3
UNITS Meters
[2D_OPTIONS]
GROUNDWATER AUTO
REPORT_2D_STEP 00:00:03
MAX_TIMESTEP .12
COUPLING_SYNC .7
OUTPUT_FILE authored.h5
[2D_VERTICES]
0 0 0
2 0 0
2 2 0
0 2 0
[2D_QUADS]
0 1 2 3 .03
[2D_TRIANGLE_NODE_MAP]
0 SINK 0 1
[2D_AQUIFER_OPTIONS]
MODE MESH
NODE_ENROLMENT ROWS
LINK_SEEPAGE NONE
GW_ET BOUNDARY_ET
CLOSURE CLOSED_FORM
[2D_AQUIFER]
* 3.6 2 .45 .05 2 HG0 1
[2D_SURFACE_OWNERSHIP]
A SUBCATCH UNIFORM
)";
        QCOMPARE(f.write(deck), qint64(deck.size()));
        f.close();
        auto engine = swmm_engine_create();
        QVERIFY(engine);
        const auto rpt = dir.filePath("fixture.rpt").toUtf8(),
                   out = dir.filePath("fixture.out").toUtf8();
        QCOMPARE(swmm_engine_open(engine, input.toUtf8().constData(), rpt.constData(),
                                  out.constData(), nullptr),
                 SWMM_OK);
        QCOMPARE(swmm_file_path_set(engine, SWMM_FILE_OUTPUT_2D, nullptr,
                                    dir.filePath("fixture.h5").toUtf8().constData()),
                 SWMM_OK);
        int objects = 0, shares = 0, cells = 0, valid = 0;
        char token[128] = {}, diagnostic[4096] = {};
        QCOMPARE(swmm_surface_owner_preview(engine, nullptr, -1, nullptr, 0, &objects, nullptr, 0,
                                            &shares, nullptr, 0, &cells, &valid, token,
                                            sizeof token, diagnostic, sizeof diagnostic, nullptr,
                                            nullptr),
                 SWMM_OK);
        QVERIFY(valid);
        const auto history = dir.filePath("fixture.history").toUtf8();
        const SWMM_CheckpointSessionOptions options{sizeof options,      1, 4,
                                                    history.constData(), 1, token};
        QCOMPARE(swmm_checkpoint_start_session(engine, &options), SWMM_OK);
        double elapsed = 0;
        for (int i = 0; i < 3; ++i)
            QCOMPARE(swmm_engine_step(engine, &elapsed), SWMM_OK);
        QCOMPARE(swmm_checkpoint_save(engine, cp.toUtf8().constData()), SWMM_OK);
        swmm_engine_close(engine);
        swmm_engine_destroy(engine);

        SWMMVis window;
        auto *workspace = OpenSWMMVisWorkspace::newInstance(QString(), &window);
        QVERIFY(workspace);
        auto *project = new SWMMVisProjectWindow(workspace, input, &window);
        QList<QString> warnings, errors;
        QVERIFY2(project->loadModel(warnings, errors), qPrintable(errors.join("\n")));
        auto *mdi = window.findChild<QMdiArea *>("mdiAreaCentral");
        QVERIFY(mdi);
        mdi->addSubWindow(project);
        project->show();
        mdi->setActiveSubWindow(project);
        auto *action = window.findChild<QAction *>("actionCheckpoints");
        QVERIFY(action);
        action->trigger();
        auto *model = window.findChild<CheckpointModel *>();
        auto *dialog = window.findChild<CheckpointsDialog *>();
        QVERIFY(model);
        QVERIFY(dialog);
        auto context = model->context();
        context.detailedSources = true;
        model->setContext(context);
        QVERIFY(model->importFile(cp));
        auto *list = dialog->findChild<QTableView *>("checkpointList");
        QVERIFY(list);
        list->selectRow(0);
        QPushButton *inspect = nullptr;
        for (auto *button : dialog->findChildren<QPushButton *>())
            if (button->text() == "Inspect")
                inspect = button;
        QVERIFY(inspect);
        inspect->click();
        QTRY_VERIFY_WITH_TIMEOUT(!model->ioBusy(), 30000);
        QVERIFY2(model->entry(0)->compatible, qPrintable(model->entry(0)->inspection));
        QCOMPARE(model->context().modelPath, input);
        QCOMPARE(model->context().ownershipToken, QString::fromUtf8(token));
        QCOMPARE(model->context().modelSha256.size(), 32);
        window.findChild<QAction *>("actionExecute")->trigger();
        QCOMPARE(window.findChildren<SimulationRunner *>().size(), 0);
        action->trigger();
        QCOMPARE(window.findChildren<CheckpointModel *>().size(), 1);
        QCOMPARE(window.findChildren<CheckpointsDialog *>().size(), 1);
        QSignalSpy finished(model, &CheckpointModel::runFinished);
        QVERIFY(model->startRun(-1, dir.filePath("application-initial")));
        QVERIFY(model->save(dir.filePath("application-initial/accepted.chk")));
        if (finished.isEmpty())
            QVERIFY(finished.wait(30000));
        const auto result = qvariant_cast<CheckpointResult>(finished.last()[0]);
        QVERIFY2(result.success, qPrintable(result.error));
        auto *status = window.findChild<SimulationStatusModel *>();
        QVERIFY(status);
        QVERIFY(status->rowCount() > 0);
        const auto *record = status->jobRecord(status->jobIdForRow(status->rowCount() - 1));
        QVERIFY(record);
        QCOMPARE(record->status, SimulationJobStatus::Success);
        QVERIFY(record->currentSimDate.isValid());
        QVERIFY(record->startSimDate.isValid());
        QVERIFY(result.acceptedSeconds >= 8);
        QVERIFY(std::isfinite(result.twoDError));
        QCOMPARE(model->rowCount(), 2);
        dialog->close();
        delete project;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
#endif
    }

private:
    QString root() const
    {
        const auto path = QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral(".")))
                              .filePath("output_checkpointapplication");
        QDir().mkpath(path);
        return path;
    }
};
QTEST_MAIN(TestCheckpointApplication)
#include "test_checkpointapplication.moc"
