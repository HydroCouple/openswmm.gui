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
#include <QCheckBox>
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
    void unrelatedEditsPreserveClimatePrecision();
    void closedEngineCannotBeAccepted();
    void constantSourcesRetainUnusedMonthlyValues();
    void a11yInvariantsHold();
    void constructsWithNullEngine();
    void hydratesFromEngine();
    void writesEditsBackOnOk();
    void noOpOkDoesNotMarkChanged();
    void setCurrentTabClamps();
    void humidityTabRoundTrips();
    void snowAndRadiationRoundTrip();
};


void TestClimatologyDialog::unrelatedEditsPreserveClimatePrecision()
{
    auto e=makeClimateEngine();const double exact=.12345678912345;
    QCOMPARE(swmm_climate_set_ati_weight(e,exact),SWMM_OK);
    double values[12];std::fill_n(values,12,1.2345678912345);
    QCOMPARE(swmm_climate_set_evap_monthly(e,values,12),SWMM_OK);
    ClimatologyDialog dialog(e);
    dialog.findChild<QDoubleSpinBox*>("clim_latitude")->setValue(50.25);
    dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    QVERIFY(dialog.wroteAnyChanges());
    double value=0;swmm_climate_get_ati_weight(e,&value);QCOMPARE(value,exact);
    double read[12];swmm_climate_get_evap_monthly(e,read,12);for(int i=0;i<12;++i)QCOMPARE(read[i],values[i]);
    swmm_engine_destroy(e);
}

void TestClimatologyDialog::closedEngineCannotBeAccepted()
{
    auto e=makeClimateEngine();ClimatologyDialog dialog(e);
    dialog.invalidateEngine();swmm_engine_destroy(e);
    QVERIFY(QMetaObject::invokeMethod(&dialog,"onAccept",Qt::DirectConnection));
    QCOMPARE(dialog.result(),int(QDialog::Rejected));QVERIFY(!dialog.wroteAnyChanges());
}

void TestClimatologyDialog::constantSourcesRetainUnusedMonthlyValues()
{
    auto e=makeClimateEngine();double evap[12],humidity[12];
    for(int i=0;i<12;++i){evap[i]=i+.123456789123;humidity[i]=35+i;}
    swmm_climate_set_evap_monthly(e,evap,12);swmm_climate_set_evap_type(e,0);
    swmm_climate_set_humidity_monthly(e,humidity,12);swmm_climate_set_humidity_type(e,0);
    ClimatologyDialog dialog(e);dialog.findChild<QDoubleSpinBox*>("clim_latitude")->setValue(51);
    dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    QVERIFY(dialog.wroteAnyChanges());double values[12];
    swmm_climate_get_evap_monthly(e,values,12);for(int i=0;i<12;++i)QCOMPARE(values[i],evap[i]);
    swmm_climate_get_humidity_monthly(e,values,12);for(int i=0;i<12;++i)QCOMPARE(values[i],humidity[i]);
    swmm_engine_destroy(e);
}

void TestClimatologyDialog::snowAndRadiationRoundTrip()
{
    SWMM_Engine e = makeClimateEngine();
    {
        ClimatologyDialog dlg(e, nullptr);
        auto *model = dlg.findChild<QComboBox*>(QStringLiteral("clim_snowModel"));
        auto *step = dlg.findChild<QDoubleSpinBox*>(QStringLiteral("clim_snowStep"));
        auto *site = dlg.findChild<QCheckBox*>(QStringLiteral("clim_radiationSite"));
        auto *latitude = dlg.findChild<QDoubleSpinBox*>(QStringLiteral("clim_site0"));
        auto *longitude = dlg.findChild<QDoubleSpinBox*>(QStringLiteral("clim_site1"));
        auto *shortwave = dlg.findChild<QComboBox*>(QStringLiteral("clim_radiationMode0"));
        auto* cloud=dlg.findChild<QComboBox*>(QStringLiteral("clim_cloudMode"));
        auto* fraction=dlg.findChild<QDoubleSpinBox*>(QStringLiteral("clim_cloud0"));
        QVERIFY(model && step && site && latitude && longitude && shortwave && cloud && fraction);
        QCOMPARE(step->maximum(),3600.);
        cloud->setCurrentIndex(cloud->findData(1));fraction->setValue(.35);
        model->setCurrentIndex(model->findData(1)); step->setValue(1800);
        site->setChecked(true); latitude->setValue(45); longitude->setValue(-120);
        shortwave->setCurrentIndex(shortwave->findData(0));
        dlg.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
        QVERIFY(dlg.wroteAnyChanges());
    }
    int model = -1; double step = 0; SWMM_RadiationConfig radiation{};
    QCOMPARE(swmm_snow_get_model(e, &model), SWMM_OK); QCOMPARE(model, 1);
    QCOMPARE(swmm_snow_get_step(e, &step), SWMM_OK); QCOMPARE(step, 1800.);
    QCOMPARE(swmm_climate_get_radiation(e, &radiation), SWMM_OK);
    SWMM_ClimateCloudConfig cloud{};char cloudName[256];
    QCOMPARE(swmm_climate_get_cloud(e,&cloud,cloudName,sizeof(cloudName)),SWMM_OK);
    QCOMPARE(cloud.mode,1);QCOMPARE(cloud.fraction,.35);
    QCOMPARE(radiation.explicit_site, 1); QCOMPARE(radiation.latitude, 45.); QCOMPARE(radiation.longitude, -120.);
    {
        ClimatologyDialog dlg(e, nullptr);
        dlg.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
        QVERIFY(!dlg.wroteAnyChanges());
    }
    swmm_engine_destroy(e);
}

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
