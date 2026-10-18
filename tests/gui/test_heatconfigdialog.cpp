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
#include "ui/heatconfigurationundo.h"
#include "ui/hydrothermalconfiguration.h"
#include <QUndoStack>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_gw2d.h>

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_heat.h>
#include <openswmm/engine/openswmm_2d.h>
#include <openswmm/engine/openswmm_climate.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_tables.h>

#include <QAccessible>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QObject>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QLineEdit>
#include <QTableWidget>
#include <QTabWidget>
#include <QTest>
#include <cmath>
#include <limits>

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
    void undoPreservesDefaultsAndRefusesExternalChanges();
    void climateAdjustmentsRollBackTogether();
    void dryPolicySurvivesInputExport();
    void savedProjectReopensWithHeatSettings();
    void resolvedDraftPreviewLeavesEngineUnchanged();
    void meshOverridesCanBePreviewedSavedAndReopened();
    void spatialOverridesCanBeEditedAndRemoved();
    void landcoverCanReturnToAirTemperatureInheritance();
    void externalEditIsNotOverwritten();
    void elementRenamesAndTagsInvalidateDraft();
    void transportPreviewDoesNotApplyDraft();
    void bedControlsAndInactiveStatusRoundTrip();
    void zeroLatitudeSiteAndComputedRadiationInOneVisit();
    void atomicEditRestoresModuleAndAuthoredTemperature();
    void tableControlsExposeFieldAndRowContext();
    void invalidationDiscardsDraftAndGuardsLaterSignals();
    void constructsWithNullEngine();
    void hydratesSourcesAndModules();
    void writesEditedSourceAndModuleOnOk();
    void noOpOkWritesNothing();
    void uncheckingClearsAConfiguredSource();
    void overrideSourcesAreParserScoped();
    void cloudEnableConfiguresAndUncheckClears();
    void boundTimeseriesNamesDisplayAndRebindOnlyOnChange();
    void exactValuesSurviveRoundedDisplay_data();
    void exactValuesSurviveRoundedDisplay();
    void invalidDraftRefusesBeforeWrites_data();
    void invalidDraftRefusesBeforeWrites();
    void timeseriesChoicesExcludeCurves();
    void failedWriteRetainsDraftAndPartialChangeFlag_data();
    void failedWriteRetainsDraftAndPartialChangeFlag();
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

void TestHeatConfigDialog::exactValuesSurviveRoundedDisplay_data()
{
    QTest::addColumn<QString>("edit");
    for (const char *name : {"none", "unrelated", "explicit"})
        QTest::newRow(name) << QString::fromLatin1(name);
}

void TestHeatConfigDialog::exactValuesSurviveRoundedDisplay()
{
    QFETCH(QString, edit);
    SWMM_Engine e = makeHeatEngine();
    QCOMPARE(swmm_heat_set_source_temp(e, SWMM_HEAT_SRC_DWF, 9.1234567), SWMM_OK);
    QCOMPARE(swmm_heat_set_node_override(e, SWMM_HEAT_SRC_DWF, 0, 8.7654321), SWMM_OK);
    QCOMPARE(swmm_heat_set_radiative(e, SWMM_HEAT_RAD_ALBEDO, 0.12345678), SWMM_OK);
    QCOMPARE(swmm_heat_set_solar(e, SWMM_HEAT_SOLAR_LATITUDE, 37.1234567), SWMM_OK);
    QCOMPARE(swmm_heat_set_cloud(e, SWMM_HEAT_CLOUD_FRACTION, 0.23456789), SWMM_OK);
    HeatConfigDialog dlg(e);
    if (edit == "unrelated")
        dlg.findChild<QCheckBox *>(QStringLiteral("hc_module_%1").arg(SWMM_HEAT_SURFACE_EXCHANGE))->setChecked(true);
    if (edit == "explicit") srcSpin(dlg, SWMM_HEAT_SRC_DWF)->setValue(14.25);
    clickOk(dlg);
    double actual = 0;
    QCOMPARE(swmm_heat_get_source_temp(e, SWMM_HEAT_SRC_DWF, &actual), SWMM_OK);
    QCOMPARE(actual, edit == "explicit" ? 14.25 : 9.1234567);
    QCOMPARE(swmm_heat_get_node_override(e, 0, nullptr, nullptr, &actual), SWMM_OK);
    QCOMPARE(actual, 8.7654321);
    QCOMPARE(swmm_heat_get_radiative(e, SWMM_HEAT_RAD_ALBEDO, &actual), SWMM_OK);
    QCOMPARE(actual, 0.12345678);
    QCOMPARE(swmm_heat_get_solar(e, SWMM_HEAT_SOLAR_LATITUDE, &actual), SWMM_OK);
    QCOMPARE(actual, 37.1234567);
    QCOMPARE(swmm_heat_get_cloud(e, SWMM_HEAT_CLOUD_FRACTION, &actual), SWMM_OK);
    QCOMPARE(actual, 0.23456789);
    QCOMPARE(dlg.wroteAnyChanges(), edit != "none");
    if (edit == "none") QCOMPARE(dlg.lastWriteCount(), 0);
    swmm_engine_destroy(e);
}

void TestHeatConfigDialog::invalidDraftRefusesBeforeWrites_data()
{
    QTest::addColumn<QString>("invalid");
    for (const char *name : {"duplicate", "missing-source", "missing-node", "missing-shortwave-series"})
        QTest::newRow(name) << QString::fromLatin1(name);
}

