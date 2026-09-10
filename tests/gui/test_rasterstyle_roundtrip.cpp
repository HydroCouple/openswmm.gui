/*!
 * \file   test_rasterstyle_roundtrip.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Raster symbology end-to-end: GISRasterLayer default-renderer
 *         selection, per-band statistics / sampling, classified rendering
 *         through the tile pipeline (and its cache invalidation), the RGB
 *         ↔ single-band switch, StyleFileIO / project-style round-trips
 *         (the dialog's Cancel + the .oswp block), and the Symbology-tab
 *         panel mounted by LayerStyleDialog.
 *
 *         Fixtures are synthesised with GDAL into <repo>/tests/output/
 *         raster_style so a human can open them in QGIS afterwards — never
 *         a temp dir (CLAUDE.md §4.1):
 *           gradient_f32.tif — 50×50 Float32, value = x + y (0..98),
 *                              NoData −9999 in the 5×5 top-left corner
 *           landuse_ct.tif   — 20×20 Byte, quadrants 1..4, 5-entry colour table
 *           rgb3.tif         — 20×20 3-band Byte (R 200, G x·10, B 50)
 *           multi_u16.tif    — 20×20 3-band UInt16 (all 1000)
 *         All in EPSG:32633, 10 m pixels, origin 500000 E / 4601000 N.
 */

#include "layers/gisrasterlayer.h"
#include "map/legendcontent.h"
#include "map/mapextent.h"
#include "map/spatialreferencesystem.h"
#include "render/classificationscheme.h"
#include "render/renderers/graduatedrasterrenderer.h"
#include "render/renderers/multibandcolorrenderer.h"
#include "render/renderers/palettedrasterrenderer.h"
#include "render/stylefileio.h"
#include "ui/dialogs/layerstyledialog.h"
#include "ui/dialogs/rastersymbologypanel.h"
#include "ui/widgets/classificationeditor.h"

#include <gdal_priv.h>
#include <ogr_spatialref.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QPainter>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTableView>
#include <QTabWidget>
#include <QTest>

#include <cmath>
#include <memory>
#include <vector>

using OpenSWMM::Render::BinMethod;
using OpenSWMM::Render::ClassificationScheme;
using OpenSWMM::Render::GraduatedRasterRenderer;
using OpenSWMM::Render::MultiBandColorRenderer;
using OpenSWMM::Render::PalettedRasterRenderer;
using OpenSWMM::Render::StyleFileIO;
using openswmmvis::ui::LayerStyleDialog;
using openswmmvis::ui::RasterSymbologyPanel;

namespace {

constexpr double kNoData = -9999.0;
constexpr int    kEpsg   = 32633;

//! Reviewable output root: <repo>/tests/output/raster_style (never a temp dir).
QString outputDir()
{
    QDir d(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral(".")));
    d.cdUp();   // tests/gui
    d.cdUp();   // tests
    const QString out = d.filePath(QStringLiteral("output/raster_style"));
    QDir().mkpath(out);
    return out;
}

QString fixture(const char *name) { return outputDir() + QLatin1Char('/') + QLatin1String(name); }

bool applyGeoref(GDALDataset *ds)
{
    double gt[6] = { 500000.0, 10.0, 0.0, 4601000.0, 0.0, -10.0 };
    if (ds->SetGeoTransform(gt) != CE_None) return false;
    OGRSpatialReference srs;
    if (srs.importFromEPSG(kEpsg) != OGRERR_NONE) return false;
    char *wkt = nullptr;
    srs.exportToWkt(&wkt);
    const bool ok = wkt && ds->SetProjection(wkt) == CE_None;
    CPLFree(wkt);
    return ok;
}

GDALDataset *createTif(const QString &path, int w, int h, int bands, GDALDataType type)
{
    GDALAllRegister();
    GDALDriver *drv = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!drv) return nullptr;
    if (QFile::exists(path)) QFile::remove(path);
    GDALDataset *ds = drv->Create(path.toUtf8().constData(), w, h, bands, type, nullptr);
    if (ds && !applyGeoref(ds)) { GDALClose(ds); return nullptr; }
    return ds;
}

