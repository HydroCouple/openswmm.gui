/*!
 * \file test_initialqualitydialog.cpp
 * \brief G-A1 — the per-element Initial Quality editor, driven through its
 *        widgets.
 *
 * \details Dependency-light dialog (Qt Widgets + `openswmm_initial_quality.h`
 *          and friends), so a test constructs it against a synthetic
 *          BUILDING engine, hydrates, clicks, and reads the engine back —
 *          the `WaterAgeSourcesDialog` precedent.
 *
 *          The claims:
 *          1. Hydration — engine rows land in the widgets (scope, element,
 *             constituent, value).
 *          2. Write-back — an added row reaches the engine on OK; the write
 *             count is exact.
 *          3. A no-op OK writes NOTHING (churn discipline).
 *          4. A row deleted in the table is removed from the engine rather
 *             than surviving as a stale key.
 *          5. Reserved species are gated: with WATER_AGE off the combo
 *             omits the age entry; on, it offers it (and heat likewise).
 */

#include "ui/dialogs/initialqualitydialog.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_initial_quality.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_pollutants.h>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QObject>
#include <QPushButton>
#include <QTableWidget>
#include <QTest>

using OpenSWMMVis::InitialQualityDialog;

namespace {

/*! A BUILDING-state engine with nodes, a link, and one pollutant. */
SWMM_Engine makeEngine(bool ageOn = false, bool heatOn = false)
{
    SWMM_Engine e = swmm_engine_new();
    Q_ASSERT(e != nullptr);
    swmm_node_add(e, "J0", 0 /*JUNCTION*/);
    swmm_node_add(e, "J1", 0 /*JUNCTION*/);
    swmm_link_add(e, "C1", 0 /*CONDUIT*/);
    swmm_pollutant_add(e, "TSS", 0 /*MG/L*/);
    if (ageOn)  swmm_options_set(e, "WATER_AGE", "YES");
    if (heatOn) swmm_options_set(e, "HEAT_TRANSPORT", "YES");
    return e;
}

QTableWidget *table(InitialQualityDialog &dlg)
{
    return dlg.findChild<QTableWidget *>(QStringLiteral("iq_table"));
}

void clickOk(InitialQualityDialog &dlg)
{
    auto *bb = dlg.findChild<QDialogButtonBox *>();
    QVERIFY(bb);
    bb->button(QDialogButtonBox::Ok)->click();
}

} // namespace

class TestInitialQualityDialog : public QObject
{
    Q_OBJECT
private slots:
    void constructsWithNullEngine();
    void hydratesEngineRows();
    void addedRowReachesEngineOnOk();
    void noOpOkWritesNothing();
    void deletedRowIsRemovedFromEngine();
    void reservedSpeciesAreOptionGated();
};

void TestInitialQualityDialog::constructsWithNullEngine()
{
    InitialQualityDialog dlg(nullptr, nullptr);
    QCOMPARE(dlg.wroteAnyChanges(), false);
}

void TestInitialQualityDialog::hydratesEngineRows()
{
    SWMM_Engine e = makeEngine();
    QCOMPARE(swmm_init_quality_set(e, 0, 1, "TSS", 12.5), SWMM_OK);
    QCOMPARE(swmm_init_quality_set(e, 1, 0, "TSS", 9.0), SWMM_OK);

    InitialQualityDialog dlg(e, nullptr);
    auto *tbl = table(dlg);
    QVERIFY(tbl);
    QCOMPARE(tbl->rowCount(), 2);

    auto *scope = qobject_cast<QComboBox *>(tbl->cellWidget(0, 0));
    auto *elem  = qobject_cast<QComboBox *>(tbl->cellWidget(0, 1));
    auto *cons  = qobject_cast<QComboBox *>(tbl->cellWidget(0, 2));
    auto *val   = qobject_cast<QDoubleSpinBox *>(tbl->cellWidget(0, 3));
    QVERIFY(scope && elem && cons && val);
    QCOMPARE(scope->currentData().toInt(), 0);
    QCOMPARE(elem->currentText(), QStringLiteral("J1"));
    QCOMPARE(cons->currentData().toString(), QStringLiteral("TSS"));
    QCOMPARE(val->value(), 12.5);

    auto *scope1 = qobject_cast<QComboBox *>(tbl->cellWidget(1, 0));
    auto *elem1  = qobject_cast<QComboBox *>(tbl->cellWidget(1, 1));
    QVERIFY(scope1 && elem1);
    QCOMPARE(scope1->currentData().toInt(), 1);
    QCOMPARE(elem1->currentText(), QStringLiteral("C1"));

    QCOMPARE(dlg.wroteAnyChanges(), false);
    swmm_engine_destroy(e);
}

