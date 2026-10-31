/**
 * @file      test_runspeciescatalog.cpp
 * @brief     V0 gate — RunSpeciesCatalog lists every species of a run once,
 *            with kind, units, `.out` column and 2D slices, keyed by name.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * workplans/GW2D_AND_PIPE_REACTION_RESULTS_VISUALIZATION_PLAN_2026-10-14.md
 * §4.2 V0: "a deck with 2 pollutants + age + temperature + 2 MSX species
 * (2D) lists 6 entries, with the MSX pair marked 1D-unavailable until E1;
 * reordering [POLLUTANTS] leaves a saved style on the same species".
 * The first two cases build the catalog from plain inputs (the `.out`
 * species list and a 2D catalog); the last runs the engine on two decks
 * that differ only in `[POLLUTANTS]` order and reads the real `.out`s.
 * Run files are written under PHASE4B_OUTPUT.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "layers/speciesattributes.h"
#include "results/runspeciescatalog.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_output.h>

#include <QDir>
#include <QFile>
#include <QtTest>

using openswmmvis::io::Mesh2DResultVariable;
using openswmmvis::results::RunSpecies;
using openswmmvis::results::RunSpeciesCatalog;

namespace
{

Mesh2DResultVariable slice(const QString &dataset, const QString &species, const QString &units)
{
    Mesh2DResultVariable v;
    v.dataset = dataset;
    v.species = species;
    v.units = units;
    v.unitsKnown = !units.isEmpty();
    v.domain = dataset.startsWith(QStringLiteral("Mesh2_face_gw_"))
                   ? Mesh2DResultVariable::Domain::Groundwater
                   : Mesh2DResultVariable::Domain::Surface;
    v.frameCount = 3;
    return v;
}

/** The `.out` columns of the gate deck: 2 pollutants + age + temperature. */
QVector<RunSpeciesCatalog::OutputSpecies> gateColumns()
{
    return {{QStringLiteral("TSS"), 0},
            {QStringLiteral("Lead"), 1},
            {QStringLiteral("__WATER_AGE__"), 0},
            {QStringLiteral("__TEMPERATURE__"), 0}};
}

/** The 2D catalog: both pollutants and two MSX species (saved as MG/L). */
QVector<Mesh2DResultVariable> gateMesh()
{
    const QString conc = QStringLiteral("Mesh2_face_species_conc");
    return {slice(conc, QStringLiteral("TSS"), QStringLiteral("MG/L")),
            slice(conc, QStringLiteral("Lead"), QStringLiteral("UG/L")),
            slice(conc, QStringLiteral("NH4"), QStringLiteral("MG/L")),
            slice(conc, QStringLiteral("Biofilm"), QStringLiteral("MG/L")),
            slice(QStringLiteral("Mesh2_face_gw_sat_conc"), QStringLiteral("TSS"),
                  QStringLiteral("MG/L"))};
}

QVector<RunSpeciesCatalog::DeclaredSpecies> gateDeclared()
{
    return {{QStringLiteral("NH4"), RunSpecies::Kind::ReactionBulk, QStringLiteral("MG/L")},
            {QStringLiteral("Biofilm"), RunSpecies::Kind::ReactionWall, QStringLiteral("MG/L")}};
}

} // namespace