bool buildGradient()
{
    GDALDataset *ds = createTif(fixture("gradient_f32.tif"), 50, 50, 1, GDT_Float32);
    if (!ds) return false;
    GDALRasterBand *b = ds->GetRasterBand(1);
    b->SetNoDataValue(kNoData);
    std::vector<float> px(50 * 50);
    for (int y = 0; y < 50; ++y)
        for (int x = 0; x < 50; ++x)
            px[size_t(y) * 50 + x] = (x < 5 && y < 5) ? float(kNoData) : float(x + y);
    const bool ok = b->RasterIO(GF_Write, 0, 0, 50, 50, px.data(), 50, 50,
                                GDT_Float32, 0, 0) == CE_None;
    GDALClose(ds);
    return ok;
}

bool buildLanduse()
{
    GDALDataset *ds = createTif(fixture("landuse_ct.tif"), 20, 20, 1, GDT_Byte);
    if (!ds) return false;
    GDALRasterBand *b = ds->GetRasterBand(1);
    std::vector<GByte> px(20 * 20);
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 20; ++x)
            px[size_t(y) * 20 + x] = GByte(1 + (x >= 10 ? 1 : 0) + (y >= 10 ? 2 : 0));
    bool ok = b->RasterIO(GF_Write, 0, 0, 20, 20, px.data(), 20, 20, GDT_Byte, 0, 0) == CE_None;
    GDALColorTable ct;
    const GDALColorEntry entries[5] = {
        { 0, 0, 0, 0 }, { 255, 0, 0, 255 }, { 0, 255, 0, 255 },
        { 0, 0, 255, 255 }, { 255, 255, 0, 255 } };
    for (int i = 0; i < 5; ++i) ct.SetColorEntry(i, &entries[i]);
    ok = ok && b->SetColorTable(&ct) == CE_None;
    b->SetColorInterpretation(GCI_PaletteIndex);
    GDALClose(ds);
    return ok;
}

bool buildRgb3()
{
    GDALDataset *ds = createTif(fixture("rgb3.tif"), 20, 20, 3, GDT_Byte);
    if (!ds) return false;
    std::vector<GByte> r(400, 200), g(400), bl(400, 50);
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 20; ++x)
            g[size_t(y) * 20 + x] = GByte(x * 10);
    bool ok = true;
    ok = ok && ds->GetRasterBand(1)->RasterIO(GF_Write, 0, 0, 20, 20, r.data(),  20, 20, GDT_Byte, 0, 0) == CE_None;
    ok = ok && ds->GetRasterBand(2)->RasterIO(GF_Write, 0, 0, 20, 20, g.data(),  20, 20, GDT_Byte, 0, 0) == CE_None;
    ok = ok && ds->GetRasterBand(3)->RasterIO(GF_Write, 0, 0, 20, 20, bl.data(), 20, 20, GDT_Byte, 0, 0) == CE_None;
    GDALClose(ds);
    return ok;
}

bool buildMultiU16()
{
    GDALDataset *ds = createTif(fixture("multi_u16.tif"), 20, 20, 3, GDT_UInt16);
    if (!ds) return false;
    std::vector<GUInt16> px(400, 1000);
    bool ok = true;
    for (int b = 1; b <= 3; ++b)
        ok = ok && ds->GetRasterBand(b)->RasterIO(GF_Write, 0, 0, 20, 20, px.data(),
                                                   20, 20, GDT_UInt16, 0, 0) == CE_None;
    GDALClose(ds);
    return ok;
}

/*! Full raster footprint in its own CRS (identity warp). */
MapExtent rasterExtent(int wPx, int hPx)
{
    return MapExtent(500000.0, 4601000.0 - 10.0 * hPx, 500000.0 + 10.0 * wPx, 4601000.0);
}

/*! Drive the async tile pipeline until it settles, then composite. Mirrors
 *  test_gisrastercrs. */
