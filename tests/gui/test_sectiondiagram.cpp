/*!
 * \file   test_sectiondiagram.cpp
 * \brief  Slices SP.2 / SP.6 — diagram model, painter and LID layer stacks.
 *
 * The painter is exercised through SectionPreviewWidget::renderToImage() under
 * the offscreen platform: the assertions are that it produces non-blank output
 * for a real model, stays blank-but-alive for an empty one, and never crashes
 * at degenerate sizes (a dock dragged down to a sliver is a real user action,
 * not a hypothetical).
 */

#include <QtTest>

#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <QPalette>

#include <algorithm>
#include <cmath>

#include "ui/sectionview/lidlayerdiagram.h"
#include "ui/sectionview/sectiondiagram.h"
#include "ui/sectionview/sectionpreviewwidget.h"

using namespace openswmmvis::sectionview;

namespace {

//! A minimal but complete model: one box, one dimension, one leader.
SectionDiagramModel makeSquareModel()
{
    SectionDiagramModel m;
    m.title    = QStringLiteral("T-1");
    m.subtitle = QStringLiteral("TEST");
    m.footer   = QStringLiteral("footer text");

    DiagramPoly box;
    box.role = DiagramRole::Conduit;
    box.pts << QPointF(-1, 0) << QPointF(1, 0) << QPointF(1, 2) << QPointF(-1, 2);
    m.polys << box;

    m.dims    << DiagramDim{ QPointF(1, 0), QPointF(1, 2),
                             QStringLiteral("2.000 m"), -24.0, false };
    m.leaders << DiagramLeader{ QPointF(0, 0), QStringLiteral("invert"),
                                QPointF(-50, 20) };
    return m;
}

//! Count pixels differing from the background — a cheap "did it draw?" probe.
int inkPixels(const QImage &img, const QColor &bg)
{
    int n = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            if (img.pixelColor(x, y) != bg) ++n;
    return n;
}

} // namespace

class TestSectionDiagram : public QObject
{
    Q_OBJECT

private slots:
    void emptyModelIsEmpty();
    void computeBoundsCoversEveryDrawable();
    void computeBoundsNeverDegenerate();
    void widgetRendersContent();
    void widgetRendersPlaceholderWhenEmpty();
    void widgetSurvivesDegenerateSizes();
    void widgetSurvivesDegenerateSizes_data();
    void rolesResolveDistinctColors();

    void lidLayerStacksMatchType();
    void lidLayerStacksMatchType_data();
    void lidUnknownThicknessRendersAsUnknown();
    void lidDiagramHasOneBoxPerLayerPlusNative();
    void lidDrainOnlyForTypesThatHaveOne();
    void lidLayersCarryMaterialTextures();
    void lidPlantedTypesGetVegetation();
    void lidPlantedTypesGetVegetation_data();
    void lidDrainIsDrawnAsAPipe();

    // Zoom / pan.
    void viewportDefaultsToFit();
    void zoomChangesTheDrawing();
    void zoomToExtentsRestoresTheFit();
    void panShiftsWithoutRescaling();
    void setModelKeepsTheView();
    void zoomIsClamped();

    // Vertical exaggeration.
    void exaggerationSnapsToConventionalRatios();
    void exaggerationIsIndependentOfPaneHeight();
    void exaggerationStaysTrueScaleWhenLegible();
    void exaggerationIsCapped();
    void explicitExaggerationIsHonoured();
    void legacyFitIsUntouchedWithoutATarget();
    void exaggeratedContentStillFits();
    void exaggeratedContentStillFits_data();

    // Plan-view inset placement.
    void planInsetsNeverReachIntoTheHeader();
    void planInsetsNeverReachIntoTheFooterStrip();
    void planInsetsCostNoWidthWhenTheFitHasVerticalSlack();
    void planInsetsAreParkedInTheLabelMarginNotCarvedOutOfTheDrawing();
    void planSpokeLabelsGetRoomForARealLinkName();
    void planInsetTitleIsNotCutToTheDialWidth();
};

// ---------------------------------------------------------------------------
// Model
// ---------------------------------------------------------------------------

void TestSectionDiagram::emptyModelIsEmpty()
{
    SectionDiagramModel m;
    QVERIFY(m.isEmpty());
    QVERIFY(m.computeBounds().isNull());

    // Dimensions and leaders alone are annotations, not content: a model with
    // nothing to annotate must still report empty so the widget shows its
    // explanatory text instead of a lone floating arrow.
    m.dims << DiagramDim{ QPointF(0, 0), QPointF(0, 1), QStringLiteral("x"), 10.0, false };
    QVERIFY(m.isEmpty());
}

void TestSectionDiagram::computeBoundsCoversEveryDrawable()
{
    SectionDiagramModel m;
    DiagramPoly p;
    p.pts << QPointF(0, 0) << QPointF(1, 1);
    m.polys << p;
    m.grounds << DiagramGround{ -5.0, 5.0, 3.0 };
    m.leaders << DiagramLeader{ QPointF(0.0, -2.0), QStringLiteral("l"), {} };

    const QRectF b = m.computeBounds();
    QCOMPARE(b.left(),   -5.0);
    QCOMPARE(b.right(),   5.0);
    QCOMPARE(b.top(),    -2.0);
    QCOMPARE(b.bottom(),  3.0);
}

