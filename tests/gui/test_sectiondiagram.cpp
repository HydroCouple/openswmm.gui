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

#include <QImage>
#include <QPalette>

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

QTEST_MAIN(TestSectionDiagram)
#include "test_sectiondiagram.moc"
