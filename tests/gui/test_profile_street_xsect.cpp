/*!
 * \file   test_profile_street_xsect.cpp
 * \brief  linkFullDepth() — the depth / open-top resolution the profile plot
 *         uses instead of trusting geom1.
 *
 * Why this exists: swmm_link_get_xsect() reports geom1 as a TABLE INDEX for
 * STREET and IRREGULAR (the engine resolves the retained name back to an
 * index), and as a real full depth for every other shape. The profile plot
 * assigned geom1 to LinkStatic::maxDepth unconditionally, so a street conduit
 * on the FIRST street got maxDepth = 0 — a zero-height tube whose crown line
 * was drawn on top of its invert — and one on the second street got a 1 ft
 * pipe. Nothing anywhere asserted what that depth should be, which is why it
 * shipped.
 *
 * These gates are engine-facing on purpose: they fail if the engine changes
 * what geom1 means for a tabulated shape, which is exactly the change that
 * would silently break the profile again.
 *
 * Fixture: tests/data/inlets/street_inlet.inp (C1 is STREET on the only
 * street, i.e. index 0 — the case that produced maxDepth = 0; D1 is CIRCULAR).
 * Artefacts go to tests/output/profile_street/ (CLAUDE.md §4.1).
 */
#include <QtTest>

#include <QDir>
#include <QString>

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_model.h>

#include "ui/sectionview/xsectsampler.h"

using openswmmvis::sectionview::linkFullDepth;
using openswmmvis::sectionview::samplerForLink;

#ifndef SWMMVIS_PSTREET_FIXTURE_DIR
#  define SWMMVIS_PSTREET_FIXTURE_DIR "."
#endif
#ifndef SWMMVIS_PSTREET_OUTPUT_DIR
#  define SWMMVIS_PSTREET_OUTPUT_DIR "."
#endif

class TestProfileStreetXsect : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void streetDepthIsTheSectionDepthNotTheStreetIndex();
    void streetIsDrawnOpenTopped();
    void closedPipeKeepsGeom1AndStaysClosed();

private:
    SWMM_Engine m_engine = nullptr;

    /*! Read a link's shape + geoms exactly as the profile adapter does. */
    bool readXsect(const char *link, int &shape, double &g1, double &g2,
                   double &g3, double &g4, int &idx) const
    {
        idx = swmm_link_index(m_engine, link);
        if (idx < 0) return false;
        return swmm_link_get_xsect(m_engine, idx, &shape, &g1, &g2, &g3, &g4)
               == SWMM_OK;
    }
};

void TestProfileStreetXsect::initTestCase()
{
    QDir out(QStringLiteral(SWMMVIS_PSTREET_OUTPUT_DIR));
    out.mkpath(QStringLiteral("."));

    const QString inp = QDir(QStringLiteral(SWMMVIS_PSTREET_FIXTURE_DIR))
                            .filePath(QStringLiteral("street_inlet.inp"));
    m_engine = swmm_engine_create();
    QVERIFY(m_engine != nullptr);
    QCOMPARE(swmm_engine_open(m_engine, inp.toUtf8().constData(),
                              out.filePath(QStringLiteral("ps.rpt")).toUtf8().constData(),
                              out.filePath(QStringLiteral("ps.out")).toUtf8().constData(),
                              nullptr),
             SWMM_OK);
}

void TestProfileStreetXsect::cleanupTestCase()
{
    if (m_engine) swmm_engine_destroy(m_engine);
    m_engine = nullptr;
}

// The regression gate on the reported bug.
void TestProfileStreetXsect::streetDepthIsTheSectionDepthNotTheStreetIndex()
{
    int shape = -1, idx = -1;
    double g1 = 0, g2 = 0, g3 = 0, g4 = 0;
    QVERIFY(readXsect("C1", shape, g1, g2, g3, g4, idx));
    QCOMPARE(shape, SWMM_XSECT_STREET);

    // The trap itself: geom1 is the street's INDEX. ST1 is the only street, so
    // the engine reports 0 — the value that used to become the crown offset.
    QCOMPARE(int(qRound(g1)), 0);

    const double depth =
        linkFullDepth(m_engine, idx, shape, g1, g2, g3, g4, /*si=*/false);

    QVERIFY2(depth > 0.0,
             "a STREET conduit reported zero full depth — the profile plot "
             "would draw its crown line on top of its invert");
    QVERIFY2(!qFuzzyCompare(depth, 1.0 + g1),
             "full depth still tracks the street index");

    // Cross-check against the engine's own section: the depth must be the one
    // the solver uses, not an approximation of it.
    const auto sampler =
        samplerForLink(m_engine, idx, shape, g1, g2, g3, g4, /*si=*/false);
    QVERIFY(sampler.isValid());
    QCOMPARE(depth, sampler.fullProps().yFull);

    // ST1 has Hcurb = 0.5 ft over a 20 ft crown at Sx = 4%, so the section is
    // a fraction of a foot deep — worth pinning loosely, since a units slip
    // here would show up as a plausible-but-wrong number.
    QVERIFY2(depth > 0.05 && depth < 5.0,
             qPrintable(QStringLiteral("street depth %1 ft is out of range for "
                                       "ST1").arg(depth)));
}

void TestProfileStreetXsect::streetIsDrawnOpenTopped()
{
    int shape = -1, idx = -1;
    double g1 = 0, g2 = 0, g3 = 0, g4 = 0;
    QVERIFY(readXsect("C1", shape, g1, g2, g3, g4, idx));

    bool openTop = false;
    linkFullDepth(m_engine, idx, shape, g1, g2, g3, g4, /*si=*/false, &openTop);
    QVERIFY2(openTop,
             "a street must be DRAWN open-topped; drawn with a soffit it reads "
             "as a box culvert");

    // …while the ENGINE still classifies it closed. The difference is a
    // deliberate presentation choice living in linkFullDepth, not a divergence
    // from the engine — tests/gui/test_xsectsampler.cpp pins the engine side.
    const auto sampler =
        samplerForLink(m_engine, idx, shape, g1, g2, g3, g4, /*si=*/false);
    QVERIFY(sampler.isValid());
    QVERIFY2(!sampler.fullProps().open,
             "engine now calls a street open — drop the STREET special case in "
             "linkFullDepth rather than keeping two sources of truth");
}

void TestProfileStreetXsect::closedPipeKeepsGeom1AndStaysClosed()
{
    int shape = -1, idx = -1;
    double g1 = 0, g2 = 0, g3 = 0, g4 = 0;
    QVERIFY(readXsect("D1", shape, g1, g2, g3, g4, idx));
    QCOMPARE(shape, SWMM_XSECT_CIRCULAR);

    // The common path must stay a plain read of geom1 — no sampler, no change
    // in value, so the fix costs nothing for ordinary pipes.
    bool openTop = true;
    const double depth =
        linkFullDepth(m_engine, idx, shape, g1, g2, g3, g4, /*si=*/false,
                      &openTop);
    QCOMPARE(depth, g1);
    QCOMPARE(depth, 1.5);          // D1 is CIRCULAR 1.5 in the fixture
    QVERIFY2(!openTop, "a circular pipe must keep its crown line");
}

QTEST_MAIN(TestProfileStreetXsect)
#include "test_profile_street_xsect.moc"
