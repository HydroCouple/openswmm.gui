/**
 * @file      test_profileplot_crown_visibility.cpp
 * @brief     Tests for profileplot crown visibility.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Pipe crown visibility at the top of the profile plot.
 *
 * recomputeBounds() fed yMax only from node rims (plus series/terrain/2D
 * extents), never from conduit crowns, and kPadFracY was 0 — so a crown that
 * coincides with the ground at the highest node mapped exactly onto
 * plotRect().top(), where paintConduits' clip rect ate half the stroke and
 * the rest merged into the axis frame; a crown ABOVE the ground (positive
 * end offsets) was clipped out entirely.
 *
 * The fixtures are pure PathStatic values (no engine): 2 junctions with
 * ground = invert + maxDepth = 1.0 and one circular-style closed conduit
 * whose crown lands on / above / below that ground line. The conduit outline
 * is repainted in an opaque pure-red 3 px pen — the only red on the canvas
 * (same unique-colour trick as test_meshprofile_shoreline_render) — and the
 * assertions scan rendered rows for red bands: the crown must survive as a
 * band strictly below the plot-rect top edge.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "plot/profilebuilder.h"
#include "plot/profileplotoptions.h"
#include "plot/profileplotwidget.h"

#include <QDir>
#include <QImage>
#include <QObject>
#include <QPen>
#include <QTest>

#include <utility>
#include <vector>

namespace
{

// Geometry shared by every case: ground (rim) elevation is 1.0 at both nodes.
constexpr double kNodeInvert = 0.0;
constexpr double kNodeDepth = 1.0; // rim = 1.0
constexpr double kLinkDepth = 1.0; // crown = link invert + 1.0
constexpr double kSpan = 10.0;

// plotRect().top() when node/link label rows are off (kMarginTop).
constexpr int kPlotTop = 16;

/**
 *
 * @brief Build a two-node ten-unit-span pipe profile for crown visibility.
 * @param offset Equal end offset above zero invert, in the fixture elevation units.
 * @param openTop True requests an open section without a crown stroke.
 * @return Owned path with node rim one, pipe depth one and two chainage stations;
 * no engine, result file or CRS is involved.
 *
 */
ProfileBuilder::PathStatic makePath(double offset, bool openTop)
{
    ProfileBuilder::PathStatic path;
    for (const char *name : {"J1", "J2"}) {
        ProfileBuilder::NodeStatic n;
        n.name = QString::fromLatin1(name);
        n.invertElev = kNodeInvert;
        n.maxDepth = kNodeDepth;
        path.nodes << n;
    }
    ProfileBuilder::LinkStatic l;
    l.name = QStringLiteral("C1");
    l.length = kSpan;
    l.offset1 = offset;
    l.offset2 = offset;
    l.maxDepth = kLinkDepth;
    l.openTop = openTop;
    path.links << l;
    path.chainage = {0.0, kSpan};
    return path;
}

//! Render one fixture and report the horizontal red bands (top row, bottom
//! row) from top to bottom. A row counts as red when at least 100 of its
//! pixels are pure-pen red — long horizontal strokes (invert/crown) qualify,
//! the few-pixel-wide vertical end caps do not.
/**
 *
 * @brief Render a crown fixture and detect long near-red horizontal stroke bands.
 * @param path Borrowed coherent profile path, read synchronously by a GUI widget.
 * @param artifactName Borrowed filename appended under SWMMVIS_PCROWN_OUTPUT_DIR.
 * Requires the configured directory to exist and GUI rendering; PNG save success
 * is deliberately unchecked.
 * @return Owned inclusive top/bottom physical-row pairs where at least 100 pixels
 * have red>200 and green/blue<60, ordered top to bottom. Uses a 500-by-300 image
 * and a red three-pixel conduit pen; this is a color-threshold probe, not exact pure red.
 *
 */