void TestHeatConfigDialog::invalidDraftRefusesBeforeWrites()
{
    QFETCH(QString, invalid);
    SWMM_Engine e = makeHeatEngine();
    QCOMPARE(swmm_heat_set_source_temp(e, SWMM_HEAT_SRC_GW, 12.0), SWMM_OK);
    HeatConfigDialog dlg(e);
    QSignalSpy applied(&dlg, &HeatConfigDialog::changesApplied);
    srcSpin(dlg, SWMM_HEAT_SRC_GW)->setValue(18.0);
    auto *tabs = dlg.findChild<QTabWidget *>(QStringLiteral("hc_tabs"));
    QWidget *invalidField = nullptr;
    if (invalid == "missing-shortwave-series") {
        dlg.findChild<QRadioButton *>(QStringLiteral("hc_swTimeseries"))->setChecked(true);
        invalidField = dlg.findChild<QComboBox *>(QStringLiteral("hc_swTsCombo"));
    } else {
        auto *add = dlg.findChild<QPushButton *>(QStringLiteral("hc_addOverride"));
        add->click();
        auto *table = dlg.findChild<QTableWidget *>(QStringLiteral("hc_overrideTable"));
        if (invalid == "duplicate") {
            add->click();
            invalidField = table->cellWidget(1, 0);
        } else {
            invalidField = table->cellWidget(0, invalid == "missing-source" ? 0 : 1);
            qobject_cast<QComboBox *>(invalidField)->setCurrentIndex(-1);
        }
    }
    dlg.setCurrentTab(HeatConfigDialog::TabCloud);
    dlg.show(); dlg.activateWindow(); QTest::qWait(1);
    clickOk(dlg);
    QVERIFY(dlg.isVisible());
    QVERIFY(!dlg.wroteAnyChanges());
    QCOMPARE(applied.count(), 0);
    QCOMPARE(dlg.lastWriteCount(), 0);
    double value = 0;
    QCOMPARE(swmm_heat_get_source_temp(e, SWMM_HEAT_SRC_GW, &value), SWMM_OK);
    QCOMPARE(value, 12.0);
    int count = -1;
    QCOMPARE(swmm_heat_node_override_count(e, &count), SWMM_OK);
    QCOMPARE(count, 0);
    auto *error = dlg.findChild<QLabel *>(QStringLiteral("hc_validationError"));
    QVERIFY(error && !error->text().isEmpty());
    QCOMPARE(tabs->currentIndex(), int(invalid == "missing-shortwave-series"
        ? HeatConfigDialog::TabRadiative : HeatConfigDialog::TabSources));
    QTRY_VERIFY(invalidField->hasFocus());
    dlg.reject();
    QCOMPARE(swmm_heat_get_source_temp(e, SWMM_HEAT_SRC_GW, &value), SWMM_OK);
    QCOMPARE(value, 12.0);
    swmm_engine_destroy(e);
}

void TestHeatConfigDialog::timeseriesChoicesExcludeCurves()
{
    SWMM_Engine e = makeHeatEngine();
    QCOMPARE(swmm_timeseries_add(e, "solar_series"), SWMM_OK);
    // The implementation takes the unified table type (1 = storage),
    // unlike the older curve-creation comment that calls storage type 0.
    QCOMPARE(swmm_curve_add(e, "storage_curve", 1), SWMM_OK);
    int curveType = -1;
    QCOMPARE(swmm_table_get_type(e, swmm_table_index(e, "storage_curve"), &curveType), SWMM_OK);
    QCOMPARE(curveType, 1);
    HeatConfigDialog dlg(e);
    for (const char *name : {"hc_swTsCombo", "hc_cloudTsCombo"}) {
        auto *combo = dlg.findChild<QComboBox *>(QLatin1String(name));
        QVERIFY(combo);
        QVERIFY(combo->findData(QStringLiteral("solar_series")) >= 0);
        QCOMPARE(combo->findData(QStringLiteral("storage_curve")), -1);
    }
    swmm_engine_destroy(e);
}

void TestHeatConfigDialog::failedWriteRetainsDraftAndPartialChangeFlag_data()
{
    QTest::addColumn<bool>("retry");
    QTest::newRow("cancel-after-failure") << false;
    QTest::newRow("correct-and-retry") << true;
}

