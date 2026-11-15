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
