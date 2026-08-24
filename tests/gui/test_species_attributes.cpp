/*!
 * \file test_species_attributes.cpp
 * \brief Y2a — the dynamic species attribute helpers (GUI plan D-G1).
 *
 * \details These pin the logic that decides, for every themeable species
 *          column, (a) what token is persisted in `.oswp`, (b) what the
 *          picker shows, (c) what unit is claimed, and (d) which engine
 *          output variable is read. Each has a defect it exists to catch:
 *
 *          - persisted token: an INDEX instead of a name would silently
 *            repoint a saved theme when species are reordered — the exact
 *            failure D-G1 rejected the enum-block design over;
 *          - unit: the `.out` unit enum has no HOURS/DEGC slot (engine
 *            A2b keyed those on the species NAME), so a picker that trusts
 *            the enum labels water age "mg/L";
 *          - output code: resolving against the WRONG run's species list,
 *            or accepting a name the run does not carry, reads a different
 *            column and renders a plausible-looking wrong map.
 *
 *          The helpers live in their own TU precisely so this test can
 *          link them — `SWMMResultsLayer` cannot be linked into a test
 *          (the closure problem `tests/gui/CMakeLists.txt:1996` records
 *          for the options dialog), so the logic that can carry a defect
 *          was put where a test can reach it.
 */

#include "layers/speciesattributes.h"

// The engine's output ABI supplies the per-kind pollutant bases.
#include <openswmm/engine/openswmm_output.h>

#include <QObject>
#include <QStringList>
#include <QTest>

using namespace OpenSWMMVis::Species;

class TestSpeciesAttributes : public QObject
{
    Q_OBJECT

private slots:
    void attributeTokenRoundTrips();
    void reservedSpeciesGetFriendlyLabelsAndUnits();
    void outCodeResolvesAgainstTheRunsSpeciesList();
    void outCodeRejectsUnknownAndMalformed();
};

void TestSpeciesAttributes::attributeTokenRoundTrips()
{
    // The token is what lands in .oswp — it must carry the NAME.
    QCOMPARE(speciesAttributeName(QStringLiteral("TSS")),
             QStringLiteral("qual:TSS"));
    QVERIFY(isSpeciesAttribute(QStringLiteral("qual:TSS")));
    QCOMPARE(speciesFromAttribute(QStringLiteral("qual:TSS")),
             QStringLiteral("TSS"));

    // Round-trip for a reserved species, whose name carries underscores.
    const QString age = QString::fromLatin1(kWaterAgeName);
    QCOMPARE(speciesFromAttribute(speciesAttributeName(age)), age);

    // Hydraulic attributes are NOT species attributes — the mappers must
    // keep routing them to their fixed codes.
    QVERIFY(!isSpeciesAttribute(QStringLiteral("depth")));
    QVERIFY(!isSpeciesAttribute(QStringLiteral("LinkFlow")));
    QVERIFY(speciesFromAttribute(QStringLiteral("depth")).isEmpty());

    // An empty species name must not produce a usable token, or "qual:"
    // would resolve to species index 0.
    QVERIFY(speciesAttributeName(QString()).isEmpty());
    QVERIFY(speciesFromAttribute(QStringLiteral("qual:")).isEmpty());
}

void TestSpeciesAttributes::reservedSpeciesGetFriendlyLabelsAndUnits()
{
    const QString age  = QString::fromLatin1(kWaterAgeName);
    const QString temp = QString::fromLatin1(kTemperatureName);

    QVERIFY(isReservedSpecies(age));
    QVERIFY(isReservedSpecies(temp));
    QVERIFY(!isReservedSpecies(QStringLiteral("TSS")));

    // Raw reserved names must never reach a picker.
    QVERIFY(!speciesDisplayLabel(age).contains(QStringLiteral("__")));
    QVERIFY(!speciesDisplayLabel(temp).contains(QStringLiteral("__")));
    // An ordinary pollutant shows its own name.
    QCOMPARE(speciesDisplayLabel(QStringLiteral("TSS")),
             QStringLiteral("TSS"));

    // Units: the reserved pair override whatever concentration unit the
    // run reports — this is the consumer side of engine A2b's name-keyed
    // unit decision.
    QCOMPARE(speciesUnitLabel(age,  QStringLiteral("mg/L")),
             QStringLiteral("h"));
    QCOMPARE(speciesUnitLabel(temp, QStringLiteral("mg/L")),
             QStringLiteral("°C"));
    QCOMPARE(speciesUnitLabel(QStringLiteral("TSS"), QStringLiteral("mg/L")),
             QStringLiteral("mg/L"));
    QCOMPARE(speciesUnitLabel(QStringLiteral("Lead"), QStringLiteral("ug/L")),
             QStringLiteral("ug/L"));
}