void TestHeatConfigDialog::failedWriteRetainsDraftAndPartialChangeFlag()
{
    QFETCH(bool, retry);
    SWMM_Engine e = makeHeatEngine();
    QCOMPARE(swmm_heat_set_source_temp(e, SWMM_HEAT_SRC_GW, 12.0), SWMM_OK);
    HeatConfigDialog dlg(e);
    QSignalSpy applied(&dlg, &HeatConfigDialog::changesApplied);
    srcSpin(dlg, SWMM_HEAT_SRC_GW)->setValue(18.0);
    auto *albedo = dlg.findChild<QDoubleSpinBox *>(QStringLiteral("hc_rad_%1").arg(SWMM_HEAT_RAD_ALBEDO));
    QVERIFY(albedo);
    // Inject a real engine refusal through the widget without a production
    // callback seam. Normal input is bounded, but setter failures must still
    // retain drafts and report the successful earlier source write.
    albedo->setMaximum(2.0);
    albedo->setValue(1.5);
    dlg.show(); dlg.activateWindow(); QTest::qWait(1);
    clickOk(dlg);
    QVERIFY(dlg.isVisible());
    QVERIFY(!dlg.wroteAnyChanges());
    QCOMPARE(dlg.lastWriteCount(), 0);
    QCOMPARE(applied.count(), 0);
    QCOMPARE(albedo->value(), 1.5);
    QTRY_VERIFY(albedo->hasFocus());
    auto *error = dlg.findChild<QLabel *>(QStringLiteral("hc_validationError"));
    QVERIFY(error && error->text().contains(QStringLiteral("albedo"), Qt::CaseInsensitive));
    double value = 0;
    QCOMPARE(swmm_heat_get_source_temp(e, SWMM_HEAT_SRC_GW, &value), SWMM_OK);
    QCOMPARE(value, 12.0);
    QCOMPARE(swmm_heat_get_radiative(e, SWMM_HEAT_RAD_ALBEDO, &value), SWMM_OK);
    QVERIFY(value != 1.5);
    if (retry) {
        // Restore the unchanged engine value: the retry writes nothing but
        // must retain the earlier partial-write notification.
        albedo->setValue(value);
        clickOk(dlg);
        QCOMPARE(dlg.result(), int(QDialog::Accepted));
        QCOMPARE(dlg.lastWriteCount(), 1);
    } else dlg.reject();
    QCOMPARE(dlg.wroteAnyChanges(), retry);
    QCOMPARE(applied.count(), retry ? 1 : 0);
    QCOMPARE(swmm_heat_get_source_temp(e, SWMM_HEAT_SRC_GW, &value), SWMM_OK);
    QCOMPARE(value, retry ? 18.0 : 12.0);
    swmm_engine_destroy(e);
}

void TestHeatConfigDialog::invalidationDiscardsDraftAndGuardsLaterSignals()
{
    SWMM_Engine e = makeHeatEngine();
    HeatConfigDialog dlg(e);
    dlg.show();
    QVERIFY(QMetaObject::invokeMethod(&dlg, "invalidateEngine", Qt::DirectConnection));
    QVERIFY(!dlg.isEnabled());
    QVERIFY(!dlg.isVisible());
    swmm_engine_destroy(e);
    // A queued/direct accepted signal after model closure cannot reuse the
    // original handle or change the rejected result.
    QVERIFY(QMetaObject::invokeMethod(&dlg, "onAccept", Qt::DirectConnection));
    QCOMPARE(dlg.result(), int(QDialog::Rejected));
    QVERIFY(!dlg.wroteAnyChanges());
}

void TestHeatConfigDialog::tableControlsExposeFieldAndRowContext()
{
    SWMM_Engine e = makeHeatEngine();
    HeatConfigDialog dlg(e);
    auto *globals = dlg.findChild<QTableWidget *>(QStringLiteral("hc_sourceTable"));
    auto *overrides = dlg.findChild<QTableWidget *>(QStringLiteral("hc_overrideTable"));
    QVERIFY(!globals->accessibleName().isEmpty());
    QVERIFY(!overrides->accessibleName().isEmpty());
    for (int row = 0; row < globals->rowCount(); ++row)
        for (int col = 1; col < globals->columnCount(); ++col)
            QVERIFY(!globals->cellWidget(row, col)->accessibleName().isEmpty());
    auto *add = dlg.findChild<QPushButton *>(QStringLiteral("hc_addOverride"));
    auto *remove = dlg.findChild<QPushButton *>(QStringLiteral("hc_removeOverride"));
    QVERIFY(!add->autoDefault());
    QVERIFY(!remove->autoDefault());
    add->click(); add->click();
    overrides->setCurrentCell(0, 0); remove->click();
    for (int col = 0; col < overrides->columnCount(); ++col) {
        auto *field = overrides->cellWidget(0, col);
        auto *accessible = QAccessible::queryAccessibleInterface(field);
        QVERIFY(accessible);
        const QString description = accessible->text(QAccessible::Description);
        QVERIFY(description.contains(QStringLiteral("1")));
        QVERIFY(description.contains(overrides->horizontalHeaderItem(col)->text()));
    }
    swmm_engine_destroy(e);
}

