/**
 * @file test_legendoverlay.cpp
 * @brief The map legend box: its size, where each row is drawn, and its placement on the map.
 *
 * The geometry checks recompute the single-column legend from font metrics, the same way the
 * legend measured itself before the multi-column layout existed, so a change in the default
 * look shows up here. Images for review go to LEGEND_STYLING_OUTPUT
 * (tests/output/legend_styling_2026-11-15); they are written once, at the end.
 */

#include <QtTest/QtTest>

#include <QApplication>
#include <QDir>
#include <QFontMetrics>
#include <QImage>

#include "layers/gisvectorlayer.h"
#include "map/legendcontent.h"
#include "map/mapcanvas.h"
#include "render/legendoverlaystyle.h"
#include "render/legendsymbolitem.h"
#include "render/renderers/categorizedrenderer.h"
#include "render/renderers/graduatedrenderer.h"
#include "render/renderers/singlesymbolrenderer.h"
#include "render/symbollayer.h"
#include "render/symbolstyle.h"
#include "ui/widgets/legendoverlay.h"

#include <memory>

using OpenSWMM::Render::CategorizedRenderer;
using OpenSWMM::Render::GraduatedRenderer;
using OpenSWMM::Render::LegendOverlayStyle;
using OpenSWMM::Render::LegendSymbolItem;
using OpenSWMM::Render::SingleSymbolRenderer;
using OpenSWMM::Render::SymbolLayer;
using OpenSWMM::Render::SymbolLayerKind;
using OpenSWMM::Render::SymbolStyle;
using openswmmvis::ui::LegendOverlay;

namespace
{

QString dataFile(const QString &name)
{
    return QStringLiteral(LEGEND_STYLING_DATA) + QLatin1Char('/') + name;
}

QString outDir()
{
    const QString dir = QStringLiteral(LEGEND_STYLING_OUTPUT);
    QDir().mkpath(dir);
    return dir;
}

SymbolStyle fill(const QColor &c)
{
    SymbolLayer layer;
    layer.kind = SymbolLayerKind::SimpleFill;
    OpenSWMM::Render::SymbolProps::writeColor(layer.props, QStringLiteral("fillColor"), c);
    return SymbolStyle{{layer}};
}

SymbolStyle marker(const QColor &c, double size)
{
    SymbolLayer layer;
    layer.kind = SymbolLayerKind::SimpleMarker;
    layer.props.insert(QStringLiteral("size"), size);
    OpenSWMM::Render::SymbolProps::writeColor(layer.props, QStringLiteral("fillColor"), c);
    return SymbolStyle{{layer}};
}

SymbolStyle line(const QColor &c, double width)
{
    SymbolLayer layer;
    layer.kind = SymbolLayerKind::SimpleLine;
    layer.props.insert(QStringLiteral("width"), width);
    OpenSWMM::Render::SymbolProps::writeColor(layer.props, QStringLiteral("color"), c);
    return SymbolStyle{{layer}};
}

/** Four layers on one map: a single symbol, three categories (fill, large marker, wide line),
 *  five graduated bins, and categories under a text-only sub-heading. */
struct LegendScene {
    std::unique_ptr<GISVectorLayer> single;
    std::unique_ptr<GISVectorLayer> kinds;
    std::unique_ptr<GISVectorLayer> values;
    std::unique_ptr<GISVectorLayer> sections;
    std::unique_ptr<MapCanvas> canvas;

