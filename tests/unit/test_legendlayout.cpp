/**
 * @file      test_legendlayout.cpp
 * @brief     Map legend geometry: where the title, headings, symbols and labels go, the
 *            legend's size, text shortening and which line a point picks.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <gtest/gtest.h>

#include "render/legendentry.h"
#include "render/legendlayout.h"
#include "render/legendoverlaystyle.h"
#include "render/symbollayer.h"

#include <QFontMetrics>
#include <QGuiApplication>

using OpenSWMM::Render::LegendEntry;
using OpenSWMM::Render::LegendLayoutResult;
using OpenSWMM::Render::LegendOverlayStyle;
using OpenSWMM::Render::SymbolLayer;
using OpenSWMM::Render::SymbolLayerKind;
using OpenSWMM::Render::SymbolStyle;
namespace LegendLayout = OpenSWMM::Render::LegendLayout;

namespace
{

// Fonts need a QGuiApplication; the offscreen platform needs no display.
struct LayoutFixture : public ::testing::Test {
    static void SetUpTestSuite()
    {
        if (!QCoreApplication::instance()) {
            if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
                qputenv("QT_QPA_PLATFORM", "offscreen");
            static int argc = 1;
            static char arg0[] = "test_legendlayout";
            static char *argv[] = {arg0, nullptr};
            new QGuiApplication(argc, argv);
        }
    }
};

SymbolStyle pass(SymbolLayerKind kind, const char *key = nullptr, double value = 0)
{
    SymbolLayer layer;
    layer.kind = kind;
    if (key)
        layer.props.insert(QString::fromLatin1(key), value);
    return SymbolStyle{{layer}};
}

LegendEntry heading(const QString &label, int block)
{
    LegendEntry e;
    e.kind = LegendEntry::Kind::GroupHeading;
    e.label = label;
    e.blockId = block;
    return e;
}

LegendEntry row(const QString &label, const SymbolStyle &symbol, int block)
{
    LegendEntry e;
    e.kind = symbol.layers.isEmpty() ? LegendEntry::Kind::Subheading : LegendEntry::Kind::Symbol;
    e.label = label;
    e.symbol = symbol;
    e.blockId = block;
    return e;
}

} // namespace

TEST_F(LayoutFixture, NothingToShowHasNoSize)
{
    LegendOverlayStyle style;
    const auto layout = LegendLayout::compute({}, style);
    EXPECT_TRUE(layout.isEmpty());
    EXPECT_EQ(layout.contentSize, QSizeF());
}

TEST_F(LayoutFixture, TitleAloneIsPaddedAndSpaced)
{
    LegendOverlayStyle style;
    style.setShowTitle(true);
    style.setTitle(QStringLiteral("Legend"));
    const QFontMetrics tfm(style.titleFont());
    const auto layout = LegendLayout::compute({}, style);
    ASSERT_EQ(layout.boxes.size(), 1);
    EXPECT_EQ(layout.boxes[0].entryIndex, -1);
    EXPECT_EQ(layout.contentSize, QSizeF(tfm.horizontalAdvance(QStringLiteral("Legend")) + 16,
                                         8 + tfm.height() + 2 + 8));
    EXPECT_EQ(layout.boxes[0].textRect.topLeft(), QPointF(8, 8));
}

TEST_F(LayoutFixture, RowsStackUnderTheirHeadingWithSymbolsBeforeLabels)
{
    LegendOverlayStyle style;
    const QFontMetrics fm(style.itemFont());
    const QFontMetrics hfm(style.layerHeaderFont());
    const QList<LegendEntry> entries = {
        heading(QStringLiteral("Layer"), 1),
        row(QStringLiteral("Fill"), pass(SymbolLayerKind::SimpleFill), 1),
        row(QStringLiteral("Big marker"), pass(SymbolLayerKind::SimpleMarker, "size", 22), 1),
        row(QStringLiteral("Section"), SymbolStyle{}, 1),
    };
    const auto layout = LegendLayout::compute(entries, style);
    ASSERT_EQ(layout.boxes.size(), 4);

    int y = 8;
    EXPECT_EQ(layout.boxes[0].textRect.top(), y);
    y += hfm.height();
    // The marker is wider than the 14 px swatch, so the layer's symbol slot is 22 wide.
    const int fillH = std::max(fm.height(), 14);
    EXPECT_EQ(layout.boxes[1].symbolRect, QRectF(8, y, 22, fillH));
    EXPECT_EQ(layout.boxes[1].textRect.left(), 8 + 22 + 6);
    y += fillH + 2;
    const int markerH = std::max(fm.height(), 22);
    EXPECT_EQ(layout.boxes[2].symbolRect, QRectF(8, y, 22, markerH));
    y += markerH + 2;
    // A sub-heading has no symbol; its text starts at the padding.
    EXPECT_TRUE(layout.boxes[3].symbolRect.isEmpty());
    EXPECT_EQ(layout.boxes[3].textRect.left(), 8);
    y += std::max(fm.height(), 14) + 2;
    EXPECT_EQ(layout.contentSize.height(), y + 8);
}

TEST_F(LayoutFixture, EachLayerHasItsOwnSymbolSlotWidth)
{
    LegendOverlayStyle style;
    const QList<LegendEntry> entries = {
        heading(QStringLiteral("Lines"), 1),
        row(QStringLiteral("Wide"), pass(SymbolLayerKind::SimpleLine, "width", 4), 1),
        heading(QStringLiteral("Fills"), 2),
        row(QStringLiteral("Fill"), pass(SymbolLayerKind::SimpleFill), 2),
    };
    const auto layout = LegendLayout::compute(entries, style);
    // Lines take at least twice the swatch; fills keep the swatch.
    EXPECT_EQ(layout.boxes[1].symbolRect.width(), 28);
    EXPECT_EQ(layout.boxes[3].symbolRect.width(), 14);
    // The second heading sits 6 px below the first layer's last row.
    EXPECT_EQ(layout.boxes[2].rect.top(), layout.boxes[1].rect.bottom() + 2 + 6);
}

TEST_F(LayoutFixture, NarrowFixedWidthShortensLabels)
{
    LegendOverlayStyle style;
    // Fits the 220 px label limit, but not a 120 px box.
    const QString longLabel = QStringLiteral("A longer legend label");
    const int labelW = QFontMetrics(style.itemFont()).horizontalAdvance(longLabel);
    ASSERT_GT(labelW, 120 - 8 - 14 - 6 - 8);
    ASSERT_LT(labelW, 220);
    const QList<LegendEntry> entries = {heading(QStringLiteral("Layer"), 1),
                                        row(longLabel, pass(SymbolLayerKind::SimpleFill), 1)};
    const auto natural = LegendLayout::compute(entries, style);
    EXPECT_EQ(natural.boxes[1].lines.value(0), longLabel);

    style.setExplicitWidth(120);
    const auto narrow = LegendLayout::compute(entries, style);
    EXPECT_EQ(narrow.width, 120);
    EXPECT_TRUE(narrow.boxes[1].lines.value(0).endsWith(QChar(0x2026)));
    EXPECT_EQ(narrow.boxes[1].textRect.right(), 120 - 8);
    // A live resize lays out for the box width, not the saved one.
    EXPECT_EQ(LegendLayout::compute(entries, style, 400).boxes[1].lines.value(0), longLabel);
    // The fitted size does not depend on the fixed width.
    EXPECT_EQ(narrow.naturalSize, natural.naturalSize);
}

TEST_F(LayoutFixture, ClickingPicksTheRowAboveTheSpaceBelowIt)
{
    LegendOverlayStyle style;
    style.setRowSpacing(6);
    const QList<LegendEntry> entries = {
        heading(QStringLiteral("A"), 1),
        row(QStringLiteral("a"), pass(SymbolLayerKind::SimpleFill), 1),
        heading(QStringLiteral("B"), 2),
        row(QStringLiteral("b"), pass(SymbolLayerKind::SimpleFill), 2)};
    const auto layout = LegendLayout::compute(entries, style);
    const QRectF rowA = layout.boxes[1].rect;
    const auto *inSpacing = layout.boxAt(QPointF(rowA.center().x(), rowA.bottom() + 3));
    ASSERT_NE(inSpacing, nullptr);
    EXPECT_EQ(inSpacing->entryIndex, 1);
    // The gap above the second heading picks nothing.
    const QRectF headingB = layout.boxes[2].rect;
    EXPECT_EQ(layout.boxAt(QPointF(headingB.center().x(), headingB.top() - 3)), nullptr);
    EXPECT_EQ(layout.boxAt(QPointF(headingB.center().x(), headingB.center().y()))->entryIndex, 2);
}

// ── Columns, wrapping, symbols and spacing ────────────────────────────────────

namespace
{
/** @p layers layers named L1…, each a heading and @p rows fill rows. */
QList<LegendEntry> layers(int count, int rows)
{
    QList<LegendEntry> out;
    for (int b = 1; b <= count; ++b) {
        out.append(heading(QStringLiteral("L%1").arg(b), b));
        for (int r = 0; r < rows; ++r)
            out.append(row(QStringLiteral("row %1.%2").arg(b).arg(r),
                           pass(SymbolLayerKind::SimpleFill), b));
    }
    return out;
}