void TestHeatConfigDialog::bedControlsAndInactiveStatusRoundTrip()
{
    auto e=makeHeatEngine();
    QCOMPARE(swmm_heat_set_sediment(e,5,1.23456789e-5),SWMM_OK);
    HeatConfigDialog dialog(e);
    bool meshGuidance=false;for(auto* label:dialog.findChildren<QLabel*>())if(label->text().contains("2D mesh-bed materials")&&label->text().contains("Assign Groundwater"))meshGuidance=true;QVERIFY(meshGuidance);
    auto* status=dialog.findChild<QLabel*>("hc_processStatus");
    QVERIFY(status->text().contains("off"));
    auto* module=dialog.findChild<QCheckBox*>("hc_module_3");QVERIFY(module);
    module->setChecked(true);
    QVERIFY(status->text().contains("inactive"));
    dialog.findChild<QCheckBox*>("hc_initialConfigured")->setChecked(true);
    dialog.findChild<QDoubleSpinBox*>("hc_bed_8")->setValue(8.75);
    dialog.findChild<QComboBox*>("hc_dryPolicy")->setCurrentIndex(1);
    dialog.resize(1000,780);dialog.setCurrentTab(HeatConfigDialog::TabBed);dialog.show();QTest::qWait(20);
    if(qEnvironmentVariableIsSet("SWMMVIS_HYDROTHERMAL_SCREENSHOT"))
        QVERIFY(dialog.grab().save(qEnvironmentVariable("SWMMVIS_HYDROTHERMAL_SCREENSHOT")));
    clickOk(dialog);QCOMPARE(dialog.result(),int(QDialog::Accepted));
    int on=0;swmm_heat_get_module(e,SWMM_HEAT_SEDIMENT_EXCHANGE,&on);QCOMPARE(on,1);
    swmm_heat_get_enabled(e,&on);QCOMPARE(on,0);
    const int component=swmm_process_component_find(e,"org.hydrocouple.openswmm.heat");QVERIFY(component>=0);
    const QDir saved(QFileInfo(QFINDTESTDATA("data/mini_2d.inp")).absolutePath()+"/hydrothermal_gis_2026-10-17/bed-controls");QVERIFY(QDir().mkpath(saved.path()));
    QCOMPARE(swmm_model_write(e,saved.filePath("project.inp").toUtf8().constData()),SWMM_OK);
    char id[256]{},config[4096]{},resolved[4096]{};
    swmm_process_component_get(e,component,id,sizeof id,config,sizeof config,resolved,sizeof resolved);
    QFile file(saved.filePath(QString::fromUtf8(config)));QVERIFY(file.open(QIODevice::ReadOnly));
    const auto data=file.readAll();QVERIFY(data.contains("SEDIMENT_EXCHANGE ON"));QVERIFY(data.contains("DRY_ELEMENT_TEMPERATURE AIR"));

    double value=0;swmm_heat_get_sediment(e,5,&value);QCOMPARE(value,1.23456789e-5);
    swmm_heat_get_sediment(e,8,&value);QCOMPARE(value,8.75);
    int policy=0;swmm_heat_get_dry_policy(e,&policy);QCOMPARE(policy,1);
    HeatConfigDialog reopened(e);QVERIFY(reopened.findChild<QCheckBox*>("hc_initialConfigured")->isChecked());
    clickOk(reopened);QCOMPARE(reopened.lastWriteCount(),0);
    swmm_engine_destroy(e);
}

void TestHeatConfigDialog::zeroLatitudeSiteAndComputedRadiationInOneVisit()
{
    auto e=makeHeatEngine();HeatConfigDialog dialog(e);
    auto* computed=dialog.findChild<QRadioButton*>("hc_swComputed");QVERIFY(!computed->isEnabled());
    dialog.findChild<QCheckBox*>("hc_explicitSite")->setChecked(true);
    QVERIFY(computed->isEnabled());computed->setChecked(true);clickOk(dialog);
    QCOMPARE(dialog.result(),int(QDialog::Accepted));
    int mode=-1;swmm_heat_get_shortwave_mode(e,&mode);QCOMPARE(mode,int(SWMM_HEAT_SW_COMPUTED));
    int sited=0;swmm_heat_get_solar_sited(e,&sited);QCOMPARE(sited,1);
    swmm_engine_destroy(e);
}

void TestHeatConfigDialog::atomicEditRestoresModuleAndAuthoredTemperature()
{
    auto e=makeHeatEngine();
    const auto fail=[](SWMM_Engine e,void*) -> int {
        swmm_heat_set_module(e,SWMM_HEAT_SEDIMENT_EXCHANGE,1);
        swmm_heat_set_sediment(e,SWMM_HEAT_SED_INITIAL_TEMPERATURE,7);
        OpenSWMM::Ui::ensureHeatConfigurationFile(e);
        return SWMM_ERR_BADPARAM;
    };
    QCOMPARE(swmm_hydrothermal_edit_atomic(e,fail,nullptr),SWMM_ERR_BADPARAM);
    int flag=-1;swmm_heat_get_module(e,SWMM_HEAT_SEDIMENT_EXCHANGE,&flag);QCOMPARE(flag,0);
    swmm_heat_sediment_temperature_configured(e,SWMM_HEAT_SED_INITIAL_TEMPERATURE,&flag);QCOMPARE(flag,0);
    QCOMPARE(swmm_process_component_count(e),0);
    swmm_engine_destroy(e);
}

void TestHeatConfigDialog::spatialOverridesCanBeEditedAndRemoved()
{
    auto e=makeHeatEngine();
    QCOMPARE(swmm_heat_set_element_override(e,SWMM_HEAT_ATTR_ALBEDO,SWMM_HEAT_SCOPE_NODE,"J0",.13),SWMM_OK);
    HeatConfigDialog dialog(e);auto* table=dialog.findChild<QTableWidget*>("hc_elementTable");
    QCOMPARE(table->rowCount(),1);
    qobject_cast<QDoubleSpinBox*>(table->cellWidget(0,3))->setValue(.29);clickOk(dialog);
    QCOMPARE(dialog.result(),int(QDialog::Accepted));
    double value=0;swmm_heat_get_element_override(e,0,nullptr,nullptr,nullptr,0,&value);QCOMPARE(value,.29);
    HeatConfigDialog remove(e);auto* rows=remove.findChild<QTableWidget*>("hc_elementTable");rows->selectRow(0);
    remove.findChild<QPushButton*>("hc_removeElement")->click();clickOk(remove);
    int count=-1;swmm_heat_element_override_count(e,&count);QCOMPARE(count,0);
    swmm_engine_destroy(e);
}

void TestHeatConfigDialog::landcoverCanReturnToAirTemperatureInheritance()
{
    auto e=makeHeatEngine();HeatConfigDialog dialog(e);
    dialog.findChild<QCheckBox*>("hc_landcoverConfigured")->setChecked(true);
    dialog.findChild<QDoubleSpinBox*>("hc_rad_8")->setValue(11.25);clickOk(dialog);
    double value=0;swmm_heat_get_radiative(e,8,&value);QCOMPARE(value,11.25);
    HeatConfigDialog reset(e);reset.findChild<QCheckBox*>("hc_landcoverConfigured")->setChecked(false);clickOk(reset);
    swmm_heat_get_radiative(e,8,&value);QVERIFY(std::isnan(value));
    HeatConfigDialog untouched(e);clickOk(untouched);QCOMPARE(untouched.lastWriteCount(),0);
    swmm_engine_destroy(e);
}