class TestRunSpeciesCatalog : public QObject
{
    Q_OBJECT

private slots:
    /*! The gate deck lists 6 entries once each; the MSX pair is 1D-unavailable. */
    void listsEverySpeciesOnceWithItsSources()
    {
        const auto catalog =
            RunSpeciesCatalog::build(gateColumns(), gateMesh(), gateDeclared(), true);
        QCOMPARE(catalog.size(), 6);
        QStringList names;
        for (const RunSpecies &s : catalog.entries())
            names << s.name;
        QCOMPARE(names, (QStringList{"TSS", "Lead", "__WATER_AGE__", "__TEMPERATURE__", "NH4",
                                     "Biofilm"}));

        const RunSpecies *tss = catalog.find(QStringLiteral("TSS"));
        QVERIFY(tss);
        QCOMPARE(tss->kind, RunSpecies::Kind::Pollutant);
        QCOMPARE(tss->reportedIndex, 0);
        QCOMPARE(tss->units, QStringLiteral("mg/L"));
        QVERIFY(tss->in1D() && tss->in2D());
        QCOMPARE(tss->meshKeys.size(), 2); // surface + aquifer slices, one entry
        QCOMPARE(tss->meshKey, slice("Mesh2_face_species_conc", "TSS", "MG/L").key());
        QVERIFY(tss->carriesLoad());

        const RunSpecies *lead = catalog.find(QStringLiteral("Lead"));
        QCOMPARE(lead->units, QStringLiteral("ug/L")); // the .out code wins
        QCOMPARE(lead->reportedIndex, 1);

        const RunSpecies *age = catalog.find(QStringLiteral("__WATER_AGE__"));
        QCOMPARE(age->kind, RunSpecies::Kind::WaterAge);
        QCOMPARE(age->units, QStringLiteral("h"));
        QVERIFY(!age->carriesLoad());
        QCOMPARE(catalog.find(QStringLiteral("__TEMPERATURE__"))->kind,
                 RunSpecies::Kind::Temperature);
        // In 1D only (no 2D slice) -> badged as such.
        QCOMPARE(catalog.availabilityBadge(*age), QStringLiteral("1D only"));

        for (const char *msx : {"NH4", "Biofilm"}) {
            const RunSpecies *s = catalog.find(QString::fromLatin1(msx));
            QVERIFY(s);
            QVERIFY2(!s->in1D(), msx); // not in the .out until engine E1
            QCOMPARE(s->reportedIndex, -1);
            QVERIFY(s->in2D());
            QCOMPARE(s->units, QStringLiteral("mg/L"));
            QCOMPARE(catalog.availabilityBadge(*s), QStringLiteral("2D only"));
            QVERIFY(!s->carriesLoad());
        }
        QCOMPARE(catalog.find(QStringLiteral("NH4"))->kind, RunSpecies::Kind::ReactionBulk);
        QCOMPARE(catalog.find(QStringLiteral("Biofilm"))->kind, RunSpecies::Kind::ReactionWall);

        // Kind badge and unit in every picker label.
        QCOMPARE(catalog.pickerLabel(*tss), QStringLiteral("TSS [pollutant] (mg/L)"));
        QCOMPARE(catalog.pickerLabel(*catalog.find(QStringLiteral("NH4"))),
                 QStringLiteral("NH4 [MSX bulk · 2D only] (mg/L)"));
        QVERIFY(catalog.pickerLabel(*age).contains(QStringLiteral("[water age · 1D only] (h)")));

        // Only the .out species are reported names, in column order.
        QCOMPARE(catalog.reportedNames(),
                 (QStringList{"TSS", "Lead", "__WATER_AGE__", "__TEMPERATURE__"}));

        // Without declarations a 2D-only name is unclassified, not a pollutant.
        const auto bare = RunSpeciesCatalog::build(gateColumns(), gateMesh());
        QCOMPARE(bare.find(QStringLiteral("NH4"))->kind, RunSpecies::Kind::Unclassified);
        QCOMPARE(bare.size(), 6);
    }

    /*! Reordering [POLLUTANTS] moves the column, not the identity: a saved
     *  "qual:TSS" style resolves to the TSS column in both runs. */
    void reorderKeepsTheNameIdentity()
    {
        const auto first = RunSpeciesCatalog::build(gateColumns(), gateMesh(), gateDeclared());
        auto reordered = gateColumns();
        std::swap(reordered[0], reordered[1]); // Lead, TSS, age, temperature
        const auto second = RunSpeciesCatalog::build(reordered, gateMesh(), gateDeclared());

        QCOMPARE(first.find(QStringLiteral("TSS"))->reportedIndex, 0);
        QCOMPARE(second.find(QStringLiteral("TSS"))->reportedIndex, 1);
        QCOMPARE(second.find(QStringLiteral("TSS"))->units, QStringLiteral("mg/L"));
        QCOMPARE(second.find(QStringLiteral("Lead"))->units, QStringLiteral("ug/L"));

        const QString saved = OpenSWMMVis::Species::speciesAttributeName(QStringLiteral("TSS"));
        for (const RunSpeciesCatalog *c : {&first, &second}) {
            const int code = OpenSWMMVis::Species::speciesOutCode(saved, c->reportedNames(),
                                                                  SWMM_OUT_NODE_POLLUT_BASE);
            QVERIFY(code >= SWMM_OUT_NODE_POLLUT_BASE);
            QCOMPARE(c->reportedNames().at(code - SWMM_OUT_NODE_POLLUT_BASE),
                     QStringLiteral("TSS"));
            QCOMPARE(code - SWMM_OUT_NODE_POLLUT_BASE, c->find(QStringLiteral("TSS"))->reportedIndex);
        }
    }

