/**
 * @file      test_pollutantregistry_rename.cpp
 * @brief     Renaming a pollutant keeps the co-pollutant links that name it
 *            (workplans/GUI_COMPREHENSIVE_REVIEW_2026-11-12.md, E-05).
 *
 * PollutantRegistry::rename() renamed only the renamed provider. Every other
 * provider kept the old name in coPollutant(), so the next saveToEngine()
 * looked that name up, found nothing and wrote co-pollutant -1 — the link was
 * silently dropped from the saved model.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "pollutant/pollutantprovider.h"
#include "pollutant/pollutantregistry.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_pollutants.h>

#include <QDir>
#include <QTest>

using openswmmvis::pollutant::PollutantProvider;
using openswmmvis::pollutant::PollutantRegistry;

namespace
{
/** @brief Reviewable output folder for the report file the engine writes. */
QString outDir()
{
    QDir d(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."))); // tests/gui/data
    d.cdUp();
    d.cdUp();
    const QString out = d.filePath(QStringLiteral("output/gui_review_2026-11-12/pollutant_rename"));
    QDir().mkpath(out);
    return out;
}

/** @brief Engine-side co-pollutant index of @p name, or -2 when the pollutant is missing. */
int engineCoIndex(SWMM_Engine e, const char *name)
{
    const int idx = swmm_pollutant_index(e, name);
    if (idx < 0)
        return -2;
    int co = -2;
    double frac = 0.0;
    if (swmm_pollutant_get_co_pollutant(e, idx, &co, &frac) != SWMM_OK)
        return -2;
    return co;
}
} // namespace

class TestPollutantRegistryRename : public QObject
{
    Q_OBJECT

private slots:
    /** @brief Rename the co-pollutant, save, and check the referencing pollutant still points at it. */
    void renameKeepsCoPollutantLinks()
    {
        const QString inp = QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral(".")))
                                .filePath(QStringLiteral("data_editor_routing.inp"));
        SWMM_Engine e = swmm_engine_create();
        QVERIFY(e);
        const QByteArray rpt = QDir(outDir()).filePath(QStringLiteral("model.rpt")).toUtf8();
        QCOMPARE(swmm_engine_open(e, inp.toUtf8().constData(), rpt.constData(), nullptr, nullptr),
                 int(SWMM_OK));

        {
            PollutantRegistry registry;
            QVERIFY(registry.loadFromEngine(e) >= 1);
            PollutantProvider *tss = registry.findByName(QStringLiteral("TSS"));
            QVERIFY(tss);
            PollutantProvider *lead = registry.create(QStringLiteral("Lead"));
            QVERIFY(lead);
            lead->setCoPollutant(QStringLiteral("TSS"));
            lead->setCoFraction(0.2);
            registry.saveToEngine();
            QCOMPARE(engineCoIndex(e, "Lead"), swmm_pollutant_index(e, "TSS"));

            QVERIFY(registry.rename(tss, QStringLiteral("Sediment")));
            QCOMPARE(lead->coPollutant(), QStringLiteral("Sediment"));

            registry.saveToEngine();
            const int sediment = swmm_pollutant_index(e, "Sediment");
            QVERIFY(sediment >= 0);
            QCOMPARE(engineCoIndex(e, "Lead"), sediment);

            // Deleting the co-pollutant: the engine sets the reference to
            // none, and the cache must agree (it kept the dead name).
            registry.remove(tss);
            QCOMPARE(engineCoIndex(e, "Lead"), -1);
            QVERIFY(lead->coPollutant().isEmpty());
        }
        swmm_engine_destroy(e);
    }
};

QTEST_MAIN(TestPollutantRegistryRename)
#include "test_pollutantregistry_rename.moc"
