/**
 * @file      test_legendoverlaystyle.cpp
 * @brief     Slice BB Phase 8.6.16 — LegendOverlayStyle JSON round-trip,
 *        defaults, anchor / background-mode enum handling, and the
 *        signal-emission contract that downstream views rely on.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <gtest/gtest.h>

#include "render/legendoverlaystyle.h"

#include <QGuiApplication>
#include <QJsonObject>
#include <QMetaEnum>
#include <QMetaProperty>
#include <QSignalSpy>

using OpenSWMM::Render::LegendOverlayStyle;

namespace
{

// Each test instance needs a QCoreApplication for Qt meta-system + signals.
struct QtAppFixture : public ::testing::Test {
    /**
     *
     * @brief Ensure a persistent core application exists for Qt signal assertions.
     * Creates one only when absent, with static argument storage kept valid for its
     * lifetime. Intentionally retains the application for this binary; does not create
     * GUI widgets, change application names or reset existing application state.
     *
     */
    static void SetUpTestSuite()
    {
        if (!QCoreApplication::instance()) {
            static int argc = 1;
            static char arg0[] = "test_legendoverlaystyle";
            static char *argv[] = {arg0, nullptr};
            // Fonts need a QGuiApplication; the offscreen platform needs no display.
            if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
                qputenv("QT_QPA_PLATFORM", "offscreen");
            new QGuiApplication(argc, argv);
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
    QFont tf;
    tf.setPointSize(13);
    tf.setBold(true);
    src.setTitleFont(tf);
    src.setItemColor(QColor(50, 60, 70));
    // Slice US.B1 — size / position.
    src.setExplicitWidth(320);
    src.setExplicitHeight(200);
    src.setMaxLabelWidth(160);
    src.setFreePosition(QPoint(42, 17));

    const QJsonObject blob = src.toJson();

    LegendOverlayStyle dst;
    dst.fromJson(blob);

    EXPECT_EQ(dst.explicitWidth(), 320);
    EXPECT_EQ(dst.explicitHeight(), 200);
    EXPECT_EQ(dst.maxLabelWidth(), 160);
    EXPECT_EQ(dst.freePosition(), QPoint(42, 17));

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
    for (auto a : {A::TopLeft, A::Top, A::TopRight, A::Right, A::BottomRight, A::Bottom,
                   A::BottomLeft, A::Left, A::Free}) {
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

// ── Slice US.B1 — size clamping + auto sentinel ─────────────────────────

TEST_F(QtAppFixture, ExplicitSize_ClampsToUsableMinimumOrAuto)
{
    LegendOverlayStyle style;
    EXPECT_EQ(style.explicitWidth(), 0); // 0 = auto by default
    EXPECT_EQ(style.explicitHeight(), 0);
    EXPECT_EQ(style.maxLabelWidth(), 220);

    style.setExplicitWidth(20); // below floor → 80
    EXPECT_EQ(style.explicitWidth(), 80);
    style.setExplicitHeight(5); // below floor → 48
    EXPECT_EQ(style.explicitHeight(), 48);
    style.setExplicitWidth(0); // auto sentinel passes through
    EXPECT_EQ(style.explicitWidth(), 0);
    style.setExplicitWidth(-10); // negative → auto
    EXPECT_EQ(style.explicitWidth(), 0);

    style.setMaxLabelWidth(10); // clamps up to 60
    EXPECT_EQ(style.maxLabelWidth(), 60);
    style.setMaxLabelWidth(5000); // clamps down to 1200
    EXPECT_EQ(style.maxLabelWidth(), 1200);
}

// ── Every property is saved and read back, and the saved object has every key ─────
namespace
{
/** A value different from @p current for a property of the type of @p current. */
QVariant otherValue(const QMetaProperty &property, const QVariant &current)
{
    if (property.isEnumType()) {
        const QMetaEnum e = property.enumerator();
        for (int k = 0; k < e.keyCount(); ++k)
            if (e.value(k) == current.toInt())
                return e.value((k + 1) % e.keyCount());
        return e.value(0);
    }
    switch (current.typeId()) {
    case QMetaType::Bool:
        return !current.toBool();
    case QMetaType::Int:
        return current.toInt() + 3;
    case QMetaType::Double:
        return current.toDouble() > 0.5 ? current.toDouble() - 0.25 : current.toDouble() + 0.25;
    case QMetaType::QString:
        return QStringLiteral("changed");
    case QMetaType::QColor:
        return QColor(1, 2, 3, 4);
    case QMetaType::QFont: {
        QFont f = current.value<QFont>();
        f.setPointSize(21);
        f.setBold(!f.bold());
        return f;
    }
    default:
        return current;
    }
}
} // namespace

TEST_F(QtAppFixture, EveryPropertyRoundTripsThroughJson)
{
    LegendOverlayStyle src;
    const LegendOverlayStyle defaults;
    const QMetaObject &meta = LegendOverlayStyle::staticMetaObject;
    int changed = 0;
    for (int i = QObject::staticMetaObject.propertyCount(); i < meta.propertyCount(); ++i) {
        const QMetaProperty property = meta.property(i);
        ASSERT_TRUE(property.write(&src, otherValue(property, property.read(&src))))
            << property.name();
        EXPECT_NE(property.read(&src), property.read(&defaults))
            << property.name() << " did not take a non-default value";
        ++changed;
    }
    EXPECT_GT(changed, 60);

    const QJsonObject saved = src.toJson();
    LegendOverlayStyle dst;
    dst.fromJson(saved);
    for (int i = QObject::staticMetaObject.propertyCount(); i < meta.propertyCount(); ++i) {
        const QMetaProperty property = meta.property(i);
        const QVariant a = property.read(&src);
        const QVariant b = property.read(&dst);
        if (a.typeId() == QMetaType::QFont) {
            EXPECT_EQ(a.value<QFont>().pointSize(), b.value<QFont>().pointSize())
                << property.name();
            EXPECT_EQ(a.value<QFont>().bold(), b.value<QFont>().bold()) << property.name();
        } else {
            EXPECT_EQ(a, b) << property.name();
        }
    }
    EXPECT_EQ(dst.toJson(), saved);
}

TEST_F(QtAppFixture, SavedObjectHasEveryKey)
{
    // Undo restores a style from a saved object, so every setting must be in it.
    const QJsonObject saved = LegendOverlayStyle().toJson();
    const QMetaObject &meta = LegendOverlayStyle::staticMetaObject;
    for (int i = QObject::staticMetaObject.propertyCount(); i < meta.propertyCount(); ++i)
        EXPECT_TRUE(saved.contains(QString::fromLatin1(meta.property(i).name())))
            << meta.property(i).name();
    EXPECT_TRUE(saved.contains(QStringLiteral("freeX")));
    EXPECT_TRUE(saved.contains(QStringLiteral("padding")));
}

TEST_F(QtAppFixture, OlderFilesKeepTheirLook)
{
    // A legend saved before the four margins, the sub-heading font and the title spacing.
    QFont item;
    item.setPointSize(15);
    QJsonObject old;
    old[QStringLiteral("padding")] = 12;
    old[QStringLiteral("itemFont")] = OpenSWMM::Render::overlayFontToJson(item);
    old[QStringLiteral("itemColor")] = QStringLiteral("#ff102030");
    old[QStringLiteral("rowSpacing")] = 5;
    LegendOverlayStyle style;
    style.fromJson(old);
    EXPECT_EQ(style.marginLeft(), 12);
    EXPECT_EQ(style.marginTop(), 12);
    EXPECT_EQ(style.marginRight(), 12);
    EXPECT_EQ(style.marginBottom(), 12);
    EXPECT_EQ(style.subheadingFont().pointSize(), 15);
    EXPECT_EQ(style.subheadingColor(), QColor(0x10, 0x20, 0x30));
    EXPECT_EQ(style.titleSpacing(), 5);

    // A newer file has both; its four margins win over "padding".
    LegendOverlayStyle newer;
    newer.setPadding(4);
    newer.setMarginRight(30);
    LegendOverlayStyle read;
    read.fromJson(newer.toJson());
    EXPECT_EQ(read.marginLeft(), 4);
    EXPECT_EQ(read.marginRight(), 30);
    // A newer file's own sub-heading font is kept.
    QFont sub;
    sub.setPointSize(9);
    newer.setSubheadingFont(sub);
    newer.setItemFont(item);
    read.fromJson(newer.toJson());
    EXPECT_EQ(read.subheadingFont().pointSize(), 9);
}

TEST_F(QtAppFixture, SettersClampToTheirRanges)
{
    LegendOverlayStyle style;
    style.setColumnCount(0);
    EXPECT_EQ(style.columnCount(), 1);
    style.setColumnCount(40);
    EXPECT_EQ(style.columnCount(), 12);
    style.setOpacity(2.0);
    EXPECT_DOUBLE_EQ(style.opacity(), 1.0);
    style.setShadowOffsetX(-50);
    EXPECT_EQ(style.shadowOffsetX(), -20);
    style.setMarginTop(-3);
    EXPECT_EQ(style.marginTop(), 0);
    style.setSwatchSize(0);
    EXPECT_EQ(style.swatchSize(), 1);
    // An automatic outline color stays automatic through a save.
    EXPECT_FALSE(style.patchOutlineColor().isValid());
    LegendOverlayStyle read;
    read.setPatchOutlineColor(Qt::red);
    read.fromJson(style.toJson());
    EXPECT_FALSE(read.patchOutlineColor().isValid());
}

// ── The legend tree ───────────────────────────────────────────────────────────
using OpenSWMM::Render::LegendItemTree;
using OpenSWMM::Render::LegendNode;

namespace
{
LegendNode node(LegendNode::Type type, const QString &id)
{
    LegendNode n;
    n.type = type;
    n.id = id;
    return n;
}
} // namespace

TEST_F(QtAppFixture, TreeSavesAndFindsItsNodes)
{
    LegendItemTree tree;
    LegendNode &layer = tree.ensureLayerNode(QStringLiteral("L1"));
    layer.heading = LegendNode::HeadingStyle::Subgroup;
    layer.itemColumns = 3;
    layer.ramp = LegendNode::RampChoice::Bar;
    tree.ensureItemNode(QStringLiteral("L1"), QStringLiteral("2")).userLabel =
        QStringLiteral("Two");
    LegendNode group = node(LegendNode::Type::Group, LegendItemTree::newNodeId());
    group.userLabel = QStringLiteral("Inputs");
    group.columnBreakBefore = true;
    LegendNode inner = node(LegendNode::Type::Layer, QStringLiteral("L2"));
    inner.symbolWidth = 30;
    group.children.append(inner);
    LegendNode text = node(LegendNode::Type::Text, LegendItemTree::newNodeId());
    text.userLabel = QStringLiteral("Note");
    group.children.append(text);
    tree.roots().append(group);

    EXPECT_EQ(tree.layerNode(QStringLiteral("L2"))->symbolWidth, 30); // inside the group
    EXPECT_EQ(tree.itemNode(QStringLiteral("L1"), QStringLiteral("2"))->userLabel,
              QStringLiteral("Two"));
    EXPECT_EQ(tree.nodeById(text.id)->userLabel, QStringLiteral("Note"));
    EXPECT_EQ(tree.itemNode(QStringLiteral("L2"), QStringLiteral("2")), nullptr);

    const LegendItemTree read = LegendItemTree::fromJson(tree.toJson());
    EXPECT_TRUE(read == tree);

    // Following the map again keeps the layers' settings, not the groups or text.
    LegendItemTree flat = tree;
    flat.flattenToLayers();
    ASSERT_EQ(flat.roots().size(), 2);
    EXPECT_EQ(flat.roots()[0].id, QStringLiteral("L1"));
    EXPECT_EQ(flat.roots()[1].id, QStringLiteral("L2"));
    EXPECT_EQ(flat.nodeById(group.id), nullptr);
}

TEST_F(QtAppFixture, RowSettingsLiveInTheTree)
{
    LegendOverlayStyle style;
    QSignalSpy treeSpy(&style, &LegendOverlayStyle::itemTreeChanged);
    QSignalSpy rowSpy(&style, &LegendOverlayStyle::itemOverrideChanged);
    style.setItemVisible(QStringLiteral("L1"), QStringLiteral("0"), false);
    EXPECT_FALSE(style.itemOverride(QStringLiteral("L1"), QStringLiteral("0")).visible);
    EXPECT_FALSE(style.itemTree().itemNode(QStringLiteral("L1"), QStringLiteral("0"))->visible);
    EXPECT_EQ(treeSpy.count(), 1);
    EXPECT_EQ(rowSpy.count(), 1);
    // Another layer's row with the same class key is not affected.
    EXPECT_TRUE(style.itemOverride(QStringLiteral("L2"), QStringLiteral("0")).visible);
    // Back to the default: while following the map, the node goes away.
    style.setItemVisible(QStringLiteral("L1"), QStringLiteral("0"), true);
    EXPECT_TRUE(style.itemTree().roots().isEmpty());
    EXPECT_EQ(treeSpy.count(), 2);
    style.setItemVisible(QStringLiteral("L1"), QStringLiteral("0"), true); // no change
    EXPECT_EQ(treeSpy.count(), 2);

    style.setItemUserLabel(QStringLiteral("L1"), QStringLiteral("1"), QStringLiteral("Renamed"));
    LegendOverlayStyle read;
    read.fromJson(style.toJson());
    EXPECT_EQ(read.itemOverride(QStringLiteral("L1"), QStringLiteral("1")).userLabel,
              QStringLiteral("Renamed"));
    read.clearItemOverrides();
    EXPECT_TRUE(read.itemTree().roots().isEmpty());
}

TEST_F(QtAppFixture, FollowingTheMapAgainDropsTheOrder)
{
    LegendOverlayStyle style;
    style.setAutoUpdate(false);
    LegendItemTree tree;
    LegendNode text = node(LegendNode::Type::Text, LegendItemTree::newNodeId());
    text.userLabel = QStringLiteral("Note");
    tree.roots().append(text);
    LegendNode layer = node(LegendNode::Type::Layer, QStringLiteral("L1"));
    tree.roots().append(layer); // no settings: kept while the order is the user's
    LegendNode renamed = node(LegendNode::Type::Layer, QStringLiteral("L2"));
    renamed.userLabel = QStringLiteral("Second");
    tree.roots().append(renamed);
    style.setItemTree(tree);
    EXPECT_EQ(style.itemTree().roots().size(), 3);

    LegendOverlayStyle read;
    read.fromJson(style.toJson());
    EXPECT_FALSE(read.autoUpdate());
    EXPECT_TRUE(read.itemTree() == style.itemTree());

    style.setAutoUpdate(true);
    ASSERT_EQ(style.itemTree().roots().size(), 1);
    EXPECT_EQ(style.itemTree().roots()[0].userLabel, QStringLiteral("Second"));
}

TEST_F(QtAppFixture, OlderRowSettingsApplyUntilTheRowIsEdited)
{
    // An older file keyed rows by the layer's object name, usually empty.
    QJsonObject old;
    QJsonObject overrides;
    overrides[QStringLiteral("|1")] = QJsonObject{{QStringLiteral("visible"), false}};
    old[QStringLiteral("itemOverrides")] = overrides;
    LegendOverlayStyle style;
    style.fromJson(old);
    EXPECT_FALSE(style.legacyItemOverride(QString(), QStringLiteral("1")).visible);
    // Kept when saved again.
    LegendOverlayStyle again;
    again.fromJson(style.toJson());
    EXPECT_FALSE(again.legacyItemOverride(QString(), QStringLiteral("1")).visible);
    // Shown again here: the tree's node wins over the older setting.
    style.setItemVisible(QStringLiteral("layer-id"), QStringLiteral("1"), true);
    ASSERT_NE(style.itemTree().itemNode(QStringLiteral("layer-id"), QStringLiteral("1")), nullptr);
    EXPECT_TRUE(style.itemOverride(QStringLiteral("layer-id"), QStringLiteral("1")).visible);
    style.clearItemOverrides();
    EXPECT_TRUE(style.legacyItemOverride(QString(), QStringLiteral("1")).visible);
}

TEST_F(QtAppFixture, LayersAreKeyedByTheirLayerId)
{
    QObject layer;
    layer.setObjectName(QStringLiteral("name"));
    EXPECT_EQ(LegendOverlayStyle::itemKey(&layer, {}), QStringLiteral("name"));
    layer.setProperty("LayerId", QStringLiteral("{id}"));
    EXPECT_EQ(LegendOverlayStyle::itemKey(&layer, {}), QStringLiteral("{id}"));
    EXPECT_EQ(LegendOverlayStyle::itemKey(nullptr, {}), QString());
}
