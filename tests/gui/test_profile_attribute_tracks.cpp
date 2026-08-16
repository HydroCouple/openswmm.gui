// Profile attribute tracks — the synced attribute-chart pane below the
// profile plot (workplans/PROFILE_ATTRIBUTE_TRACKS_PLAN_2026-08-16.md).
//
// Covers the pieces that run without an engine .out file or a results
// layer:
//   - ProfileAttributeTrackOptions: generic accessors ↔ Q_PROPERTY parity,
//     one changed() per edit, QSettings round-trip;
//   - ProfileAttributeTracksWidget: the pixel↔virtual-x mapping (the
//     alignment contract with ProfilePlotWidget — same margins, same
//     linear map), track sizing, and gap handling in painting inputs;
//   - ProfileAttributeSampler::isNodeAttribute/isTrackableAttribute.
//
// The sampler's engine path and the real pixel alignment against a live
// profile plot need a .out file + display — covered by the manual steps in
// workplans/PROFILE_ATTRIBUTE_TRACKS_VERIFICATION.md.
#include <QtTest/QtTest>

#include <QPen>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "plot/profileattributesampler.h"
#include "plot/profileattributetrackoptions.h"
#include "plot/profileattributetrackswidget.h"

using openswmmvis::plot::PlotAttribute;

class TestProfileAttributeTracks : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    // Options
    void genericAndPropertyAccessorsAgree();
    void oneChangedSignalPerEdit();
    void settingsRoundTrip();
    void visibleAttributesCanonicalOrder();

    // Sampler classification
    void attributeClassification();

    // Widget mapping / sizing
    void pixelMappingMatchesProfileFormula();
    void pixelMappingRoundTrips();
    void minimumHeightTracksCount();
    void followRangeDoesNotEcho();

private:
    QTemporaryDir mSettingsDir;
};

