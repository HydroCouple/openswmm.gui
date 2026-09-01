/*!
 * \file test_heatconfigdialog.cpp
 * \brief G4g — the Heat Configuration editor, driven through its widgets
 *        against a synthetic BUILDING engine (the WaterAgeSourcesDialog
 *        precedent: dependency-light, so the wiring has an observer).
 *
 * \details The claims:
 *          1. Hydration — configured sources land checked with their °C,
 *             unconfigured ones land unchecked at the 20 °C default;
 *             module toggles and radiative scalars hydrate.
 *          2. Write-back — an edited source temperature and a toggled
 *             module reach the engine on OK.
 *          3. A no-op OK writes NOTHING (the churn discipline) — and in
 *             particular does NOT invent [HEAT_SOURCES] rows for
 *             unchecked sources or mark cloud cover configured.
 *          4. Unchecking a configured source CLEARS it (back to default,
 *             no row) rather than writing 20 °C as configuration.
 *          5. The override editor is parser-scoped: only DWF and
 *             EXTERNAL_INFLOW are offered.
 *          6. Enabling cloud with untouched values still configures it
 *             (writing any parameter marks it configured), and
 *             unchecking clears it.
 */

#include "ui/dialogs/heatconfigdialog.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_heat.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_tables.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QObject>
#include <QPushButton>
#include <QRadioButton>
#include <QTableWidget>
#include <QTest>

using OpenSWMMVis::HeatConfigDialog;

namespace {

SWMM_Engine makeHeatEngine()
{
    SWMM_Engine e = swmm_engine_new();
    Q_ASSERT(e != nullptr);
    swmm_node_add(e, "J0", 0 /*JUNCTION*/);
    swmm_node_add(e, "J1", 0 /*JUNCTION*/);
    return e;
}

QCheckBox *srcCheck(HeatConfigDialog &dlg, int source)
{
    return dlg.findChild<QCheckBox *>(
        QStringLiteral("hc_srcCheck_%1").arg(source));
}

QDoubleSpinBox *srcSpin(HeatConfigDialog &dlg, int source)
{
    return dlg.findChild<QDoubleSpinBox *>(
        QStringLiteral("hc_srcSpin_%1").arg(source));
}

void clickOk(HeatConfigDialog &dlg)
{
    auto *bb = dlg.findChild<QDialogButtonBox *>();
    QVERIFY(bb);
    bb->button(QDialogButtonBox::Ok)->click();
}

} // namespace

class TestHeatConfigDialog : public QObject
{
    Q_OBJECT
private slots:
    void constructsWithNullEngine();
    void hydratesSourcesAndModules();
    void writesEditedSourceAndModuleOnOk();
    void noOpOkWritesNothing();
    void uncheckingClearsAConfiguredSource();
    void overrideSourcesAreParserScoped();
    void cloudEnableConfiguresAndUncheckClears();
    void boundTimeseriesNamesDisplayAndRebindOnlyOnChange();
};

void TestHeatConfigDialog::constructsWithNullEngine()
{
    HeatConfigDialog dlg(nullptr);
    QVERIFY(srcCheck(dlg, SWMM_HEAT_SRC_DWF) != nullptr);
}

void TestHeatConfigDialog::hydratesSourcesAndModules()
{
    SWMM_Engine e = makeHeatEngine();
    QCOMPARE(swmm_heat_set_source_temp(e, SWMM_HEAT_SRC_GW, 11.5), SWMM_OK);
    QCOMPARE(swmm_heat_set_module(e, SWMM_HEAT_RADIATIVE_EXCHANGE, 1),
             SWMM_OK);
    QCOMPARE(swmm_heat_set_radiative(e, SWMM_HEAT_RAD_ALBEDO, 0.12),
             SWMM_OK);

    HeatConfigDialog dlg(e);
    QVERIFY(srcCheck(dlg, SWMM_HEAT_SRC_GW)->isChecked());
    QCOMPARE(srcSpin(dlg, SWMM_HEAT_SRC_GW)->value(), 11.5);
    QVERIFY(!srcCheck(dlg, SWMM_HEAT_SRC_DWF)->isChecked());
    QCOMPARE(srcSpin(dlg, SWMM_HEAT_SRC_DWF)->value(), 20.0);

    auto *mod = dlg.findChild<QCheckBox *>(
        QStringLiteral("hc_module_%1").arg(SWMM_HEAT_RADIATIVE_EXCHANGE));
    QVERIFY(mod && mod->isChecked());
    auto *alb = dlg.findChild<QDoubleSpinBox *>(
        QStringLiteral("hc_rad_%1").arg(SWMM_HEAT_RAD_ALBEDO));
    QVERIFY(alb);
    QCOMPARE(alb->value(), 0.12);

    swmm_engine_destroy(e);
}

