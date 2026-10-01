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
#include <QTableWidget>
#include <QTabWidget>
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
    QVERIFY(dlg.wroteAnyChanges());
    QCOMPARE(dlg.lastWriteCount(), 1);
    QCOMPARE(applied.count(), 1);
    QCOMPARE(albedo->value(), 1.5);
    QTRY_VERIFY(albedo->hasFocus());
    auto *error = dlg.findChild<QLabel *>(QStringLiteral("hc_validationError"));
    QVERIFY(error && error->text().contains(QStringLiteral("albedo"), Qt::CaseInsensitive));
    double value = 0;
    QCOMPARE(swmm_heat_get_source_temp(e, SWMM_HEAT_SRC_GW, &value), SWMM_OK);
    QCOMPARE(value, 18.0);
    QCOMPARE(swmm_heat_get_radiative(e, SWMM_HEAT_RAD_ALBEDO, &value), SWMM_OK);
    QVERIFY(value != 1.5);
    if (retry) {
        // Restore the unchanged engine value: the retry writes nothing but
        // must retain the earlier partial-write notification.
        albedo->setValue(value);
        clickOk(dlg);
        QCOMPARE(dlg.result(), int(QDialog::Accepted));
        QCOMPARE(dlg.lastWriteCount(), 0);
    } else dlg.reject();
    QVERIFY(dlg.wroteAnyChanges());
    QCOMPARE(applied.count(), 1);
    QCOMPARE(swmm_heat_get_source_temp(e, SWMM_HEAT_SRC_GW, &value), SWMM_OK);
    QCOMPARE(value, 18.0);
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

QTEST_MAIN(TestHeatConfigDialog)
#include "test_heatconfigdialog.moc"
