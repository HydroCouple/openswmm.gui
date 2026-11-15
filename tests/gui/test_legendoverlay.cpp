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
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QJsonObject>
#include <QLineEdit>
#include <QMetaProperty>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QTreeView>
#include <QMouseEvent>
#include <QUndoStack>

#include "layers/gisvectorlayer.h"
#include "map/legendcontent.h"
#include "map/legendstyleundorecorder.h"
#include "map/mapundostack.h"
#include "render/legendlayout.h"
#include "map/mapcanvas.h"
#include "render/legendoverlaystyle.h"
#include "render/legendsymbolitem.h"
#include "render/classificationscheme.h"
#include "render/featureref.h"
#include "render/ifeaturerenderer.h"
#include "render/renderers/categorizedrenderer.h"
#include "render/renderers/graduatedrasterrenderer.h"
#include "render/renderers/singlebandpseudocolorrenderer.h"
#include "render/sublayers/scalarfillsublayer.h"
#include "render/renderers/graduatedrenderer.h"
#include "render/renderers/singlesymbolrenderer.h"
#include "render/symbollayer.h"
#include "render/symbolstyle.h"
#include "ui/dialogs/legendpropertiesdialog.h"
#include "ui/models/legenditemtreemodel.h"
#include "ui/models/legendlayertreemodel.h"
#include "ui/panels/propertiespanel.h"
#include "ui/widgets/colorbutton.h"
#include "ui/widgets/legendoverlay.h"

#include <memory>

using OpenSWMM::Render::CategorizedRenderer;
using OpenSWMM::Render::GraduatedRenderer;
using OpenSWMM::Render::LegendOverlayStyle;
using OpenSWMM::Render::LegendEntry;
using OpenSWMM::Render::LegendItemTree;
using OpenSWMM::Render::LegendNode;
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

/** A renderer whose legend is a continuous blue-to-yellow ramp over 0..2 m: "< 0" and "> 2"
 *  rows around six samples, tagged as one ramp. */
class RampRenderer : public OpenSWMM::Render::IFeatureRenderer
{
public:
    static QColor at(double f) // the ramp, exactly
    {
        return QColor(int(std::lround(20 + f * (240 - 20))), int(std::lround(40 + f * (220 - 40))),
                      int(std::lround(200 + f * (30 - 200))));
    }
    QString rendererId() const override { return QStringLiteral("testRamp"); }
    SymbolStyle symbolFor(const OpenSWMM::Render::FeatureRef &, const QVariantMap &) const override
    {
        return fill(Qt::gray);
    }
    QList<LegendSymbolItem> legendSymbolItems() const override
    {
        QList<LegendSymbolItem> rows;
        LegendSymbolItem below;
        below.label = QStringLiteral("< 0 m");
        below.symbol = fill(QColor(0, 0, 0));
        rows.append(below);
        for (int i = 0; i < 6; ++i) {
            LegendSymbolItem row;
            row.label = QStringLiteral("%1 m").arg(i * 0.4);
            row.symbol = fill(at(i / 5.0));
            row.classKey = QString::number(i);
            rows.append(row);
        }
        LegendSymbolItem above;
        above.label = QStringLiteral("> 2 m");
        above.symbol = fill(QColor(255, 0, 0));
        rows.append(above);
        OpenSWMM::Render::LegendRampSpec spec;
        spec.lo = 0;
        spec.hi = 2;
        spec.unitsSuffix = QStringLiteral(" m");
        spec.title = QStringLiteral("Water depth");
        for (int k = 0; k <= 10; ++k)
            spec.stops.append({k / 10.0, at(k / 10.0)});
        spec.belowColor = QColor(0, 0, 0);
        spec.aboveColor = QColor(255, 0, 0);
        OpenSWMM::Render::tagLegendRamp(rows, 0, int(rows.size()), spec);
        return rows;
    }
    QJsonObject toJson() const override { return {}; }
    void fromJson(const QJsonObject &) override {}
    std::unique_ptr<IFeatureRenderer> clone() const override
    {
        return std::make_unique<RampRenderer>();
    }
};

/** Labels of the legend's lines in drawing order, headings marked "#". */
QStringList lineLabels(const QList<LegendEntry> &entries)
{
    QStringList out;
    for (const auto &e : entries)
        out.append(e.kind == LegendEntry::Kind::GroupHeading ? QStringLiteral("#") + e.label
                                                             : e.label);
    return out;
}

