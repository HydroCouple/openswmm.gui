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
#include <openswmm/engine/openswmm_heat.h>
#include <openswmm/engine/openswmm_snow.h>

#include <QComboBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QObject>
#include <QPushButton>
#include <QLabel>
#include <QDialog>
#include <QTableWidget>
#include <QTest>
#include <QTimer>
#include <QApplication>
#include <QMessageBox>
#include <QTemporaryFile>

#include "dialog_a11y_checks.h"
#include "ui/widgets/relativepathpicker.h"

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
    void lateSetterFailureLeavesEngineUntouched();
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
    void evaporationPreviewShowsRatesCoverageAndRuntimeForcingCaveat();
    void meteorologyPreviewStatesRadiationPrecedence();
};


namespace {
// Every value the failing commit below would have written before the refusal.
QString climateState(SWMM_Engine e)
{
    QString s;double v=0;int i=0;double m[12];double a[10];
    swmm_climate_get_latitude(e,&v);s+=QString::number(v,'g',17)+'|';
    swmm_climate_get_evap_type(e,&i);s+=QString::number(i)+'|';
    const auto months=[&](int (*get)(SWMM_Engine,double*,int)){get(e,m,12);for(double x:m)s+=QString::number(x,'g',17)+',';};
    months(swmm_climate_get_evap_monthly);months(swmm_climate_get_wind_monthly);months(swmm_climate_get_humidity_monthly);
    months(swmm_climate_get_adjust_temperature);months(swmm_climate_get_adjust_evaporation);
    swmm_climate_get_humidity_type(e,&i);s+=QString::number(i)+'|';
    swmm_climate_get_snow_temp(e,&v);s+=QString::number(v,'g',17)+'|';
    swmm_climate_get_adc_impervious(e,a,10);for(double x:a)s+=QString::number(x,'g',17)+',';
    swmm_snow_get_model(e,&i);s+=QString::number(i)+'|';
    unsigned flags=0;swmm_heat_get_authoring_flags(e,&flags);s+=QString::number(flags)+'|';
    SWMM_RadiationConfig radiation{};swmm_climate_get_radiation(e,&radiation);
    s+=QString::number(radiation.explicit_site)+QString::number(radiation.latitude,'g',17);
    return s;
}
} // namespace

void TestClimatologyDialog::lateSetterFailureLeavesEngineUntouched()
{
    // C1 (closure audit): the areal-depletion curve is written after the
    // temperature, evaporation, wind, humidity, snow-melt and snow-model
    // setters. An out-of-range curve value is refused by the engine itself,
    // so every earlier accepted write must be rolled back.
    auto e=makeClimateEngine();const QString before=climateState(e);
    ClimatologyDialog dialog(e);
    dialog.findChild<QDoubleSpinBox*>("clim_latitude")->setValue(47.75);
    dialog.findChild<QDoubleSpinBox*>("clim_snowTemp")->setValue(31.5);
    auto* humidity=dialog.findChild<QComboBox*>("clim_humType");
    humidity->setCurrentIndex(humidity->findData(int(SWMM_HUMIDITY_CONSTANT)));
    dialog.findChild<QDoubleSpinBox*>("clim_humConstant")->setValue(61);
    auto* model=dialog.findChild<QComboBox*>("clim_snowModel");model->setCurrentIndex(model->findData(1));
    auto* site=dialog.findChild<QCheckBox*>("clim_radiationSite");site->setChecked(true);
    dialog.findChild<QDoubleSpinBox*>("clim_site0")->setValue(12);
    auto* adc=dialog.findChild<QTableWidget*>("clim_adc");QVERIFY(adc);
    adc->item(9,0)->setText(QStringLiteral("1.5"));
    QString warning;int warnings=0;
    QTimer closer;closer.setInterval(10);
    QObject::connect(&closer,&QTimer::timeout,[&]{
        if(auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){warning=box->text();++warnings;box->accept();}
    });
    closer.start();
    dialog.show();
    dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    closer.stop();
    QCOMPARE(warnings,1);
    QVERIFY2(warning.contains(QStringLiteral("adc impervious"))&&warning.contains(QStringLiteral("No changes were applied")),qPrintable(warning));
    QVERIFY(dialog.isVisible());QCOMPARE(dialog.result(),int(QDialog::Rejected));
    QVERIFY(!dialog.wroteAnyChanges());
    QCOMPARE(climateState(e),before);
    // The draft survives: correcting the one value commits every edit.
    adc->item(9,0)->setText(QStringLiteral("1"));
    dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    QCOMPARE(dialog.result(),int(QDialog::Accepted));QVERIFY(dialog.wroteAnyChanges());
    double value=0;swmm_climate_get_latitude(e,&value);QCOMPARE(value,47.75);
    int mode=-1;swmm_snow_get_model(e,&mode);QCOMPARE(mode,1);
    swmm_climate_get_humidity_type(e,&mode);QCOMPARE(mode,int(SWMM_HUMIDITY_CONSTANT));
    swmm_engine_destroy(e);
}

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

