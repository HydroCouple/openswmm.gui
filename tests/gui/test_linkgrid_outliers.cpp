/*!
 * \file   test_linkgrid_outliers.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  A handful of corrupt coordinates must not make a model unusable.
 *
 *         West Whiteland's 2024 model has 10 junctions (J45194, J45202,
 *         J45231, J45269 and neighbours) sitting up to 40,000,000 units from
 *         a network that really spans 27,000 x 22,000 — a projection failure
 *         on one imported group. Sizing LinkSpatialGrid from the raw union of
 *         bounding boxes stretched it to 23.8M x 70.3M; cell size then scaled
 *         up to satisfy the 1024^2 cap until the ENTIRE real network fitted
 *         in a single cell. Every spatial query returned every link, so
 *         hover, hit-testing and paint all degenerated to linear scans over
 *         281,049 links and the file was effectively unusable.
 *
 *         rebuild() now sizes from a Tukey-fenced extent and clamps the
 *         far-away links into edge cells; query() clamps to match, so those
 *         links stay reachable rather than becoming invisible.
 *
 *         Driven through the public layer API on a fixture carrying the same
 *         signature, so this pins the behaviour a user sees — both that a
 *         normal link is still selectable when a degenerate extent is
 *         present, and that the corrupt ones did not silently disappear.
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
    return QDir(dataDir()).filePath(QStringLiteral("corrupt_coords_fixture.inp"));
}

}  // namespace

class TestLinkGridOutliers : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void normalLinkStaysHitTestable();
    void corruptCoordinateLinkStaysHitTestable();
    void extentReflectsTheCorruptCoordinates();
};

void TestLinkGridOutliers::initTestCase()
{
    QVERIFY2(QFile::exists(fixturePath()),
             "corrupt_coords_fixture.inp missing from the gui-test data dir");
}

// The point of the fix: a link in the dense, well-formed part of the model is
// still found by a click on it, even though the model's raw extent is
// degenerate. This passed before too (a linear scan finds everything) — it is
// here so a future indexing change cannot trade correctness for speed.
void TestLinkGridOutliers::normalLinkStaysHitTestable()
{
    SWMMModelLayer layer(fixturePath(), nullptr);
    QList<QString> warnings, errors;
    QVERIFY(layer.loadModel(warnings, errors));

    // Midpoint of conduit C0, which runs J0 -> J1.
    const double x = (2548695.0 + 2548765.0) / 2.0;
    const double y = (250578.0 + 250853.0) / 2.0;

    const QVariantMap hit = layer.identifyAt(x, y, 60.0);
    QCOMPARE(hit.value(QStringLiteral("elementType")).toString(), QStringLiteral("Link"));
    QCOMPARE(hit.value(QStringLiteral("elementName")).toString(), QStringLiteral("C0"));
}

// The regression this guards: rebuild() clamps out-of-fence links into edge
// cells, so query() must clamp as well. If it instead rejected rects falling
// outside the fenced extent, these links would be stored but unreachable —
// present in the model, invisible to every click.
void TestLinkGridOutliers::corruptCoordinateLinkStaysHitTestable()
{
    SWMMModelLayer layer(fixturePath(), nullptr);
    QList<QString> warnings, errors;
    QVERIFY(layer.loadModel(warnings, errors));

    // Midpoint of CBAD0 (BAD0 -> BAD1), out at ~2.6e7, 2.9e7. The endpoints
    // are 800 units apart so the midpoint is unambiguously on the conduit —
    // identifyAt resolves nodes before links, so a point near either end
    // would legitimately return the junction instead.
    const double x = (26360865.9 + 26361665.9) / 2.0;
    const double y = 29071714.45;

    const QVariantMap hit = layer.identifyAt(x, y, 60.0);
    QCOMPARE(hit.value(QStringLiteral("elementType")).toString(), QStringLiteral("Link"));
    QCOMPARE(hit.value(QStringLiteral("elementName")).toString(), QStringLiteral("CBAD0"));
}

// The layer extent still covers everything — the fence changes how the index
// is sized, not what the model contains. Pins that the fix did not "solve" the
// problem by dropping the offending objects.
void TestLinkGridOutliers::extentReflectsTheCorruptCoordinates()
{
    SWMMModelLayer layer(fixturePath(), nullptr);
    QList<QString> warnings, errors;
    QVERIFY(layer.loadModel(warnings, errors));

    QCOMPARE(layer.cachedNodeCount(), 45);   // 40 J + 4 BAD + 1 outfall
    QVERIFY(layer.extent().isValid());
    QVERIFY2(layer.extent().xMax() > 2.0e7,
             "the far-away junctions must still be part of the model extent");
}

QTEST_MAIN(TestLinkGridOutliers)
#include "test_linkgrid_outliers.moc"