QImage renderLayer(GISRasterLayer &layer, const MapExtent &e, const QSize &size)
{
    SpatialReferenceSystem canvas(QStringLiteral("EPSG"), kEpsg);
    QImage img(size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    layer.setViewportSize(img.width(), img.height());
    for (int round = 0; round < 40; ++round) {
        layer.fetchCache(e, img.size(), &canvas);
        QSignalSpy repaint(&layer, &GISRasterLayer::repaintRequested);
        if (!repaint.wait(250) && round > 2)
            break;
    }
    QPainter p(&img);
    layer.render(&p, e, img.size(), &canvas);
    p.end();
    return img;
}

/*! Opaque colours that cover at least \p minPixels pixels, and the
 *  transparent-pixel count. Dominance filtering keeps a stray resampled edge
 *  pixel from inflating the count. */
struct ColourCensus { int dominant = 0; int transparent = 0; QHash<QRgb, int> counts; };
ColourCensus census(const QImage &img, int minPixels = 5)
{
    ColourCensus c;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x) {
            const QRgb px = img.pixel(x, y);
            if (qAlpha(px) == 0) { ++c.transparent; continue; }
            ++c.counts[px];
        }
    for (auto it = c.counts.cbegin(); it != c.counts.cend(); ++it)
        if (it.value() >= minPixels) ++c.dominant;
    return c;
}

std::unique_ptr<GraduatedRasterRenderer> makeClassified(int classes, const char *ramp,
                                                        double lo, double hi,
                                                        const QVector<double> &samples = {})
{
    auto g = std::make_unique<GraduatedRasterRenderer>();
    ClassificationScheme s = g->scheme();
    s.setMode(ClassificationScheme::ClassMode::Classified);
    s.setMethod(samples.isEmpty() ? BinMethod::EqualInterval : BinMethod::Quantile);
    s.setClassCount(classes);
    s.setRampName(QLatin1String(ramp));
    g->setDataRange(lo, hi);
    g->setScheme(s);
    g->reclassify(samples);
    return g;
}

} // namespace