void TestSectionDiagram::computeBoundsNeverDegenerate()
{
    // A single horizontal ground line has zero height; the fit maths divides
    // by the extent, so bounds must never come back flat.
    SectionDiagramModel m;
    m.grounds << DiagramGround{ 0.0, 10.0, 1.0 };

    const QRectF b = m.computeBounds();
    QVERIFY(b.height() > 0.0);
    QVERIFY(b.width()  > 0.0);
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

void TestSectionDiagram::widgetRendersContent()
{
    SectionPreviewWidget w;
    w.setModel(makeSquareModel());

    const QImage img = w.renderToImage(QSize(400, 320));
    QCOMPARE(img.size(), QSize(400, 320));

    const int ink = inkPixels(img, w.palette().color(QPalette::Base));
    QVERIFY2(ink > 200, qPrintable(QStringLiteral("only %1 non-background px").arg(ink)));
}

void TestSectionDiagram::widgetRendersPlaceholderWhenEmpty()
{
    SectionPreviewWidget w;
    w.setPlaceholderText(QStringLiteral("nothing selected"));
    w.setModel(SectionDiagramModel{});

    const QImage img = w.renderToImage(QSize(320, 240));
    // The placeholder is text, so *some* ink — but far less than a drawing.
    const int ink = inkPixels(img, w.palette().color(QPalette::Base));
    QVERIFY(ink > 0);
    QVERIFY(ink < 320 * 240 / 4);
}

void TestSectionDiagram::widgetSurvivesDegenerateSizes_data()
{
    QTest::addColumn<QSize>("size");
    QTest::newRow("sliver-wide")   << QSize(400, 3);
    QTest::newRow("sliver-tall")   << QSize(3, 400);
    QTest::newRow("tiny")          << QSize(1, 1);
    QTest::newRow("narrow-dock")   << QSize(140, 200);
    QTest::newRow("no-annotation") << QSize(210, 160);
}

void TestSectionDiagram::widgetSurvivesDegenerateSizes()
{
    QFETCH(QSize, size);

    SectionPreviewWidget w;
    w.setModel(makeSquareModel());
    // The assertion is that this returns at all: the decluttering path drops
    // leaders, then dimensions, then bails before the fit divides by zero.
    const QImage img = w.renderToImage(size);
    QCOMPARE(img.size(), size);
}

void TestSectionDiagram::rolesResolveDistinctColors()
{
    const QPalette pal;
    // Roles that sit next to each other in a LID stack must be tellable apart,
    // otherwise the layer diagram reads as one undifferentiated block.
    QVERIFY(diagramFillColor(DiagramRole::Media, pal)
            != diagramFillColor(DiagramRole::Gravel, pal));
    QVERIFY(diagramFillColor(DiagramRole::Vegetation, pal)
            != diagramFillColor(DiagramRole::Soil, pal));
    QVERIFY(diagramFillColor(DiagramRole::Conduit, pal)
            != diagramFillColor(DiagramRole::Structure, pal));
}

// ---------------------------------------------------------------------------
// LID layer diagram
// ---------------------------------------------------------------------------

void TestSectionDiagram::lidLayerStacksMatchType_data()
{
    QTest::addColumn<int>("type");
    QTest::addColumn<int>("layerCount");
    QTest::addColumn<bool>("hasSoil");
    QTest::addColumn<bool>("hasStorage");

    QTest::newRow("bio-cell")     << 0 << 3 << true  << true;
    QTest::newRow("rain-garden")  << 1 << 2 << true  << false;
    QTest::newRow("green-roof")   << 2 << 3 << true  << false;
    QTest::newRow("infil-trench") << 3 << 2 << false << true;
    QTest::newRow("perm-pave")    << 4 << 4 << true  << true;
    QTest::newRow("rain-barrel")  << 5 << 1 << false << true;
    QTest::newRow("rooftop")      << 6 << 1 << false << false;
    QTest::newRow("veg-swale")    << 7 << 1 << false << false;
}

void TestSectionDiagram::lidLayerStacksMatchType()
{
    QFETCH(int, type);
    QFETCH(int, layerCount);
    QFETCH(bool, hasSoil);
    QFETCH(bool, hasStorage);

    const QVector<LidLayer> layers = lidLayersFor(static_cast<LidType>(type));
    QCOMPARE(int(layers.size()), layerCount);
    QCOMPARE(layers.contains(LidLayer::Soil),    hasSoil);
    QCOMPARE(layers.contains(LidLayer::Storage), hasStorage);
}

void TestSectionDiagram::lidUnknownThicknessRendersAsUnknown()
{
    // The engine has LID setters but no getters, so a control loaded from a
    // file has no readable thicknesses. Those layers must be flagged, not
    // silently drawn as if they were zero-thickness.
    LidDiagramInput in;
    in.type = LidType::BioCell;
    in.name = QStringLiteral("BC-1");
    // No thicknesses set at all.

    const SectionDiagramModel m = buildLidLayerDiagram(in);
    QVERIFY(!m.isEmpty());

    int unknown = 0;
    for (const DiagramPoly &p : m.polys) if (p.unknown) ++unknown;
    QCOMPARE(unknown, 3);   // surface + soil + storage, all unknown

    // Once a value is entered that layer stops being flagged.
    in.soilThickness = 0.5;
    const SectionDiagramModel m2 = buildLidLayerDiagram(in);
    int unknown2 = 0;
    for (const DiagramPoly &p : m2.polys) if (p.unknown) ++unknown2;
    QCOMPARE(unknown2, 2);
}

void TestSectionDiagram::lidDiagramHasOneBoxPerLayerPlusNative()
{
    LidDiagramInput in;
    in.type             = LidType::BioCell;
    in.surfaceStorage   = 0.15;
    in.soilThickness    = 0.50;
    in.storageThickness = 0.30;

    const SectionDiagramModel m = buildLidLayerDiagram(in);
    // 3 layers + the ponded water filling the surface layer + the native soil
    // block below the stack. (Ponding is drawn only when the surface layer has
    // a known storage depth, which it does here.)
    QCOMPARE(int(m.polys.size()), 5);
    // One thickness dimension per layer; native soil and the water are not
    // dimensioned, and no drain offset was given.
    QCOMPARE(int(m.dims.size()), 3);

    // Layers stack downward from y = 0 without overlapping. Collect the layer
    // boxes by their inset label rather than by index — the water and ornament
    // polys are interleaved and their positions are an implementation detail.
    double surfaceTop = 0.0, soilTop = 0.0, storageTop = 0.0;
    for (const DiagramPoly &p : m.polys) {
        if (p.insetLabel == QStringLiteral("Surface")) surfaceTop = p.pts.at(0).y();
        if (p.insetLabel == QStringLiteral("Soil"))    soilTop    = p.pts.at(0).y();
        if (p.insetLabel == QStringLiteral("Storage")) storageTop = p.pts.at(0).y();
    }
    QVERIFY(surfaceTop > soilTop);
    QVERIFY(soilTop    > storageTop);
}

void TestSectionDiagram::lidDrainOnlyForTypesThatHaveOne()
{
    QVERIFY(lidHasDrain(LidType::BioCell));
    QVERIFY(lidHasDrain(LidType::RainBarrel));
    // A rain garden is a bio-cell without the underdrain — drawing one would
    // misrepresent the model.
    QVERIFY(!lidHasDrain(LidType::RainGarden));
    QVERIFY(!lidHasDrain(LidType::VegSwale));
}

// ---------------------------------------------------------------------------
// LID richness
// ---------------------------------------------------------------------------

void TestSectionDiagram::lidLayersCarryMaterialTextures()
{
    LidDiagramInput in;
    in.type             = LidType::BioCell;
    in.surfaceStorage   = 0.15;
    in.soilThickness    = 0.50;
    in.storageThickness = 0.30;

    const SectionDiagramModel m = buildLidLayerDiagram(in);

    // Soil must be stippled and storage gravelled: colour alone does not
    // survive greyscale printing, and it is the material — not the hue — that
    // tells a reviewer which layer they are looking at.
    bool stipple = false, gravel = false, hatch = false;
    for (const DiagramPoly &p : m.polys) {
        if (p.texture == DiagramTexture::Stipple) stipple = true;
        if (p.texture == DiagramTexture::Gravel)  gravel  = true;
        if (p.texture == DiagramTexture::Hatch)   hatch   = true;   // native soil
    }
    QVERIFY(stipple);
    QVERIFY(gravel);
    QVERIFY(hatch);

    // Permeable pavement swaps the pavement course to a paver pattern.
    LidDiagramInput pp;
    pp.type              = LidType::PermPavement;
    pp.surfaceStorage    = 0.01;
    pp.pavementThickness = 0.10;
    pp.soilThickness     = 0.15;
    pp.storageThickness  = 0.45;
    bool brick = false;
    for (const DiagramPoly &p : buildLidLayerDiagram(pp).polys)
        if (p.texture == DiagramTexture::Brick) brick = true;
    QVERIFY(brick);
}

void TestSectionDiagram::lidPlantedTypesGetVegetation_data()
{
    QTest::addColumn<int>("type");
    QTest::addColumn<bool>("planted");
    QTest::addColumn<bool>("grass");

    QTest::newRow("bio-cell")     << 0 << true  << false;  // shrubs
    QTest::newRow("rain-garden")  << 1 << true  << false;
    QTest::newRow("green-roof")   << 2 << true  << true;   // sedum / turf
    QTest::newRow("infil-trench") << 3 << false << false;  // stone-filled
    QTest::newRow("perm-pave")    << 4 << false << false;
    QTest::newRow("rain-barrel")  << 5 << false << false;
    QTest::newRow("veg-swale")    << 7 << true  << true;
}

void TestSectionDiagram::lidPlantedTypesGetVegetation()
{
    QFETCH(int, type);
    QFETCH(bool, planted);
    QFETCH(bool, grass);

    LidDiagramInput in;
    in.type             = static_cast<LidType>(type);
    in.surfaceStorage   = 0.10;
    in.soilThickness    = 0.40;
    in.storageThickness = 0.30;

    const SectionDiagramModel m = buildLidLayerDiagram(in);
    QCOMPARE(!m.vegetation.isEmpty(), planted);
    if (planted) {
        QVERIFY(m.vegetation.first().count > 0);
        QVERIFY(m.vegetation.first().height > 0.0);
        QCOMPARE(m.vegetation.first().grass, grass);
    }
}

void TestSectionDiagram::lidDrainIsDrawnAsAPipe()
{
    LidDiagramInput in;
    in.type             = LidType::BioCell;
    in.surfaceStorage   = 0.15;
    in.soilThickness    = 0.50;
    in.storageThickness = 0.30;
    in.drainOffset      = 0.08;

    const SectionDiagramModel m = buildLidLayerDiagram(in);
    // The underdrain is a pipe the user is specifying an offset TO, so it is
    // drawn as a perforated pipe in section rather than as a line.
    QCOMPARE(m.circles.size(), 1);
    QVERIFY(m.circles.first().perforated);
    QVERIFY(m.circles.first().radius > 0.0);

    // A type with no underdrain must not sprout one.
    LidDiagramInput swale;
    swale.type           = LidType::VegSwale;
    swale.surfaceStorage = 0.20;
    QVERIFY(buildLidLayerDiagram(swale).circles.isEmpty());
}

// ---------------------------------------------------------------------------
// Zoom / pan
// ---------------------------------------------------------------------------

void TestSectionDiagram::viewportDefaultsToFit()
{
    SectionPreviewWidget w;
    QVERIFY(w.viewport().isIdentity());
    QCOMPARE(w.viewport().zoom, 1.0);
}

void TestSectionDiagram::zoomChangesTheDrawing()
{
    SectionPreviewWidget w;
    w.setModel(makeSquareModel());

    const QImage fitted = w.renderToImage(QSize(400, 320));
    const int inkFitted = inkPixels(fitted, w.palette().color(QPalette::Base));

    w.zoomBy(3.0, QPointF(200, 160));
    QVERIFY(w.viewport().zoom > 1.0);

    const QImage zoomed = w.renderToImage(QSize(400, 320));
    QVERIFY(zoomed != fitted);
    // Geometry grows with zoom, so a filled box covers more of the canvas.
    QVERIFY(inkPixels(zoomed, w.palette().color(QPalette::Base)) > inkFitted);
}

void TestSectionDiagram::zoomToExtentsRestoresTheFit()
{
    SectionPreviewWidget w;
    w.setModel(makeSquareModel());
    const QImage fitted = w.renderToImage(QSize(400, 320));

    w.zoomBy(4.0, QPointF(120, 90));
    w.setViewport({ w.viewport().zoom, QPointF(37.0, -12.0) });
    QVERIFY(!w.viewport().isIdentity());

    w.zoomToExtents();
    QVERIFY(w.viewport().isIdentity());
    // Middle double-click must land exactly back on the automatic fit — an
    // "almost" reset is worse than none, because it accumulates.
    QCOMPARE(w.renderToImage(QSize(400, 320)), fitted);
}

void TestSectionDiagram::panShiftsWithoutRescaling()
{
    SectionPreviewWidget w;
    w.setModel(makeSquareModel());

    DiagramViewport v;
    v.panPx = QPointF(40.0, 0.0);
    w.setViewport(v);

    QCOMPARE(w.viewport().zoom, 1.0);
    QCOMPARE(w.viewport().panPx, QPointF(40.0, 0.0));

    // Same model, same scale, different position: compare against an unpanned
    // widget holding the SAME model, so the only difference under test is the
    // pan (comparing against a default-constructed widget would pass trivially
    // because that one is empty).
    SectionPreviewWidget ref;
    ref.setModel(makeSquareModel());
    QVERIFY(w.renderToImage(QSize(400, 320)) != ref.renderToImage(QSize(400, 320)));
}

void TestSectionDiagram::setModelKeepsTheView()
{
    SectionPreviewWidget w;
    w.setModel(makeSquareModel());
    w.zoomBy(2.5, QPointF(200, 160));
    const DiagramViewport before = w.viewport();

    // Hosts rebuild the model on every keystroke; that must not yank the view
    // back to fit while the user is reading a zoomed-in dimension.
    w.setModel(makeSquareModel());
    QCOMPARE(w.viewport().zoom, before.zoom);
    QCOMPARE(w.viewport().panPx, before.panPx);
}

void TestSectionDiagram::zoomIsClamped()
{
    SectionPreviewWidget w;
    w.setModel(makeSquareModel());

    for (int i = 0; i < 200; ++i) w.zoomBy(2.0, QPointF(200, 160));
    QVERIFY(w.viewport().zoom <= 200.0);

    for (int i = 0; i < 400; ++i) w.zoomBy(0.5, QPointF(200, 160));
    QVERIFY(w.viewport().zoom >= 0.05);

    // Degenerate factors must be ignored, not propagated into the transform.
    const DiagramViewport before = w.viewport();
    w.zoomBy(0.0, QPointF(200, 160));
    w.zoomBy(-1.0, QPointF(200, 160));
    QCOMPARE(w.viewport().zoom, before.zoom);
}

// ---------------------------------------------------------------------------
// Vertical exaggeration
//
// The widget reports the ratio it actually drew at, so these assert it
// directly. An earlier draft inferred it from the ink bounding box, which
// silently measured the exaggeration NOTE — anchored to the pane bottom — and
// so grew with the pane no matter what the fit did.
// ---------------------------------------------------------------------------

namespace {

//! A 120 m reach at 0.25 % with ~4.5 m of vertical content: the case that
//! prompted this. Naturally 26.7:1, so a 6:1 target asks for 4.4x → snaps 4x.
SectionDiagramModel makeReachModel()
{
    SectionDiagramModel m;
    m.uniformScale            = false;
    m.maxVerticalExaggeration = 10.0;
    m.targetDrawnAspect       = 6.0;
    m.annotateExaggeration    = true;

    DiagramPoly barrel;
    barrel.role = DiagramRole::Conduit;
    barrel.pts << QPointF(0.0,   100.30) << QPointF(120.0, 100.00)
               << QPointF(120.0,  99.40) << QPointF(0.0,    99.70);
    m.polys << barrel;
    m.polylines << DiagramPolyline{
        QPolygonF({ QPointF(0.0, 103.9), QPointF(120.0, 103.6) }),
        DiagramRole::Muted, true, QString() };
    return m;
}

double exaggerationAt(SectionPreviewWidget &w, const QSize &pane)
{
    w.renderToImage(pane);
    return w.achievedVerticalExaggeration();
}

} // namespace

void TestSectionDiagram::exaggerationSnapsToConventionalRatios()
{
    SectionPreviewWidget w;
    w.setModel(makeReachModel());

    // 120 / 4.5 = 26.67 natural aspect; / 6 target = 4.44 → snapped DOWN to 4.
    // Snapping down matters: the drawing must never be more distorted than the
    // annotation claims.
    QCOMPARE(exaggerationAt(w, QSize(560, 300)), 4.0);
}

void TestSectionDiagram::exaggerationIsIndependentOfPaneHeight()
{
    // THE regression this guards. The old fit stretched the vertical to fill
    // the pane, so the same pipe read as ~1.3 % in a short dock and ~2.7 % in a
    // tall one. The ratio is now a property of the model, not of the pane.
    SectionPreviewWidget w;
    w.setModel(makeReachModel());

    const double shortPane = exaggerationAt(w, QSize(560, 260));
    const double tallPane  = exaggerationAt(w, QSize(560, 620));
    const double narrow    = exaggerationAt(w, QSize(300, 400));
    const double wide      = exaggerationAt(w, QSize(1100, 300));

    QCOMPARE(shortPane, tallPane);
    QCOMPARE(shortPane, narrow);
    QCOMPARE(shortPane, wide);   // width must not move it either
}

void TestSectionDiagram::exaggerationStaysTrueScaleWhenLegible()
{
    // A short, steep reach is already near the target aspect, so automatic
    // must not distort it at all — the ratio is clamped at 1.0 rather than
    // being allowed to COMPRESS the vertical and understate the slope.
    SectionDiagramModel m;
    m.uniformScale            = false;
    m.maxVerticalExaggeration = 10.0;
    m.targetDrawnAspect       = 6.0;

    DiagramPoly barrel;
    barrel.role = DiagramRole::Conduit;
    barrel.pts << QPointF(0.0, 3.0) << QPointF(15.0, 2.7)
               << QPointF(15.0, 2.1) << QPointF(0.0, 2.4);
    m.polys << barrel;
    m.polylines << DiagramPolyline{
        QPolygonF({ QPointF(0.0, 0.0), QPointF(15.0, 0.0) }),
        DiagramRole::Muted, false, QString() };

    SectionPreviewWidget w;
    w.setModel(m);
    // 15 / 3 = 5:1 natural, below the 6:1 target → no exaggeration.
    QCOMPARE(exaggerationAt(w, QSize(560, 300)), 1.0);
}

void TestSectionDiagram::exaggerationIsCapped()
{
    // A very long, shallow reach cannot be drawn usefully at true scale, but
    // the distortion still has a stated ceiling.
    SectionDiagramModel m = makeReachModel();
    m.polys.clear();
    DiagramPoly barrel;
    barrel.role = DiagramRole::Conduit;
    barrel.pts << QPointF(0.0, 5.0) << QPointF(800.0, 4.2)
               << QPointF(800.0, 3.6) << QPointF(0.0, 4.4);
    m.polys << barrel;
    m.polylines.clear();
    m.polylines << DiagramPolyline{
        QPolygonF({ QPointF(0.0, 0.0), QPointF(800.0, 0.0) }),
        DiagramRole::Muted, false, QString() };

    SectionPreviewWidget w;
    w.setModel(m);
    QCOMPARE(exaggerationAt(w, QSize(560, 300)), 10.0);   // == maxVerticalExaggeration
}

void TestSectionDiagram::explicitExaggerationIsHonoured()
{
    SectionPreviewWidget w;
    for (double ve : { 1.0, 2.0, 5.0, 10.0, 20.0 }) {
        SectionDiagramModel m = makeReachModel();
        m.verticalExaggeration = ve;      // overrides the automatic choice
        w.setModel(m);
        QCOMPARE(exaggerationAt(w, QSize(560, 300)), ve);
    }
}

void TestSectionDiagram::legacyFitIsUntouchedWithoutATarget()
{
    // Node profiles and LID stacks put an ARBITRARY unit on x, so a V:H ratio
    // there is arithmetic on nothing. They leave targetDrawnAspect at 0 and
    // must keep filling the pane, unsnapped and uncapped — snapping them would
    // silently resize the drawing (a LID stack whose fill ratio is 0.47 would
    // round to 1.0 and lose half its width).
    LidDiagramInput in;
    in.type             = LidType::BioCell;
    in.surfaceStorage   = 0.15;
    in.soilThickness    = 0.50;
    in.storageThickness = 0.30;

    const SectionDiagramModel lid = buildLidLayerDiagram(in);
    QVERIFY(!lid.uniformScale);
    QCOMPARE(lid.targetDrawnAspect, 0.0);
    QCOMPARE(lid.maxVerticalExaggeration, 0.0);
    QVERIFY(!lid.annotateExaggeration);

    // Filling the pane means the ratio DOES track the pane — which is correct
    // here, and is exactly what must not happen to a link profile.
    SectionPreviewWidget w;
    w.setModel(lid);
    const double wide = exaggerationAt(w, QSize(600, 240));
    const double tall = exaggerationAt(w, QSize(600, 600));
    QVERIFY(tall > wide);
}

void TestSectionDiagram::exaggeratedContentStillFits_data()
{
    QTest::addColumn<double>("ve");
    QTest::addColumn<QSize>("pane");
    for (double ve : { 1.0, 2.0, 5.0, 10.0, 50.0 }) {
        QTest::addRow("ve%.0f-wide", ve)   << ve << QSize(560, 260);
        QTest::addRow("ve%.0f-tall", ve)   << ve << QSize(300, 620);
        QTest::addRow("ve%.0f-square", ve) << ve << QSize(360, 360);
    }
}

void TestSectionDiagram::exaggeratedContentStillFits()
{
    QFETCH(double, ve);
    QFETCH(QSize, pane);

    // Holding a ratio means one axis stops binding and the other must take
    // over. Assert against the FIT RECT, not the image: the painter clips to
    // the drawing area, so comparing ink to the image bounds could never fail.
    SectionDiagramModel m = makeReachModel();
    m.verticalExaggeration = ve;

    SectionPreviewWidget w;
    w.setModel(m);
    w.renderToImage(pane);

    QCOMPARE(w.achievedVerticalExaggeration(), ve);

    const QRectF fit = w.lastFitRect();
    QVERIFY(fit.isValid());
    QVERIFY2(fit.width() > 1.0 && fit.height() > 1.0,
             "fit rect collapsed — the drawing has nowhere to go");
    QVERIFY2(pane.width() >= fit.width() && pane.height() >= fit.height(),
             "fit rect escapes the pane");
}

QTEST_MAIN(TestSectionDiagram)
// ---------------------------------------------------------------------------
// Plan-view insets
// ---------------------------------------------------------------------------

namespace {

//! \p m plus an inlet/outlet compass pair, as a link profile carries.
SectionDiagramModel withPlanInsets(SectionDiagramModel m)
{
    // One spoke straight UP on each dial: that is the bearing whose label sits
    // furthest above the ring, and so the one that reaches the header first.
    const QVector<PlanSpoke> up{ {  90.0, QStringLiteral("B1"), true },
                                 {  20.0, QStringLiteral("C1"), false } };
    const QVector<PlanSpoke> dn{ {  90.0, QStringLiteral("B2"), true },
                                 {  20.0, QStringLiteral("C1"), false } };
    m.planInsets << PlanInset{ up, QStringLiteral("Inlet · J1"),
                               PlanInset::Side::Left }
                 << PlanInset{ dn, QStringLiteral("Outlet · J2"),
                               PlanInset::Side::Right };
    return m;
}

//! Paint \p m into a \p pane-sized image, reporting the drawing's fit rect.
QImage paintAt(const SectionDiagramModel &m, const QSize &pane, QRectF *fitOut)
{
    QImage img(pane, QImage::Format_ARGB32_Premultiplied);
    const QPalette pal;
    img.fill(pal.color(QPalette::Base));
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    paintSectionDiagram(p, QRectF(QPointF(0, 0), QSizeF(pane)), m, pal,
                        DiagramViewport{}, fitOut, nullptr);
    return img;
}

//! The row range where two same-size renders differ. When the two models
//! differ only in one piece of text, these rows ARE the band holding that
//! text — found rather than assumed, so the probe survives the dials moving.
QPair<int, int> diffRows(const QImage &a, const QImage &b)
{
    int y0 = a.height(), y1 = -1;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x)
            if (a.pixelColor(x, y) != b.pixelColor(x, y)) {
                y0 = std::min(y0, y);
                y1 = std::max(y1, y);
                break;
            }
    return { y0, y1 };
}

//! A reach with leader labels on both ends — what actually sets the margins.
SectionDiagramModel makeLabelledReachModel()
{
    SectionDiagramModel m = makeReachModel();
    m.title    = QStringLiteral("C1 — Profile");
    m.subtitle = QStringLiteral("J1 → J2");
    m.leaders << DiagramLeader{ QPointF(0.0,   100.30),
                                QStringLiteral("Crown 100.30"), QPointF(-40, -20) }
              << DiagramLeader{ QPointF(0.0,    99.70),
                                QStringLiteral("Inv 99.70"),    QPointF(-40,  20) }
              << DiagramLeader{ QPointF(120.0, 100.00),
                                QStringLiteral("Crown 100.00"), QPointF( 40, -20) }
              << DiagramLeader{ QPointF(120.0,  99.40),
                                QStringLiteral("Inv 99.40"),    QPointF( 40,  20) };
    return m;
}

} // namespace