/** Column of each heading, in order. */
QList<int> headingColumns(const LegendLayoutResult &layout, const QList<LegendEntry> &entries)
{
    QList<int> out;
    for (const auto &box : layout.boxes)
        if (box.entryIndex >= 0 && entries[box.entryIndex].kind == LegendEntry::Kind::GroupHeading)
            out.append(box.column);
    return out;
}
} // namespace

TEST_F(LayoutFixture, LayersSpreadEvenlyOverColumns)
{
    LegendOverlayStyle style;
    const auto entries = layers(6, 2);
    style.setColumnCount(2);
    auto layout = LegendLayout::compute(entries, style);
    EXPECT_EQ(headingColumns(layout, entries), (QList<int>{0, 0, 0, 1, 1, 1}));
    EXPECT_EQ(layout.columnCount, 2);
    style.setColumnCount(3);
    layout = LegendLayout::compute(entries, style);
    EXPECT_EQ(headingColumns(layout, entries), (QList<int>{0, 0, 1, 1, 2, 2}));
    // The second column starts after the first column's width and the column spacing, and both
    // columns start at the top.
    const auto &first = layout.boxes[0];
    const auto &third = layout.boxes[2 * 3]; // heading of the third layer, top of column 1
    EXPECT_EQ(third.column, 1);
    EXPECT_EQ(third.rect.top(), first.rect.top());
    EXPECT_EQ(third.rect.left(), first.rect.right() + style.columnSpacing());
    // Two columns are shorter and wider than one.
    style.setColumnCount(1);
    const auto single = LegendLayout::compute(entries, style);
    style.setColumnCount(2);
    const auto twin = LegendLayout::compute(entries, style);
    EXPECT_LT(twin.contentSize.height(), single.contentSize.height());
    EXPECT_GT(twin.contentSize.width(), single.contentSize.width());
}