class TestRasterStyleRoundTrip : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY2(buildGradient(), "could not write gradient_f32.tif");
        QVERIFY2(buildLanduse(),  "could not write landuse_ct.tif");
        QVERIFY2(buildRgb3(),     "could not write rgb3.tif");
        QVERIFY2(buildMultiU16(), "could not write multi_u16.tif");
    }

    void defaultRenderers_matchDatasetContent()
    {
        {
            GISRasterLayer layer(fixture("gradient_f32.tif"));
            auto *g = dynamic_cast<GraduatedRasterRenderer *>(layer.rasterRenderer());
            QVERIFY2(g, "single-band float raster should open Graduated");
            QCOMPARE(g->scheme().mode(), ClassificationScheme::ClassMode::Continuous);
            // NoData excluded from the statistics: the 5×5 corner hides every
            // x+y < 5, so the band minimum is 5, not 0 (and never −9999).
            QVERIFY(std::abs(g->dataMin() - 5.0)  < 1e-6);
            QVERIFY(std::abs(g->dataMax() - 98.0) < 1e-6);
            QVERIFY(layer.hasNoDataValue());
            QCOMPARE(layer.noDataValue(), kNoData);
        }
        {
            GISRasterLayer layer(fixture("landuse_ct.tif"));
            auto *p = dynamic_cast<PalettedRasterRenderer *>(layer.rasterRenderer());
            QVERIFY2(p, "raster with a colour table should open Paletted");
            QVERIFY(layer.hasColorTable());
            // GeoTIFF stores an 8-bit palette as 256 entries, so the table is
            // filtered to the values the data actually uses (1..4).
            QList<int> values;
            for (const auto &c : p->classes()) values.append(c.value);
            QCOMPARE(values, (QList<int>{ 1, 2, 3, 4 }));
            QCOMPARE(p->colorForValue(1.0), QColor(255, 0, 0, 255));
            QCOMPARE(p->colorForValue(2.0), QColor(0, 255, 0, 255));
            QCOMPARE(p->colorForValue(4.0), QColor(255, 255, 0, 255));
        }
        {
            GISRasterLayer layer(fixture("rgb3.tif"));
            auto *mb = dynamic_cast<MultiBandColorRenderer *>(layer.rasterRenderer());
            QVERIFY2(mb, "3-band Byte raster should open as RGB composite");
            QCOMPARE(mb->redBand(), 1);
            QCOMPARE(mb->greenBand(), 2);
            QCOMPARE(mb->blueBand(), 3);
            QCOMPARE(mb->alphaBand(), 0);
            QVERIFY(layer.isByteRaster());
        }
        {
            GISRasterLayer layer(fixture("multi_u16.tif"));
            QVERIFY2(dynamic_cast<GraduatedRasterRenderer *>(layer.rasterRenderer()),
                     "non-Byte multi-band raster must not take the Byte RGB path");
            QVERIFY(!layer.isByteRaster());
        }
    }

    void bandData_sampleRangeUniqueValues()
    {
        GISRasterLayer layer(fixture("gradient_f32.tif"));
        const QVector<double> s = layer.sampleValues(1);
        QCOMPARE(s.size(), 50 * 50 - 25);              // NoData corner dropped
        double mn = 1e9, mx = -1e9;
        for (double v : s) { QVERIFY(v != kNoData); mn = std::min(mn, v); mx = std::max(mx, v); }
        QCOMPARE(mn, 5.0);   // x+y < 5 lies entirely inside the NoData corner
        QCOMPARE(mx, 98.0);
        // Capped sample stays on a regular grid ≤ the cap.
        GISRasterLayer fresh(fixture("gradient_f32.tif"));
        QVERIFY(fresh.sampleValues(1, 100).size() <= 100);

        const auto [lo, hi] = layer.bandRange(1);
        QVERIFY(std::abs(lo - 5.0) < 1e-6);
        QVERIFY(std::abs(hi - 98.0) < 1e-6);
        QVERIFY(std::isnan(layer.bandRange(7).first));   // no such band

        GISRasterLayer lu(fixture("landuse_ct.tif"));
        QCOMPARE(lu.uniqueValues(1), (QList<int>{ 1, 2, 3, 4 }));
        QCOMPARE(lu.colorTableClasses().size(), 4);      // 256-slot palette, 4 used
    }

    void classifiedRender_paintsClassColoursAndInvalidates()
    {
        GISRasterLayer layer(fixture("gradient_f32.tif"));
        layer.setRasterRenderer(makeClassified(3, "viridis", 0.0, 98.0));
        const MapExtent e = rasterExtent(50, 50);

        const QImage first = renderLayer(layer, e, QSize(50, 50));
        const ColourCensus c1 = census(first);
        QVERIFY2(c1.transparent > 0, "NoData corner must render transparent");
        QCOMPARE(c1.dominant, 3);

        // In-place edit: 5 classes. Without invalidation the cached tile
        // would still show 3 colours.
        auto *g = dynamic_cast<GraduatedRasterRenderer *>(layer.rasterRenderer());
        QVERIFY(g);
        ClassificationScheme s = g->scheme();
        s.setClassCount(5);
        g->setScheme(s);
        g->reclassify(layer.sampleValues(1));
        QSignalSpy changed(&layer, &GISRasterLayer::rasterRendererChanged);
        layer.notifyRasterRendererEdited();
        QCOMPARE(changed.count(), 1);
        const ColourCensus c2 = census(renderLayer(layer, e, QSize(50, 50)));
        QCOMPARE(c2.dominant, 5);

        // Legend follows the renderer: one row per class.
        QCOMPARE(openswmmvis::map::LegendContent::legendItemsFor(&layer).size(), 5);
    }

    void rgb_switchToGraduatedOnBand2()
    {
        GISRasterLayer layer(fixture("rgb3.tif"));
        const MapExtent e = rasterExtent(20, 20);
        const QImage rgb = renderLayer(layer, e, QSize(20, 20));
        const QRgb pxRgb = rgb.pixel(10, 10);
        QCOMPARE(qAlpha(pxRgb), 255);
        QCOMPARE(qRed(pxRgb), 200);
        QCOMPARE(qBlue(pxRgb), 50);

        layer.setRenderBand(2);
        const auto [lo, hi] = layer.bandRange(2);
        QVERIFY(std::abs(lo - 0.0) < 1e-6);
        QVERIFY(std::abs(hi - 190.0) < 1e-6);
        auto g = std::make_unique<GraduatedRasterRenderer>();   // continuous grayscale
        g->setDataRange(lo, hi);
        layer.setRasterRenderer(std::move(g));

        const QImage gray = renderLayer(layer, e, QSize(20, 20));
        const QRgb pxGray = gray.pixel(10, 10);
        QCOMPARE(qAlpha(pxGray), 255);
        QCOMPARE(qRed(pxGray), qGreen(pxGray));
        QCOMPARE(qGreen(pxGray), qBlue(pxGray));
        QVERIFY(qRed(gray.pixel(1, 10)) < qRed(gray.pixel(18, 10)));   // ramps with x
        QCOMPARE(openswmmvis::map::LegendContent::legendItemsFor(&layer).size(),
                 GraduatedRasterRenderer::kContinuousLegendRows);
    }

    void styleJson_roundTripAndCancelRestore()
    {
        GISRasterLayer a(fixture("gradient_f32.tif"));
        a.setHillshadeParams(200.0, 30.0, 2.0, 0.7);
        a.setHillshadeEnabled(true);
        a.setRasterRenderer(makeClassified(4, "plasma", 0.0, 98.0, a.sampleValues(1)));

        const QJsonObject snap = StyleFileIO::styleToJson(&a);
        QCOMPARE(snap.value(QStringLiteral("layerType")).toString(), QStringLiteral("GISRasterLayer"));
        QCOMPARE(snap.value(QStringLiteral("renderBand")).toInt(), 1);
        QCOMPARE(snap.value(QStringLiteral("rasterRenderer")).toObject()
                     .value(QStringLiteral("id")).toString(), QStringLiteral("graduatedraster"));
        QVERIFY(snap.value(QStringLiteral("hillshade")).toObject().value(QStringLiteral("enabled")).toBool());

        // A fresh layer (open-time default) adopts the whole block.
        GISRasterLayer b(fixture("gradient_f32.tif"));
        QVERIFY(StyleFileIO::applyStyleJson(&b, snap).ok);
        QCOMPARE(StyleFileIO::styleToJson(&b), snap);
        auto *ga = dynamic_cast<GraduatedRasterRenderer *>(a.rasterRenderer());
        auto *gb = dynamic_cast<GraduatedRasterRenderer *>(b.rasterRenderer());
        QVERIFY(ga && gb);
        QCOMPARE(gb->edges(), ga->edges());
        QVERIFY(b.hillshadeEnabled());
        QCOMPARE(b.hillshadeAzimuthDeg(), 200.0);

        // Cancel path: mutate, then restore the opening snapshot.
        auto p = std::make_unique<PalettedRasterRenderer>();
        p->buildClassesFromValues({ 1, 2 });
        a.setRasterRenderer(std::move(p));
        a.setHillshadeEnabled(false);
        QVERIFY(dynamic_cast<PalettedRasterRenderer *>(a.rasterRenderer()));
        QVERIFY(StyleFileIO::applyStyleJson(&a, snap).ok);
        QCOMPARE(StyleFileIO::styleToJson(&a), snap);
        QVERIFY(dynamic_cast<GraduatedRasterRenderer *>(a.rasterRenderer()));

        // The nested .oswp block is the same shape; an empty block (older
        // project) leaves the layer untouched.
        const QJsonObject block = StyleFileIO::rasterStyleToJson(&a);
        QCOMPARE(block.value(QStringLiteral("rasterRenderer")).toObject(),
                 snap.value(QStringLiteral("rasterRenderer")).toObject());
        StyleFileIO::applyRasterStyleJson(&a, QJsonObject{});
        QCOMPARE(StyleFileIO::styleToJson(&a), snap);
    }

    void dialog_mountsPanelSwitchesRendererAndCancels()
    {
        GISRasterLayer layer(fixture("gradient_f32.tif"));
        const QJsonObject opening = StyleFileIO::styleToJson(&layer);

        LayerStyleDialog dlg(&layer, QString(), nullptr);
        auto *panel = dlg.findChild<RasterSymbologyPanel *>();
        QVERIFY2(panel, "Symbology tab must mount RasterSymbologyPanel for a raster");
        QCOMPARE(panel->currentKind(), RasterSymbologyPanel::Kind::Graduated);

        auto *combo = panel->findChild<QComboBox *>(QStringLiteral("rasterRendererKind"));
        QVERIFY(combo);
        QVERIFY(combo->findData(int(RasterSymbologyPanel::Kind::MultiBand)) < 0);   // 1 band

        combo->setCurrentIndex(combo->findData(int(RasterSymbologyPanel::Kind::Paletted)));
        auto *pal = dynamic_cast<PalettedRasterRenderer *>(layer.rasterRenderer());
        QVERIFY(pal);
        QCOMPARE(panel->currentKind(), RasterSymbologyPanel::Kind::Paletted);
        // Unique-value classification seeded the classes (values 5..98) and
        // the class table shows every one of them.
        QVERIFY(pal->classes().size() > 1);
        auto *table = panel->findChild<QTableView *>(QStringLiteral("rasterPalettedTable"));
        QVERIFY(table && table->model());
        QCOMPARE(table->model()->rowCount(), pal->classes().size());

        combo->setCurrentIndex(combo->findData(int(RasterSymbologyPanel::Kind::Graduated)));
        auto *g = dynamic_cast<GraduatedRasterRenderer *>(layer.rasterRenderer());
        QVERIFY(g);

        // An edit in the shared ClassificationEditor reaches the live renderer.
        // The class-count spin is the one whose range is 1..64 (label
        // precision is 0..9).
        auto *editor = panel->findChild<openswmmvis::ui::ClassificationEditor *>();
        QVERIFY(editor);
        QSpinBox *countSpin = nullptr;
        for (QSpinBox *s : editor->findChildren<QSpinBox *>())
            if (s->maximum() == 64) countSpin = s;
        QVERIFY(countSpin);
        QSignalSpy changed(&layer, &GISRasterLayer::rasterRendererChanged);
        countSpin->setValue(7);
        QCOMPARE(g->scheme().classCount(), 7);
        QCOMPARE(g->edges().size(), 8);
        QVERIFY(changed.count() >= 1);

        auto *clip = panel->findChild<QCheckBox *>(QStringLiteral("rasterClipOutOfRange"));
        QVERIFY(clip);
        clip->setChecked(true);
        QVERIFY(g->clipOutOfRange());

        // Cancel restores the opening state (renderer + hillshade + band).
        auto *bb = dlg.findChild<QDialogButtonBox *>();
        QVERIFY(bb);
        bb->button(QDialogButtonBox::Cancel)->click();
        QCOMPARE(StyleFileIO::styleToJson(&layer), opening);
    }

    /*! Reviewable artefacts: the Symbology tab for each renderer family,
     *  grabbed offscreen into tests/output/raster_style/symbology_*.png. */
    void dialog_screenshotsForReview()
    {
        struct Shot { const char *file; const char *png; };
        const Shot shots[] = {
            { "gradient_f32.tif", "symbology_graduated.png" },
            { "landuse_ct.tif",   "symbology_paletted.png"  },
            { "rgb3.tif",         "symbology_multiband.png" },
        };
        for (const Shot &s : shots) {
            GISRasterLayer layer(fixture(s.file));
            LayerStyleDialog dlg(&layer, QString(), nullptr);
            auto *tabs = dlg.findChild<QTabWidget *>(QStringLiteral("tabs"));
            QVERIFY(tabs);
            for (int i = 0; i < tabs->count(); ++i)
                if (tabs->tabText(i).remove(QLatin1Char('&')) == QStringLiteral("Symbology"))
                    tabs->setCurrentIndex(i);
            dlg.resize(760, 980);
            dlg.show();
            QTest::qWait(100);
            const QString path = outputDir() + QLatin1Char('/') + QLatin1String(s.png);
            QVERIFY2(dlg.grab().save(path), qPrintable(QStringLiteral("cannot write %1").arg(path)));
            dlg.close();
        }
    }
};

QTEST_MAIN(TestRasterStyleRoundTrip)
#include "test_rasterstyle_roundtrip.moc"
