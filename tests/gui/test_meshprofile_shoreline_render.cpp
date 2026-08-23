/*!
 * \file   test_meshprofile_shoreline_render.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * 2D profile shoreline intercept — PAINTER side
 * (workplans/HANDOFF_PROFILE_SHORELINE_INTERCEPT_2026-08-23.md).
 *
 * `MeshProfileInterp::shorelineIntercept` is pinned by test_meshprofileinterp;
 * what that leaves untested is the four lines in `paintWetBand` that splice the
 * intercepts onto each run — where a swapped leading/trailing, a point added to
 * the wrong end, or a fill polygon that no longer closes on the ground would
 * all still satisfy the helper's own tests.
 *
 * The assertion is sampling-independence: a COARSELY sampled profile must paint
 * its shoreline in the same place as a densely sampled one over identical
 * terrain and water. That is exactly the property premature truncation broke —
 * the coarse band stopped up to one resample step short — and it needs no
 * access to the widget's private pixel mapping to check.
 */

#include "plot/meshprofileplotoptions.h"
#include "plot/meshprofileplotwidget.h"
#include "plot/profilesection.h"

#include <QImage>
#include <QObject>
#include <QPainter>
#include <QTest>

#include <cmath>

namespace {

// Water surface and bed of the fixture. The bed rises through the WSE at
// chainage 9.5, which is deliberately NOT on the coarse sample grid — a
// shoreline that lands on a sample would be painted correctly with or without
// the fix and would make the test vacuous.
constexpr double kWse       = 0.95;
constexpr double kBedSlope  = 0.1;
constexpr double kShoreline = kWse / kBedSlope;   // 9.5
constexpr double kEnd       = 12.0;

ProfileSection::Section makeProfile(double step)
{
    ProfileSection::Section sec;
    sec.hasResults = true;
    for (double c = 0.0; c <= kEnd + 1e-9; c += step) {
        ProfileSection::Sample s;
        s.chainage       = c;
        s.ground         = kBedSlope * c;
        s.depthNow       = std::max(0.0, kWse - s.ground);
        s.maxDepth       = s.depthNow;
        s.triIdx         = 0;
        s.cellHasSurface = true;
        s.scenePt        = QPointF(c, 0.0);
        sec.samples << s;
    }
    return sec;
}

//! Paint one profile and report the rightmost column carrying water fill.
int waterEdgeColumn(double step, MeshProfilePlotOptions *opts, const QSize &size)
{
    MeshProfilePlotWidget w;
    w.setOptions(opts);
    w.resize(size);
    w.setProfile(makeProfile(step));

    QImage img(size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::white);
    w.render(&img);

    // The depth fill is the only pure-red thing on the canvas (see the options
    // below), so "is this pixel water?" needs no tolerance games.
    int edge = -1;
    for (int x = size.width() - 1; x >= 0 && edge < 0; --x)
        for (int y = 0; y < size.height(); ++y) {
            const QColor c = img.pixelColor(x, y);
            if (c.red() > 200 && c.green() < 60 && c.blue() < 60) { edge = x; break; }
        }
    return edge;
}

} // namespace

class TestMeshProfileShorelineRender : public QObject
{
    Q_OBJECT
private slots:
    void coarseSamplingPaintsTheSameShorelineAsDense();
};

void TestMeshProfileShorelineRender::coarseSamplingPaintsTheSameShorelineAsDense()
{
    const QSize size(700, 360);

    MeshProfilePlotOptions opts;
    opts.setDepthFillBrush(QBrush(QColor(255, 0, 0)));      // opaque, unique
    opts.setShowMaxEnvelopeFill(false);
    opts.setShowMaxEnvelopeLine(false);
    opts.setShowWseLine(false);                             // fill only
    // The legend paints a swatch in the SAME brush, parked near the right
    // edge and independent of the data — leave it on and every render reports
    // the swatch's column as the shoreline, which is a test that can never
    // fail. (It could not, until this line.)
    opts.setLegendVisible(false);
    opts.setShowTimeLabel(false);

    // Both profiles cover the same chainage range, so both share a pixel
    // mapping and the columns are directly comparable.
    const int dense  = waterEdgeColumn(0.05, &opts, size);
    const int coarse = waterEdgeColumn(1.00, &opts, size);

    QVERIFY2(dense > 0, "the dense profile painted no water at all");
    QVERIFY2(coarse > 0, "the coarse profile painted no water at all");

    // One coarse step is 1.0 of 12.0 units across ~640 px of plot — about
    // 53 px. Truncation put the coarse edge half a step (~27 px) short; the
    // intercept has to bring it within a pixel or two of the dense one.
    QVERIFY2(std::abs(dense - coarse) <= 3,
             qPrintable(QStringLiteral("coarse shoreline at column %1, dense at "
                                       "%2 — the coarse band is still stopping "
                                       "short of the WSE/ground crossing")
                            .arg(coarse).arg(dense)));
}

QTEST_MAIN(TestMeshProfileShorelineRender)
#include "test_meshprofile_shoreline_render.moc"
