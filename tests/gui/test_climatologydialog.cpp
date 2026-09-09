/*!
 * \file   test_climatologydialog.cpp
 * \brief  QtTest: ClimatologyDialog reads from / writes to the engine via the
 *         swmm_climate_* C API. Uses a synthetic BUILDING-state engine (no .inp
 *         needed), stamps climate values, and verifies the dialog hydrates its
 *         widgets and writes edits back on OK.
 *
 * \see ui/dialogs/climatologydialog.h
 */

#include "ui/dialogs/climatologydialog.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_climate.h>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QObject>
#include <QPushButton>
#include <QTest>

#include "dialog_a11y_checks.h"

namespace {

SWMM_Engine makeClimateEngine()
{
    SWMM_Engine e = swmm_engine_new();   // BUILDING state — editable
    Q_ASSERT(e != nullptr);
    swmm_climate_set_latitude(e, 41.5);
    swmm_climate_set_snow_temp(e, 33.0);
    swmm_climate_set_evap_type(e, 1);    // MONTHLY
    return e;
}

} // namespace

class TestClimatologyDialog : public QObject
{
    Q_OBJECT
private slots:
    void a11yInvariantsHold();
    void constructsWithNullEngine();
    void hydratesFromEngine();
    void writesEditsBackOnOk();
    void noOpOkDoesNotMarkChanged();
    void setCurrentTabClamps();
    void humidityTabRoundTrips();
};

void TestClimatologyDialog::constructsWithNullEngine()
{
    // Must not crash with no engine (read path early-returns).
    ClimatologyDialog dlg(nullptr, nullptr);
    QCOMPARE(dlg.wroteAnyChanges(), false);
}

void TestClimatologyDialog::hydratesFromEngine()
{
    SWMM_Engine e = makeClimateEngine();
    ClimatologyDialog dlg(e, nullptr);

    auto *lat = dlg.findChild<QDoubleSpinBox *>(QStringLiteral("clim_latitude"));
    auto *snow = dlg.findChild<QDoubleSpinBox *>(QStringLiteral("clim_snowTemp"));
    auto *evap = dlg.findChild<QComboBox *>(QStringLiteral("clim_evapType"));
    QVERIFY(lat && snow && evap);
    QCOMPARE(lat->value(), 41.5);
    QCOMPARE(snow->value(), 33.0);
    QCOMPARE(evap->currentData().toInt(), 1);   // MONTHLY
    QCOMPARE(dlg.wroteAnyChanges(), false);

    swmm_engine_destroy(e);
}

void TestClimatologyDialog::writesEditsBackOnOk()
{
    SWMM_Engine e = makeClimateEngine();
    ClimatologyDialog dlg(e, nullptr);

    auto *lat = dlg.findChild<QDoubleSpinBox *>(QStringLiteral("clim_latitude"));
    QVERIFY(lat);
    lat->setValue(50.25);

    auto *bb = dlg.findChild<QDialogButtonBox *>();
    QVERIFY(bb);
    bb->button(QDialogButtonBox::Ok)->click();   // → onAccept → writeToEngine

    QVERIFY(dlg.wroteAnyChanges());
    double v = 0.0;
    QCOMPARE(swmm_climate_get_latitude(e, &v), SWMM_OK);
    QCOMPARE(v, 50.25);

    swmm_engine_destroy(e);
}

void TestClimatologyDialog::noOpOkDoesNotMarkChanged()
{
    SWMM_Engine e = makeClimateEngine();
    ClimatologyDialog dlg(e, nullptr);

    auto *bb = dlg.findChild<QDialogButtonBox *>();
    QVERIFY(bb);
    bb->button(QDialogButtonBox::Ok)->click();   // no edits made

    QCOMPARE(dlg.wroteAnyChanges(), false);
    swmm_engine_destroy(e);
}

void TestClimatologyDialog::setCurrentTabClamps()
{
    SWMM_Engine e = makeClimateEngine();
    ClimatologyDialog dlg(e, nullptr);
    dlg.setCurrentTab(ClimatologyDialog::TabAdjustments);  // last tab
    dlg.setCurrentTab(999);                                // out of range: no crash
    dlg.setCurrentTab(ClimatologyDialog::TabTemperature);
    swmm_engine_destroy(e);
}

void TestClimatologyDialog::humidityTabRoundTrips()
{
    // Hydrate: a DEWPOINT / TIMESERIES engine shows up in the new tab.
    SWMM_Engine e = makeClimateEngine();
    QCOMPARE(swmm_climate_set_humidity_variable(e, SWMM_HUMIDITY_DEWPOINT), SWMM_OK);
    QCOMPARE(swmm_climate_set_humidity_timeseries(e, "rh_ts"), SWMM_OK);
    {
        ClimatologyDialog dlg(e, nullptr);
        auto *var  = dlg.findChild<QComboBox *>(QStringLiteral("clim_humVar"));
        auto *type = dlg.findChild<QComboBox *>(QStringLiteral("clim_humType"));
        QVERIFY(var && type);
        QCOMPARE(var->currentData().toInt(), int(SWMM_HUMIDITY_DEWPOINT));
        QCOMPARE(type->currentData().toInt(), int(SWMM_HUMIDITY_TIMESERIES));
        QCOMPARE(dlg.wroteAnyChanges(), false);
    }

    // Write back: switch to a constant RH and OK.
    {
        ClimatologyDialog dlg(e, nullptr);
        auto *var  = dlg.findChild<QComboBox *>(QStringLiteral("clim_humVar"));
        auto *type = dlg.findChild<QComboBox *>(QStringLiteral("clim_humType"));
        auto *val  = dlg.findChild<QDoubleSpinBox *>(QStringLiteral("clim_humConstant"));
        QVERIFY(var && type && val);
        var->setCurrentIndex(var->findData(int(SWMM_HUMIDITY_RELATIVE)));
        type->setCurrentIndex(type->findData(int(SWMM_HUMIDITY_CONSTANT)));
        val->setValue(72.5);

        auto *bb = dlg.findChild<QDialogButtonBox *>();
        QVERIFY(bb);
        bb->button(QDialogButtonBox::Ok)->click();
        QVERIFY(dlg.wroteAnyChanges());
    }
    int i = -1;
    QCOMPARE(swmm_climate_get_humidity_variable(e, &i), SWMM_OK);
    QCOMPARE(i, int(SWMM_HUMIDITY_RELATIVE));
    QCOMPARE(swmm_climate_get_humidity_type(e, &i), SWMM_OK);
    QCOMPARE(i, int(SWMM_HUMIDITY_CONSTANT));
    double h[12] = {};
    QCOMPARE(swmm_climate_get_humidity_monthly(e, h, 12), SWMM_OK);
    QCOMPARE(h[0], 72.5);
    QCOMPARE(h[11], 72.5);

    swmm_engine_destroy(e);
}

void TestClimatologyDialog::a11yInvariantsHold()
{
    ClimatologyDialog dlg(nullptr, nullptr);
    swmmvis_test::assertDialogA11y(&dlg);
}

QTEST_MAIN(TestClimatologyDialog)
#include "test_climatologydialog.moc"
