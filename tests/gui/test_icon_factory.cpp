// UI redesign P3 — IconFactory / ThemedIconEngine: every catalog icon
// alias renders a non-null themed pixmap, glyph colors differ between
// light scheme, dark scheme, and disabled mode (the whole point of the
// engine), repeated requests hit the pixmap cache stably, and unknown
// aliases yield null icons instead of crashing.
//
// Links the real swmmvis.qrc so aliases resolve exactly as in the app.
#include <QtTest/QtTest>

#include <QDir>
#include <QFile>
#include <QFont>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QSvgRenderer>
#include <QImage>
#include <QPixmap>

#include "ui/actioncatalog.h"
#include "ui/theme/iconfactory.h"
#include "ui/theme/thememanager.h"
#include "ui/theme/themetokens.h"

using openswmmvis::ui::IconFactory;
using openswmmvis::ui::ThemeManager;
using openswmmvis::ui::kActionCatalog;

namespace {
struct ReviewedIcon { const char *alias; const char *action; const char *label; };
const ReviewedIcon reviewedIcons[] = {
    {"Aquifer", "actionNewAquifer", "Aquifer (1D)"},
    {"GW2DParams", "actionMesh2DGWParams", "Aquifer parameters (2D)"},
    {"GW2DParams", "actionAssignGroundwater", "Assign groundwater"},
    {"GW2DInit", "actionMesh2DGWInitCond", "Groundwater initial state"},
    {"Select", "actionSelect", "Pointer / rectangle selection"},
    {"SelectByPolygon", "actionSelectByPolygon", "Polygon selection"},
    {"SelectTriNode", "actionMeshSelectVertex", "Mesh vertex selection"},
    {"SelectTriEdge", "actionMeshSelectEdge", "Mesh edge selection"},
    {"SelectCell", "actionPick2DCells", "Results cell selection"},
    {"MeshAssignRaster", "actionMeshAssignFromRaster", "Assign raster to cells"},
    {"MeshAssignVector", "actionMeshAssignFromVector", "Assign features to cells"},
    {"Profile", "actionPlotProfile", "Network profile"},
    {"Profile2D", "actionPlotProfile2D", "2D section profile"},
    {"Profile2D", "actionSaved2DSections", "Saved 2D sections"},
    {"ImportGIS", "actionImportFeatureLayer", "Import GIS features"},
    {"ExportMap", "actionExportMap", "Export map"},
    {"Export2DResults", "actionExport2DResults", "Export 2D results"},
    {"Style", "actionSetStyle", "Results appearance"},
    {"Pollutant", "actionNewPollutant", "Pollutant"},
    {"ReactionSystem", "actionEditReactionSystem", "Reaction system / species"},
};

bool receivesCatalogTheme(const ReviewedIcon &icon)
{
    for (const auto &entry : kActionCatalog)
        if (QLatin1String(entry.objectName) == QLatin1String(icon.action))
            return QLatin1String(entry.icon) == QLatin1String(icon.alias);
    return false;
}

QIcon effectiveReviewedIcon(const ReviewedIcon &icon)
{
    // Mirrors the final registerActions() icon sweep. The sole uncatalogued
    // reviewed action currently starts with its raw qrc icon in swmmvis.cpp.
    return receivesCatalogTheme(icon) ? IconFactory::icon(QLatin1String(icon.alias))
        : QIcon(QStringLiteral(":/swmmvis/%1").arg(QLatin1String(icon.alias)));
}
}

class TestIconFactory : public QObject
{
    Q_OBJECT

private slots:
    void cleanup();

    void registeredSvgResourcesHaveExplicitThemePolicy();
    void writeReviewContactSheets();
    void cellSelectionUsesThemedCatalogBinding();
    void reviewedGlyphsStayInsideCanvas();
    void reviewedAliasesRenderAcrossStatesAndSizes();
    void allCatalogAliasesRender();
    void schemesAndModesDiffer();
    void cacheIsStable();
    void unknownAliasIsNull();
};

void TestIconFactory::cleanup()
{
    ThemeManager::instance()->setMode(ThemeManager::Mode::System);
}

void TestIconFactory::allCatalogAliasesRender()
{
    for (const auto &entry : kActionCatalog) {
        if (!entry.icon || !*entry.icon)
            continue;
        const QString alias = QString::fromLatin1(entry.icon);
        const QIcon icon = IconFactory::icon(alias);
        QVERIFY2(!icon.isNull(),
                 qPrintable(QStringLiteral("alias '%1' (entry '%2') did not resolve")
                                .arg(alias, QLatin1String(entry.id))));
        const QPixmap pm = icon.pixmap(20, 20);
        QVERIFY2(!pm.isNull(),
                 qPrintable(QStringLiteral("alias '%1' rendered a null pixmap")
                                .arg(alias)));
    }
}