void TestProfileAttributeTracks::initTestCase()
{
    QVERIFY(mSettingsDir.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("openswmm-test"));
    QCoreApplication::setApplicationName(QStringLiteral("attrtracks-test"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       mSettingsDir.path());
}

void TestProfileAttributeTracks::init()
{
    QSettings s;
    s.clear();
}

void TestProfileAttributeTracks::genericAndPropertyAccessorsAgree()
{
    ProfileAttributeTrackOptions opt;
    QVERIFY(!opt.linkVelocityVisible());

    opt.setAttributeVisible(PlotAttribute::LinkVelocity, true);
    QVERIFY(opt.linkVelocityVisible());          // named getter sees it
    QVERIFY(opt.isAttributeVisible(PlotAttribute::LinkVelocity));

    const QPen pen(QColor(10, 20, 30), 3.0, Qt::DashLine);
    opt.setNodeDepthPen(pen);                    // named setter
    QCOMPARE(opt.penFor(PlotAttribute::NodeDepth), pen);   // generic getter

    // And via the meta-object, the way QPropertyModel edits them.
    QVERIFY(opt.setProperty("linkFlowVisible", true));
    QVERIFY(opt.isAttributeVisible(PlotAttribute::LinkFlow));
    QCOMPARE(opt.property("nodeDepthPen").value<QPen>(), pen);
}

void TestProfileAttributeTracks::oneChangedSignalPerEdit()
{
    ProfileAttributeTrackOptions opt;
    QSignalSpy spy(&opt, &ProfileAttributeTrackOptions::changed);

    opt.setAttributeVisible(PlotAttribute::NodeDepth, true);
    QCOMPARE(spy.count(), 1);
    opt.setAttributeVisible(PlotAttribute::NodeDepth, true);   // no-op
    QCOMPARE(spy.count(), 1);

    opt.setTrackHeightPx(150);
    QCOMPARE(spy.count(), 2);
    opt.setTrackHeightPx(150);                                 // no-op
    QCOMPARE(spy.count(), 2);

    // Out-of-range values clamp; a clamp to the current value is a no-op.
    opt.setEnvelopeOpacity(2.0);   // clamps to 1.0
    QCOMPARE(spy.count(), 3);
    opt.setEnvelopeOpacity(1.5);   // clamps to 1.0 again — no change
    QCOMPARE(spy.count(), 3);
}

void TestProfileAttributeTracks::settingsRoundTrip()
{
    const QPen fancy(QColor(200, 100, 50), 2.5, Qt::DotLine);
    {
        ProfileAttributeTrackOptions opt;
        opt.setAttributeVisible(PlotAttribute::LinkVelocity, true);
        opt.setAttributeVisible(PlotAttribute::NodeDepth, true);
        opt.setPenFor(PlotAttribute::LinkVelocity, fancy);
        opt.setTrackHeightPx(140);
        opt.setEnvelopesVisible(false);

        QSettings s;
        s.beginGroup(QStringLiteral("Tracks"));
        opt.writeTo(s);
        s.endGroup();
    }
    {
        ProfileAttributeTrackOptions fresh;
        QSignalSpy spy(&fresh, &ProfileAttributeTrackOptions::changed);
        QSettings s;
        s.beginGroup(QStringLiteral("Tracks"));
        fresh.readFrom(s);
        s.endGroup();

        QCOMPARE(spy.count(), 1);   // batched: exactly one changed() emission
        QVERIFY(fresh.isAttributeVisible(PlotAttribute::LinkVelocity));
        QVERIFY(fresh.isAttributeVisible(PlotAttribute::NodeDepth));
        QVERIFY(!fresh.isAttributeVisible(PlotAttribute::LinkFlow));
        QCOMPARE(fresh.penFor(PlotAttribute::LinkVelocity), fancy);
        QCOMPARE(fresh.trackHeightPx(), 140);
        QVERIFY(!fresh.envelopesVisible());
    }
}

void TestProfileAttributeTracks::visibleAttributesCanonicalOrder()
{
    ProfileAttributeTrackOptions opt;
    // Enable in scrambled order; the getter must report canonical order
    // (node attrs first, each list in presentation order).
    opt.setAttributeVisible(PlotAttribute::LinkCapacity, true);
    opt.setAttributeVisible(PlotAttribute::NodeOverflow, true);
    opt.setAttributeVisible(PlotAttribute::NodeDepth, true);
    opt.setAttributeVisible(PlotAttribute::LinkFlow, true);

    const auto vis = opt.visibleAttributes();
    QCOMPARE(vis.size(), 4);
    QCOMPARE(vis[0], PlotAttribute::NodeDepth);
    QCOMPARE(vis[1], PlotAttribute::NodeOverflow);
    QCOMPARE(vis[2], PlotAttribute::LinkFlow);
    QCOMPARE(vis[3], PlotAttribute::LinkCapacity);
    QVERIFY(opt.anyAttributeVisible());
}

void TestProfileAttributeTracks::attributeClassification()
{
    using namespace ProfileAttributeSampler;
    QVERIFY(isNodeAttribute(PlotAttribute::NodeDepth));
    QVERIFY(isNodeAttribute(PlotAttribute::NodeOverflow));
    QVERIFY(!isNodeAttribute(PlotAttribute::LinkVelocity));
    QVERIFY(isTrackableAttribute(PlotAttribute::LinkCapacity));
    QVERIFY(!isTrackableAttribute(PlotAttribute::SubcatchRunoff));
    QVERIFY(!isTrackableAttribute(PlotAttribute::Mesh2DDepth));
    QVERIFY(!isTrackableAttribute(PlotAttribute::SystemRainfall));
    QVERIFY(!isTrackableAttribute(PlotAttribute::Unknown));
}

void TestProfileAttributeTracks::pixelMappingMatchesProfileFormula()
{
    // The alignment contract: for the same widget width, margins and
    // visible range, the tracks pane's x-mapping must equal the profile
    // plot's dataToPixel x formula:
    //   px = left + (vx - xmin) / (xmax - xmin) * (width - left - right)
    ProfileAttributeTracksWidget w;
    w.resize(800, 300);
    w.setHorizontalMargins(64, 16);     // the profile's gutters
    w.setVisibleXRange(0.0, 1000.0);

    const double left  = 64.0;
    const double plotW = 800.0 - 64.0 - 16.0;

    QCOMPARE(w.pixelForVirtualX(0.0),    left);
    QCOMPARE(w.pixelForVirtualX(1000.0), left + plotW);
    QCOMPARE(w.pixelForVirtualX(500.0),  left + plotW * 0.5);

    // A zoomed range maps proportionally.
    w.setVisibleXRange(250.0, 750.0);
    QCOMPARE(w.pixelForVirtualX(250.0), left);
    QCOMPARE(w.pixelForVirtualX(500.0), left + plotW * 0.5);
}

void TestProfileAttributeTracks::pixelMappingRoundTrips()
{
    ProfileAttributeTracksWidget w;
    w.resize(640, 200);
    w.setHorizontalMargins(64, 16);
    w.setVisibleXRange(-50.0, 4321.0);

    for (double vx : {-50.0, 0.0, 123.456, 4000.0, 4321.0}) {
        const double px = w.pixelForVirtualX(vx);
        QVERIFY2(std::abs(w.virtualXForPixel(px) - vx) < 1e-6,
                 qPrintable(QStringLiteral("round trip failed for %1").arg(vx)));
    }
}

void TestProfileAttributeTracks::minimumHeightTracksCount()
{
    ProfileAttributeTrackOptions opt;
    opt.setTrackHeightPx(100);

    ProfileAttributeTracksWidget w;
    w.setOptions(&opt);
    QCOMPARE(w.minimumHeight(), 0);     // no tracks → collapses to nothing

    ProfileAttributeTracksWidget::Track t1;
    t1.attribute = PlotAttribute::NodeDepth;
    t1.title = QStringLiteral("Depth (ft)");
    ProfileAttributeTracksWidget::Track t2;
    t2.attribute = PlotAttribute::LinkVelocity;
    t2.isNodeAttribute = false;
    t2.title = QStringLiteral("Velocity (ft/s)");

    w.setTracks({t1, t2});
    QCOMPARE(w.trackCount(), 2);
    QVERIFY(w.minimumHeight() > 2 * 100);   // 2 tracks + gaps + axis strip

    w.setTracks({});
    QCOMPARE(w.minimumHeight(), 0);
}

void TestProfileAttributeTracks::followRangeDoesNotEcho()
{
    // setVisibleXRange is the "follow the profile" entry point; it must
    // never re-emit visibleXRangeChanged, or the host's guard would have
    // to absorb an echo on every profile pan.
    ProfileAttributeTracksWidget w;
    QSignalSpy spy(&w, &ProfileAttributeTracksWidget::visibleXRangeChanged);
    w.setVisibleXRange(10.0, 90.0);
    w.setVisibleXRange(20.0, 80.0);
    QCOMPARE(spy.count(), 0);
}

QTEST_MAIN(TestProfileAttributeTracks)
#include "test_profile_attribute_tracks.moc"