    explicit LegendScene(QSize canvasSize)
    {
        const QString path = dataFile(QStringLiteral("legend_points.geojson"));
        single = std::make_unique<GISVectorLayer>(path);
        single->setName(QStringLiteral("Single layer"));
        single->setObjectName(QStringLiteral("single"));
        single->setRenderer(std::make_unique<SingleSymbolRenderer>(fill(QColor(220, 30, 30)),
                                                                   QStringLiteral("All points")));

        kinds = std::make_unique<GISVectorLayer>(path);
        kinds->setName(QStringLiteral("Point kinds"));
        kinds->setObjectName(QStringLiteral("kinds"));
        auto categorized = std::make_unique<CategorizedRenderer>();
        categorized->setClassifyAttribute(QStringLiteral("kind"));
        categorized->setCategories({{QStringLiteral("A"), QStringLiteral("Kind A (fill)"),
                                     fill(QColor(20, 160, 40))},
                                    {QStringLiteral("B"), QStringLiteral("Kind B (large marker)"),
                                     marker(QColor(30, 60, 220), 22)},
                                    {QStringLiteral("C"), QStringLiteral("Kind C (wide line)"),
                                     line(QColor(200, 0, 200), 4)}});
        kinds->setRenderer(std::move(categorized));

        values = std::make_unique<GISVectorLayer>(path);
        values->setName(QStringLiteral("Graduated values"));
        values->setObjectName(QStringLiteral("values"));
        auto graduated = std::make_unique<GraduatedRenderer>();
        graduated->setClassifyAttribute(QStringLiteral("value"));
        graduated->setBaseSymbol(fill(Qt::gray));
        values->setRenderer(std::move(graduated));

        sections = std::make_unique<GISVectorLayer>(path);
        sections->setName(QStringLiteral("Sectioned"));
        sections->setObjectName(QStringLiteral("sections"));
        auto sectioned = std::make_unique<CategorizedRenderer>();
        sectioned->setClassifyAttribute(QStringLiteral("kind"));
        sectioned->setCategories(
            {{QStringLiteral("header"), QStringLiteral("A section heading with a long name"),
              SymbolStyle{}},
             {QStringLiteral("A"), QStringLiteral("First"), fill(QColor(250, 160, 0))},
             {QStringLiteral("B"), QStringLiteral("Second"), fill(QColor(0, 170, 170))}});
        sections->setRenderer(std::move(sectioned));

        canvas = std::make_unique<MapCanvas>();
        canvas->resize(canvasSize);
        // Bottom of the stack first: the legend lists the top layer first.
        canvas->addLayer(sections.get(), false);
        canvas->addLayer(values.get(), false);
        canvas->addLayer(kinds.get(), false);
        canvas->addLayer(single.get(), false);
    }
    ~LegendScene()
    {
        // The canvas lets go of its layers before they are destroyed.
        while (!canvas->layers().isEmpty())
            canvas->takeLayer(0, false);
        canvas.reset();
    }
};

/** Symbol slot size the single-column legend reserves for @p symbol. */
QSize legacySymbolExtent(const SymbolStyle &symbol, int fallback)
{
    QSize extent(fallback, fallback);
    for (const auto &pass : symbol.layers) {
        if (pass.kind == SymbolLayerKind::SimpleLine) {
            const int width = qCeil(std::max(0., pass.props.value("width", 1.).toDouble()));
            extent = extent.expandedTo(QSize(std::max(2 * fallback, 2 * width), width));
        } else if (pass.kind == SymbolLayerKind::SimpleMarker) {
            const int diameter = qCeil(std::max(0., pass.props.value("size", fallback).toDouble()));
            extent = extent.expandedTo(QSize(diameter, diameter));
        }
    }
    return extent;
}

struct Spot {
    QRect slot;
    QColor color;
    QString label;
};

struct Legacy {
    QSize box;
    QList<Spot> spots; ///< rows drawn with a one-pass symbol, in drawing order
    int contentHeight = 0;
};

/** The single-column legend's size and symbol slots, measured from font metrics. */
Legacy legacyLayout(const MapCanvas &canvas, const LegendOverlayStyle &style)
{
    const int padding = style.padding();
    const int rowSpacing = style.rowSpacing();
    const int swatch = style.swatchSize();
    const int maxLabel = style.maxLabelWidth();
    const QFontMetrics fm(style.itemFont());
    const QFontMetrics hfm(style.layerHeaderFont());
    const QFontMetrics tfm(style.titleFont());

    Legacy out;
    int contentW = 0;
    int y = padding;
    if (style.showTitle() && !style.title().isEmpty()) {
        contentW = std::min(tfm.horizontalAdvance(style.title()), maxLabel);
        y += tfm.height() + rowSpacing;
    }
    bool first = true;
    for (int i = canvas.layers().size() - 1; i >= 0; --i) {
        OpenSWMMVisLayer *layer = canvas.layers().at(i);
        if (!layer->isVisible())
            continue;
        const auto rows = openswmmvis::map::LegendContent::legendItemsFor(layer);
        int symbolW = swatch;
        for (const auto &row : rows)
            if (row.visible)
                symbolW = std::max(symbolW, legacySymbolExtent(row.symbol, swatch).width());
        if (!first)
            y += 6;
        first = false;
        contentW = std::max(contentW, std::min(hfm.horizontalAdvance(layer->name()), maxLabel));
        y += hfm.height();
        for (const auto &row : rows) {
            if (!row.visible)
                continue;
            const bool hasSwatch = !row.symbol.layers.isEmpty();
            const int labelW = std::min(fm.horizontalAdvance(row.effectiveLabel()), maxLabel);
            contentW = std::max(contentW, hasSwatch ? symbolW + 6 + labelW : labelW);
            const int rowH =
                std::max(fm.height(), legacySymbolExtent(row.symbol, swatch).height());
            if (row.symbol.layers.size() == 1)
                out.spots.append({QRect(padding, y, symbolW, rowH),
                                  OpenSWMM::Render::SymbolProps::firstColor(row.symbol),
                                  row.effectiveLabel()});
            y += rowH + rowSpacing;
        }
    }
    out.contentHeight = y + padding;
    int w = contentW + 2 * padding;
    int h = out.contentHeight;
    if (style.explicitWidth() > 0)
        w = style.explicitWidth();
    if (style.explicitHeight() > 0)
        h = style.explicitHeight();
    h = std::min(h, std::max(48, canvas.height() - 24));
    out.box = QSize(w, h);
    return out;
}

QImage drawn(QWidget *widget)
{
    QImage image(widget->size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    widget->render(&image, QPoint(), QRegion(), QWidget::RenderFlags(QWidget::DrawChildren));
    return image.convertToFormat(QImage::Format_ARGB32);
}

bool near(const QColor &a, const QColor &b, int tol = 6)
{
    return std::abs(a.red() - b.red()) <= tol && std::abs(a.green() - b.green()) <= tol &&
           std::abs(a.blue() - b.blue()) <= tol;
}

QString describe(const QColor &c)
{
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(c.red())
        .arg(c.green())
        .arg(c.blue())
        .arg(c.alpha());
}

} // namespace

class TestLegendOverlay : public QObject
{
    Q_OBJECT