void TestIconFactory::schemesAndModesDiffer()
{
    auto *theme = ThemeManager::instance();
    const QIcon icon = IconFactory::icon(QStringLiteral("Open"));
    QVERIFY(!icon.isNull());

    theme->setMode(ThemeManager::Mode::Light);
    const QImage light = icon.pixmap(24, 24, QIcon::Normal).toImage();
    const QImage lightDisabled = icon.pixmap(24, 24, QIcon::Disabled).toImage();

    theme->setMode(ThemeManager::Mode::Dark);
    const QImage dark = icon.pixmap(24, 24, QIcon::Normal).toImage();

    QVERIFY(!light.isNull() && !dark.isNull());
    QVERIFY2(light != dark, "light and dark renders are identical — "
                            "glyph substitution is not happening");
    QVERIFY2(light != lightDisabled, "normal and disabled renders are identical");
}

void TestIconFactory::cacheIsStable()
{
    ThemeManager::instance()->setMode(ThemeManager::Mode::Light);
    const QIcon icon = IconFactory::icon(QStringLiteral("Save"));
    const QPixmap first  = icon.pixmap(20, 20);
    const QPixmap second = icon.pixmap(20, 20);
    QCOMPARE(first.cacheKey(), second.cacheKey());   // served from QPixmapCache
}

void TestIconFactory::unknownAliasIsNull()
{
    QVERIFY(IconFactory::icon(QStringLiteral("NoSuchAliasEver")).isNull());
}

void TestIconFactory::registeredSvgResourcesHaveExplicitThemePolicy()
{
    // These colored badges deliberately retain status color, white check/X,
    // and dark outline in both themes. Other chrome SVGs must adapt.
    const QStringList fullColorAliases{QStringLiteral("RuleValid"), QStringLiteral("RuleInvalid")};
    int svgCount = 0;
    for (const QString &alias : QDir(QStringLiteral(":/swmmvis")).entryList(QDir::Files)) {
        QFile file(QStringLiteral(":/swmmvis/%1").arg(alias));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray bytes = file.readAll();
        if (!bytes.contains("<svg")) continue;
        ++svgCount;
        QVERIFY2(QSvgRenderer(bytes).isValid(), qPrintable(alias));
        const QIcon icon = IconFactory::icon(alias);
        ThemeManager::instance()->setMode(ThemeManager::Mode::Light);
        const QImage light = icon.pixmap(24, 24).toImage();
        ThemeManager::instance()->setMode(ThemeManager::Mode::Dark);
        const QImage dark = icon.pixmap(24, 24).toImage();
        QVERIFY2(!light.isNull() && !dark.isNull(), qPrintable(alias));
        if (fullColorAliases.contains(alias))
            QCOMPARE(light, dark);
        else
            QVERIFY2(light != dark, qPrintable(QStringLiteral("%1 does not adapt; use a supported glyph token or document a full-color exception.").arg(alias)));
    }
    QVERIFY(svgCount > 0);
}