void TestInitialQualityDialog::addedRowReachesEngineOnOk()
{
    SWMM_Engine e = makeEngine();
    QCOMPARE(swmm_init_quality_set(e, 0, 0, "TSS", 5.0), SWMM_OK);

    InitialQualityDialog dlg(e, nullptr);
    auto *add = dlg.findChild<QPushButton *>(QStringLiteral("iq_addBtn"));
    QVERIFY(add);
    add->click();

    auto *tbl = table(dlg);
    QVERIFY(tbl);
    QCOMPARE(tbl->rowCount(), 2);
    const int r = 1;
    auto *elem = qobject_cast<QComboBox *>(tbl->cellWidget(r, 1));
    auto *val  = qobject_cast<QDoubleSpinBox *>(tbl->cellWidget(r, 3));
    QVERIFY(elem && val);
    elem->setCurrentIndex(elem->findText(QStringLiteral("J1")));
    val->setValue(12.5);
    clickOk(dlg);

    QVERIFY(dlg.wroteAnyChanges());
    QCOMPARE(dlg.lastWriteCount(), 1);          // exactly the new row
    QCOMPARE(swmm_init_quality_count(e), 2);
    swmm_engine_destroy(e);
}

void TestInitialQualityDialog::noOpOkWritesNothing()
{
    SWMM_Engine e = makeEngine();
    QCOMPARE(swmm_init_quality_set(e, 0, 0, "TSS", 5.0), SWMM_OK);

    InitialQualityDialog dlg(e, nullptr);
    clickOk(dlg);

    QCOMPARE(dlg.lastWriteCount(), 0);
    QCOMPARE(dlg.wroteAnyChanges(), false);
    QCOMPARE(swmm_init_quality_count(e), 1);
    double v = 0.0;
    int is_link = 0, elem = -1;
    char buf[64];
    QCOMPARE(swmm_init_quality_get(e, 0, &is_link, &elem, buf, 64, &v),
             SWMM_OK);
    QCOMPARE(v, 5.0);
    swmm_engine_destroy(e);
}

void TestInitialQualityDialog::deletedRowIsRemovedFromEngine()
{
    SWMM_Engine e = makeEngine();
    QCOMPARE(swmm_init_quality_set(e, 0, 0, "TSS", 5.0), SWMM_OK);
    QCOMPARE(swmm_init_quality_set(e, 1, 0, "TSS", 9.0), SWMM_OK);

    InitialQualityDialog dlg(e, nullptr);
    auto *tbl = table(dlg);
    QVERIFY(tbl);
    QCOMPARE(tbl->rowCount(), 2);
    tbl->setCurrentCell(0, 0);
    auto *rem = dlg.findChild<QPushButton *>(QStringLiteral("iq_removeBtn"));
    QVERIFY(rem);
    rem->click();
    QCOMPARE(tbl->rowCount(), 1);
    clickOk(dlg);

    QVERIFY(dlg.wroteAnyChanges());
    QCOMPARE(swmm_init_quality_count(e), 1);    // stale key would read 2
    int is_link = 0, elem = -1;
    char buf[64];
    double v = 0.0;
    QCOMPARE(swmm_init_quality_get(e, 0, &is_link, &elem, buf, 64, &v),
             SWMM_OK);
    QCOMPARE(is_link, 1);                       // the LINK row survived
    swmm_engine_destroy(e);
}

void TestInitialQualityDialog::reservedSpeciesAreOptionGated()
{
    // Age and heat OFF: only the pollutant is offered.
    {
        SWMM_Engine e = makeEngine(false, false);
        InitialQualityDialog dlg(e, nullptr);
        auto *add = dlg.findChild<QPushButton *>(
            QStringLiteral("iq_addBtn"));
        QVERIFY(add);
        add->click();
        auto *cons = qobject_cast<QComboBox *>(
            table(dlg)->cellWidget(0, 2));
        QVERIFY(cons);
        QCOMPARE(cons->count(), 1);
        QCOMPARE(cons->itemData(0).toString(), QStringLiteral("TSS"));
        swmm_engine_destroy(e);
    }
    // Both ON: pollutant + age + temperature.
    {
        SWMM_Engine e = makeEngine(true, true);
        InitialQualityDialog dlg(e, nullptr);
        auto *add = dlg.findChild<QPushButton *>(
            QStringLiteral("iq_addBtn"));
        QVERIFY(add);
        add->click();
        auto *cons = qobject_cast<QComboBox *>(
            table(dlg)->cellWidget(0, 2));
        QVERIFY(cons);
        QCOMPARE(cons->count(), 3);
        QVector<QString> offered;
        for (int i = 0; i < cons->count(); ++i)
            offered.append(cons->itemData(i).toString());
        QVERIFY(offered.contains(QStringLiteral("__WATER_AGE__")));
        QVERIFY(offered.contains(QStringLiteral("__TEMPERATURE__")));

        // Reserved species accept negatives (signed age, D-NS1); pollutant
        // selection floors the spin at zero.
        auto *val = qobject_cast<QDoubleSpinBox *>(
            table(dlg)->cellWidget(0, 3));
        QVERIFY(val);
        QCOMPARE(val->minimum(), 0.0);          // pollutant selected first
        cons->setCurrentIndex(
            cons->findData(QStringLiteral("__WATER_AGE__")));
        QVERIFY(val->minimum() < 0.0);
        swmm_engine_destroy(e);
    }
}

QTEST_MAIN(TestInitialQualityDialog)
#include "test_initialqualitydialog.moc"