std::vector<std::pair<int, int>> redBands(const ProfileBuilder::PathStatic &path,
                                          const QString &artifactName)
{
    ProfilePlotOptions opts;
    opts.setConduitOutlinePen(QPen(QColor(255, 0, 0), 3.0, Qt::SolidLine));

    ProfilePlotWidget w;
    w.setOptions(&opts);
    w.resize(500, 300);
    w.setPath(path);

    QImage img(500, 300, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::white);
    w.render(&img);
    img.save(QStringLiteral(SWMMVIS_PCROWN_OUTPUT_DIR "/") + artifactName);

    std::vector<std::pair<int, int>> bands;
    bool inBand = false;
    for (int y = 0; y < img.height(); ++y) {
        int reds = 0;
        for (int x = 0; x < img.width(); ++x) {
            const QColor c = img.pixelColor(x, y);
            if (c.red() > 200 && c.green() < 60 && c.blue() < 60)
                ++reds;
        }
        const bool red = reds >= 100;
        if (red && !inBand) {
            bands.push_back({y, y});
            inBand = true;
        } else if (red) {
            bands.back().second = y;
        } else {
            inBand = false;
        }
    }
    return bands;
}

} // namespace

class TestProfilePlotCrownVisibility : public QObject
{
    Q_OBJECT
private slots:
    /**
     *
     * @brief Require the configured crown-review artifact directory to be created before
     * rendering cases.
     *
     */
    void initTestCase();
    /**
     *
     * @brief Require crown/invert red bands with crown at least two rows below plot top and a
     * 20-row inter-band gap.
     * Writes crown_at_ground.png through redBands; no engine run.
     *
     */
    void crownAtGroundSeparatesFromTheFrame();
    /**
     *
     * @brief Require an above-ground crown plus invert to produce two bands, with the crown clear
     * of the plot top.
     * Writes crown_above_ground.png through redBands.
     *
     */
    void crownAboveGroundIsDrawn();
    /**
     *
     * @brief Require an open-top profile to produce only the invert red band.
     * Writes open_top.png through redBands.
     *
     */
    void openTopDrawsNoCrown();
};

void TestProfilePlotCrownVisibility::initTestCase()
{
    QVERIFY(QDir().mkpath(QStringLiteral(SWMMVIS_PCROWN_OUTPUT_DIR)));
}

void TestProfilePlotCrownVisibility::crownAtGroundSeparatesFromTheFrame()
{
    // Crown (1.0) coincides with the ground rim (1.0) at every node.
    const auto bands = redBands(makePath(0.0, false), QStringLiteral("crown_at_ground.png"));

    QVERIFY2(bands.size() == 2,
             qPrintable(QStringLiteral("expected crown + invert bands, got %1").arg(bands.size())));
    // The crown must sit clear of the clipped top edge / axis frame — not be
    // a half-stroke remnant painted onto plotRect().top() itself.
    QVERIFY2(bands.front().first >= kPlotTop + 2,
             qPrintable(QStringLiteral("crown band starts at row %1 — merged "
                                       "into the plot frame at row %2")
                            .arg(bands.front().first)
                            .arg(kPlotTop)));
    // And the second band really is the invert, far below.
    QVERIFY2(bands.back().first - bands.front().second >= 20,
             "crown and invert bands are implausibly close");
}

void TestProfilePlotCrownVisibility::crownAboveGroundIsDrawn()
{
    // End offsets lift the barrel: crown 1.5 sits ABOVE the ground rim 1.0.
    const auto bands = redBands(makePath(0.5, false), QStringLiteral("crown_above_ground.png"));

    QVERIFY2(bands.size() == 2, qPrintable(QStringLiteral("expected crown + invert bands, got %1 "
                                                          "— an above-ground crown was clipped "
                                                          "out of the y-extent")
                                               .arg(bands.size())));
    QVERIFY2(bands.front().first >= kPlotTop + 2,
             qPrintable(QStringLiteral("crown band starts at row %1 — on the "
                                       "plot frame")
                            .arg(bands.front().first)));
}

void TestProfilePlotCrownVisibility::openTopDrawsNoCrown()
{
    // Open sections intentionally draw no soffit line — invert band only.
    const auto bands = redBands(makePath(0.0, true), QStringLiteral("open_top.png"));

    QVERIFY2(bands.size() == 1, qPrintable(QStringLiteral("expected the invert band only for an "
                                                          "open-top section, got %1")
                                               .arg(bands.size())));
}

QTEST_MAIN(TestProfilePlotCrownVisibility)
#include "test_profileplot_crown_visibility.moc"
