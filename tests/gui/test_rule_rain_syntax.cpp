// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>
#include <QTextBlock>
#include <QTextLayout>
#include "controls/rulesyntaxhighlighter.h"

using openswmmvis::controls::RuleSyntaxHighlighter;

class TestRuleRainSyntax : public QObject {
    Q_OBJECT
private slots:
    void offersLegacyRainfallAttributes() {
        const auto words = RuleSyntaxHighlighter::allKeywords();
        QVERIFY(words.contains(QStringLiteral("GAGE")));
        QVERIFY(words.contains(QStringLiteral("INTENSITY")));
        QVERIFY(words.contains(QStringLiteral("24-HR_DEPTH")));
        QVERIFY(words.contains(QStringLiteral("48-HR_DEPTH")));
        QVERIFY(!words.contains(QStringLiteral("72-HR_DEPTH")));
        QVERIFY(!words.contains(QStringLiteral("3-DAY_DEPTH")));
    }
    void highlightsTheWholeParameterizedAttribute_data() {
        QTest::addColumn<QString>("attribute");
        for (const auto* attr : {"1-HR_DEPTH", "17-HR_DEPTH", "24-hr_depth", "48-HR_DEPTH"})
            QTest::newRow(attr) << QString::fromLatin1(attr);
    }
    void highlightsTheWholeParameterizedAttribute() {
        QFETCH(QString, attribute);
        QTextDocument document;
        const QString prefix = QStringLiteral("IF GAGE RG ");
        document.setPlainText(prefix + attribute + QStringLiteral(" >= 1.5 ; 24-HR_DEPTH"));
        RuleSyntaxHighlighter highlighter(&document);
        highlighter.rehighlight();
        bool found = false;
        for (const auto& range : document.firstBlock().layout()->formats()) {
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