void TestHeatConfigDialog::externalEditIsNotOverwritten()
{
    auto e=makeHeatEngine();HeatConfigDialog dialog(e);
    srcCheck(dialog,SWMM_HEAT_SRC_GW)->setChecked(true);srcSpin(dialog,SWMM_HEAT_SRC_GW)->setValue(7);
    swmm_heat_set_source_temp(e,SWMM_HEAT_SRC_GW,13);clickOk(dialog);
    QVERIFY(!dialog.wroteAnyChanges());
    QVERIFY(dialog.findChild<QLabel*>("hc_validationError")->text().contains("another editor"));
    double value=0;swmm_heat_get_source_temp(e,SWMM_HEAT_SRC_GW,&value);QCOMPARE(value,13.);
    swmm_engine_destroy(e);
}

void TestHeatConfigDialog::transportPreviewDoesNotApplyDraft()
{
    auto e=makeHeatEngine();SWMM_TransportDraft draft{};
    draft.heat=1;draft.groundwater=-1;
    for(int& enabled:draft.surface_class)enabled=1;
    SWMM_TransportMatrix matrix{};
    QCOMPARE(swmm_preview_transport_matrix(e,&draft,&matrix),SWMM_OK);
    QCOMPARE(matrix.cell[SWMM_TRANSPORT_DOMAIN_NETWORK_1D][SWMM_TRANSPORT_CLASS_TEMPERATURE].state,SWMM_TRANSPORT_ENABLED);
    int enabled=1;swmm_heat_get_enabled(e,&enabled);QCOMPARE(enabled,0);
    draft.ignore_quality=1;QCOMPARE(swmm_preview_transport_matrix(e,&draft,&matrix),SWMM_OK);
    QCOMPARE(matrix.cell[SWMM_TRANSPORT_DOMAIN_NETWORK_1D][SWMM_TRANSPORT_CLASS_TEMPERATURE].state,SWMM_TRANSPORT_DISABLED_BY_USER);
    swmm_engine_destroy(e);
}

void TestHeatConfigDialog::undoPreservesDefaultsAndRefusesExternalChanges()
{
    auto e=makeHeatEngine();QObject owner;int changes=0,errors=0;
    OpenSWMMVis::HeatSnapshot before(swmm_heat_snapshot_create(e));QVERIFY(before);
    const auto beforeId=HeatConfigDialog::configurationSignature(e);
    swmm_heat_set_module(e,SWMM_HEAT_SEDIMENT_EXCHANGE,1);
    swmm_heat_set_sediment(e,8,7.25);
    QCOMPARE(OpenSWMM::Ui::ensureHeatConfigurationFile(e),SWMM_OK);
    QCOMPARE(swmm_process_component_count(e),1);
    OpenSWMMVis::HeatSnapshot after(swmm_heat_snapshot_create(e));QVERIFY(after);
    const auto afterId=HeatConfigDialog::configurationSignature(e);
    {
        QUndoStack stack;
        stack.push(new OpenSWMMVis::HeatConfigurationUndo(&owner,e,std::move(before),std::move(after),beforeId,afterId,
            [e]{return e;},HeatConfigDialog::configurationSignature,[&]{++changes;},[&](QString){++errors;}));
        stack.undo();QCOMPARE(HeatConfigDialog::configurationSignature(e),beforeId);
        QCOMPARE(swmm_process_component_count(e),0);
        int configured=1;swmm_heat_sediment_temperature_configured(e,8,&configured);QCOMPARE(configured,0);
        stack.redo();QCOMPARE(HeatConfigDialog::configurationSignature(e),afterId);
        swmm_heat_set_source_temp(e,SWMM_HEAT_SRC_GW,19.);
        stack.undo();QCOMPARE(errors,1);QCOMPARE(changes,2);
        double value=0;swmm_heat_get_source_temp(e,SWMM_HEAT_SRC_GW,&value);QCOMPARE(value,19.);
    }
    swmm_engine_destroy(e);
}

void TestHeatConfigDialog::climateAdjustmentsRollBackTogether()
{
    auto e=makeHeatEngine();
    const auto callback=[](SWMM_Engine engine,void*) -> int {
        double values[12];std::fill_n(values,12,2.5);
        swmm_climate_set_adjust_evaporation(engine,values,12);
        swmm_climate_set_adjust_rainfall(engine,values,12);
        return SWMM_ERR_BADPARAM;
    };
    QCOMPARE(swmm_hydrothermal_edit_atomic(e,callback,nullptr),SWMM_ERR_BADPARAM);
    double values[12]{};swmm_climate_get_adjust_evaporation(e,values,12);
    for(double value:values)QCOMPARE(value,0.);
    swmm_climate_get_adjust_rainfall(e,values,12);for(double value:values)QCOMPARE(value,1.);
    swmm_engine_destroy(e);
}