TEST_F(LayoutFixture, LayersStayWholeUnlessSplit)
{
    LegendOverlayStyle style;
    style.setColumnCount(2);
    QList<LegendEntry> entries = layers(1, 10);
    entries += layers(1, 1).mid(0); // a second, small layer (block 1 again: renumber)
    entries[11].blockId = entries[12].blockId = 2;
    auto layout = LegendLayout::compute(entries, style);
    for (int i = 0; i <= 10; ++i)
        EXPECT_EQ(layout.boxes[i].column, 0) << i;

    style.setSplitLayers(true);
    layout = LegendLayout::compute(entries, style);
    EXPECT_EQ(layout.columnCount, 2);
    // The big layer's rows continue in the second column; its heading stays with its first row.
    EXPECT_EQ(layout.boxes[0].column, layout.boxes[1].column);
    EXPECT_EQ(layout.boxes[10].column, 1);
    // No column ends with a heading whose rows are in the next column.
    EXPECT_EQ(layout.boxes[11].column, layout.boxes[12].column);
}

TEST_F(LayoutFixture, EqualColumnWidthsAndSeparators)
{
    LegendOverlayStyle style;
    style.setColumnCount(2);
    QList<LegendEntry> entries = {heading(QStringLiteral("Short"), 1),
                                  row(QStringLiteral("a"), pass(SymbolLayerKind::SimpleFill), 1),
                                  heading(QStringLiteral("A much longer heading"), 2),
                                  row(QStringLiteral("b"), pass(SymbolLayerKind::SimpleFill), 2)};
    auto layout = LegendLayout::compute(entries, style);
    EXPECT_LT(layout.boxes[0].rect.width(), layout.boxes[2].rect.width());
    EXPECT_TRUE(layout.columnSeparators.isEmpty());
    style.setEqualColumnWidths(true);
    style.setShowColumnSeparators(true);
    layout = LegendLayout::compute(entries, style);
    EXPECT_EQ(layout.boxes[0].rect.width(), layout.boxes[2].rect.width());
    ASSERT_EQ(layout.columnSeparators.size(), 1);
    const double gapMiddle = layout.boxes[0].rect.right() + style.columnSpacing() / 2.0;
    EXPECT_DOUBLE_EQ(layout.columnSeparators[0].x1(), gapMiddle);
}