void TestSectionDiagram::planInsetsNeverReachIntoTheHeader()
{
    // The dials live at the BOTTOM of the pane now, so nothing they draw has
    // any business near the title or subtitle. Nothing above the drawing area
    // may change when the insets are added — the same whole-strip claim that
    // caught overhanging spoke labels when the dials lived up here.
    const QSize pane(560, 480);
    const SectionDiagramModel bare = makeLabelledReachModel();

    const QImage without = paintAt(bare, pane, nullptr);
    const QImage with    = paintAt(withPlanInsets(bare), pane, nullptr);

    // Vacuity guard: an inset that was dropped changes no pixel anywhere, and
    // would satisfy the header comparison for the wrong reason.
    int drawn = 0;
    for (int y = 40; y < pane.height(); ++y)
        for (int x = 0; x < pane.width(); ++x)
            if (without.pixelColor(x, y) != with.pixelColor(x, y)) ++drawn;
    QVERIFY2(drawn > 100, "no plan dials were drawn at all");

    // Title (18 px) + subtitle (14 px) below an 8 px top margin.
    for (int y = 0; y < 40; ++y)
        for (int x = 0; x < pane.width(); ++x)
            QVERIFY2(without.pixelColor(x, y) == with.pixelColor(x, y),
                     qPrintable(QStringLiteral("header pixel (%1,%2) changed "
                                               "when the plan insets were added")
                                    .arg(x).arg(y)));
}