void TestHeatConfigDialog::writesEditedSourceAndModuleOnOk()
{
    SWMM_Engine e = makeHeatEngine();
    HeatConfigDialog dlg(e);

    srcCheck(dlg, SWMM_HEAT_SRC_GW)->setChecked(true);
    srcSpin(dlg, SWMM_HEAT_SRC_GW)->setValue(14.25);
    auto *mod = dlg.findChild<QCheckBox *>(
        QStringLiteral("hc_module_%1").arg(SWMM_HEAT_SURFACE_EXCHANGE));
    QVERIFY(mod);
    mod->setChecked(true);
    clickOk(dlg);
    QVERIFY(dlg.wroteAnyChanges());

    double t = 0.0;
    int configured = 0, on = 0;
    QCOMPARE(swmm_heat_get_source_temp(e, SWMM_HEAT_SRC_GW, &t), SWMM_OK);
    QCOMPARE(t, 14.25);
    QCOMPARE(swmm_heat_get_source_configured(e, SWMM_HEAT_SRC_GW,
                                             &configured), SWMM_OK);
    QCOMPARE(configured, 1);
    QCOMPARE(swmm_heat_get_module(e, SWMM_HEAT_SURFACE_EXCHANGE, &on),
             SWMM_OK);
    QCOMPARE(on, 1);

    swmm_engine_destroy(e);
}

void TestHeatConfigDialog::noOpOkWritesNothing()
{
    SWMM_Engine e = makeHeatEngine();
    QCOMPARE(swmm_heat_set_source_temp(e, SWMM_HEAT_SRC_DWF, 9.0), SWMM_OK);

    HeatConfigDialog dlg(e);
    clickOk(dlg);
    QVERIFY(!dlg.wroteAnyChanges());
    QCOMPARE(dlg.lastWriteCount(), 0);

    // In particular: no invented [HEAT_SOURCES] rows, no invented cloud.
    int configured = 0;
    QCOMPARE(swmm_heat_get_source_configured(e, SWMM_HEAT_SRC_GW,
                                             &configured), SWMM_OK);
    QCOMPARE(configured, 0);
    QCOMPARE(swmm_heat_get_cloud_configured(e, &configured), SWMM_OK);
    QCOMPARE(configured, 0);

    swmm_engine_destroy(e);
}

void TestHeatConfigDialog::uncheckingClearsAConfiguredSource()
{
    SWMM_Engine e = makeHeatEngine();
    QCOMPARE(swmm_heat_set_source_temp(e, SWMM_HEAT_SRC_RDII, 7.5), SWMM_OK);

    HeatConfigDialog dlg(e);
    QVERIFY(srcCheck(dlg, SWMM_HEAT_SRC_RDII)->isChecked());
    srcCheck(dlg, SWMM_HEAT_SRC_RDII)->setChecked(false);
    clickOk(dlg);
    QVERIFY(dlg.wroteAnyChanges());

    int configured = 1;
    double t = 0.0;
    QCOMPARE(swmm_heat_get_source_configured(e, SWMM_HEAT_SRC_RDII,
                                             &configured), SWMM_OK);
    QCOMPARE(configured, 0);
    QCOMPARE(swmm_heat_get_source_temp(e, SWMM_HEAT_SRC_RDII, &t), SWMM_OK);
    QCOMPARE(t, 20.0);   // back to the default, not a stored 20 °C row

    swmm_engine_destroy(e);
}

void TestHeatConfigDialog::overrideSourcesAreParserScoped()
{
    SWMM_Engine e = makeHeatEngine();
    HeatConfigDialog dlg(e);

    auto *add = dlg.findChild<QPushButton *>(
        QStringLiteral("hc_addOverride"));
    QVERIFY(add);
    add->click();
    auto *table = dlg.findChild<QTableWidget *>(
        QStringLiteral("hc_overrideTable"));
    QVERIFY(table);
    QCOMPARE(table->rowCount(), 1);
    auto *combo = qobject_cast<QComboBox *>(table->cellWidget(0, 0));
    QVERIFY(combo);
    QCOMPARE(combo->count(), 2);   // DWF + EXTERNAL_INFLOW, nothing else
    QVERIFY(combo->findData(SWMM_HEAT_SRC_DWF) >= 0);
    QVERIFY(combo->findData(SWMM_HEAT_SRC_EXTERNAL_INFLOW) >= 0);
    QVERIFY(combo->findData(SWMM_HEAT_SRC_GW) < 0);

    swmm_engine_destroy(e);
}