void TestSpeciesAttributes::outCodeResolvesAgainstTheRunsSpeciesList()
{
    // A run carrying two pollutants then the two reserved columns — the
    // order the engine writes them (pollutants first, reserved trailing).
    const QStringList species = {
        QStringLiteral("TSS"), QStringLiteral("Lead"),
        QString::fromLatin1(kWaterAgeName),
        QString::fromLatin1(kTemperatureName),
    };

    QCOMPARE(speciesOutCode(QStringLiteral("qual:TSS"), species,
                            SWMM_OUT_NODE_POLLUT_BASE),
             int(SWMM_OUT_NODE_POLLUT_BASE) + 0);
    QCOMPARE(speciesOutCode(QStringLiteral("qual:Lead"), species,
                            SWMM_OUT_NODE_POLLUT_BASE),
             int(SWMM_OUT_NODE_POLLUT_BASE) + 1);
    QCOMPARE(speciesOutCode(speciesAttributeName(
                                QString::fromLatin1(kWaterAgeName)),
                            species, SWMM_OUT_NODE_POLLUT_BASE),
             int(SWMM_OUT_NODE_POLLUT_BASE) + 2);

    // Per-kind bases differ; the same token resolves to a different code
    // for links and subcatchments.
    QCOMPARE(speciesOutCode(QStringLiteral("qual:Lead"), species,
                            SWMM_OUT_LINK_POLLUT_BASE),
             int(SWMM_OUT_LINK_POLLUT_BASE) + 1);
    QCOMPARE(speciesOutCode(QStringLiteral("qual:Lead"), species,
                            SWMM_OUT_SUBCATCH_POLLUT_BASE),
             int(SWMM_OUT_SUBCATCH_POLLUT_BASE) + 1);

    // THE persistence claim: reordering the run's species moves the code,
    // and the SAME saved token still finds the right column. An index-
    // based design would silently read the other pollutant here.
    const QStringList reordered = {
        QStringLiteral("Lead"), QStringLiteral("TSS"),
    };
    QCOMPARE(speciesOutCode(QStringLiteral("qual:TSS"), reordered,
                            SWMM_OUT_NODE_POLLUT_BASE),
             int(SWMM_OUT_NODE_POLLUT_BASE) + 1);
}

void TestSpeciesAttributes::outCodeRejectsUnknownAndMalformed()
{
    const QStringList species = { QStringLiteral("TSS") };

    // A theme saved against a species this run does not carry must resolve
    // to -1 — every call site already skips a negative code, which is what
    // turns "missing species" into "no theme" instead of "wrong column".
    QCOMPARE(speciesOutCode(QStringLiteral("qual:Lead"), species,
                            SWMM_OUT_NODE_POLLUT_BASE), -1);
    // A run with no quality at all.
    QCOMPARE(speciesOutCode(QStringLiteral("qual:TSS"), QStringList(),
                            SWMM_OUT_NODE_POLLUT_BASE), -1);
    // Not a species token.
    QCOMPARE(speciesOutCode(QStringLiteral("depth"), species,
                            SWMM_OUT_NODE_POLLUT_BASE), -1);
    // Malformed: prefix with no name must not resolve to index 0.
    QCOMPARE(speciesOutCode(QStringLiteral("qual:"), species,
                            SWMM_OUT_NODE_POLLUT_BASE), -1);
    // Case sensitivity: species names are engine identifiers, compared
    // exactly. A near-miss must fail loudly rather than pick a neighbour.
    QCOMPARE(speciesOutCode(QStringLiteral("qual:tss"), species,
                            SWMM_OUT_NODE_POLLUT_BASE), -1);
}

QTEST_MAIN(TestSpeciesAttributes)
#include "test_species_attributes.moc"
