/*!
 * \file   test_registry_rename.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Renaming a data object must not duplicate it in the engine, and
 *         snow-pack parameters must survive a save/reload round trip.
 *
 *         Registry rename() used to update only the provider. The next
 *         saveToEngine() then looked up the NEW name, did not find it, and
 *         called the *_add API — leaving TWO objects in the engine, the
 *         original orphaned along with everything referencing it. The five
 *         registries covered here now call the engine's rename API first.
 *
 *         The snow-pack slots additionally pin the parameter round trip:
 *         SnowpackProvider carried only a name and saveToEngine skipped any
 *         snow pack that already existed, so melt coefficients had nowhere to
 *         go.
 *
 *         Uses QTEST_APPLESS_MAIN — no widgets, just registries over a bare
 *         engine handle.
 */
#include "aquifer/aquiferprovider.h"
#include "aquifer/aquiferregistry.h"
#include "inlet/inletprovider.h"
#include "inlet/inletregistry.h"
#include "lid/lidcontrolprovider.h"
#include "lid/lidcontrolregistry.h"
#include "snowpack/snowpackprovider.h"
#include "snowpack/snowpackregistry.h"
#include "street/streetprovider.h"
#include "street/streetregistry.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_infrastructure.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_subcatchments.h>

#include <QObject>
#include <QTest>

using openswmmvis::aquifer::AquiferRegistry;
using openswmmvis::inlet::InletRegistry;
using openswmmvis::lid::LidControlRegistry;
using openswmmvis::snowpack::SnowpackProvider;
using openswmmvis::snowpack::SnowpackRegistry;
using openswmmvis::street::StreetRegistry;

class TestRegistryRename : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void snowpackRenameDoesNotDuplicate();
    void aquiferRenameDoesNotDuplicate();
    void lidRenameDoesNotDuplicate();
    void streetRenameDoesNotDuplicate();
    void inletRenameDoesNotDuplicate();

    void snowpackRenameRejectsDuplicateName();
    void snowpackParametersReachTheEngine();
    void snowpackParametersRoundTrip();

private:
    SWMM_Engine m_engine = nullptr;
};

void TestRegistryRename::init()
{
    // swmm_engine_new (not swmm_engine_create) starts the context in BUILDING,
    // which the engine's *_add and *_rename lifecycle guards require.
    m_engine = swmm_engine_new();
    QVERIFY(m_engine != nullptr);
}

void TestRegistryRename::cleanup()
{
    if (m_engine) {
        swmm_engine_destroy(m_engine);
        m_engine = nullptr;
    }
}

// ---------------------------------------------------------------------------
// The duplicate-on-rename regression, once per affected registry
// ---------------------------------------------------------------------------

void TestRegistryRename::snowpackRenameDoesNotDuplicate()
{
    SnowpackRegistry reg;
    auto *p = reg.create(QStringLiteral("SP1"));
    QVERIFY(p);
    QCOMPARE(reg.saveToEngine(m_engine), 1);
    QCOMPARE(swmm_snowpack_count(m_engine), 1);

    QVERIFY(reg.rename(p, QStringLiteral("Residential")));
    reg.saveToEngine(m_engine);

    QCOMPARE(swmm_snowpack_count(m_engine), 1);
    QCOMPARE(swmm_snowpack_index(m_engine, "Residential"), 0);
    QVERIFY(swmm_snowpack_index(m_engine, "SP1") < 0);
}

void TestRegistryRename::aquiferRenameDoesNotDuplicate()
{
    AquiferRegistry reg;
    auto *p = reg.create(QStringLiteral("AQ1"));
    QVERIFY(p);
    reg.saveToEngine(m_engine);
    QCOMPARE(swmm_aquifer_count(m_engine), 1);

    QVERIFY(reg.rename(p, QStringLiteral("Upper")));
    reg.saveToEngine(m_engine);

    QCOMPARE(swmm_aquifer_count(m_engine), 1);
    QCOMPARE(swmm_aquifer_index(m_engine, "Upper"), 0);
    QVERIFY(swmm_aquifer_index(m_engine, "AQ1") < 0);
}

void TestRegistryRename::lidRenameDoesNotDuplicate()
{
    LidControlRegistry reg;
    auto *p = reg.create(QStringLiteral("LID1"));
    QVERIFY(p);
    reg.saveToEngine(m_engine);
    QCOMPARE(swmm_lid_count(m_engine), 1);

    QVERIFY(reg.rename(p, QStringLiteral("BioRetention")));
    reg.saveToEngine(m_engine);

    QCOMPARE(swmm_lid_count(m_engine), 1);
    QCOMPARE(swmm_lid_index(m_engine, "BioRetention"), 0);
    QVERIFY(swmm_lid_index(m_engine, "LID1") < 0);
}

void TestRegistryRename::streetRenameDoesNotDuplicate()
{
    StreetRegistry reg;
    auto *p = reg.create(QStringLiteral("ST1"));
    QVERIFY(p);
    reg.saveToEngine(m_engine);
    QCOMPARE(swmm_street_count(m_engine), 1);

    QVERIFY(reg.rename(p, QStringLiteral("Main")));
    reg.saveToEngine(m_engine);

    QCOMPARE(swmm_street_count(m_engine), 1);
    QCOMPARE(swmm_street_index(m_engine, "Main"), 0);
    QVERIFY(swmm_street_index(m_engine, "ST1") < 0);
}

