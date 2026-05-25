/**
 * @file test_legendoverlaystyle.cpp
 * @brief Slice BB Phase 8.6.16 — LegendOverlayStyle JSON round-trip,
 *        defaults, anchor / background-mode enum handling, and the
 *        signal-emission contract that downstream views rely on.
 */
#include <gtest/gtest.h>

#include "render/legendoverlaystyle.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QSignalSpy>

using OpenSWMM::Render::LegendOverlayStyle;

namespace {

// Each test instance needs a QCoreApplication for Qt meta-system + signals.
struct QtAppFixture : public ::testing::Test
{
    static void SetUpTestSuite()
    {
        if (!QCoreApplication::instance()) {
            static int argc = 1;
            static char  arg0[] = "test_legendoverlaystyle";
            static char *argv[] = { arg0, nullptr };
            new QCoreApplication(argc, argv);
        }
    }
};

} // namespace

// ── Defaults reproduce legacy overlay appearance ────────────────────────

TEST_F(QtAppFixture, Defaults_MatchLegacyLegendOverlay)
{
    LegendOverlayStyle style;
    EXPECT_EQ(style.padding(), 8);
    EXPECT_EQ(style.swatchSize(), 14);
    EXPECT_EQ(style.rowSpacing(), 2);
    EXPECT_EQ(style.cornerRadius(), 6);
    EXPECT_EQ(style.anchor(), LegendOverlayStyle::Anchor::BottomRight);
    EXPECT_EQ(style.backgroundMode(), LegendOverlayStyle::BackgroundMode::Solid);
    EXPECT_TRUE(style.showFrame());
    EXPECT_FALSE(style.showTitle());
    EXPECT_DOUBLE_EQ(style.opacity(), 1.0);
    EXPECT_DOUBLE_EQ(style.frameWidth(), 1.0);
}

// ── JSON round-trip preserves every field ───────────────────────────────

TEST_F(QtAppFixture, JsonRoundTrip_PreservesAllFields)
{
    LegendOverlayStyle src;
    src.setShowTitle(true);
    src.setTitle(QStringLiteral("My run — 2026-05-24"));
    src.setPadding(12);
    src.setSwatchSize(20);
    src.setRowSpacing(4);
    src.setAnchor(LegendOverlayStyle::Anchor::TopLeft);
    src.setOpacity(0.75);
    src.setShowFrame(false);
    src.setFrameColor(QColor(10, 20, 30, 200));
    src.setFrameWidth(2.5);
    src.setCornerRadius(12);
    src.setBackgroundMode(LegendOverlayStyle::BackgroundMode::Gradient);
    src.setBackgroundColor(QColor(255, 255, 255, 220));
    src.setGradientEndColor(QColor(200, 220, 255, 220));
    src.setGradientOrientation(Qt::Horizontal);
    QFont tf; tf.setPointSize(13); tf.setBold(true);
    src.setTitleFont(tf);
    src.setItemColor(QColor(50, 60, 70));

    const QJsonObject blob = src.toJson();

    LegendOverlayStyle dst;
    dst.fromJson(blob);

    EXPECT_EQ(dst.showTitle(), src.showTitle());
    EXPECT_EQ(dst.title(), src.title());
    EXPECT_EQ(dst.padding(), src.padding());
    EXPECT_EQ(dst.swatchSize(), src.swatchSize());
    EXPECT_EQ(dst.rowSpacing(), src.rowSpacing());
    EXPECT_EQ(dst.anchor(), src.anchor());
    EXPECT_DOUBLE_EQ(dst.opacity(), src.opacity());
    EXPECT_EQ(dst.showFrame(), src.showFrame());
    EXPECT_EQ(dst.frameColor(), src.frameColor());
    EXPECT_DOUBLE_EQ(dst.frameWidth(), src.frameWidth());
    EXPECT_EQ(dst.cornerRadius(), src.cornerRadius());
    EXPECT_EQ(dst.backgroundMode(), src.backgroundMode());
    EXPECT_EQ(dst.backgroundColor(), src.backgroundColor());
    EXPECT_EQ(dst.gradientEndColor(), src.gradientEndColor());
    EXPECT_EQ(dst.gradientOrientation(), src.gradientOrientation());
    EXPECT_EQ(dst.titleFont().pointSize(), 13);
    EXPECT_TRUE(dst.titleFont().bold());
    EXPECT_EQ(dst.itemColor(), src.itemColor());
}

// ── Anchor enum covers all 9 positions through JSON ─────────────────────

TEST_F(QtAppFixture, AnchorEnum_AllValuesRoundTrip)
{
    using A = LegendOverlayStyle::Anchor;
    for (auto a : { A::TopLeft, A::Top, A::TopRight, A::Right,
                    A::BottomRight, A::Bottom, A::BottomLeft, A::Left, A::Free }) {
        LegendOverlayStyle src;
        src.setAnchor(a);
        LegendOverlayStyle dst;
        dst.fromJson(src.toJson());
        EXPECT_EQ(dst.anchor(), a)
            << "Anchor enum value " << static_cast<int>(a) << " did not round-trip";
    }
}

// ── Background mode switching emits change signal + serializes ──────────

TEST_F(QtAppFixture, BackgroundMode_SwitchEmitsChangedAndSerializes)
{
    LegendOverlayStyle style;
    QSignalSpy changedSpy(&style, &LegendOverlayStyle::changed);
    QSignalSpy modeSpy(&style, &LegendOverlayStyle::backgroundModeChanged);

    style.setBackgroundMode(LegendOverlayStyle::BackgroundMode::Gradient);
    EXPECT_EQ(modeSpy.count(), 1);
    EXPECT_EQ(changedSpy.count(), 1);

    // No-op re-set should NOT emit again.
    style.setBackgroundMode(LegendOverlayStyle::BackgroundMode::Gradient);
    EXPECT_EQ(modeSpy.count(), 1);
    EXPECT_EQ(changedSpy.count(), 1);

    // None mode also round-trips.
    style.setBackgroundMode(LegendOverlayStyle::BackgroundMode::None);
    EXPECT_EQ(modeSpy.count(), 2);
    LegendOverlayStyle dst;
    dst.fromJson(style.toJson());
    EXPECT_EQ(dst.backgroundMode(), LegendOverlayStyle::BackgroundMode::None);
}

// ── ResetToDefaults restores every field from a fully-customized state ──

TEST_F(QtAppFixture, ResetToDefaults_RestoresAllFields)
{
    LegendOverlayStyle style;
    style.setPadding(99);
    style.setSwatchSize(40);
    style.setShowTitle(true);
    style.setTitle(QStringLiteral("Custom"));
    style.setAnchor(LegendOverlayStyle::Anchor::Free);
    style.setBackgroundMode(LegendOverlayStyle::BackgroundMode::None);

    QSignalSpy changedSpy(&style, &LegendOverlayStyle::changed);
    style.resetToDefaults();
    // Reset fires several setters, so changed() arrives multiple times.
    EXPECT_GT(changedSpy.count(), 0);

    EXPECT_EQ(style.padding(), 8);
    EXPECT_EQ(style.swatchSize(), 14);
    EXPECT_FALSE(style.showTitle());
    EXPECT_TRUE(style.title().isEmpty());
    EXPECT_EQ(style.anchor(), LegendOverlayStyle::Anchor::BottomRight);
    EXPECT_EQ(style.backgroundMode(), LegendOverlayStyle::BackgroundMode::Solid);
}