void TestSectionDiagram::planInsetsNeverReachIntoTheFooterStrip()
{
    // The mirror of the header gate, for the dials' new home: the band is
    // carved off the BOTTOM of the drawing area, so the furniture at risk
    // from an overhanging spoke label is now the V:H note and the bottom
    // margin below the band. Nothing in that strip may change when the
    // insets are added.
    const QSize pane(560, 480);
    const SectionDiagramModel bare = makeLabelledReachModel();

    const QImage without = paintAt(bare, pane, nullptr);
    const QImage with    = paintAt(withPlanInsets(bare), pane, nullptr);

    // Vacuity guard, same as the header gate: a dropped inset changes no
    // pixel anywhere and would pass the strip comparison for the wrong
    // reason.
    int drawn = 0;
    for (int y = 0; y < pane.height() - 6; ++y)
        for (int x = 0; x < pane.width(); ++x)
            if (without.pixelColor(x, y) != with.pixelColor(x, y)) ++drawn;
    QVERIFY2(drawn > 100, "no plan dials were drawn at all");

    // Only the bottom MARGIN is invariant down here: the V:H note directly
    // above it legitimately re-renders when the band changes the drawing
    // height (a different fitted ratio is a different string), so the probe
    // stays below it. A label that punches through the note row into the
    // margin is still caught.
    for (int y = pane.height() - 6; y < pane.height(); ++y)
        for (int x = 0; x < pane.width(); ++x)
            QVERIFY2(without.pixelColor(x, y) == with.pixelColor(x, y),
                     qPrintable(QStringLiteral("bottom-margin pixel (%1,%2) "
                                               "changed when the plan insets "
                                               "were added")
                                    .arg(x).arg(y)));
}