    QList<QPair<QString, QImage>> m_images; ///< written once, in cleanupTestCase()

    /** @brief The legend is the measured size, anchored bottom-right 12 px in, with each
     *  row's symbol drawn in its slot. */
    void checkLegacyGeometry(const QString &name, LegendScene &scene, LegendOverlay &legend)
    {
        const Legacy expected = legacyLayout(*scene.canvas, *legend.style());
        QCOMPARE(legend.size(), expected.box);
        const QSize canvas = scene.canvas->size();
        QCOMPARE(legend.pos(), QPoint(canvas.width() - expected.box.width() - 12,
                                      canvas.height() - expected.box.height() - 12));
        const QImage image = drawn(&legend);
        m_images.append({name, image});
        int probed = 0;
        for (const Spot &spot : expected.spots) {
            if (spot.slot.bottom() >= image.height())
                continue; // scrolled out of the box
            const QColor got = image.pixelColor(spot.slot.center());
            QVERIFY2(near(got, spot.color),
                     qPrintable(QStringLiteral("%1: %2 at %3,%4 is %5, expected %6")
                                    .arg(name, spot.label)
                                    .arg(spot.slot.center().x())
                                    .arg(spot.slot.center().y())
                                    .arg(describe(got), describe(spot.color))));
            ++probed;
        }
        QVERIFY2(probed >= 3, qPrintable(name));
    }

private slots:
    void cleanupTestCase()
    {
        for (const auto &[name, image] : std::as_const(m_images))
            image.save(outDir() + QLatin1Char('/') + name + QStringLiteral(".png"));
    }

