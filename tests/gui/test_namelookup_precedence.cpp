/*!
 * \file   test_namelookup_precedence.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Pins the name-resolution contract of identifyByName() / nodeIndex()
 *         / linkIndex() after they were converted from linear scans to hash
 *         lookups (GUI load-perf Phase 1).
 *
 *         Those three used to scan m_nodes → m_links → m_catchments →
 *         m_gages. That made every caller iterating a category quadratic:
 *         restoring a 13 KB .oswp onto a 122k-link model spent 22 s inside
 *         rebuildKindFeatureColors, because each link had to fail a full
 *         42k-node scan before its own scan began.
 *
 *         The obvious fix — reuse the existing m_nameToSoa hash — would have
 *         been WRONG. m_nameToSoa is single-keyed across all four kinds and
 *         its insert order gives catchments and gages an unconditional
 *         overwrite, so a subcatchment colliding with a rain gage resolves
 *         to the GAGE there, whereas the scans resolved to the subcatchment.
 *         Per-kind maps preserve the scan semantics instead.
 *
 *         SWMM namespaces each kind separately, so these collisions are legal
 *         and appear in real models. typed_selection_fixture.inp carries both
 *         shapes deliberately: gage+subcatchment "S1", junction+conduit "X1".
 *
 *         Uses QTEST_MAIN (offscreen QPA) because SWMMModelLayer is a QObject
 *         layer that expects a QApplication.
 */
#include "layers/swmmmodellayer.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QObject>
#include <QTest>
#include <QVariantMap>

namespace {

QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

QString fixturePath()
{
    return QDir(dataDir()).filePath(QStringLiteral("typed_selection_fixture.inp"));
}

}  // namespace

class TestNameLookupPrecedence : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();

    void nodeWinsOverCollidingLink();
    void subcatchmentWinsOverCollidingGage();
    void perKindIndicesSeeThroughCollisions();
    void unknownNameResolvesToNothing();
    void renameMovesOnlyTheRenamedKind();
    void incrementalAddsStayResolvable();
};

void TestNameLookupPrecedence::initTestCase()
{
    QVERIFY2(QFile::exists(fixturePath()),
             "typed_selection_fixture.inp missing from the gui-test data dir");
}

// A junction and a conduit are both named X1. The scans checked nodes first,
// so identifyByName must still report the junction.
void TestNameLookupPrecedence::nodeWinsOverCollidingLink()
{
    SWMMModelLayer layer(fixturePath(), nullptr);
    QList<QString> warnings, errors;
    QVERIFY(layer.loadModel(warnings, errors));

    const QVariantMap m = layer.identifyByName(QStringLiteral("X1"));
    QCOMPARE(m.value(QStringLiteral("Type")).toString(), QStringLiteral("Node"));
    QCOMPARE(m.value(QStringLiteral("Name")).toString(), QStringLiteral("X1"));
}

// A rain gage and a subcatchment are both named S1. The scan order put
// catchments ahead of gages — the case a naive m_nameToSoa swap would have
// inverted, because gages insert into that hash last and unconditionally.
void TestNameLookupPrecedence::subcatchmentWinsOverCollidingGage()
{
    SWMMModelLayer layer(fixturePath(), nullptr);
    QList<QString> warnings, errors;
    QVERIFY(layer.loadModel(warnings, errors));

    const QVariantMap m = layer.identifyByName(QStringLiteral("S1"));
    QCOMPARE(m.value(QStringLiteral("Type")).toString(),
             QStringLiteral("Subcatchment"));
    QCOMPARE(m.value(QStringLiteral("Name")).toString(), QStringLiteral("S1"));
}

// nodeIndex/linkIndex are kind-scoped: the conduit X1 stays reachable even
// though the junction X1 outranks it in identifyByName, and neither reports
// a hit for a name that belongs to no node/link at all.
void TestNameLookupPrecedence::perKindIndicesSeeThroughCollisions()
{
    SWMMModelLayer layer(fixturePath(), nullptr);
    QList<QString> warnings, errors;
    QVERIFY(layer.loadModel(warnings, errors));

    const int nIdx = layer.nodeIndex(QStringLiteral("X1"));
    const int lIdx = layer.linkIndex(QStringLiteral("X1"));
    QVERIFY2(nIdx >= 0, "junction X1 must resolve");
    QVERIFY2(lIdx >= 0, "conduit X1 must resolve despite the junction of the same name");

    QCOMPARE(layer.nodeIndex(QStringLiteral("S1")), -1);
    QCOMPARE(layer.linkIndex(QStringLiteral("S1")), -1);

    // A plain, collision-free conduit still resolves.
    QVERIFY(layer.linkIndex(QStringLiteral("C1")) >= 0);
}