void TestSectionDiagram::planInsetsCostNoWidthWhenTheFitHasVerticalSlack()
{
    // A capped exaggeration leaves the vertical short of the pane in a tall
    // dock. That slack is free real estate: the dials take a strip off the
    // top and the drawing keeps every pixel of its width.
    const QSize pane(520, 620);
    QRectF without, with;
    const QImage bare  = paintAt(makeLabelledReachModel(), pane, &without);
    const QImage dials = paintAt(withPlanInsets(makeLabelledReachModel()),
                                 pane, &with);

    // Vacuity guard: dropping the dials also costs no width.
    int drawn = 0;
    for (int y = 0; y < pane.height(); ++y)
        for (int x = 0; x < pane.width(); ++x)
            if (bare.pixelColor(x, y) != dials.pixelColor(x, y)) ++drawn;
    QVERIFY2(drawn > 100, "no plan dials were drawn at all");

    QVERIFY(without.width() > 0.0);
    QCOMPARE(with.width(), without.width());
}

void TestSectionDiagram::planInsetsAreParkedInTheLabelMarginNotCarvedOutOfTheDrawing()
{
    // THE regression this guards. A wide, short dock has no vertical slack to
    // pay for the strip, and the insets used to answer that by carving their
    // full width out of the drawing ON TOP of the margin the leader labels
    // already held — which left a 100 ft reach drawn across the middle third
    // of a 900 px pane. The dial belongs IN that margin, so the most it may
    // cost is the amount by which it overhangs one.
    const QSize pane(900, 300);
    QRectF without, with;
    const QImage bare  = paintAt(makeLabelledReachModel(), pane, &without);
    const QImage dials = paintAt(withPlanInsets(makeLabelledReachModel()),
                                 pane, &with);

    QVERIFY(without.width() > 0.0);
    QVERIFY2(with.width() > 0.0, "the drawing must survive the insets at all");

    // Vacuity guard: the dials have to actually BE there. Dropping them would
    // satisfy every width assertion below for the wrong reason.
    int changed = 0;
    for (int y = 40; y < 170; ++y)
        for (int x = 0; x < 110; ++x)
            if (bare.pixelColor(x, y) != dials.pixelColor(x, y)) ++changed;
    QVERIFY2(changed > 100, "no inlet dial was drawn in the left margin");

    // A dial is 96 px wide with a 10 px gap, and it is parked in the margin
    // the leader labels already hold — so it may push the drawing in only as
    // far as its own footprint, and not at all where the margin was already
    // wider than that. The old code carved its width out of the drawing ON TOP
    // of that margin, which is what left a reach drawn across the middle third
    // of a 900 px pane.
    const double ceiling = std::max(without.left(), 14.0 + 96.0 + 10.0);
    QVERIFY2(with.left() <= ceiling,
             qPrintable(QStringLiteral("drawing starts %1 px in; ceiling is %2")
                            .arg(with.left()).arg(ceiling)));
    const double rightCeiling =
        std::max(pane.width() - without.right(), 14.0 + 96.0 + 10.0);
    QVERIFY2(pane.width() - with.right() <= rightCeiling,
             qPrintable(QStringLiteral("drawing ends %1 px from the right edge; "
                                       "ceiling is %2")
                            .arg(pane.width() - with.right()).arg(rightCeiling)));
}