void TestHeatConfigDialog::elementRenamesAndTagsInvalidateDraft()
{
    for(bool rename:{false,true}) {
        auto e=makeHeatEngine();HeatConfigDialog dialog(e);
        srcCheck(dialog,SWMM_HEAT_SRC_RAINFALL)->setChecked(true);srcSpin(dialog,SWMM_HEAT_SRC_RAINFALL)->setValue(14.);
        QCOMPARE(rename?swmm_node_rename(e,0,"Renamed"):swmm_node_set_tag(e,0,"New tag"),SWMM_OK);
        clickOk(dialog);QVERIFY(dialog.result()!=QDialog::Accepted);QVERIFY(!dialog.wroteAnyChanges());
        int configured=1;QCOMPARE(swmm_heat_get_source_configured(e,SWMM_HEAT_SRC_RAINFALL,&configured),SWMM_OK);QCOMPARE(configured,0);
        swmm_engine_destroy(e);
    }
}

void TestHeatConfigDialog::resolvedDraftPreviewLeavesEngineUnchanged()
{
    auto e=makeHeatEngine();QCOMPARE(swmm_node_set_tag(e,0,"forest"),SWMM_OK);
    QCOMPARE(swmm_heat_set_element_override(e,SWMM_HEAT_ATTR_ALBEDO,SWMM_HEAT_SCOPE_TAG,"forest",.2),SWMM_OK);
    HeatConfigDialog dialog(e);const auto original=HeatConfigDialog::configurationSignature(e);
    dialog.setCurrentTab(HeatConfigDialog::TabSpatial);auto* table=dialog.findChild<QTableWidget*>("hc_elementTable");QCOMPARE(table->rowCount(),1);table->setCurrentCell(0,0);
    qobject_cast<QDoubleSpinBox*>(table->cellWidget(0,3))->setValue(.7);
    dialog.findChild<QPushButton*>("hc_previewElements")->click();auto* resolved=dialog.findChild<QTableWidget*>("hc_resolvedElements");QCOMPARE(resolved->rowCount(),1);QCOMPARE(resolved->item(0,0)->text(),QString("J0"));QCOMPARE(resolved->item(0,1)->text().toDouble(),.7);
    QCOMPARE(HeatConfigDialog::configurationSignature(e),original);
    qobject_cast<QDoubleSpinBox*>(table->cellWidget(0,3))->setValue(.8);QCOMPARE(resolved->rowCount(),0);
    double value=0;int source=-1;char name[100]{};QCOMPARE(swmm_heat_get_effective_element(e,0,SWMM_HEAT_SCOPE_NODE,"J0",&value,&source,name,sizeof name),SWMM_OK);QCOMPARE(value,.2);QCOMPARE(source,1);
    swmm_engine_destroy(e);
}

