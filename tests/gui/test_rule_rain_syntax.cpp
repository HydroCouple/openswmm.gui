// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      test_rule_rain_syntax.cpp
 * @brief     Tests for rule rain syntax.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <QtTest>
#include <QTextBlock>
#include <QTextLayout>
#include "controls/rulesyntaxhighlighter.h"

using openswmmvis::controls::RuleSyntaxHighlighter;

class TestRuleRainSyntax : public QObject
{
    Q_OBJECT
private slots:
    /**
     *
     * @brief Require supported legacy GAGE/intensity/hour-depth keywords and exclude unsupported
     * 72-hour/3-day aliases.
     *
     */
    void offersLegacyRainfallAttributes()
    {
        const auto words = RuleSyntaxHighlighter::allKeywords();
        QVERIFY(words.contains(QStringLiteral("GAGE")));
        QVERIFY(words.contains(QStringLiteral("INTENSITY")));
        QVERIFY(words.contains(QStringLiteral("24-HR_DEPTH")));
        QVERIFY(words.contains(QStringLiteral("48-HR_DEPTH")));
        QVERIFY(!words.contains(QStringLiteral("72-HR_DEPTH")));
        QVERIFY(!words.contains(QStringLiteral("3-DAY_DEPTH")));
    }
    /**
     *
     * @brief Supply 1/17/24/48-hour attribute spellings including lowercase token letters.
     *
     */
    void highlightsTheWholeParameterizedAttribute_data()
    {
        QTest::addColumn<QString>("attribute");
        for (const auto *attr : {"1-HR_DEPTH", "17-HR_DEPTH", "24-hr_depth", "48-HR_DEPTH"})
            QTest::newRow(attr) << QString::fromLatin1(attr);
    }
    /**
     *
     * @brief Require one italic syntax range to span the complete parameterized rainfall token in
     * a control premise.
     * The later comment token is not separately asserted; no engine rule is parsed or run.
     *
     */
    void highlightsTheWholeParameterizedAttribute()
    {
        QFETCH(QString, attribute);
        QTextDocument document;
        const QString prefix = QStringLiteral("IF GAGE RG ");
        document.setPlainText(prefix + attribute + QStringLiteral(" >= 1.5 ; 24-HR_DEPTH"));
        RuleSyntaxHighlighter highlighter(&document);
        highlighter.rehighlight();
        bool found = false;
        for (const auto &range : document.firstBlock().layout()->formats()) {
            if (range.start == prefix.size() && range.length == attribute.size()) {
                QVERIFY(range.format.fontItalic());
                found = true;
            }
        }
        QVERIFY(found);
    }
};

QTEST_MAIN(TestRuleRainSyntax)
#include "test_rule_rain_syntax.moc"