void TestSectionDiagram::planSpokeLabelsGetRoomForARealLinkName()
{
    // The dial is anchored to the pane edge, so a label ringed around it had
    // ~58 px to live in and was cut — "TRUNK_MAIN_1234" and "TRUNK_MAIN_9999"
    // both render as "TRUN…", which is the one thing a link label must never
    // do. Leadering the labels inboard is what buys the width back.
    const QSize pane(560, 480);

    auto renderWith = [&](const QString &name) {
        SectionDiagramModel m = makeLabelledReachModel();
        const QVector<PlanSpoke> up{ { 90.0, name, true } };
        m.planInsets << PlanInset{ up, QStringLiteral("Inlet · J1"),
                                   PlanInset::Side::Left };
        return paintAt(m, pane, nullptr);
    };

    // The dial band below the drawing holds the label and nothing else in the
    // pane's left half, so the rightmost ink in its rows IS the label's end.
    auto labelReach = [&](const QImage &img, QPair<int, int> rows) {
        int maxX = 0;
        for (int y = rows.first; y <= rows.second; ++y)
            for (int x = 0; x < 280; ++x)
                if (img.pixelColor(x, y) != img.pixelColor(0, 0)) maxX = std::max(maxX, x);
        return maxX;
    };

    const QString kShort = QStringLiteral("C1");
    const QString kLong  = QStringLiteral("TRUNK_MAIN_1234");

    const QImage shortImg = renderWith(kShort);
    const QImage longImg  = renderWith(kLong);
    // The dial band sits at the bottom of the pane; find its label rows
    // rather than assume them — the two renders differ only where the label
    // text (characters, plate, leader) lands.
    const auto rows = diffRows(shortImg, longImg);
    QVERIFY2(rows.second >= rows.first, "no spoke label was drawn to measure");
    // The placement contract itself: the cardinal-direction dials live at
    // the BOTTOM of the pane, so their label rows sit in its lower half.
    QVERIFY2(rows.first > pane.height() / 2,
             qPrintable(QStringLiteral("the dial's label rows start at y=%1 "
                                       "of %2 — the compass is not at the "
                                       "bottom")
                            .arg(rows.first).arg(pane.height())));
    const int shortReach = labelReach(shortImg, rows);
    const int longReach  = labelReach(longImg, rows);

    // The bar is the string's OWN width, measured in the font the painter
    // uses. Anything less means characters were dropped — a fixed pixel bar
    // passes happily on an elided "TRUNK_…", which is still the bug.
    QImage probe(1, 1, QImage::Format_ARGB32_Premultiplied);
    QPainter pp(&probe);
    const QFontMetricsF fm(pp.font());
    const double needed = fm.horizontalAdvance(kLong) - fm.horizontalAdvance(kShort);

    QVERIFY2(shortReach > 0, "no dial was drawn to measure");
    QVERIFY2(longReach - shortReach >= needed - 6.0,
             qPrintable(QStringLiteral("a long name reached %1 px further than "
                                       "a short one but its extra characters "
                                       "need %2 px — it is still being cut")
                            .arg(longReach - shortReach).arg(needed)));
}