void TestHeatConfigDialog::meshOverridesCanBePreviewedSavedAndReopened()
{
    const auto input=QFINDTESTDATA("data/mini_2d.inp");QVERIFY(!input.isEmpty());
    QTemporaryDir directory;QVERIFY(directory.isValid());
    auto e=swmm_engine_create();QVERIFY(e);
    const auto report=directory.filePath("before.rpt").toUtf8();
    const auto output=directory.filePath("before.out").toUtf8();
    QCOMPARE(swmm_engine_open(e,input.toUtf8().constData(),report.constData(),output.constData(),nullptr),SWMM_OK);
    QCOMPARE(swmm_process_component_register(e,"org.hydrocouple.openswmm.heat","heat.cfg"),SWMM_OK);
    int cells=0;QCOMPARE(swmm_2d_cell_count(e,&cells),SWMM_OK);QCOMPARE(cells,2);
    QCOMPARE(swmm_2d_set_triangle_tag(e,0,"north"),SWMM_OK);
    QCOMPARE(swmm_2d_set_triangle_tag(e,1,"north"),SWMM_OK);
    QCOMPARE(swmm_heat_set_element_override(e,SWMM_HEAT_ATTR_THERMAL_DIFFUSIVITY,
        SWMM_HEAT_SCOPE_MESH_TAG,"north",.012),SWMM_OK);
    QCOMPARE(swmm_heat_set_element_override(e,SWMM_HEAT_ATTR_ALBEDO,
        SWMM_HEAT_SCOPE_CELL2D,"2",.22),SWMM_OK);

    HeatConfigDialog dialog(e);dialog.setCurrentTab(HeatConfigDialog::TabSpatial);
    auto* table=dialog.findChild<QTableWidget*>("hc_elementTable");QVERIFY(table);QCOMPARE(table->rowCount(),2);
    auto* scope=qobject_cast<QComboBox*>(table->cellWidget(0,1));
    auto* property=qobject_cast<QComboBox*>(table->cellWidget(0,0));
    auto* target=qobject_cast<QComboBox*>(table->cellWidget(0,2));
    QVERIFY(scope&&property&&target);
    QCOMPARE(scope->itemText(scope->findData(SWMM_HEAT_SCOPE_MESH_TAG)),QString("Mesh region tag"));
    QCOMPARE(scope->itemText(scope->findData(SWMM_HEAT_SCOPE_CELL2D)),QString("2D cell"));
    QCOMPARE(scope->currentData().toInt(),SWMM_HEAT_SCOPE_MESH_TAG);
    QVERIFY(target->findText("north")>=0);
    QCOMPARE(target->currentText(),QString("north"));
    QVERIFY(property->findData(SWMM_HEAT_ATTR_MESH_POROSITY)>=0);
    QVERIFY(property->findData(SWMM_HEAT_ATTR_GROUND_SOURCE)>=0);

    auto* preview=dialog.findChild<QPushButton*>("hc_previewElements");QVERIFY(preview);
    auto* resolved=dialog.findChild<QTableWidget*>("hc_resolvedElements");QVERIFY(resolved);
    auto* value=qobject_cast<QDoubleSpinBox*>(table->cellWidget(0,3));QVERIFY(value);value->setValue(.018);
    table->setCurrentCell(0,0);
    const auto original=HeatConfigDialog::configurationSignature(e);
    preview->click();QCOMPARE(resolved->rowCount(),2);
    QCOMPARE(resolved->item(0,0)->text(),QString("1"));QCOMPARE(resolved->item(1,0)->text(),QString("2"));
    QCOMPARE(resolved->item(0,1)->text().toDouble(),.018);QCOMPARE(HeatConfigDialog::configurationSignature(e),original);

    table->setCurrentCell(1,0);preview->click();QCOMPARE(resolved->rowCount(),1);
    QCOMPARE(resolved->item(0,0)->text(),QString("2"));QCOMPARE(resolved->item(0,1)->text().toDouble(),.22);
    auto* cellScope=qobject_cast<QComboBox*>(table->cellWidget(1,1));
    auto* cellName=qobject_cast<QComboBox*>(table->cellWidget(1,2));
    QCOMPARE(cellScope->currentData().toInt(),SWMM_HEAT_SCOPE_CELL2D);
    QVERIFY(cellName->isEditable());QVERIFY(cellName->lineEdit()->validator());QCOMPARE(cellName->currentText(),QString("2"));

    clickOk(dialog);QCOMPARE(dialog.result(),int(QDialog::Accepted));
    int count=0;QCOMPARE(swmm_heat_element_override_count(e,&count),SWMM_OK);QCOMPARE(count,2);
    const auto saved=directory.filePath("saved.inp").toUtf8();QCOMPARE(swmm_model_write(e,saved.constData()),SWMM_OK);
    auto reopened=swmm_engine_create();QVERIFY(reopened);
    const auto reopenedReport=directory.filePath("reopened.rpt").toUtf8();
    const auto reopenedOut=directory.filePath("reopened.out").toUtf8();
    QCOMPARE(swmm_engine_open(reopened,saved.constData(),reopenedReport.constData(),reopenedOut.constData(),nullptr),SWMM_OK);
    QCOMPARE(swmm_heat_element_override_count(reopened,&count),SWMM_OK);QCOMPARE(count,2);
    bool meshTag=false,cell=false;
    for(int i=0;i<count;++i) {
        int attr=0,scopeValue=0;double configured=0;char name[128]{};
        QCOMPARE(swmm_heat_get_element_override(reopened,i,&attr,&scopeValue,name,sizeof name,&configured),SWMM_OK);
        if(scopeValue==SWMM_HEAT_SCOPE_MESH_TAG) {meshTag=true;QCOMPARE(QString::fromUtf8(name),QString("north"));QCOMPARE(configured,.018);}
        if(scopeValue==SWMM_HEAT_SCOPE_CELL2D) {cell=true;QCOMPARE(QString::fromUtf8(name),QString("2"));QCOMPARE(configured,.22);}
    }
    QVERIFY(meshTag);QVERIFY(cell);
    const auto configuredSignature=HeatConfigDialog::configurationSignature(e);
    HeatConfigDialog unsupported(e);auto* unsupportedTable=unsupported.findChild<QTableWidget*>("hc_elementTable");
    auto* unsupportedProperty=qobject_cast<QComboBox*>(unsupportedTable->cellWidget(1,0));
    unsupportedProperty->setCurrentIndex(unsupportedProperty->findData(SWMM_HEAT_ATTR_GROUND_SOURCE));
    auto* unsupportedValue=qobject_cast<QDoubleSpinBox*>(unsupportedTable->cellWidget(1,3));
    QCOMPARE(unsupportedValue->decimals(),0);QCOMPARE(unsupportedValue->minimum(),0.);QCOMPARE(unsupportedValue->maximum(),1.);
    clickOk(unsupported);QVERIFY(unsupported.result()!=QDialog::Accepted);QVERIFY(!unsupported.wroteAnyChanges());
    QCOMPARE(HeatConfigDialog::configurationSignature(e),configuredSignature);
    swmm_engine_destroy(reopened);swmm_engine_destroy(e);
}

void TestHeatConfigDialog::dryPolicySurvivesInputExport()
{
    auto e=makeHeatEngine();const QDir directory(QFileInfo(QFINDTESTDATA("data/mini_2d.inp")).absolutePath()+"/hydrothermal_gis_2026-10-17/dry-policy");QVERIFY(QDir().mkpath(directory.path()));
    QCOMPARE(swmm_process_component_register(e,"org.hydrocouple.openswmm.heat","heat.cfg"),SWMM_OK);
    for(int policy:{1,2}) {
        QCOMPARE(swmm_heat_set_dry_policy(e,policy),SWMM_OK);
        const auto path=directory.filePath("heat.inp");
        QCOMPARE(swmm_model_write(e,path.toUtf8().constData()),SWMM_OK);
        QFile file(directory.filePath("heat.cfg"));QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().contains(policy==1?"DRY_ELEMENT_TEMPERATURE AIR":"DRY_ELEMENT_TEMPERATURE DEFAULT"));
    }
    swmm_heat_set_dry_policy(e,0);
    swmm_heat_set_source_temp(e,SWMM_HEAT_SRC_GW,13.25);
    const auto path=directory.filePath("heat.inp");QCOMPARE(swmm_model_write(e,path.toUtf8().constData()),SWMM_OK);
    swmm_heat_clear_source_temp(e,SWMM_HEAT_SRC_GW);
    QCOMPARE(swmm_model_write(e,path.toUtf8().constData()),SWMM_OK);
    QFile cleared(directory.filePath("heat.cfg"));QVERIFY(cleared.open(QIODevice::ReadOnly));
    const auto text=cleared.readAll();QVERIFY(!text.contains("13.25"));QVERIFY(!text.contains("DRY_ELEMENT_TEMPERATURE"));QVERIFY(!text.trimmed().isEmpty());
    swmm_engine_destroy(e);
}

