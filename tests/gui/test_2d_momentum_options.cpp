// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Caleb Buahin
#include "ui/dialogs/simoptions/twodpage.h"
#include "ui/dialogs/simoptions/datespage.h"
#include "ui/dialogs/simulationoptionsdialog.h"
#include "ui/dialogs/simoptions/simoptionscontext.h"
#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_2d.h>
#include <QComboBox>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QGroupBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QTest>
#include <QTabWidget>
#include <QTimeEdit>
#include <qcustomeditors.h>
#include <memory>

using namespace openswmmvis::ui;
namespace
{
QString outputDir()
{
    const auto path =
        QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", "."))
            .absoluteFilePath("../../verification/coriolis_smagorinsky_2026-10-24/gui-results");
    QDir().mkpath(path);
    return path;
}
struct Engine {
    SWMM_Engine e = swmm_engine_create();
    ~Engine()
    {
        if (e)
            swmm_engine_destroy(e);
    }
    int open(const QString &input = {})
    {
        const auto file =
            input.isEmpty()
                ? QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", ".")).filePath("mini_2d.inp")
                : input;
        const int code = swmm_engine_open(
            e, file.toUtf8().constData(),
            QDir(outputDir()).filePath("model.rpt").toUtf8().constData(), nullptr,
            file.endsWith(".gpkg") ? "org.hydrocouple.openswmm.plugins.geopackage" : nullptr);
        return code ? code : swmm_2d_prepare_for_edit(e);
    }
};
QString get(SWMM_Engine e, const char *key)
{
    char value[256]{};
    swmm_options_get_ext(e, key, value, sizeof value);
    return value;
}
template <class W> W *option(TwoDPage &page, const char *key)
{
    for (auto *w : page.findChildren<W *>())
        if (w->property("optionKey").toString().split(',').contains(key))
            return w;
    return nullptr;
}
}
class Test2DMomentumOptions : public QObject
{
    Q_OBJECT
private slots:
    void fractionalBenchmarkScheduleSurvivesOptionsAndSave()
    {
        Engine engine;
        QCOMPARE(engine.open(), 0);
        for (const char *key : {"REPORT_STEP", "WET_STEP", "DRY_STEP"})
            QCOMPARE(swmm_options_set(engine.e, key, "0.1"), 0);
        QCOMPARE(swmm_options_set(engine.e, "DRY_STEP", "0.123456"), 0);
        SimOptionsContext context(engine.e, nullptr, nullptr, {});
        DatesPage dates(context);
        dates.read();
        QCOMPARE(double(dates.reportStepSeconds()), .1);
        QCOMPARE(double(dates.wetStepSeconds()), .1);
        dates.tabs()->setCurrentIndex(DatesPage::TabSteps);
        dates.resize(760, 550);
        dates.show();
        QCoreApplication::processEvents();
        QVERIFY(dates.grab().save(QDir(outputDir()).filePath("fractional-clock.png")));
        TwoDPage page(context);
        page.setScheduleHooks({[&] { return dates.wetStepSeconds(); },
                               [&] { return dates.reportStepSeconds(); },
                               [&] { return dates.durationSeconds(); }});
        QObject::connect(&dates, &DatesPage::scheduleChanged, &page, &TwoDPage::scheduleChanged);
        page.read();
        auto *mirror = page.findChild<QCustomTimespanEdit *>("report2DStepEdit");
        QVERIFY(mirror);
        QCOMPARE(mirror->findChild<QTimeEdit *>()->time().msec(), 100);
        QCustomTimespanEdit *report = nullptr;
        for (auto *edit : dates.findChildren<QCustomTimespanEdit *>())
            if (edit->property("optionKey").toString().split(',').contains("REPORT_STEP"))
                report = edit;
        QVERIFY(report);
        auto *clock = report->findChild<QTimeEdit *>();
        QVERIFY(clock);
        QCOMPARE(clock->displayFormat(), "HH:mm:ss.zzz");
        clock->setTime(QTime(0, 0, 0, 200));
        QCOMPARE(double(dates.reportStepSeconds()), .2);
        QCOMPARE(mirror->findChild<QTimeEdit *>()->time().msec(), 200);
        page.findChild<QCheckBox *>("report2DStepSameBox")->setChecked(false);
        mirror->findChild<QTimeEdit *>()->setTime(QTime(0, 0, 0, 400));
        page.findChild<QCheckBox *>("infil2DStepSameBox")->setChecked(false);
        page.findChild<QCustomTimespanEdit *>("infil2DStepEdit")
            ->findChild<QTimeEdit *>()->setTime(QTime(0, 0, 0, 125));
        dates.write();
        page.write();
        const auto file = QDir(outputDir()).filePath("fractional-options.inp");
        QCOMPARE(swmm_model_write(engine.e, file.toUtf8().constData()), 0);
        Engine reopened;
        QCOMPARE(reopened.open(file), 0);
        char value[256]{};
        QCOMPARE(swmm_options_get(reopened.e, "REPORT_STEP", value, sizeof value), 0);
        QCOMPARE(QString::fromUtf8(value).toDouble(), .2);
        QCOMPARE(swmm_options_get(reopened.e, "WET_STEP", value, sizeof value), 0);
        QCOMPARE(QString::fromUtf8(value).toDouble(), .1);
        QCOMPARE(swmm_options_get(reopened.e, "DRY_STEP", value, sizeof value), 0);
        QCOMPARE(QString::fromUtf8(value).toDouble(), .123456);
        QCOMPARE(get(reopened.e, "REPORT_2D_STEP").toDouble(), .4);
        QCOMPARE(get(reopened.e, "INFIL_STEP").toDouble(), .125);
    }
    void defaultsAndCancel()
    {
        Engine engine;
        QCOMPARE(engine.open(), 0);
        SimOptionsContext context(engine.e, nullptr, nullptr, {});
        {
            TwoDPage page(context);
            page.read();
            QCOMPARE(page.findChild<QComboBox *>("coriolisCombo")->currentData().toString(), "OFF");
            QCOMPARE(page.findChild<QComboBox *>("turbulenceModelCombo")->currentData().toString(),
                     "NONE");
            page.findChild<QComboBox *>("turbulenceModelCombo")->setCurrentIndex(1);
        }
        QCOMPARE(get(engine.e, "TURBULENCE_MODEL"), "NONE");
        QCOMPARE(get(engine.e, "CORIOLIS_LATITUDE"), "UNSET");
    }
    void editsSaveAndReopen()
    {
        Engine engine;
        QCOMPARE(engine.open(), 0);
        SimOptionsContext context(engine.e, nullptr, nullptr, {});
        TwoDPage page(context);
        page.read();
        auto *momentum = option<QComboBox>(page, "MOMENTUM_EQUATION");
        QVERIFY(momentum);
        momentum->setCurrentIndex(momentum->findData("FULL_SWE"));
        auto *tiers = option<QSpinBox>(page, "LTS_TIERS");
        QVERIFY(tiers);
        tiers->setValue(1);
        auto *order = option<QSpinBox>(page, "RECONSTRUCTION_ORDER");
        QVERIFY(order);
        order->setValue(2);
        page.findChild<QComboBox *>("coriolisCombo")->setCurrentIndex(1);
        page.findChild<QLineEdit *>("coriolisLatitudeEdit")->setText("-41.123456789");
        page.findChild<QComboBox *>("turbulenceModelCombo")->setCurrentIndex(1);
        page.findChild<QLineEdit *>("smagorinskyCsEdit")->setText("0.123456789");
        QString warning;
        QVERIFY2(page.validate(&warning), qPrintable(warning));
        page.resize(900, 1500);
        page.show();
        QTest::qWait(30);
        page.grab().save(QDir(outputDir()).filePath("momentum-options.png"));
        page.write();
        QCOMPARE(get(engine.e, "CORIOLIS"), "LATITUDE");
        QCOMPARE(get(engine.e, "TURBULENCE_MODEL"), "SMAGORINSKY");
        QCOMPARE(get(engine.e, "SMAGORINSKY_CS").toDouble(), .123456789);
        QCOMPARE(swmm_2d_set_edge_wall_drag(engine.e, 0, 0, .0123456789), 0);
        QCOMPARE(swmm_2d_set_edge_bc_group(engine.e,0,0,"THROAT"),0);
        QCOMPARE(swmm_2d_set_edge_bc_group(engine.e,1,0,"OTHER_WALLS"),0);
        QCOMPARE(swmm_2d_set_edge_wall_drag(engine.e, 0, 1, .01), SWMM_ERR_BADPARAM); // interior
        QCOMPARE(swmm_2d_set_edge_wall_drag(engine.e, 0, 0, -.01), SWMM_ERR_BADPARAM);
        const auto path = QDir(outputDir()).filePath("roundtrip.inp");
        QCOMPARE(swmm_model_write(engine.e, path.toUtf8().constData()), 0);
        QFile written(path); QVERIFY(written.open(QIODevice::ReadOnly));
        const auto authored=written.readAll(); QVERIFY(authored.contains("THROAT"));
        QVERIFY(authored.contains("OTHER_WALLS"));
        Engine reopened;
        QCOMPARE(reopened.open(path), 0);
        double cw=0;
        QCOMPARE(swmm_2d_get_edge_wall_drag(reopened.e,0,0,&cw),0);
        QCOMPARE(cw,.0123456789);
        QCOMPARE(get(reopened.e, "CORIOLIS_LATITUDE").toDouble(), -41.123456789);
        QCOMPARE(get(reopened.e, "TURBULENCE_MODEL"), "SMAGORINSKY");
        QCOMPARE(get(reopened.e, "SMAGORINSKY_CS").toDouble(), .123456789);
        QCOMPARE(get(reopened.e, "RECONSTRUCTION_ORDER"), "2");
        const auto gpkg = QDir(outputDir()).filePath("roundtrip.gpkg");
        QFile::remove(gpkg);
        QCOMPARE(swmm_model_write_with_plugin(engine.e, gpkg.toUtf8().constData(),
                                              "org.hydrocouple.openswmm.plugins.geopackage"),
                 0);
        Engine spatial;
        QCOMPARE(spatial.open(gpkg), 0);
        QCOMPARE(swmm_2d_get_edge_wall_drag(spatial.e,0,0,&cw),0);
        QCOMPARE(cw,.0123456789);
        const auto again=QDir(outputDir()).filePath("gpkg-wall.inp");
        QCOMPARE(swmm_model_write(spatial.e,again.toUtf8().constData()),0);
        QFile saved(again); QVERIFY(saved.open(QIODevice::ReadOnly));
        const auto gpkgText=saved.readAll(); QVERIFY(gpkgText.contains("THROAT"));
        QVERIFY(gpkgText.contains("OTHER_WALLS"));
        QCOMPARE(get(spatial.e, "CORIOLIS_LATITUDE").toDouble(), -41.123456789);
        QCOMPARE(get(spatial.e, "TURBULENCE_MODEL"), "SMAGORINSKY");
        QCOMPARE(get(spatial.e, "SMAGORINSKY_CS").toDouble(), .123456789);
        QCOMPARE(get(spatial.e, "RECONSTRUCTION_ORDER"), "2");
    }
    void invalidDraftsAndMissingCapability()
    {
        Engine engine;
        QCOMPARE(engine.open(), 0);
        SimOptionsContext context(engine.e, nullptr, nullptr, {});
        TwoDPage page(context);
        page.read();
        page.findChild<QComboBox *>("coriolisCombo")->setCurrentIndex(1);
        QString warning;
        QVERIFY(!page.validate(&warning));
        auto *momentum = option<QComboBox>(page, "MOMENTUM_EQUATION");
        QVERIFY(momentum);
        momentum->setCurrentIndex(momentum->findData("FULL_SWE"));
        option<QSpinBox>(page, "LTS_TIERS")->setValue(1);
        QVERIFY(!page.validate(&warning));
        page.findChild<QLineEdit *>("coriolisLatitudeEdit")->setText("91");
        QVERIFY(!page.validate(&warning));
        page.findChild<QLineEdit *>("coriolisLatitudeEdit")->setText("0");
        QVERIFY2(page.validate(&warning), qPrintable(warning));
        SimOptionsContext missing(nullptr, nullptr, nullptr, {});
        TwoDPage old(missing);
        old.read();
        QVERIFY(!old.findChild<QGroupBox *>("momentumPhysicsGroup")->isEnabled());
    }
};
QTEST_MAIN(Test2DMomentumOptions)
#include "test_2d_momentum_options.moc"