void TestHeatConfigDialog::cloudEnableConfiguresAndUncheckClears()
{
    SWMM_Engine e = makeHeatEngine();
    {
        HeatConfigDialog dlg(e);
        auto *en = dlg.findChild<QCheckBox *>(
            QStringLiteral("hc_cloudEnable"));
        QVERIFY(en);
        en->setChecked(true);
        auto *frac = dlg.findChild<QDoubleSpinBox *>(
            QStringLiteral("hc_cloud_%1").arg(SWMM_HEAT_CLOUD_FRACTION));
        QVERIFY(frac);
        frac->setValue(0.4);
        clickOk(dlg);
        QVERIFY(dlg.wroteAnyChanges());
    }
    int configured = 0;
    double v = 0.0;
    QCOMPARE(swmm_heat_get_cloud_configured(e, &configured), SWMM_OK);
    QCOMPARE(configured, 1);
    QCOMPARE(swmm_heat_get_cloud(e, SWMM_HEAT_CLOUD_FRACTION, &v), SWMM_OK);
    QCOMPARE(v, 0.4);

    {
        HeatConfigDialog dlg(e);
        auto *en = dlg.findChild<QCheckBox *>(
            QStringLiteral("hc_cloudEnable"));
        QVERIFY(en && en->isChecked());
        en->setChecked(false);
        clickOk(dlg);
        QVERIFY(dlg.wroteAnyChanges());
    }
    QCOMPARE(swmm_heat_get_cloud_configured(e, &configured), SWMM_OK);
    QCOMPARE(configured, 0);

    swmm_engine_destroy(e);
}

void TestHeatConfigDialog::boundTimeseriesNamesDisplayAndRebindOnlyOnChange()
{
    // The G4g gap, closed end to end: the engine getters (d868b2c3) hand
    // the dialog the bound series NAME, the combo displays and preselects
    // it, and OK rebinds only when the selection moved off it.
    SWMM_Engine e = makeHeatEngine();
    QCOMPARE(swmm_timeseries_add(e, "sw_series"), SWMM_OK);
    QCOMPARE(swmm_timeseries_add(e, "alt_series"), SWMM_OK);
    QCOMPARE(swmm_heat_set_shortwave_timeseries(e, "sw_series"), SWMM_OK);

    {   // Displays the binding, and an untouched OK is still a no-op —
        // reselecting what was shown must not count as a rebind.
        HeatConfigDialog dlg(e);
        auto *combo = dlg.findChild<QComboBox *>(
            QStringLiteral("hc_swTsCombo"));
        QVERIFY(combo);
        QCOMPARE(combo->currentData().toString(),
                 QStringLiteral("sw_series"));
        // No binding on the cloud side: the placeholder row is selected.
        auto *cloud = dlg.findChild<QComboBox *>(
            QStringLiteral("hc_cloudTsCombo"));
        QVERIFY(cloud);
        QVERIFY(cloud->currentData().toString().isEmpty());
        clickOk(dlg);
        QVERIFY(!dlg.wroteAnyChanges());
    }

    {   // A real rebind writes, and the getter sees the new name.
        HeatConfigDialog dlg(e);
        auto *radio = dlg.findChild<QRadioButton *>(
            QStringLiteral("hc_swTimeseries"));
        QVERIFY(radio);
        radio->setChecked(true);
        auto *combo = dlg.findChild<QComboBox *>(
            QStringLiteral("hc_swTsCombo"));
        combo->setCurrentIndex(combo->findData(QStringLiteral("alt_series")));
        clickOk(dlg);
        QVERIFY(dlg.wroteAnyChanges());
        char buf[64] = {0};
        QCOMPARE(swmm_heat_get_shortwave_timeseries(e, buf, sizeof buf),
                 SWMM_OK);
        QCOMPARE(QString::fromUtf8(buf), QStringLiteral("alt_series"));
    }
    swmm_engine_destroy(e);
}

QTEST_MAIN(TestHeatConfigDialog)
#include "test_heatconfigdialog.moc"