void TestHeatConfigDialog::savedProjectReopensWithHeatSettings()
{
    const auto input=QFINDTESTDATA("data/mini_2d.inp");QVERIFY(!input.isEmpty());
    const QDir directory(QFileInfo(input).absolutePath()+"/hydrothermal_gis_2026-10-17/saved-heat");QVERIFY(QDir().mkpath(directory.path()));
    auto e=swmm_engine_create();QVERIFY(e);
    QCOMPARE(swmm_engine_open(e,input.toUtf8().constData(),directory.filePath("before.rpt").toUtf8().constData(),directory.filePath("before.out").toUtf8().constData(),nullptr),SWMM_OK);
    QCOMPARE(swmm_heat_set_element_override(e,SWMM_HEAT_ATTR_ALBEDO,SWMM_HEAT_SCOPE_NODE,"J1",.4),SWMM_OK);
    HeatConfigDialog dialog(e);dialog.findChild<QCheckBox*>("hc_heatEnabled")->setChecked(true);
    dialog.findChild<QCheckBox*>("hc_module_3")->setChecked(true);
    dialog.findChild<QCheckBox*>("hc_groundConfigured")->setChecked(true);
    dialog.findChild<QDoubleSpinBox*>("hc_bed_4")->setValue(9.);
    dialog.findChild<QDoubleSpinBox*>("hc_bed_5")->setValue(1e-6);
    dialog.findChild<QComboBox*>("hc_dryPolicy")->setCurrentIndex(1);
    dialog.findChild<QCheckBox*>("hc_landcoverConfigured")->setChecked(true);
    dialog.findChild<QDoubleSpinBox*>("hc_rad_8")->setValue(15.5);
    clickOk(dialog);QCOMPARE(dialog.result(),int(QDialog::Accepted));
    QCOMPARE(swmm_gw2d_row_add(e,0,nullptr,-1,1.,5.,.45,.1,2.),SWMM_OK);
    SWMM_TransportMatrix matrix{};QCOMPARE(swmm_get_transport_matrix(e,&matrix),SWMM_OK);
    QCOMPARE(matrix.cell[SWMM_TRANSPORT_DOMAIN_GROUNDWATER][SWMM_TRANSPORT_CLASS_TEMPERATURE].state,SWMM_TRANSPORT_ENABLED);
    const auto path=directory.filePath("saved.inp");QCOMPARE(swmm_model_write(e,path.toUtf8().constData()),SWMM_OK);
    auto reopened=swmm_engine_create();QVERIFY(reopened);
    QCOMPARE(swmm_engine_open(reopened,path.toUtf8().constData(),directory.filePath("after.rpt").toUtf8().constData(),directory.filePath("after.out").toUtf8().constData(),nullptr),SWMM_OK);
    int enabled=0;swmm_heat_get_enabled(reopened,&enabled);QCOMPARE(enabled,1);
    swmm_heat_get_module(reopened,SWMM_HEAT_SEDIMENT_EXCHANGE,&enabled);QCOMPARE(enabled,1);
    double value=0;swmm_heat_get_sediment(reopened,4,&value);QCOMPARE(value,9.);
    swmm_heat_get_sediment(reopened,5,&value);QCOMPARE(value,1e-6);
    swmm_heat_sediment_temperature_configured(reopened,8,&enabled);QCOMPARE(enabled,0);
    swmm_heat_get_radiative(reopened,8,&value);QCOMPARE(value,15.5);
    int policy=0;swmm_heat_get_dry_policy(reopened,&policy);QCOMPARE(policy,1);
    int count=0;swmm_heat_element_override_count(reopened,&count);QCOMPARE(count,1);
    swmm_engine_destroy(reopened);
    swmm_heat_set_module(e,SWMM_HEAT_SEDIMENT_EXCHANGE,0);swmm_heat_set_dry_policy(e,0);
    swmm_heat_clear_sediment_temperature(e,4);swmm_heat_set_sediment(e,5,0);
    swmm_heat_set_radiative(e,8,std::numeric_limits<double>::quiet_NaN());swmm_heat_remove_element_override(e,0);
    QCOMPARE(swmm_model_write(e,path.toUtf8().constData()),SWMM_OK);
    reopened=swmm_engine_create();QVERIFY(reopened);
    QCOMPARE(swmm_engine_open(reopened,path.toUtf8().constData(),directory.filePath("cleared.rpt").toUtf8().constData(),directory.filePath("cleared.out").toUtf8().constData(),nullptr),SWMM_OK);
    swmm_heat_get_module(reopened,SWMM_HEAT_SEDIMENT_EXCHANGE,&enabled);QCOMPARE(enabled,0);
    swmm_heat_get_radiative(reopened,8,&value);QVERIFY(std::isnan(value));
    swmm_heat_get_dry_policy(reopened,&policy);QCOMPARE(policy,0);
    swmm_heat_element_override_count(reopened,&count);QCOMPARE(count,0);
    swmm_engine_destroy(reopened);swmm_engine_destroy(e);
}

QTEST_MAIN(TestHeatConfigDialog)
#include "test_heatconfigdialog.moc"