TEST_F(LayoutFixture, WrappedLabelsGrowTheirRow)
{
    LegendOverlayStyle style;
    style.setLabelOverflow(LegendOverlayStyle::LabelOverflow::Wrap);
    style.setMaxLabelWidth(80);
    style.setLineSpacing(3);
    const QString label = QStringLiteral("one two three four five six seven eight");
    const QList<LegendEntry> entries = {heading(QStringLiteral("L"), 1),
                                        row(label, pass(SymbolLayerKind::SimpleFill), 1)};
    const auto layout = LegendLayout::compute(entries, style);
    const auto &box = layout.boxes[1];
    ASSERT_GT(box.lines.size(), 2);
    EXPECT_EQ(box.lines.join(QLatin1Char(' ')), label);
    const QFontMetrics fm(style.itemFont());
    for (const QString &line : box.lines)
        EXPECT_LE(fm.horizontalAdvance(line), 80) << line.toStdString();
    EXPECT_EQ(box.rect.height(), box.lines.size() * fm.height() + (box.lines.size() - 1) * 3);

    // A narrower fixed width wraps into more lines.
    style.setMaxLabelWidth(400);
    const auto wide = LegendLayout::compute(entries, style);
    style.setExplicitWidth(90);
    const auto narrow = LegendLayout::compute(entries, style);
    EXPECT_GT(narrow.boxes[1].lines.size(), wide.boxes[1].lines.size());

    // Labels also break at the wrap text.
    style.setExplicitWidth(0);
    style.setWrapString(QStringLiteral("|"));
    const auto split = LegendLayout::compute(
        {heading(QStringLiteral("L"), 1),
         row(QStringLiteral("Top|Bottom"), pass(SymbolLayerKind::SimpleFill), 1)},
        style);
    EXPECT_EQ(split.boxes[1].lines, (QStringList{QStringLiteral("Top"), QStringLiteral("Bottom")}));
}

TEST_F(LayoutFixture, SymbolsOnTheRightAndFittedSymbols)
{
    LegendOverlayStyle style;
    const QList<LegendEntry> entries = {
        heading(QStringLiteral("L"), 1),
        row(QStringLiteral("Big"), pass(SymbolLayerKind::SimpleMarker, "size", 40), 1)};
    style.setSymbolPosition(LegendOverlayStyle::SymbolPosition::Right);
    auto layout = LegendLayout::compute(entries, style);
    const auto &box = layout.boxes[1];
    EXPECT_EQ(box.symbolRect.right(), box.rect.right());
    EXPECT_EQ(box.textRect.left(), box.rect.left());
    EXPECT_LE(box.textRect.right() + style.symbolLabelSpacing(), box.symbolRect.left());
    // As on the map, the slot grows to the marker; fitted, it keeps the set size.
    EXPECT_EQ(box.symbolRect.width(), 40);
    style.setSymbolSizeMode(LegendOverlayStyle::SymbolSizeMode::Fit);
    style.setSymbolWidth(16);
    style.setSymbolHeight(12);
    layout = LegendLayout::compute(entries, style);
    EXPECT_EQ(layout.boxes[1].symbolRect.width(), 16);
    EXPECT_EQ(LegendLayout::symbolExtent(entries[1].symbol, style), QSize(16, 12));
    // The largest symbol size limits a marker on the map too.
    style.setSymbolSizeMode(LegendOverlayStyle::SymbolSizeMode::MapSize);
    style.setSymbolWidth(0);
    style.setSymbolHeight(0);
    style.setMaxSymbolSize(20);
    EXPECT_EQ(LegendLayout::symbolExtent(entries[1].symbol, style), QSize(20, 20));
}

TEST_F(LayoutFixture, MarginsIndentsAndSpacing)
{
    LegendOverlayStyle style;
    style.setMarginLeft(20);
    style.setMarginTop(15);
    style.setMarginRight(5);
    style.setMarginBottom(9);
    style.setItemIndent(10);
    style.setGroupSpacing(12);
    style.setHeaderSpacing(4);
    const QList<LegendEntry> entries = {
        heading(QStringLiteral("A"), 1),
        row(QStringLiteral("a"), pass(SymbolLayerKind::SimpleFill), 1),
        heading(QStringLiteral("B"), 2),
        row(QStringLiteral("b"), pass(SymbolLayerKind::SimpleFill), 2)};
    const auto layout = LegendLayout::compute(entries, style);
    const QFontMetrics fm(style.itemFont());
    const QFontMetrics hfm(style.layerHeaderFont());
    EXPECT_EQ(layout.boxes[0].rect.topLeft(), QPointF(20, 15));
    EXPECT_EQ(layout.boxes[1].rect.top(), 15 + hfm.height() + 4);
    EXPECT_EQ(layout.boxes[1].symbolRect.left(), 20 + 10);
    EXPECT_EQ(layout.boxes[2].rect.top(), layout.boxes[1].rect.bottom() + 2 + 12);
    const int rowW = 10 + 14 + 6 +
                     std::max(fm.horizontalAdvance(QStringLiteral("a")),
                              fm.horizontalAdvance(QStringLiteral("b")));
    const int widest = std::max({rowW, hfm.horizontalAdvance(QStringLiteral("A")),
                                 hfm.horizontalAdvance(QStringLiteral("B"))});
    EXPECT_EQ(layout.contentSize.width(), 20 + widest + 5);
    EXPECT_EQ(layout.contentSize.height(), layout.boxes[3].rect.bottom() + 2 + 9);
}
