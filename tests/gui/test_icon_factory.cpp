// UI redesign P3 — IconFactory / ThemedIconEngine: every catalog icon
// alias renders a non-null themed pixmap, glyph colors differ between
// light scheme, dark scheme, and disabled mode (the whole point of the
// engine), repeated requests hit the pixmap cache stably, and unknown
// aliases yield null icons instead of crashing.
//
// Links the real swmmvis.qrc so aliases resolve exactly as in the app.
#include <QtTest/QtTest>

#include <QIcon>
#include <QImage>
#include <QPixmap>

#include "ui/actioncatalog.h"
#include "ui/theme/iconfactory.h"
#include "ui/theme/thememanager.h"

using openswmmvis::ui::IconFactory;
using openswmmvis::ui::ThemeManager;
using openswmmvis::ui::kActionCatalog;

class TestIconFactory : public QObject
{
    Q_OBJECT

private slots:
    void cleanup();

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

QTEST_MAIN(TestIconFactory)
#include "test_icon_factory.moc"