    /** @brief Default style: every layer, top first; symbol column per layer; sub-headings
     *  are text only. */
    void defaultLegendMatchesTheSingleColumnGeometry()
    {
        LegendScene scene(QSize(800, 700));
        LegendOverlay legend(scene.canvas.get());
        QCOMPARE(openswmmvis::map::LegendContent::legendItemsFor(scene.values.get()).size(), 5);
        checkLegacyGeometry(QStringLiteral("legacy_default"), scene, legend);
    }

    /** @brief A title, larger fonts, more padding and spacing, a larger swatch and a fixed
     *  width keep the single-column geometry. */
    void styledLegendMatchesTheSingleColumnGeometry()
    {
        LegendScene scene(QSize(800, 900));
        LegendOverlay legend(scene.canvas.get());
        LegendOverlayStyle *style = legend.style();
        style->setShowTitle(true);
        style->setTitle(QStringLiteral("Legend title"));
        QFont title;
        title.setPointSize(18);
        title.setBold(true);
        style->setTitleFont(title);
        QFont header;
        header.setPointSize(16);
        header.setItalic(true);
        style->setLayerHeaderFont(header);
        QFont item;
        item.setPointSize(15);
        style->setItemFont(item);
        style->setPadding(10);
        style->setRowSpacing(4);
        style->setSwatchSize(18);
        style->setExplicitWidth(300);
        checkLegacyGeometry(QStringLiteral("legacy_styled"), scene, legend);
    }

    /** @brief On a short map the box stops 12 px from the top and bottom; the rows scroll. */
    void tallLegendIsCappedToTheMap()
    {
        LegendScene scene(QSize(800, 200));
        LegendOverlay legend(scene.canvas.get());
        const Legacy expected = legacyLayout(*scene.canvas, *legend.style());
        QVERIFY(expected.contentHeight > 176);
        QCOMPARE(legend.height(), 176);
        checkLegacyGeometry(QStringLiteral("legacy_capped"), scene, legend);
    }

    /** @brief Opacity fades the whole box (it used to have no effect on the legend). */
    void opacityFadesTheWholeBox()
    {
        LegendScene scene(QSize(800, 700));
        LegendOverlay legend(scene.canvas.get());
        legend.style()->setShowFrame(false);
        legend.style()->setBackgroundColor(QColor(255, 255, 255, 255));
        const QPoint padding(3, legend.height() / 2);
        QCOMPARE(drawn(&legend).pixelColor(padding).alpha(), 255);
        legend.style()->setOpacity(0.5);
        const QImage faded = drawn(&legend);
        m_images.append({QStringLiteral("opacity_half"), faded});
        QVERIFY2(std::abs(faded.pixelColor(padding).alpha() - 128) <= 2,
                 qPrintable(describe(faded.pixelColor(padding))));
    }

    /** @brief Hidden layers and hidden rows leave no space. */
    void hiddenLayersAndRowsLeaveNoSpace()
    {
        LegendScene scene(QSize(800, 700));
        LegendOverlay legend(scene.canvas.get());
        scene.values->setVisible(false);
        legend.style()->setItemVisible(QStringLiteral("kinds"), QStringLiteral("1"), false);
        // The measure above does not apply row overrides; take the hidden 22 px marker row
        // out of its height.
        const Legacy expected = legacyLayout(*scene.canvas, *legend.style());
        const QFontMetrics fm(legend.style()->itemFont());
        const int hiddenRowH = std::max(fm.height(), 22) + legend.style()->rowSpacing();
        QCOMPARE(legend.height(), expected.box.height() - hiddenRowH);
        m_images.append({QStringLiteral("legacy_hidden"), drawn(&legend)});
    }
};

QTEST_MAIN(TestLegendOverlay)
#include "test_legendoverlay.moc"
