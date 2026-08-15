/*!
 * \file   test_categoricalpalette.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice BB-γ — CategoricalPalette named-lookup API + 7 Plotly categorical
 * palettes (Plotly / D3 / G10 / T10 / Alphabet / Dark24 / Light24).
 * Self-contained: pulls in only categoricalpalette.cpp.
 */

#include "render/categoricalpalette.h"

#include <QObject>
#include <QSet>
#include <QTest>

class TestCategoricalPalette : public QObject
{
    Q_OBJECT
private slots:

    void byName_known_returns_non_default()
    {
        const QList<QColor> def    = CategoricalPalette::byName(QStringLiteral("Default"));
        const QList<QColor> plotly = CategoricalPalette::byName(QStringLiteral("Plotly"));
        QVERIFY(!def.isEmpty());
        QVERIFY(!plotly.isEmpty());
        // Plotly's first colour (Indigo-ish blue) differs from Tab10's first
        // colour (#1f77b4) — confirm we actually hit a different palette.
        QVERIFY(plotly.first().rgba() != def.first().rgba());
    }

    void byName_unknown_falls_back_to_default()
    {
        const QList<QColor> def = CategoricalPalette::byName(QStringLiteral("Default"));
        const QList<QColor> bogus = CategoricalPalette::byName(QStringLiteral("does-not-exist"));
        QCOMPARE(bogus.size(), def.size());
        QCOMPARE(bogus.first().rgba(), def.first().rgba());
    }

    void byName_case_insensitive()
    {
        const QList<QColor> a = CategoricalPalette::byName(QStringLiteral("D3"));
        const QList<QColor> b = CategoricalPalette::byName(QStringLiteral("d3"));
        const QList<QColor> c = CategoricalPalette::byName(QStringLiteral("  d3  "));
        QCOMPARE(a.size(), b.size());
        QCOMPARE(a.size(), c.size());
        QCOMPARE(a.first().rgba(), b.first().rgba());
        QCOMPARE(a.first().rgba(), c.first().rgba());
    }

    void builtinNames_lists_all_eight()
    {
        // Default (Tab10 alias) + 7 Plotly entries = 8.
        const QStringList names = CategoricalPalette::builtinNames();
        QCOMPARE(names.size(), 8);
        QVERIFY(names.contains(QStringLiteral("Default")));
        QVERIFY(names.contains(QStringLiteral("Plotly")));
        QVERIFY(names.contains(QStringLiteral("D3")));
        QVERIFY(names.contains(QStringLiteral("G10")));
        QVERIFY(names.contains(QStringLiteral("T10")));
        QVERIFY(names.contains(QStringLiteral("Alphabet")));
        QVERIFY(names.contains(QStringLiteral("Dark24")));
        QVERIFY(names.contains(QStringLiteral("Light24")));
    }

    void palette_sizes_match_plotly_reference()
    {
        // Plotly reference: Plotly/D3/G10/T10 = 10 colors each;
        // Alphabet = 26; Dark24 / Light24 = 24 each.
        QCOMPARE(CategoricalPalette::byName(QStringLiteral("Plotly")).size(),   10);
        QCOMPARE(CategoricalPalette::byName(QStringLiteral("D3")).size(),       10);
        QCOMPARE(CategoricalPalette::byName(QStringLiteral("G10")).size(),      10);
        QCOMPARE(CategoricalPalette::byName(QStringLiteral("T10")).size(),      10);
        QCOMPARE(CategoricalPalette::byName(QStringLiteral("Alphabet")).size(), 26);
        QCOMPARE(CategoricalPalette::byName(QStringLiteral("Dark24")).size(),   24);
        QCOMPARE(CategoricalPalette::byName(QStringLiteral("Light24")).size(),  24);
    }

    void at_and_size_unchanged_by_named_lookup_API()
    {
        // Slice BB-γ is additive — the pre-existing at()/size()/palette()
        // API must keep working identically (Tab10 default).
        QCOMPARE(CategoricalPalette::size(), 10);
        QCOMPARE(CategoricalPalette::at(0), QColor(0x1f, 0x77, 0xb4));   // first Tab10 entry
        QCOMPARE(CategoricalPalette::palette().size(), 10);
    }
};

QTEST_APPLESS_MAIN(TestCategoricalPalette)
#include "test_categoricalpalette.moc"