void TestClimatologyDialog::evaporationPreviewShowsRatesCoverageAndRuntimeForcingCaveat()
{
    SWMM_Engine e = makeClimateEngine();
    constexpr double start = 43831.0; // 2020-01-01, OADate
    QCOMPARE(swmm_options_set_start_date(e, start), SWMM_OK);
    QCOMPARE(swmm_options_set_end_date(e, start + 1.0), SWMM_OK);
    double monthly[12]{};
    QCOMPARE(swmm_climate_set_evap_monthly(e, monthly, 12), SWMM_OK);
    double adjustment[12]{}; adjustment[0] = 0.25;
    QCOMPARE(swmm_climate_set_adjust_evaporation(e, adjustment, 12), SWMM_OK);

    ClimatologyDialog dialog(e);
    auto *help = dialog.findChild<QLabel*>(QStringLiteral("clim_adjustment_help"));
    QVERIFY(help);
    QVERIFY(help->text().contains(QStringLiteral("rate added to the source"), Qt::CaseInsensitive));
    auto *monthlyGrid = dialog.findChild<QTableWidget*>(QStringLiteral("clim_evapMonthly"));
    auto *adjustments = dialog.findChild<QTableWidget*>(QStringLiteral("clim_adjust"));
    QVERIFY(monthlyGrid && adjustments);
    monthlyGrid->item(0, 0)->setText(QStringLiteral("0.5"));
    adjustments->item(0, 1)->setText(QStringLiteral("0.1"));
    auto *preview = dialog.findChild<QPushButton*>(QStringLiteral("clim_evapPreview"));
    QVERIFY(preview && preview->isEnabled());

    bool opened = false, tablePopulated = false, forcingExplained = false;
    QTimer closer;
    closer.setInterval(10);
    QObject::connect(&closer, &QTimer::timeout, [&] {
        auto *modal = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!modal || modal->objectName() != QStringLiteral("clim_evapPreviewDialog")) return;
        opened = true;
        auto *table = modal->findChild<QTableWidget*>(QStringLiteral("clim_evapPreviewTable"));
        auto *summary = modal->findChild<QLabel*>(QStringLiteral("clim_evapPreviewSummary"));
        tablePopulated = table && table->rowCount() == 1 &&
                         table->item(0, 1) && table->item(0, 1)->text() == QStringLiteral("0.5") &&
                         table->item(0, 2) && table->item(0, 2)->text() == QStringLiteral("0.1") &&
                         table->item(0, 3) && table->item(0, 3)->text() == QStringLiteral("0.6");
        forcingExplained = summary && summary->text().contains(QStringLiteral("Live runtime forcing"));
        if (auto *box = modal->findChild<QDialogButtonBox*>())
            if (auto *close = box->button(QDialogButtonBox::Close)) close->click();
    });
    closer.start();
    preview->click();
    closer.stop();
    QVERIFY(opened);
    QVERIFY(tablePopulated);
    QVERIFY(forcingExplained);
    swmm_engine_destroy(e);
}

void TestClimatologyDialog::meteorologyPreviewStatesRadiationPrecedence()
{
    SWMM_Engine e = makeClimateEngine();
    constexpr double start = 43831.0;
    QCOMPARE(swmm_options_set_start_date(e, start), SWMM_OK);
    QCOMPARE(swmm_options_set_end_date(e, start + 6.0 / 24.0), SWMM_OK);
    QTemporaryFile met;
    QVERIFY(met.open());
    QByteArray rows;
    for (int hour = 0; hour <= 6; ++hour) {
        rows += "01/01/2020 ";
        rows += QByteArray::number(hour).rightJustified(2, '0');
        rows += ":00 2 70 1 100 200\n";
    }
    QCOMPARE(met.write(rows), qint64(rows.size()));
    met.flush();

    ClimatologyDialog dialog(e);
    auto *file = dialog.findChild<openswmmvis::ui::RelativePathPicker*>(QStringLiteral("clim_metFile"));
    auto *preview = dialog.findChild<QPushButton*>(QStringLiteral("clim_metPreview"));
    QVERIFY(file && preview);
    file->setPath(met.fileName());

    QString previewText;
    QTimer closer;
    closer.setInterval(10);
    QObject::connect(&closer, &QTimer::timeout, [&] {
        if (auto *box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            previewText = box->text();
            box->accept();
        }
    });
    closer.start();
    preview->click();
    closer.stop();
    QVERIFY2(previewText.contains(QStringLiteral("radiation overrides selected series and computed values"), Qt::CaseInsensitive),
             qPrintable(previewText));
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