void TestIconFactory::writeReviewContactSheets()
{
    const QString output = qEnvironmentVariable("SWMMVIS_ICON_REVIEW_OUTPUT");
    if (output.isEmpty()) return;
    QVERIFY2(QDir().mkpath(output), qPrintable(output));
    QJsonArray bindings;
    for (const auto &entry : reviewedIcons)
        bindings.append(QJsonObject{{"action", QLatin1String(entry.action)},
                                    {"alias", QLatin1String(entry.alias)},
                                    {"label", QLatin1String(entry.label)},
                                    {"effectiveBinding", receivesCatalogTheme(entry) ? "IconFactory catalog sweep" : "direct QIcon resource"}});
    QFile manifest(QDir(output).filePath(QStringLiteral("reviewed-bindings.json")));
    QVERIFY(manifest.open(QIODevice::WriteOnly));
    manifest.write(QJsonDocument(bindings).toJson());
    for (const auto scheme : {ThemeManager::Mode::Light, ThemeManager::Mode::Dark}) {
        ThemeManager::instance()->setMode(scheme);
        const auto &colors = ThemeManager::instance()->colors();
        const QString schemeName = scheme == ThemeManager::Mode::Light ? QStringLiteral("light") : QStringLiteral("dark");
        for (const auto mode : {QIcon::Normal, QIcon::Disabled, QIcon::Selected}) {
            const QString modeName = mode == QIcon::Normal ? QStringLiteral("normal")
                : mode == QIcon::Disabled ? QStringLiteral("disabled") : QStringLiteral("selected");
            const QColor background = mode == QIcon::Selected ? colors.selectionFill : colors.surfaceWindow;
            const QColor text = mode == QIcon::Selected ? colors.selectionText : colors.text;
            QImage sheet(780, 90 + int(std::size(reviewedIcons)) * 78, QImage::Format_ARGB32_Premultiplied);
            sheet.fill(background);
            QPainter painter(&sheet);
            painter.setPen(text);
            QFont font = painter.font(); font.setPixelSize(13); painter.setFont(font);
            painter.drawText(QRect(16, 8, 752, 26), QStringLiteral("Effective action icons — %1 / %2").arg(schemeName, modeName));
            painter.drawText(QRect(16, 35, 752, 22), QStringLiteral("Native pixels at 1×; last column is 32 logical pixels at 2× DPR, shown enlarged."));
            const int sizes[] = {16, 20, 24, 32, 64};
            for (int column = 0; column < 5; ++column)
                painter.drawText(QRect(325 + column * 88, 63, 86, 22), Qt::AlignCenter,
                    column == 4 ? QStringLiteral("32 @ 2×") : QString::number(sizes[column]));
            int row = 0;
            for (const auto &entry : reviewedIcons) {
                const int top = 90 + row * 78;
                painter.drawText(QRect(16, top + 15, 300, 20), QLatin1String(entry.label));
                painter.drawText(QRect(16, top + 37, 300, 20), QStringLiteral("%1 · %2").arg(QLatin1String(entry.alias),
                    receivesCatalogTheme(entry) ? QStringLiteral("themed") : QStringLiteral("raw resource")));
                const QIcon icon = effectiveReviewedIcon(entry);
                for (int column = 0; column < 5; ++column) {
                    QPixmap pixmap = column == 4 ? icon.pixmap(QSize(32, 32), 2.0, mode)
                                               : icon.pixmap(QSize(sizes[column], sizes[column]), 1.0, mode);
                    QVERIFY(!pixmap.isNull());
                    QImage image = pixmap.toImage(); image.setDevicePixelRatio(1.0);
                    painter.drawImage(QPoint(325 + column * 88 + (86 - image.width()) / 2,
                                             top + (78 - image.height()) / 2), image);
                }
                ++row;
            }
            painter.end();
            QVERIFY(sheet.save(QDir(output).filePath(QStringLiteral("icons_%1_%2.png").arg(schemeName, modeName))));
        }
    }
}

void TestIconFactory::cellSelectionUsesThemedCatalogBinding()
{
    bool found = false;
    for (const auto &entry : kActionCatalog) {
        if (QLatin1String(entry.objectName) != QLatin1String("actionPick2DCells")) continue;
        found = true;
        QCOMPARE(QLatin1String(entry.icon), QLatin1String("SelectCell"));
    }
    QVERIFY2(found, "The actual cell-selection toolbar action bypasses the theme-aware catalog sweep.");
}

void TestIconFactory::reviewedGlyphsStayInsideCanvas()
{
    for (const char *alias : {"GW2DParams", "GW2DInit", "SelectByPolygon", "SelectCell", "ImportGIS"}) {
        const QImage image = IconFactory::icon(QLatin1String(alias)).pixmap(32, 32).toImage();
        QVERIFY(!image.isNull());
        for (int pixel = 0; pixel < image.width(); ++pixel) {
            QVERIFY2(qAlpha(image.pixel(pixel, 0)) == 0 && qAlpha(image.pixel(pixel, image.height() - 1)) == 0
                     && qAlpha(image.pixel(0, pixel)) == 0 && qAlpha(image.pixel(image.width() - 1, pixel)) == 0,
                     qPrintable(QStringLiteral("%1 touches the canvas boundary; check for clipped strokes.").arg(QLatin1String(alias))));
        }
    }
}

void TestIconFactory::reviewedAliasesRenderAcrossStatesAndSizes()
{
    for (const auto &entry : reviewedIcons) {
        const QIcon icon = IconFactory::icon(QLatin1String(entry.alias));
        for (const auto scheme : {ThemeManager::Mode::Light, ThemeManager::Mode::Dark}) {
            ThemeManager::instance()->setMode(scheme);
            for (const auto mode : {QIcon::Normal, QIcon::Disabled, QIcon::Selected}) {
                for (const int size : {16, 20, 24, 32}) {
                    const QImage image = icon.pixmap(QSize(size, size), 1.0, mode).toImage();
                    QCOMPARE(image.size(), QSize(size, size));
                    int painted = 0;
                    for (int y = 0; y < size; ++y)
                        for (int x = 0; x < size; ++x)
                            painted += qAlpha(image.pixel(x, y)) > 0;
                    QVERIFY2(painted > size, entry.alias);
                    QVERIFY2(painted < size * size, entry.alias);
                }
            }
        }
        const QPixmap retina = icon.pixmap(QSize(32, 32), 2.0);
        QCOMPARE(retina.size(), QSize(64, 64));
        QCOMPARE(retina.devicePixelRatio(), 2.0);
    }
}

QTEST_MAIN(TestIconFactory)
#include "test_icon_factory.moc"