void TestRegistryRename::inletRenameDoesNotDuplicate()
{
    InletRegistry reg;
    auto *p = reg.create(QStringLiteral("IN1"));
    QVERIFY(p);
    reg.saveToEngine(m_engine);
    QCOMPARE(swmm_inlet_count(m_engine), 1);

    QVERIFY(reg.rename(p, QStringLiteral("Grate_A")));
    reg.saveToEngine(m_engine);

    QCOMPARE(swmm_inlet_count(m_engine), 1);
    QCOMPARE(swmm_inlet_index(m_engine, "Grate_A"), 0);
    QVERIFY(swmm_inlet_index(m_engine, "IN1") < 0);
}

// ---------------------------------------------------------------------------
// Rename still refuses a collision, and refusing leaves nothing half-applied
// ---------------------------------------------------------------------------

void TestRegistryRename::snowpackRenameRejectsDuplicateName()
{
    SnowpackRegistry reg;
    auto *a = reg.create(QStringLiteral("SP1"));
    auto *b = reg.create(QStringLiteral("SP2"));
    QVERIFY(a && b);
    reg.saveToEngine(m_engine);

    QVERIFY(!reg.rename(a, QStringLiteral("SP2")));
    QCOMPARE(a->name(), QStringLiteral("SP1"));
    QCOMPARE(swmm_snowpack_count(m_engine), 2);
    QCOMPARE(swmm_snowpack_index(m_engine, "SP1"), 0);
}

// ---------------------------------------------------------------------------
// Snow-pack parameters
// ---------------------------------------------------------------------------

void TestRegistryRename::snowpackParametersReachTheEngine()
{
    SnowpackRegistry reg;
    auto *p = reg.create(QStringLiteral("SP1"));
    QVERIFY(p);
    // Flush once so the snow pack exists in the engine. The old saveToEngine
    // did `continue` from here on, so every later edit was dropped.
    reg.saveToEngine(m_engine);

    p->setParam(SnowpackProvider::PlowableCmin, 0.05);
    p->setParam(SnowpackProvider::PlowableCmax, 0.12);
    p->setParam(SnowpackProvider::ImperviousTbase, 32.0);
    p->setParam(SnowpackProvider::PerviousFwFrac, 0.10);
    p->setParam(SnowpackProvider::RemovalDsnow, 2.5);
    p->setRemovalSubcatch(QStringLiteral("S1"));

    reg.saveToEngine(m_engine);

    double cmin = 0, cmax = 0, tbase = 0, fwfrac = 0, sd0 = 0, fw0 = 0, last = 0;
    QCOMPARE(swmm_snowpack_get_plowable(m_engine, 0, &cmin, &cmax, &tbase,
                                        &fwfrac, &sd0, &fw0, &last), SWMM_OK);
    QCOMPARE(cmin, 0.05);
    QCOMPARE(cmax, 0.12);

    QCOMPARE(swmm_snowpack_get_impervious(m_engine, 0, &cmin, &cmax, &tbase,
                                          &fwfrac, &sd0, &fw0, &last), SWMM_OK);
    QCOMPARE(tbase, 32.0);

    QCOMPARE(swmm_snowpack_get_pervious(m_engine, 0, &cmin, &cmax, &tbase,
                                        &fwfrac, &sd0, &fw0, &last), SWMM_OK);
    QCOMPARE(fwfrac, 0.10);

    double dsnow = 0, fout = 0, fimp = 0, fperv = 0, fimelt = 0, fsub = 0;
    QCOMPARE(swmm_snowpack_get_removal(m_engine, 0, &dsnow, &fout, &fimp,
                                       &fperv, &fimelt, &fsub), SWMM_OK);
    QCOMPARE(dsnow, 2.5);

    char buf[64] = {};
    QCOMPARE(swmm_snowpack_get_removal_subcatch(m_engine, 0, buf, sizeof(buf)),
             SWMM_OK);
    QCOMPARE(QString::fromUtf8(buf), QStringLiteral("S1"));
}

void TestRegistryRename::snowpackParametersRoundTrip()
{
    {
        SnowpackRegistry writer;
        auto *p = writer.create(QStringLiteral("SP1"));
        QVERIFY(p);
        p->setParam(SnowpackProvider::PlowableCmin, 0.01);
        p->setParam(SnowpackProvider::PerviousLast, 4.0);
        p->setParam(SnowpackProvider::RemovalFOut, 0.25);
        writer.saveToEngine(m_engine);
    }

    // A second registry over the same engine must see the values — this is the
    // path the editor takes when it is reopened on an existing model.
    SnowpackRegistry reader;
    QCOMPARE(reader.loadFromEngine(m_engine), 1);
    auto *q = reader.findByName(QStringLiteral("SP1"));
    QVERIFY(q);
    QCOMPARE(q->param(SnowpackProvider::PlowableCmin), 0.01);
    QCOMPARE(q->param(SnowpackProvider::PerviousLast), 4.0);
    QCOMPARE(q->param(SnowpackProvider::RemovalFOut), 0.25);
}

QTEST_APPLESS_MAIN(TestRegistryRename)
#include "test_registry_rename.moc"