QStringList headingLabels(const QList<LegendEntry> &entries)
{
    QStringList out;
    for (const auto &e : entries)
        if (e.kind == LegendEntry::Kind::GroupHeading)
            out.append(e.label);
    return out;
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
    void initTestCase()
    {
        // Own settings, so a default legend style saved by a developer cannot change results.
        QCoreApplication::setOrganizationName(QStringLiteral("OpenSWMM-test"));
        QCoreApplication::setApplicationName(QStringLiteral("test_legendoverlay"));
        LegendOverlayStyle::setDefaultPreset({});
    }

    void cleanupTestCase()
    {
        LegendOverlayStyle::setDefaultPreset({});
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

    /** @brief A legend saved before the new settings (title, larger fonts, more padding and
     *  spacing, a larger swatch, a fixed width) keeps its single-column geometry: its
     *  sub-headings keep the item font and its title keeps the row spacing below it. */
    void styledLegendMatchesTheSingleColumnGeometry()
    {
        LegendScene scene(QSize(800, 900));
        LegendOverlay legend(scene.canvas.get());
        LegendOverlayStyle *style = legend.style();
        QFont title;
        title.setPointSize(18);
        title.setBold(true);
        QFont header;
        header.setPointSize(16);
        header.setItalic(true);
        QFont item;
        item.setPointSize(15);
        QJsonObject saved;
        saved[QStringLiteral("showTitle")] = true;
        saved[QStringLiteral("title")] = QStringLiteral("Legend title");
        saved[QStringLiteral("titleFont")] = OpenSWMM::Render::overlayFontToJson(title);
        saved[QStringLiteral("layerHeaderFont")] = OpenSWMM::Render::overlayFontToJson(header);
        saved[QStringLiteral("itemFont")] = OpenSWMM::Render::overlayFontToJson(item);
        saved[QStringLiteral("padding")] = 10;
        saved[QStringLiteral("rowSpacing")] = 4;
        saved[QStringLiteral("swatchSize")] = 18;
        saved[QStringLiteral("explicitWidth")] = 300;
        style->fromJson(saved);
        QCOMPARE(style->subheadingFont().pointSize(), 15);
        QCOMPARE(style->titleSpacing(), 4);
        QCOMPARE(style->marginTop(), 10);
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

    /** @brief Two columns make the legend shorter and wider, with a line between them; a shadow
     *  grows the widget by its offset and is drawn outside the box. */
    void columnsSeparatorsAndShadowAreDrawn()
    {
        LegendScene scene(QSize(900, 700));
        LegendOverlay legend(scene.canvas.get());
        const QSize single = legend.size();
        LegendOverlayStyle *style = legend.style();
        style->setColumnCount(2);
        style->setShowColumnSeparators(true);
        style->setColumnSeparatorColor(QColor(255, 0, 0));
        style->setColumnSeparatorWidth(2);
        QVERIFY2(legend.height() < single.height() && legend.width() > single.width(),
                 qPrintable(QStringLiteral("%1x%2 vs %3x%4")
                                .arg(legend.width())
                                .arg(legend.height())
                                .arg(single.width())
                                .arg(single.height())));
        const QSize box = legend.size();
        // A label that fits its column is not shortened.
        {
            const auto entries =
                openswmmvis::map::LegendContent::buildEntries(scene.canvas.get(), style);
            const auto layout = OpenSWMM::Render::LegendLayout::compute(entries, *style);
            for (const auto &b : layout.boxes)
                if (b.entryIndex >= 0)
                    QCOMPARE(b.lines.value(0), entries[b.entryIndex].label);
        }
        QImage image = drawn(&legend);
        int red = 0;
        const int midY = image.height() / 2;
        for (int x = 0; x < image.width(); ++x) {
            const QColor c = image.pixelColor(x, midY);
            if (c.red() > 200 && c.green() < 60 && c.blue() < 60)
                ++red;
        }
        QVERIFY2(red >= 1 && red <= 4, qPrintable(QString::number(red)));

        style->setShowShadow(true);
        style->setShadowColor(QColor(0, 0, 0, 255));
        style->setShadowOffsetX(5);
        style->setShadowOffsetY(5);
        QCOMPARE(legend.size(), box + QSize(5, 5));
        image = drawn(&legend);
        m_images.append({QStringLiteral("two_columns_shadow"), image});
        const QColor shadow = image.pixelColor(image.width() - 2, image.height() / 2);
        QVERIFY2(shadow.alpha() > 200 && shadow.red() < 40, qPrintable(describe(shadow)));
        const QColor inside = image.pixelColor(3, image.height() / 2);
        QVERIFY2(inside.red() > 200 && inside.green() > 200, qPrintable(describe(inside)));
        // The anchor places the widget, shadow included, 12 px in from the map's corner.
        QCOMPARE(legend.geometry().bottomRight(), QPoint(900 - 12 - 1, 700 - 12 - 1));
    }

    /** @brief Many settings at once, drawn for review: a centered wrapped title, wrapped and
     *  right-aligned labels, symbols on the right, zigzag lines, round fills, indents, a dashed
     *  frame and a gradient. */
    void showcaseOfTheNewSettings()
    {
        LegendScene scene(QSize(900, 900));
        LegendOverlay legend(scene.canvas.get());
        LegendOverlayStyle *style = legend.style();
        style->setShowTitle(true);
        style->setTitle(QStringLiteral("Drainage network legend with a long title"));
        QFont title;
        title.setPointSize(16);
        title.setBold(true);
        style->setTitleFont(title);
        style->setTitleAlignment(LegendOverlayStyle::TextAlignment::Center);
        style->setTitleSpacing(10);
        style->setLabelOverflow(LegendOverlayStyle::LabelOverflow::Wrap);
        style->setMaxLabelWidth(120);
        style->setLineSpacing(2);
        style->setColumnCount(2);
        style->setShowColumnSeparators(true);
        style->setSymbolPosition(LegendOverlayStyle::SymbolPosition::Right);
        style->setItemAlignment(LegendOverlayStyle::TextAlignment::Right);
        style->setLinePatchShape(LegendOverlayStyle::LinePatchShape::ZigZag);
        style->setAreaPatchShape(LegendOverlayStyle::AreaPatchShape::Ellipse);
        style->setSymbolWidth(24);
        style->setSymbolHeight(16);
        style->setSubheadingColor(QColor(150, 40, 40));
        QFont sub;
        sub.setItalic(true);
        style->setSubheadingFont(sub);
        style->setItemIndent(8);
        style->setGroupSpacing(12);
        style->setHeaderSpacing(4);
        style->setMarginLeft(14);
        style->setMarginRight(14);
        style->setFrameStyle(LegendOverlayStyle::FrameStyle::Dash);
        style->setFrameWidth(2);
        style->setBackgroundMode(LegendOverlayStyle::BackgroundMode::Gradient);
        style->setBackgroundColor(QColor(255, 255, 255, 240));
        style->setGradientEndColor(QColor(220, 235, 250, 240));
        style->setShowShadow(true);
        const QImage image = drawn(&legend);
        m_images.append({QStringLiteral("showcase"), image});
        QVERIFY(!image.isNull());
        QVERIFY(legend.width() > 200);
    }

    /** @brief A layer's heading can be renamed, drawn as a sub-heading or hidden, and the
     *  whole layer left out of the legend; a row can be renamed and hidden. */
    void layerAndRowSettingsChangeTheLegend()
    {
        LegendScene scene(QSize(800, 700));
        LegendOverlay legend(scene.canvas.get());
        LegendOverlayStyle *style = legend.style();
        const QString kinds = LegendOverlayStyle::itemKey(scene.kinds.get(), {});
        const QString values = LegendOverlayStyle::itemKey(scene.values.get(), {});
        const QString single = LegendOverlayStyle::itemKey(scene.single.get(), {});
        QVERIFY(!kinds.isEmpty() && kinds != values); // layers have their own ids

        LegendItemTree tree = style->itemTree();
        tree.ensureLayerNode(kinds).userLabel = QStringLiteral("Kinds (renamed)");
        tree.ensureLayerNode(values).heading = LegendNode::HeadingStyle::Hidden;
        tree.ensureLayerNode(single).visible = false;
        tree.ensureItemNode(kinds, QStringLiteral("0")).userLabel = QStringLiteral("A renamed");
        tree.ensureItemNode(kinds, QStringLiteral("2")).visible = false;
        style->setItemTree(tree);

        const auto entries =
            openswmmvis::map::LegendContent::buildEntries(scene.canvas.get(), style);
        QCOMPARE(headingLabels(entries),
                 (QStringList{QStringLiteral("Kinds (renamed)"), QStringLiteral("Sectioned")}));
        const QStringList labels = lineLabels(entries);
        QVERIFY(labels.contains(QStringLiteral("A renamed")));
        QVERIFY(!labels.contains(QStringLiteral("Kind C (wide line)")));
        QVERIFY(!labels.contains(QStringLiteral("All points")));
        QVERIFY(labels.contains(QStringLiteral("0 – 0.2"))); // rows of the headless layer stay

        tree = style->itemTree();
        tree.ensureLayerNode(kinds).heading = LegendNode::HeadingStyle::Subgroup;
        style->setItemTree(tree);
        const auto sub = openswmmvis::map::LegendContent::buildEntries(scene.canvas.get(), style);
        QCOMPARE(sub.first().kind, LegendEntry::Kind::Subheading);
        QCOMPARE(sub.first().label, QStringLiteral("Kinds (renamed)"));
        m_images.append({QStringLiteral("layer_settings"), drawn(&legend)});

        // Saved with the project and read back.
        LegendOverlayStyle read;
        read.fromJson(style->toJson());
        QVERIFY(read.itemTree() == style->itemTree());
    }

    /** @brief Taking over the order: the user's order, groups and text lines are shown; layers
     *  added to the map later are not; renderer rows that go away are dropped and new ones go
     *  after the stored ones. Following the map again restores map order and keeps renames. */
    void theUsersOrderGroupsAndText()
    {
        LegendScene scene(QSize(800, 700));
        LegendOverlay legend(scene.canvas.get());
        LegendOverlayStyle *style = legend.style();
        const auto before =
            openswmmvis::map::LegendContent::buildEntries(scene.canvas.get(), style);

        // Turning auto update off keeps what the legend shows.
        LegendItemTree tree =
            openswmmvis::map::LegendContent::currentTree(scene.canvas.get(), style);
        style->setAutoUpdate(false);
        style->setItemTree(tree);
        QCOMPARE(
            lineLabels(openswmmvis::map::LegendContent::buildEntries(scene.canvas.get(), style)),
            lineLabels(before));

        // Reorder: the last layer first; a group around two layers; a text line at the end.
        tree = style->itemTree();
        QCOMPARE(tree.roots().size(), 4);
        tree.roots().move(3, 0); // Sectioned first
        LegendNode group;
        group.type = LegendNode::Type::Group;
        group.id = LegendItemTree::newNodeId();
        group.userLabel = QStringLiteral("Points");
        group.children = {tree.roots()[1], tree.roots()[2]}; // Single, Kinds
        tree.roots().remove(1, 2);
        tree.roots().insert(1, group);
        LegendNode text;
        text.type = LegendNode::Type::Text;
        text.id = LegendItemTree::newNodeId();
        text.userLabel = QStringLiteral("Source: test data");
        tree.roots().append(text);
        // Stored row order: Kind C before Kind A.
        LegendNode *kindsNode = tree.layerNode(LegendOverlayStyle::itemKey(scene.kinds.get(), {}));
        kindsNode->children.move(2, 0);
        style->setItemTree(tree);

        auto entries = openswmmvis::map::LegendContent::buildEntries(scene.canvas.get(), style);
        QCOMPARE(headingLabels(entries),
                 (QStringList{QStringLiteral("Sectioned"), QStringLiteral("Points"),
                              QStringLiteral("Single layer"), QStringLiteral("Point kinds"),
                              QStringLiteral("Graduated values")}));
        QCOMPARE(entries.last().kind, LegendEntry::Kind::Text);
        QCOMPARE(entries.last().label, QStringLiteral("Source: test data"));
        int kindsAt = -1;
        for (int i = 0; i < entries.size(); ++i)
            if (entries[i].label == QStringLiteral("Point kinds"))
                kindsAt = i;
        QCOMPARE(entries[kindsAt].depth, 1); // inside the group
        QCOMPARE(entries[kindsAt + 1].label, QStringLiteral("Kind C (wide line)"));
        m_images.append({QStringLiteral("users_order"), drawn(&legend)});

        // The renderer gains Kind D: it goes after the stored rows. A stored row the renderer
        // no longer has is skipped.
        auto *categorized = static_cast<CategorizedRenderer *>(scene.kinds->renderer());
        auto categories = categorized->categories();
        categories.append(
            {QStringLiteral("D"), QStringLiteral("Kind D"), fill(QColor(90, 90, 90))});
        categorized->setCategories(categories);
        LegendItemTree t2 = style->itemTree();
        t2.ensureItemNode(LegendOverlayStyle::itemKey(scene.kinds.get(), {}), QStringLiteral("99"))
            .userLabel = QStringLiteral("Gone");
        style->setItemTree(t2);
        entries = openswmmvis::map::LegendContent::buildEntries(scene.canvas.get(), style);
        QStringList kindRows;
        for (const auto &e : entries)
            if (e.layer == scene.kinds.get() && e.kind == LegendEntry::Kind::Symbol)
                kindRows.append(e.label);
        QCOMPARE(kindRows,
                 (QStringList{QStringLiteral("Kind C (wide line)"), QStringLiteral("Kind A (fill)"),
                              QStringLiteral("Kind B (large marker)"), QStringLiteral("Kind D")}));

        // A layer added to the map later is not in the user's legend.
        GISVectorLayer extra(dataFile(QStringLiteral("legend_points.geojson")));
        extra.setName(QStringLiteral("Added later"));
        scene.canvas->addLayer(&extra, false);
        entries = openswmmvis::map::LegendContent::buildEntries(scene.canvas.get(), style);
        QVERIFY(!headingLabels(entries).contains(QStringLiteral("Added later")));

        // Following the map again: map order, the new layer listed, no group or text.
        style->setAutoUpdate(true);
        entries = openswmmvis::map::LegendContent::buildEntries(scene.canvas.get(), style);
        QCOMPARE(headingLabels(entries).first(), QStringLiteral("Added later"));
        QVERIFY(!headingLabels(entries).contains(QStringLiteral("Points")));
        QCOMPARE(entries.last().kind, LegendEntry::Kind::Symbol);
        scene.canvas->takeLayer(scene.canvas->layers().indexOf(&extra), false);
    }

    /** @brief A column break starts a new column at its layer; a layer with sub-columns lays its
     *  rows side by side. */
    void columnBreaksAndSubColumns()
    {
        LegendScene scene(QSize(900, 700));
        LegendOverlay legend(scene.canvas.get());
        LegendOverlayStyle *style = legend.style();
        style->setColumnCount(3);
        LegendItemTree tree;
        tree.ensureLayerNode(LegendOverlayStyle::itemKey(scene.kinds.get(), {})).columnBreakBefore =
            true;
        LegendNode &values =
            tree.ensureLayerNode(LegendOverlayStyle::itemKey(scene.values.get(), {}));
        values.columnBreakBefore = true;
        values.itemColumns = 2;
        style->setItemTree(tree);
        const auto entries =
            openswmmvis::map::LegendContent::buildEntries(scene.canvas.get(), style);
        const auto layout = OpenSWMM::Render::LegendLayout::compute(entries, *style);
        QHash<QString, int> column;
        QHash<QString, QRectF> rect;
        for (const auto &box : layout.boxes)
            if (box.entryIndex >= 0) {
                column.insert(entries[box.entryIndex].label, box.column);
                rect.insert(entries[box.entryIndex].label, box.rect);
            }
        QCOMPARE(column.value(QStringLiteral("Single layer")), 0);
        QCOMPARE(column.value(QStringLiteral("Point kinds")), 1);
        QCOMPARE(column.value(QStringLiteral("Graduated values")), 2);
        // 0–0.2 and 0.2–0.4 side by side, 0.4–0.6 below the first.
        const QRectF a = rect.value(QStringLiteral("0 – 0.2"));
        const QRectF b = rect.value(QStringLiteral("0.2 – 0.4"));
        const QRectF c = rect.value(QStringLiteral("0.4 – 0.6"));
        QCOMPARE(a.top(), b.top());
        QVERIFY(b.left() > a.right());
        QCOMPARE(c.left(), a.left());
        QVERIFY(c.top() > a.bottom());
        m_images.append({QStringLiteral("breaks_and_subcolumns"), drawn(&legend)});
    }

    /** @brief The Legend dock lists the layers and rows in the legend's order, with the
     *  legend's labels and hidden rows unchecked. */
    void legendDockMatchesTheLegend()
    {
        LegendScene scene(QSize(800, 700));
        LegendOverlay legend(scene.canvas.get());
        LegendOverlayStyle *style = legend.style();
        openswmmvis::ui::LegendLayerTreeModel dock(scene.canvas.get());
        dock.setOverlayStyle(style);
        const auto dockLayers = [&dock]() {
            QStringList names;
            for (int r = 0; r < dock.rowCount(); ++r)
                names.append(dock.index(r, 0).data().toString());
            return names;
        };
        QCOMPARE(dockLayers(), headingLabels(openswmmvis::map::LegendContent::buildEntries(
                                   scene.canvas.get(), style)));

        LegendItemTree tree =
            openswmmvis::map::LegendContent::currentTree(scene.canvas.get(), style);
        tree.roots().move(0, 3);
        style->setAutoUpdate(false);
        style->setItemTree(tree);
        const auto entries =
            openswmmvis::map::LegendContent::buildEntries(scene.canvas.get(), style);
        QCOMPARE(dockLayers(), headingLabels(entries));
        QCOMPARE(dockLayers().last(), QStringLiteral("Single layer"));

        const QString kinds = LegendOverlayStyle::itemKey(scene.kinds.get(), {});
        style->setItemUserLabel(kinds, QStringLiteral("0"), QStringLiteral("Renamed A"));
        style->setItemVisible(kinds, QStringLiteral("1"), false);
        const int kindsRow = int(dockLayers().indexOf(QStringLiteral("Point kinds")));
        const QModelIndex kindsIndex = dock.index(kindsRow, 0);
        QCOMPARE(dock.index(0, 0, kindsIndex).data().toString(), QStringLiteral("Renamed A"));
        QCOMPARE(dock.index(1, 0, kindsIndex).data(Qt::CheckStateRole).toInt(), int(Qt::Unchecked));
        // Checking it in the dock shows it in the legend again.
        QVERIFY(dock.setData(dock.index(1, 0, kindsIndex), Qt::Checked, Qt::CheckStateRole));
        QVERIFY(lineLabels(openswmmvis::map::LegendContent::buildEntries(scene.canvas.get(), style))
                    .contains(QStringLiteral("Kind B (large marker)")));
    }

    /** @brief Row settings from an older file (keyed by the layer's object name) still apply,
     *  until the row is changed here. */
    void olderFileRowSettingsStillApply()
    {
        LegendScene scene(QSize(800, 700));
        LegendOverlay legend(scene.canvas.get());
        LegendOverlayStyle *style = legend.style();
        QJsonObject old;
        old[QStringLiteral("itemOverrides")] = QJsonObject{
            {QStringLiteral("kinds|1"), QJsonObject{{QStringLiteral("visible"), false}}},
            {QStringLiteral("kinds|0"),
             QJsonObject{{QStringLiteral("userLabel"), QStringLiteral("Old name")}}}};
        style->fromJson(old);
        QStringList labels =
            lineLabels(openswmmvis::map::LegendContent::buildEntries(scene.canvas.get(), style));
        QVERIFY(!labels.contains(QStringLiteral("Kind B (large marker)")));
        QVERIFY(labels.contains(QStringLiteral("Old name")));
        style->setItemVisible(LegendOverlayStyle::itemKey(scene.kinds.get(), {}),
                              QStringLiteral("1"), true);
        labels =
            lineLabels(openswmmvis::map::LegendContent::buildEntries(scene.canvas.get(), style));
        QVERIFY(labels.contains(QStringLiteral("Kind B (large marker)")));
    }

    /** @brief A continuous ramp is drawn as one color bar whose colors follow the ramp; a layer
     *  or the whole legend can show the sampled swatches instead. */
    void continuousRampsDrawAsAColorBar()
    {
        LegendScene scene(QSize(800, 1000));
        GISVectorLayer depth(dataFile(QStringLiteral("legend_points.geojson")));
        depth.setName(QStringLiteral("Depth"));
        depth.setRenderer(std::make_unique<RampRenderer>());
        scene.canvas->addLayer(&depth, false);
        const auto release = qScopeGuard(
            [&] { scene.canvas->takeLayer(scene.canvas->layers().indexOf(&depth), false); });
        LegendOverlay legend(scene.canvas.get());
        LegendOverlayStyle *style = legend.style();

        auto entries = openswmmvis::map::LegendContent::buildEntries(scene.canvas.get(), style);
        QCOMPARE(entries[0].label, QStringLiteral("Depth"));
        QCOMPARE(entries[1].kind, LegendEntry::Kind::RampBar);
        QCOMPARE(entries[1].label, QStringLiteral("Water depth"));
        QCOMPARE(entries[2].kind, LegendEntry::Kind::GroupHeading); // no swatch rows

        // The bar's colors, sampled up its middle, follow the ramp (high at the top).
        const auto layout = OpenSWMM::Render::LegendLayout::compute(entries, *style);
        const QRectF bar = layout.boxes[1].barRect;
        const QImage image = drawn(&legend);
        m_images.append({QStringLiteral("ramp_vertical"), image});
        for (double f : {0.1, 0.3, 0.5, 0.7, 0.9}) {
            const QPoint at(int(bar.center().x()), int(bar.bottom() - f * bar.height()));
            const QColor got = image.pixelColor(at);
            QVERIFY2(near(got, RampRenderer::at(f), 8),
                     qPrintable(QStringLiteral("%1: %2 vs %3")
                                    .arg(f)
                                    .arg(describe(got), describe(RampRenderer::at(f)))));
        }
        // The end caps show the below- and above-range colors.
        const QColor top = image.pixelColor(int(bar.center().x()), int(bar.top()) - 4);
        const QColor bottom = image.pixelColor(int(bar.center().x()), int(bar.bottom()) + 4);
        QVERIFY2(near(top, QColor(255, 0, 0), 10), qPrintable(describe(top)));
        QVERIFY2(near(bottom, QColor(0, 0, 0), 10), qPrintable(describe(bottom)));

        style->setRampOrientation(LegendOverlayStyle::RampOrientation::Horizontal);
        style->setRampLength(220);
        m_images.append({QStringLiteral("ramp_horizontal"), drawn(&legend)});

        // Swatches for the whole legend, or for this layer.
        const QString key = LegendOverlayStyle::itemKey(&depth, {});
        style->setRampDisplay(LegendOverlayStyle::RampDisplay::Swatches);
        entries = openswmmvis::map::LegendContent::buildEntries(scene.canvas.get(), style);
        QCOMPARE(entries[1].label, QStringLiteral("< 0 m"));
        QCOMPARE(entries[8].label, QStringLiteral("> 2 m"));
        LegendItemTree tree = style->itemTree();
        tree.ensureLayerNode(key).ramp = LegendNode::RampChoice::Bar;
        style->setItemTree(tree);
        entries = openswmmvis::map::LegendContent::buildEntries(scene.canvas.get(), style);
        QCOMPARE(entries[1].kind, LegendEntry::Kind::RampBar);
        style->setRampDisplay(LegendOverlayStyle::RampDisplay::Bar);
        tree.ensureLayerNode(key).ramp = LegendNode::RampChoice::Swatches;
        style->setItemTree(tree);
        entries = openswmmvis::map::LegendContent::buildEntries(scene.canvas.get(), style);
        QCOMPARE(entries[1].kind, LegendEntry::Kind::Symbol);
    }

    /** @brief Raster and 2D depth fill renderers keep their legend rows and tag their continuous
     *  ramps;
     *  classified rows are not tagged. */
    void continuousRenderersTagTheirRamps()
    {
        using OpenSWMM::Render::ClassificationScheme;
        const auto sameRamp = [](const QList<LegendSymbolItem> &rows) {
            if (rows.isEmpty() || rows.first().rampId == 0 || !rows.first().ramp)
                return false;
            for (const auto &row : rows)
                if (row.rampId != rows.first().rampId)
                    return false;
            return rows.first().ramp->isValid();
        };

        OpenSWMM::Render::GraduatedRasterRenderer raster;
        raster.setDataRange(0.0, 100.0);
        auto rows = raster.legendSymbolItems();
        QCOMPARE(rows.size(), OpenSWMM::Render::GraduatedRasterRenderer::kContinuousLegendRows);
        QCOMPARE(rows.first().label, raster.scheme().formatValue(0.0));
        QVERIFY(sameRamp(rows));
        QCOMPARE(rows.first().ramp->lo, 0.0);
        QCOMPARE(rows.first().ramp->hi, 100.0);
        QCOMPARE(rows.first().ramp->stops.first().second, QColor(Qt::black));
        QCOMPARE(rows.first().ramp->stops.last().second, QColor(Qt::white));
        ClassificationScheme classes = raster.scheme();
        classes.setMode(ClassificationScheme::ClassMode::Classified);
        raster.setScheme(classes);
        for (const auto &row : raster.legendSymbolItems())
            QCOMPARE(row.rampId, 0);

        OpenSWMM::Render::SingleBandPseudoColorRenderer pseudo;
        pseudo.setRange(0.0, 10.0);
        pseudo.setStops({{0.0, QColor(255, 0, 0)}, {1.0, QColor(0, 0, 255)}});
        rows = pseudo.legendSymbolItems();
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows.first().label, QStringLiteral("0"));
        QCOMPARE(rows.last().label, QStringLiteral("10"));
        QVERIFY(sameRamp(rows));

        OpenSWMM::Render::CellDepthFillSublayer fillLayer(QStringLiteral("cellFill"));
        auto *fillStyle = static_cast<OpenSWMM::Render::ScalarFillStyle *>(fillLayer.style());
        fillStyle->setClassified(false);
        fillStyle->setUseCustomRange(true);
        fillStyle->setRangeMin(0.0);
        fillStyle->setRangeMax(2.0);
        rows = fillLayer.legendSymbolItems();
        QVERIFY(rows.size() >= 6);
        QVERIFY(sameRamp(rows));
        fillStyle->setClassified(true);
        for (const auto &row : fillLayer.legendSymbolItems())
            QCOMPARE(row.rampId, 0);
    }

    /** @brief Legend edits are undo steps on the map's stack: separate edits undo one by one,
     *  a quick burst of one setting and a reset are one step each, a Legend dock edit and a drag
     *  are undoable, and settings loaded with a project are not an undo step. */
    void legendEditsAreUndoable()
    {
        LegendScene scene(QSize(800, 700));
        scene.canvas->show();
        QVERIFY(QTest::qWaitForWindowExposed(scene.canvas.get()));
        LegendOverlay legend(scene.canvas.get());
        legend.show();
        LegendOverlayStyle *style = legend.style();
        QUndoStack *stack = scene.canvas->undoStack();
        QVERIFY(stack);
        QVERIFY(openswmmvis::map::LegendStyleUndoRecorder::forStyle(style));
        const auto settle = [] { QCoreApplication::processEvents(); };
        const QJsonObject start = style->toJson();
        const int base = stack->index();

        style->setColumnCount(2);
        settle();
        style->setTitle(QStringLiteral("Undo me"));
        settle();
        style->setFrameStyle(LegendOverlayStyle::FrameStyle::Dash);
        settle();
        QCOMPARE(stack->index(), base + 3);
        stack->undo();
        QCOMPARE(style->frameStyle(), LegendOverlayStyle::FrameStyle::Solid);
        stack->undo();
        stack->undo();
        QCOMPARE(style->toJson(), start);
        stack->redo();
        stack->redo();
        stack->redo();
        QCOMPARE(style->columnCount(), 2);
        QCOMPARE(style->title(), QStringLiteral("Undo me"));
        settle();
        QCOMPARE(stack->index(), base + 3); // applying undo/redo records nothing

        // A spin box held down: one step.
        int count = stack->index();
        for (int i = 1; i <= 10; ++i) {
            style->setRowSpacing(2 + i);
            settle();
        }
        QCOMPARE(stack->index(), count + 1);
        stack->undo();
        QCOMPARE(style->rowSpacing(), 2);
        stack->redo();

        // A reset changes many settings: one step.
        count = stack->index();
        const QJsonObject beforeReset = style->toJson();
        style->resetToDefaults();
        settle();
        QCOMPARE(stack->index(), count + 1);
        stack->undo();
        QCOMPARE(style->toJson(), beforeReset);

        // A Legend dock edit.
        openswmmvis::ui::LegendLayerTreeModel dock(scene.canvas.get());
        dock.setOverlayStyle(style);
        const QString kinds = LegendOverlayStyle::itemKey(scene.kinds.get(), {});
        int kindsRow = -1;
        for (int r = 0; r < dock.rowCount(); ++r)
            if (dock.index(r, 0).data().toString() == QStringLiteral("Point kinds"))
                kindsRow = r;
        QVERIFY(kindsRow >= 0);
        QVERIFY(dock.setData(dock.index(0, 0, dock.index(kindsRow, 0)), Qt::Unchecked,
                             Qt::CheckStateRole));
        settle();
        QVERIFY(!style->itemOverride(kinds, QStringLiteral("0")).visible);
        stack->undo();
        QVERIFY(style->itemOverride(kinds, QStringLiteral("0")).visible);

        // A drag: one step that restores the anchor and the position together.
        count = stack->index();
        const QPoint startPos = legend.pos();
        const QPointF grab(legend.width() / 2.0, legend.height() / 2.0);
        QMouseEvent press(QEvent::MouseButtonPress, grab, legend.mapToGlobal(grab), Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&legend, &press);
        const QPointF to = grab + QPointF(-120, -90);
        QMouseEvent move(QEvent::MouseMove, to, legend.mapToGlobal(to), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&legend, &move);
        QMouseEvent release(QEvent::MouseButtonRelease, to, legend.mapToGlobal(to), Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&legend, &release);
        settle();
        QCOMPARE(style->anchor(), LegendOverlayStyle::Anchor::Free);
        QCOMPARE(legend.pos(), startPos + QPoint(-120, -90));
        QCOMPARE(stack->index(), count + 1);
        stack->undo();
        QCOMPARE(style->anchor(), LegendOverlayStyle::Anchor::BottomRight);
        QCOMPARE(legend.pos(), startPos);

        // Loading a project's legend is not an undo step.
        count = stack->index();
        LegendOverlayStyle saved;
        saved.setColumnCount(4);
        style->fromJson(saved.toJson());
        openswmmvis::map::LegendStyleUndoRecorder::forStyle(style)->resync();
        settle();
        QCOMPARE(stack->index(), count);
        QCOMPARE(style->columnCount(), 4);
    }

    /** @brief Every legend setting has a control in the Legend Properties dialog (named after
     *  the setting) that writes it, and each control follows changes made elsewhere. */
    void dialogControlsEditTheStyleAndFollowIt()
    {
        LegendOverlayStyle style;
        openswmmvis::ui::LegendPropertiesDialog dialog(&style);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        const QMetaObject &meta = LegendOverlayStyle::staticMetaObject;
        int checked = 0;
        for (int i = QObject::staticMetaObject.propertyCount(); i < meta.propertyCount(); ++i) {
            const QMetaProperty property = meta.property(i);
            const QString name = QString::fromLatin1(property.name());
            if (name == QLatin1String("autoUpdate"))
                continue; // the Items tab, which needs a map (itemsTabEditsTheTree)
            const QVariant before = property.read(&style);
            if (before.typeId() == QMetaType::QFont) {
                auto *size = dialog.findChild<QDoubleSpinBox *>(name + QStringLiteral(".size"));
                auto *bold = dialog.findChild<QCheckBox *>(name + QStringLiteral(".bold"));
                QVERIFY2(size && bold, qPrintable(name));
                size->setValue(19);
                bold->setChecked(!bold->isChecked());
                const QFont f = property.read(&style).value<QFont>();
                QCOMPARE(f.pointSizeF(), 19.0);
                QFont outside = f;
                outside.setPointSize(11);
                property.write(&style, outside);
                QTRY_COMPARE(size->value(), 11.0);
                ++checked;
                continue;
            }
            QWidget *control = dialog.findChild<QWidget *>(name);
            QVERIFY2(control, qPrintable(QStringLiteral("no control for ") + name));
            if (auto *box = qobject_cast<QCheckBox *>(control)) {
                box->setChecked(!box->isChecked());
                QCOMPARE(property.read(&style).toBool(), box->isChecked());
                property.write(&style, !box->isChecked());
                QTRY_COMPARE(box->isChecked(), property.read(&style).toBool());
            } else if (auto *spinBox = qobject_cast<QSpinBox *>(control)) {
                const int to = spinBox->value() == spinBox->maximum() ? spinBox->value() - 1
                                                                      : spinBox->value() + 1;
                spinBox->setValue(to);
                QCOMPARE(property.read(&style).toInt(), style.property(property.name()).toInt());
                QVERIFY2(property.read(&style) != before, qPrintable(name));
                property.write(&style, before);
                QTRY_COMPARE(spinBox->value(), before.toInt());
            } else if (auto *dbl = qobject_cast<QDoubleSpinBox *>(control)) {
                const double to = dbl->value() + dbl->singleStep() <= dbl->maximum()
                                      ? dbl->value() + dbl->singleStep()
                                      : dbl->value() - dbl->singleStep();
                dbl->setValue(to);
                QVERIFY2(std::abs(property.read(&style).toDouble() - to) < 1e-9, qPrintable(name));
                property.write(&style, before);
                QTRY_VERIFY(std::abs(dbl->value() - before.toDouble()) < 1e-9);
            } else if (auto *comboBox = qobject_cast<QComboBox *>(control)) {
                QVERIFY2(comboBox->count() >= 2, qPrintable(name));
                comboBox->setCurrentIndex((comboBox->currentIndex() + 1) % comboBox->count());
                QCOMPARE(property.read(&style).toInt(), comboBox->currentData().toInt());
                property.write(&style, before);
                QTRY_COMPARE(comboBox->currentData().toInt(), before.toInt());
            } else if (auto *colorButton = qobject_cast<openswmmvis::ui::ColorButton *>(control)) {
                colorButton->setColor(QColor(1, 2, 3, 4));
                QCOMPARE(property.read(&style).value<QColor>(), QColor(1, 2, 3, 4));
                property.write(&style, QColor(9, 8, 7));
                QTRY_COMPARE(colorButton->color(), QColor(9, 8, 7));
            } else if (auto *edit = qobject_cast<QLineEdit *>(control)) {
                edit->clear();
                QTest::keyClicks(edit, QStringLiteral("abc"));
                QCOMPARE(property.read(&style).toString(), QStringLiteral("abc"));
                property.write(&style, QStringLiteral("outside"));
                QTRY_COMPARE(edit->text(), QStringLiteral("outside"));
            } else {
                QFAIL(qPrintable(QStringLiteral("unknown control for ") + name));
            }
            ++checked;
        }
        QVERIFY(checked > 70);

        // Each tab, for review.
        auto *tabs = dialog.findChild<QTabWidget *>(QStringLiteral("legendTabs"));
        QVERIFY(tabs);
        for (int t = 0; t < tabs->count(); ++t) {
            tabs->setCurrentIndex(t);
            QCoreApplication::processEvents();
            m_images.append({QStringLiteral("dialog_tab_%1_%2").arg(t).arg(tabs->tabText(t)),
                             dialog.grab().toImage()});
        }
    }

    /** @brief The Items tab: taking over the order keeps what the legend shows; reorder, group,
     *  text lines and the selected item's settings write the legend tree; following the map
     *  again keeps the settings. */
    void itemsTabEditsTheTree()
    {
        LegendScene scene(QSize(800, 700));
        LegendOverlay legend(scene.canvas.get());
        LegendOverlayStyle *style = legend.style();
        openswmmvis::ui::LegendPropertiesDialog dialog(style, nullptr, scene.canvas.get());
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        auto *model = dialog.itemsModel();
        auto *view = dialog.findChild<QTreeView *>(QStringLiteral("legendItemsTree"));
        QVERIFY(model && view);
        QCOMPARE(model->rowCount(), 4);
        QCOMPARE(model->index(0, 0).data().toString(), QStringLiteral("Single layer"));
        // Structure buttons wait for the user's order.
        QVERIFY(!dialog.findChild<QPushButton *>(QStringLiteral("addGroup"))->isEnabled());

        const auto before =
            openswmmvis::map::LegendContent::buildEntries(scene.canvas.get(), style);
        dialog.findChild<QCheckBox *>(QStringLiteral("autoUpdate"))->setChecked(false);
        QVERIFY(!style->autoUpdate());
        QCOMPARE(
            lineLabels(openswmmvis::map::LegendContent::buildEntries(scene.canvas.get(), style)),
            lineLabels(before));
        QTRY_VERIFY(dialog.findChild<QPushButton *>(QStringLiteral("addGroup"))->isEnabled());

        // Select "Point kinds" and move it up.
        const auto select = [&](const QString &label) {
            for (int r = 0; r < model->rowCount(); ++r)
                if (model->index(r, 0).data().toString() == label) {
                    view->setCurrentIndex(model->index(r, 0));
                    return true;
                }
            return false;
        };
        QVERIFY(select(QStringLiteral("Point kinds")));
        dialog.findChild<QPushButton *>(QStringLiteral("itemUp"))->click();
        QTRY_COMPARE(model->index(0, 0).data().toString(), QStringLiteral("Point kinds"));
        QCOMPARE(view->currentIndex().data().toString(), QStringLiteral("Point kinds"));

        // A group after it, then "Single layer" into the group.
        dialog.findChild<QPushButton *>(QStringLiteral("addGroup"))->click();
        QTRY_COMPARE(model->rowCount(), 5);
        QCOMPARE(model->index(1, 0).data().toString(), QStringLiteral("Group"));
        QVERIFY(model->setData(model->index(1, 0), QStringLiteral("Points"), Qt::EditRole));
        QTRY_COMPARE(model->index(1, 0).data().toString(), QStringLiteral("Points"));
        QVERIFY(select(QStringLiteral("Single layer")));
        dialog.findChild<QPushButton *>(QStringLiteral("itemIntoGroup"))->click();
        QTRY_COMPARE(model->rowCount(), 4);
        QCOMPARE(model->rowCount(model->index(1, 0)), 1);
        // A text line at the end.
        QVERIFY(select(QStringLiteral("Sectioned")));
        dialog.findChild<QPushButton *>(QStringLiteral("addText"))->click();
        QTRY_COMPARE(model->rowCount(), 5);
        QVERIFY(model->setData(model->index(4, 0), QStringLiteral("Data: test"), Qt::EditRole));
        auto entries = openswmmvis::map::LegendContent::buildEntries(scene.canvas.get(), style);
        QCOMPARE(headingLabels(entries),
                 (QStringList{QStringLiteral("Point kinds"), QStringLiteral("Points"),
                              QStringLiteral("Single layer"), QStringLiteral("Graduated values"),
                              QStringLiteral("Sectioned")}));
        QTRY_COMPARE(
            openswmmvis::map::LegendContent::buildEntries(scene.canvas.get(), style).last().label,
            QStringLiteral("Data: test"));

        // The selected item's settings.
        QVERIFY(select(QStringLiteral("Graduated values")));
        auto *heading = dialog.findChild<QComboBox *>(QStringLiteral("nodeHeading"));
        heading->setCurrentIndex(heading->findData(int(LegendNode::HeadingStyle::Hidden)));
        dialog.findChild<QSpinBox *>(QStringLiteral("nodeItemColumns"))->setValue(2);
        dialog.findChild<QCheckBox *>(QStringLiteral("nodeColumnBreak"))->setChecked(true);
        const LegendNode *values =
            style->itemTree().layerNode(LegendOverlayStyle::itemKey(scene.values.get(), {}));
        QVERIFY(values);
        QCOMPARE(values->heading, LegendNode::HeadingStyle::Hidden);
        QCOMPARE(values->itemColumns, 2);
        QVERIFY(values->columnBreakBefore);
        // Uncheck a row: hidden in the legend.
        const QModelIndex kinds = model->index(0, 0);
        QVERIFY(model->setData(model->index(1, 0, kinds), Qt::Unchecked, Qt::CheckStateRole));
        QVERIFY(
            !lineLabels(openswmmvis::map::LegendContent::buildEntries(scene.canvas.get(), style))
                 .contains(QStringLiteral("Kind B (large marker)")));
        m_images.append({QStringLiteral("dialog_items"), dialog.grab().toImage()});
        m_images.append({QStringLiteral("dialog_items_legend"), drawn(&legend)});

        // Following the map again: no group or text; the layer settings stay.
        dialog.findChild<QCheckBox *>(QStringLiteral("autoUpdate"))->setChecked(true);
        entries = openswmmvis::map::LegendContent::buildEntries(scene.canvas.get(), style);
        QVERIFY(!headingLabels(entries).contains(QStringLiteral("Points")));
        QCOMPARE(style->itemTree()
                     .layerNode(LegendOverlayStyle::itemKey(scene.values.get(), {}))
                     ->itemColumns,
                 2);
    }

    /** @brief A saved look (style file) restores the look but not the title text, items,
     *  position or size; a file that is not a legend style is refused; the default look applies
     *  to new legends until the built-in one is restored. */
    void presetsAndTheDefaultLook()
    {
        LegendOverlayStyle style;
        openswmmvis::ui::LegendPropertiesDialog dialog(&style);
        style.setColumnCount(3);
        style.setShowShadow(true);
        style.setTitle(QStringLiteral("Mine"));
        style.setFreePosition(QPoint(40, 50));
        style.setItemVisible(QStringLiteral("L1"), QStringLiteral("0"), false);
        const QString file = outDir() + QStringLiteral("/look.oswlegend");
        QString error;
        QVERIFY2(dialog.savePreset(file, &error), qPrintable(error));

        LegendOverlayStyle other;
        openswmmvis::ui::LegendPropertiesDialog otherDialog(&other);
        other.setTitle(QStringLiteral("Theirs"));
        other.setFreePosition(QPoint(7, 8));
        QVERIFY2(otherDialog.loadPreset(file, &error), qPrintable(error));
        QCOMPARE(other.columnCount(), 3);
        QVERIFY(other.showShadow());
        QCOMPARE(other.title(), QStringLiteral("Theirs"));
        QCOMPARE(other.freePosition(), QPoint(7, 8));
        QVERIFY(other.itemOverride(QStringLiteral("L1"), QStringLiteral("0")).visible);

        const QString notStyle = outDir() + QStringLiteral("/not_a_style.oswlegend");
        QFile bad(notStyle);
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write(R"({"type": "something-else", "style": {"columnCount": 9}})");
        bad.close();
        QVERIFY(!otherDialog.loadPreset(notStyle, &error));
        QVERIFY(error.contains(QStringLiteral("not a legend style")));
        QCOMPARE(other.columnCount(), 3);

        LegendOverlayStyle::setDefaultPreset(style.presetJson());
        {
            LegendScene scene(QSize(800, 700));
            LegendOverlay fresh(scene.canvas.get());
            QCOMPARE(fresh.style()->columnCount(), 3);
            QVERIFY(fresh.style()->showShadow());
            QVERIFY(fresh.style()->title().isEmpty());
        }
        LegendOverlayStyle::setDefaultPreset({});
        {
            LegendScene scene(QSize(800, 700));
            LegendOverlay fresh(scene.canvas.get());
            QCOMPARE(fresh.style()->columnCount(), 1);
        }
    }

    /** @brief The Properties panel shows the legend's settings in groups, and edits there write
     *  the legend. */
    void propertiesPanelGroupsTheLegend()
    {
        PropertiesPanel panel;
        panel.resize(420, 800);
        panel.show();
        LegendOverlayStyle style;
        panel.showStyleObject(&style, QStringLiteral("Legend"));
        auto *tree = panel.findChild<QTreeView *>();
        QAbstractItemModel *model = tree->model();
        QStringList groups;
        for (int r = 0; r < model->rowCount(); ++r)
            groups.append(model->index(r, 0).data().toString());
        for (const char *g : {"Title", "Columns", "Size", "Text", "Symbols", "Spacing", "Content",
                              "Color ramps", "General", "Margins", "Frame", "Background", "Shadow"})
            QVERIFY2(groups.contains(QString::fromLatin1(g)),
                     qPrintable(QString::fromLatin1(g) + QStringLiteral(" in ") +
                                groups.join(QStringLiteral(", "))));
        QModelIndex columns;
        for (int r = 0; r < model->rowCount() && !columns.isValid(); ++r) {
            const QModelIndex group = model->index(r, 0);
            for (int c = 0; c < model->rowCount(group); ++c)
                if (model->index(c, 0, group).data().toString() ==
                    QStringLiteral("Columns — Count"))
                    columns = model->index(c, 0, group);
        }
        QVERIFY(columns.isValid());
        QVERIFY(model->setData(columns.siblingAtColumn(1), 3, Qt::EditRole));
        QCOMPARE(style.columnCount(), 3);
        m_images.append({QStringLiteral("properties_panel"), panel.grab().toImage()});
    }

    /** @brief Hidden layers and hidden rows leave no space. */
    void hiddenLayersAndRowsLeaveNoSpace()
    {
        LegendScene scene(QSize(800, 700));
        LegendOverlay legend(scene.canvas.get());
        scene.values->setVisible(false);
        legend.style()->setItemVisible(LegendOverlayStyle::itemKey(scene.kinds.get(), {}),
                                       QStringLiteral("1"), false);
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