    /*! Real `.out`s from two decks whose [POLLUTANTS] differ only in order:
     *  names, unit codes and columns come from the file. */
    void readsARealOutputAndFollowsTheReorder()
    {
        const QString folder = QStringLiteral(PHASE4B_OUTPUT);
        QVERIFY(QDir().mkpath(folder));
        const QByteArray head =
            "[OPTIONS]\nFLOW_UNITS CMS\nFLOW_ROUTING KINWAVE\nSTART_DATE 01/01/2026\n"
            "END_DATE 01/01/2026\nEND_TIME 00:10:00\nREPORT_STEP 00:01:00\nROUTING_STEP 60\n"
            "[JUNCTIONS]\nJ1 10 2 0 0 0\n[OUTFALLS]\nO1 8 FREE NO\n"
            "[CONDUITS]\nC1 J1 O1 100 0.013 0 0 0\n[XSECTIONS]\nC1 CIRCULAR 1 0 0 0 1\n"
            "[INFLOWS]\nJ1 FLOW \"\" FLOW 1.0 1.0 0.05\n"
            "[COORDINATES]\nJ1 0 0\nO1 100 0\n";
        const QByteArray a = "[POLLUTANTS]\nTSS MG/L 0 0 0 0 0\nLead UG/L 0 0 0 0 0\n"
                             "Ecoli #/L 0 0 0 0 0\n";
        const QByteArray b = "[POLLUTANTS]\nEcoli #/L 0 0 0 0 0\nLead UG/L 0 0 0 0 0\n"
                             "TSS MG/L 0 0 0 0 0\n";
        QStringList outs;
        for (const auto &[name, pollutants] :
             {std::pair{QStringLiteral("catalog_order_a"), a},
              std::pair{QStringLiteral("catalog_order_b"), b}}) {
            const QString inp = QDir(folder).filePath(name + ".inp");
            QFile f(inp);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(head + pollutants);
            f.close();
            const QString out = QDir(folder).filePath(name + ".out");
            QCOMPARE(swmm_engine_run(inp.toUtf8().constData(),
                                     QDir(folder).filePath(name + ".rpt").toUtf8().constData(),
                                     out.toUtf8().constData(), nullptr),
                     0);
            outs << out;
        }
        SWMM_Output ha = swmm_output_open(outs[0].toUtf8().constData());
        SWMM_Output hb = swmm_output_open(outs[1].toUtf8().constData());
        QVERIFY(ha && hb);
        const auto ca = RunSpeciesCatalog::fromOutput(ha);
        const auto cb = RunSpeciesCatalog::fromOutput(hb);
        QCOMPARE(ca.reportedNames(), (QStringList{"TSS", "Lead", "Ecoli"}));
        QCOMPARE(cb.reportedNames(), (QStringList{"Ecoli", "Lead", "TSS"}));
        for (const RunSpeciesCatalog *c : {&ca, &cb}) {
            QCOMPARE(c->find(QStringLiteral("TSS"))->units, QStringLiteral("mg/L"));
            QCOMPARE(c->find(QStringLiteral("Lead"))->units, QStringLiteral("ug/L"));
            QCOMPARE(c->find(QStringLiteral("Ecoli"))->units, QStringLiteral("count/L"));
            QCOMPARE(c->find(QStringLiteral("Ecoli"))->concentrationCode, 2);
            QVERIFY(c->find(QStringLiteral("Ecoli"))->carriesLoad());
            QVERIFY(!c->has2D());
        }
        QCOMPARE(ca.find(QStringLiteral("TSS"))->reportedIndex, 0);
        QCOMPARE(cb.find(QStringLiteral("TSS"))->reportedIndex, 2);
        swmm_output_close(ha);
        swmm_output_close(hb);
    }
};

QTEST_MAIN(TestRunSpeciesCatalog)
#include "test_runspeciescatalog.moc"
