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
int waterEdgeColumn(double step, MeshProfilePlotOptions *opts, const QSize &size, bool exact = false)
{
    MeshProfilePlotWidget w;
    w.setOptions(opts);
    w.resize(size);
    auto profile = makeProfile(step);
    profile.exactWaterGeometry = exact;
    for (auto& sample : profile.samples) {
        sample.signedDepthNow = kWse - sample.ground;
        sample.signedMaxDepth = sample.signedDepthNow;
    }
    w.setProfile(profile);

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
    void exactIntervalPaintsAnalyticShoreline();
    void exactProfileDoesNotBridgeDryGap();
    void exactFillHasNoCellSeams_data();
    void exactFillHasNoCellSeams();
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

void TestMeshProfileShorelineRender::exactIntervalPaintsAnalyticShoreline()
{
    MeshProfilePlotOptions opts;
    opts.setDepthFillBrush(QBrush(QColor(255,0,0)));
    opts.setShowMaxEnvelopeFill(false);
    opts.setShowMaxEnvelopeLine(false);
    opts.setShowWseLine(false);
    opts.setLegendVisible(false);
    opts.setShowTimeLabel(false);
    const QSize size(700,360);
    const int analytic = waterEdgeColumn(0.05,&opts,size,true);
    const int interval = waterEdgeColumn(12.0,&opts,size,true);
    QVERIFY(analytic > 0);
    QVERIFY(std::abs(analytic-interval) <= 2);
}

void TestMeshProfileShorelineRender::exactProfileDoesNotBridgeDryGap()
{
    MeshProfilePlotOptions opts;
    opts.setDepthFillBrush(QBrush(QColor(255,0,0)));
    opts.setShowMaxEnvelopeFill(false);
    opts.setShowMaxEnvelopeLine(false);
    opts.setShowWseLine(false);
    opts.setLegendVisible(false);
    opts.setShowTimeLabel(false);
    auto profile = makeProfile(3.0);
    profile.exactWaterGeometry = true;
    // Three cell-owned intervals. The middle cell has no water, even though
    // the two adjacent cells carry the same stage above its bed.
    profile.samples.clear();
    for (int cell=0; cell<3; ++cell) {
        const double endpoints[4] = {0,3,9,12};
        for (int end=0; end<2; ++end) {
            ProfileSection::Sample sample;
            sample.chainage=endpoints[cell+end];
            sample.ground=kBedSlope*sample.chainage;
            sample.breakBefore=end==0;
            sample.signedDepthNow=cell==1 ? std::numeric_limits<double>::quiet_NaN()
                                          : kWse-sample.ground;
            sample.depthNow=std::max(0.0,sample.signedDepthNow);
            sample.maxDepth=sample.depthNow;
            sample.signedMaxDepth=sample.signedDepthNow;
            profile.samples.push_back(sample);
        }
    }
    MeshProfilePlotWidget w;
    w.setOptions(&opts);
    w.resize(700,360);
    w.setProfile(profile);
    QImage image(w.size(),QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    w.render(&image);
    auto waterAt=[&](int x) {
        for (int y=0;y<image.height();++y) {
            const auto c=image.pixelColor(x,y);
            if (c.red()>200 && c.green()<60 && c.blue()<60) return true;
        }
        return false;
    };
    int first=-1,last=-1,dryColumns=0;
    for (int x=0;x<image.width();++x)
        if (waterAt(x)) { if (first<0) first=x; last=x; }
    QVERIFY(first>=0 && last>first);
    for (int x=first;x<=last;++x) if (!waterAt(x)) ++dryColumns;
    QVERIFY2(dryColumns>200,"Water was painted across the dry middle cell");
    const QString artifactDir=qEnvironmentVariable("VFR_TEST_ARTIFACTS");
    if (!artifactDir.isEmpty()) QVERIFY(image.save(artifactDir+"/profile-dry-gap.png"));

    QVector<double> zero(profile.samples.size(),0);
    QVector<double> dry(profile.samples.size(),std::numeric_limits<double>::quiet_NaN());
    w.setCurrentDepths(zero,{},dry);
    image.fill(Qt::white);
    w.render(&image);
    for (int x=0;x<image.width();++x) QVERIFY(!waterAt(x));
}

void TestMeshProfileShorelineRender::exactFillHasNoCellSeams_data()
{
    QTest::addColumn<int>("pass");
    QTest::addColumn<int>("alpha");
    QTest::addColumn<double>("dpr");
    for (int pass=0;pass<3;++pass)
        for (int alpha : {120,255})
            for (double dpr : {1.0,1.5,2.0})
                QTest::newRow(qPrintable(QString("pass%1-alpha%2-dpr%3").arg(pass).arg(alpha).arg(dpr)))
                    << pass << alpha << dpr;
}

void TestMeshProfileShorelineRender::exactFillHasNoCellSeams()
{
    QFETCH(int,pass);
    QFETCH(int,alpha);
    QFETCH(double,dpr);
    MeshProfilePlotOptions opts;
    const QBrush brush(QColor(85,168,230,alpha));
    opts.setDepthFillBrush(brush); opts.setMaxEnvelopeBrush(brush); opts.setSoilFill(brush);
    opts.setShowDepthFill(pass==0); opts.setShowMaxEnvelopeFill(pass==1);
    opts.setShowWseLine(false); opts.setShowMaxEnvelopeLine(false);
    opts.setGroundLinePen(QPen(Qt::NoPen)); opts.setShowCellBoundaries(false);
    opts.setLegendVisible(false); opts.setShowTimeLabel(false);
    auto render=[&](int cells) {
        ProfileSection::Section profile;
        profile.hasResults=true; profile.exactWaterGeometry=true;
        for (int cell=0;cell<cells;++cell)
            for (int end=0;end<2;++end) {
                ProfileSection::Sample s;
                s.chainage=12.0*(cell+end)/cells;
                s.ground=0.4+0.01*s.chainage;
                s.signedDepthNow=1.2-0.02*s.chainage-s.ground;
                s.signedMaxDepth=s.signedDepthNow;
                s.depthNow=s.maxDepth=s.signedDepthNow;
                s.breakBefore=end==0;
                profile.samples.push_back(s);
            }
        MeshProfilePlotWidget w;
        w.setOptions(&opts); w.resize(703,360); w.setProfile(profile);
        w.setAxisEdgeValue(MeshProfilePlotWidget::AxisEdge::XMinimum,0);
        w.setAxisEdgeValue(MeshProfilePlotWidget::AxisEdge::XMaximum,12);
        w.setAxisEdgeValue(MeshProfilePlotWidget::AxisEdge::YMinimum,-0.5);
        w.setAxisEdgeValue(MeshProfilePlotWidget::AxisEdge::YMaximum,2);
        QImage image(QSize(qRound(w.width()*dpr),qRound(w.height()*dpr)),QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(dpr); image.fill(Qt::white); w.render(&image);
        return image;
    };
    const QImage reference=render(1), partitioned=render(13);
    const QString dir=qEnvironmentVariable("VFR_TEST_ARTIFACTS");
    if (!dir.isEmpty() && pass==0 && alpha==120 && dpr==1.0) {
        QVERIFY(reference.save(dir+"/profile-seam-reference.png"));
        QVERIFY(partitioned.save(dir+"/profile-cell-seams.png"));
    }
    // Compare fill interiors, away from the physical shoreline/ground edge.
    // Partitioning identical geometry must not change opacity at cell edges.
    int maxDifference=0;
    const int center=pass==2 ? 260 : 170;
    for (int y=qRound((center-12)*dpr);y<qRound((center+12)*dpr);++y)
        for (int x=qRound(90*dpr);x<qRound(675*dpr);++x) {
            const auto a=reference.pixelColor(x,y), b=partitioned.pixelColor(x,y);
            maxDifference=std::max({maxDifference,std::abs(a.red()-b.red()),
                std::abs(a.green()-b.green()),std::abs(a.blue()-b.blue())});
        }
    QVERIFY2(maxDifference<=1,qPrintable(QString("Cell boundaries changed fill color by %1/255").arg(maxDifference)));
}

QTEST_MAIN(TestMeshProfileShorelineRender)
#include "test_meshprofile_shoreline_render.moc"