void TestNameLookupPrecedence::unknownNameResolvesToNothing()
{
    SWMMModelLayer layer(fixturePath(), nullptr);
    QList<QString> warnings, errors;
    QVERIFY(layer.loadModel(warnings, errors));

    QVERIFY(layer.identifyByName(QStringLiteral("nope_not_here")).isEmpty());
    QVERIFY(layer.identifyByName(QString()).isEmpty());
    QCOMPARE(layer.nodeIndex(QStringLiteral("nope_not_here")), -1);
    QCOMPARE(layer.linkIndex(QStringLiteral("nope_not_here")), -1);
}

// Renaming the junction X1 must move only the node entry; the conduit X1
// keeps its own name. This is the coherence the per-kind maps must maintain
// incrementally (renameObject), not via a full index rebuild.
void TestNameLookupPrecedence::renameMovesOnlyTheRenamedKind()
{
    SWMMModelLayer layer(fixturePath(), nullptr);
    QList<QString> warnings, errors;
    QVERIFY(layer.loadModel(warnings, errors));

    const int linkBefore = layer.linkIndex(QStringLiteral("X1"));
    QVERIFY(linkBefore >= 0);

    // kKindNode hint: rename the junction only, never the conduit of the
    // same name.
    QVERIFY2(layer.applyRename(QStringLiteral("X1"), QStringLiteral("X1_renamed"),
                               SWMMModelLayer::kKindNode),
             "renaming the junction X1 should succeed");

    // The node moved...
    QVERIFY(layer.nodeIndex(QStringLiteral("X1_renamed")) >= 0);
    QCOMPARE(layer.nodeIndex(QStringLiteral("X1")), -1);

    // ...and the conduit of the same name did not.
    QCOMPARE(layer.linkIndex(QStringLiteral("X1")), linkBefore);

    // With the node gone from X1, identifyByName now falls through to the link.
    QCOMPARE(layer.identifyByName(QStringLiteral("X1"))
                 .value(QStringLiteral("Type")).toString(),
             QStringLiteral("Link"));
}

// Adding a subcatchment or gage goes through appendCatchSceneEntry() /
// appendGageSceneEntry(), which patch the name indices in place — the callers
// do NOT rebuild the category index. A per-kind map that is only populated in
// rebuildCategoryIndex() therefore goes stale, and the newly drawn object
// silently resolves to nothing in the attribute table and property browser.
// (The rest of the suite did not catch that; hence this slot.)
void TestNameLookupPrecedence::incrementalAddsStayResolvable()
{
    SWMMModelLayer layer(fixturePath(), nullptr);
    QList<QString> warnings, errors;
    QVERIFY(layer.loadModel(warnings, errors));

    QVERIFY2(layer.applyGageAdd(QStringLiteral("G_added"), 4242.0, 2424.0),
             "adding a rain gage should succeed");
    const QVariantMap gm = layer.identifyByName(QStringLiteral("G_added"));
    QCOMPARE(gm.value(QStringLiteral("Type")).toString(), QStringLiteral("Rain Gage"));

    const QVector<QPointF> ring{{0.0, 0.0}, {100.0, 0.0}, {100.0, 100.0}, {0.0, 100.0}};
    QVERIFY2(layer.applySubcatchAdd(QStringLiteral("S_added"), ring),
             "adding a subcatchment should succeed");
    const QVariantMap sm = layer.identifyByName(QStringLiteral("S_added"));
    QCOMPARE(sm.value(QStringLiteral("Type")).toString(), QStringLiteral("Subcatchment"));

    // Pre-existing entries must survive the incremental inserts.
    QCOMPARE(layer.identifyByName(QStringLiteral("S1"))
                 .value(QStringLiteral("Type")).toString(),
             QStringLiteral("Subcatchment"));
    QVERIFY(layer.linkIndex(QStringLiteral("C1")) >= 0);
}

QTEST_MAIN(TestNameLookupPrecedence)
#include "test_namelookup_precedence.moc"