void TestSectionDiagram::planInsetTitleIsNotCutToTheDialWidth()
{
    // "Inlet · MH_1234567" is the caption that tells the reader which end of
    // the reach they are looking at; cut to the dial's 96 px box it becomes
    // "Inlet · MH…", which identifies nothing.
    const QSize pane(560, 480);

    auto renderWith = [&](const QString &title) {
        SectionDiagramModel m = makeLabelledReachModel();
        const QVector<PlanSpoke> up{ { 90.0, QStringLiteral("C1"), true } };
        m.planInsets << PlanInset{ up, title, PlanInset::Side::Left };
        return paintAt(m, pane, nullptr);
    };

    // The caption strip sits below the dial's box in the bottom band; its
    // rows are found from the render pair rather than assumed, and the
    // rightmost ink in them IS the end of the caption.
    auto captionReach = [&](const QImage &img, QPair<int, int> rows) {
        int maxX = 0;
        for (int y = rows.first; y <= rows.second; ++y)
            for (int x = 0; x < 400; ++x)
                if (img.pixelColor(x, y) != img.pixelColor(0, 0)) maxX = std::max(maxX, x);
        return maxX;
    };

    // Both captions are past the dial's width, so both are anchored the same
    // way and the difference in reach is purely the difference in text — a
    // short caption is centred under the dial and would compare apples to
    // oranges.
    const QString kShort = QStringLiteral("Inlet · MANHOLE_1234");
    const QString kLong  = QStringLiteral("Inlet · MANHOLE_1234567890");

    const QImage shortImg = renderWith(kShort);
    const QImage longImg  = renderWith(kLong);
    const auto rows = diffRows(shortImg, longImg);
    QVERIFY2(rows.second >= rows.first, "no caption was drawn to measure");
    const int shortReach = captionReach(shortImg, rows);
    const int longReach  = captionReach(longImg, rows);

    QImage probe(1, 1, QImage::Format_ARGB32_Premultiplied);
    QPainter pp(&probe);
    const QFontMetricsF fm(pp.font());
    const double needed = fm.horizontalAdvance(kLong) - fm.horizontalAdvance(kShort);

    QVERIFY2(shortReach > 0, "no caption was drawn to measure");
    QVERIFY2(longReach - shortReach >= needed - 8.0,
             qPrintable(QStringLiteral("a long caption reached %1 px further "
                                       "than a short one but needs %2 px — it "
                                       "is still being cut to the dial")
                            .arg(longReach - shortReach).arg(needed)));
}

#include "test_sectiondiagram.moc"
