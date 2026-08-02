// UI redesign P1 — integrity audit of the action catalog
// (include/ui/actioncatalog.h). No widgets and no main window — the
// catalog is a pure data table and the registry's shortcut parsing needs
// only the application/platform-theme layer. Failures here mean a catalog
// edit broke an invariant the palette / shortcut editor / tabbed toolbar
// rely on.
#include <QtTest/QtTest>

#include <QFile>
#include <QSet>

#include "ui/actioncatalog.h"
#include "ui/actionregistry.h"

using openswmmvis::ui::ActionCatalogEntry;
using openswmmvis::ui::ActionRegistry;
using openswmmvis::ui::kActionCatalog;
using openswmmvis::ui::kActionCatalogTabs;

class TestActionCatalog : public QObject
{
    Q_OBJECT

private slots:
    void uniqueIds();
    void uniqueObjectNames();
    void requiredFieldsPresent();
    void defaultShortcutsResolve();
    void noDuplicateDefaultShortcuts();
    void tabsAreKnown();
    void iconAliasesResolveInQrc();
};

void TestActionCatalog::uniqueIds()
{
    QSet<QString> seen;
    for (const ActionCatalogEntry &e : kActionCatalog) {
        const QString id = QString::fromLatin1(e.id);
        QVERIFY2(!id.isEmpty(), "catalog entry with empty id");
        QVERIFY2(!seen.contains(id),
                 qPrintable(QStringLiteral("duplicate catalog id '%1'").arg(id)));
        seen.insert(id);
    }
}

void TestActionCatalog::uniqueObjectNames()
{
    QSet<QString> seen;
    for (const ActionCatalogEntry &e : kActionCatalog) {
        const QString name = QString::fromLatin1(e.objectName);
        QVERIFY2(!name.isEmpty(),
                 qPrintable(QStringLiteral("entry '%1' has empty objectName")
                                .arg(QLatin1String(e.id))));
        QVERIFY2(!seen.contains(name),
                 qPrintable(QStringLiteral("objectName '%1' used by two entries")
                                .arg(name)));
        seen.insert(name);
    }
}

void TestActionCatalog::requiredFieldsPresent()
{
    for (const ActionCatalogEntry &e : kActionCatalog) {
        QVERIFY2(e.category && *e.category,
                 qPrintable(QStringLiteral("entry '%1' has empty category")
                                .arg(QLatin1String(e.id))));
        QVERIFY2(e.menuPath && *e.menuPath,
                 qPrintable(QStringLiteral("entry '%1' has empty menuPath")
                                .arg(QLatin1String(e.id))));
    }
}

void TestActionCatalog::defaultShortcutsResolve()
{
    // Non-empty specs must resolve to at least one binding — this is what
    // catches an unknown "std:" token or a string QKeySequence can't parse
    // (the historical bare-word-to-media-key defect family).
    auto *registry = ActionRegistry::instance();
    for (const ActionCatalogEntry &e : kActionCatalog) {
        const QString spec = QString::fromLatin1(e.defaultShortcut);
        if (spec.isEmpty())
            continue;
        const auto bindings = registry->defaultShortcuts(QLatin1String(e.id));
        QVERIFY2(!bindings.isEmpty(),
                 qPrintable(QStringLiteral(
                     "entry '%1' default shortcut '%2' resolves to nothing")
                         .arg(QLatin1String(e.id), spec)));
    }
}

void TestActionCatalog::noDuplicateDefaultShortcuts()
{
    auto *registry = ActionRegistry::instance();
    QHash<QString, QString> seen;   // portable sequence -> id
    for (const ActionCatalogEntry &e : kActionCatalog) {
        const QString id = QString::fromLatin1(e.id);
        const auto bindings = registry->defaultShortcuts(id);
        for (const QKeySequence &seq : bindings) {
            const QString key = seq.toString(QKeySequence::PortableText);
            if (key.isEmpty())
                continue;
            QVERIFY2(!seen.contains(key),
                     qPrintable(QStringLiteral(
                         "default shortcut '%1' assigned to both '%2' and '%3'")
                             .arg(key, seen.value(key), id)));
            seen.insert(key, id);
        }
    }
}

void TestActionCatalog::tabsAreKnown()
{
    QSet<QString> known;
    for (const char *tab : kActionCatalogTabs)
        known.insert(QString::fromLatin1(tab));
    for (const ActionCatalogEntry &e : kActionCatalog) {
        const QString tab = QString::fromLatin1(e.tab);
        if (tab.isEmpty())
            continue;   // menu-only entry
        QVERIFY2(known.contains(tab),
                 qPrintable(QStringLiteral("entry '%1' names unknown tab '%2'")
                                .arg(QLatin1String(e.id), tab)));
    }
}

void TestActionCatalog::iconAliasesResolveInQrc()
{
    // P3 — every non-empty icon alias must exist under :/swmmvis/ (the
    // real swmmvis.qrc is linked into this test).
    for (const ActionCatalogEntry &e : kActionCatalog) {
        if (!e.icon || !*e.icon)
            continue;
        const QString path =
            QStringLiteral(":/swmmvis/%1").arg(QLatin1String(e.icon));
        QVERIFY2(QFile::exists(path),
                 qPrintable(QStringLiteral("entry '%1' icon alias '%2' missing from qrc")
                                .arg(QLatin1String(e.id), QLatin1String(e.icon))));
    }
}

// QTEST_MAIN (not GUILESS): QKeySequence::keyBindings resolves standard
// keys through the platform theme, which only exists with a QGuiApplication.
QTEST_MAIN(TestActionCatalog)
#include "test_action_catalog.moc"
