/*!
 * \file   rulesyntaxhighlighter.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "controls/rulesyntaxhighlighter.h"

#include <QRegularExpression>
#include <QSet>

namespace openswmmvis::controls {

namespace {

// Build the vocabulary once and reuse — these are upper-case canonical
// forms; matching is case-insensitive at the call site.
const QStringList &blockKwSet() {
    static const QStringList kw = {
        QStringLiteral("RULE"), QStringLiteral("IF"), QStringLiteral("THEN"),
        QStringLiteral("ELSE"), QStringLiteral("AND"), QStringLiteral("OR"),
        QStringLiteral("NOT"),  QStringLiteral("PRIORITY")
    };
    return kw;
}
const QStringList &objectTypeSet() {
    static const QStringList kw = {
        QStringLiteral("NODE"), QStringLiteral("LINK"), QStringLiteral("CONDUIT"),
        QStringLiteral("PUMP"), QStringLiteral("ORIFICE"), QStringLiteral("WEIR"),
        QStringLiteral("OUTLET"), QStringLiteral("SUBCATCH"),
        QStringLiteral("SIMULATION"), QStringLiteral("GAGE")
    };
    return kw;
}
const QStringList &variableSet() {
    static const QStringList kw = {
        QStringLiteral("DEPTH"), QStringLiteral("HEAD"), QStringLiteral("FLOW"),
        QStringLiteral("VOLUME"), QStringLiteral("INFLOW"), QStringLiteral("SETTING"),
        QStringLiteral("STATUS"), QStringLiteral("CLOCKTIME"), QStringLiteral("DATE"),
        QStringLiteral("MONTH"), QStringLiteral("DAY"), QStringLiteral("TIME"),
        QStringLiteral("OVERFLOW")
    };
    return kw;
}

QSet<QString> toUpperSet(const QStringList &xs) {
    QSet<QString> s;
    s.reserve(xs.size());
    for (const auto &x : xs) s.insert(x.toUpper());
    return s;
}

} // namespace

RuleSyntaxHighlighter::RuleSyntaxHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    rebuildFormats_();
}

RuleSyntaxHighlighter::~RuleSyntaxHighlighter() = default;

void RuleSyntaxHighlighter::setPalette(const QPalette &palette)
{
    m_palette = palette;
    rebuildFormats_();
    rehighlight();
}

QStringList RuleSyntaxHighlighter::blockKeywords()   { return blockKwSet(); }
QStringList RuleSyntaxHighlighter::objectTypes()     { return objectTypeSet(); }
QStringList RuleSyntaxHighlighter::variableNames()   { return variableSet(); }

QStringList RuleSyntaxHighlighter::operatorTokens() {
    return {QStringLiteral("="),  QStringLiteral("<"),  QStringLiteral(">"),
            QStringLiteral("<="), QStringLiteral(">="), QStringLiteral("<>"),
            QStringLiteral("IS")};
}

QStringList RuleSyntaxHighlighter::allKeywords() {
    QStringList out;
    out.reserve(blockKwSet().size() + objectTypeSet().size() + variableSet().size());
    out += blockKwSet();
    out += objectTypeSet();
    out += variableSet();
    return out;
}

void RuleSyntaxHighlighter::rebuildFormats_()
{
    // Resolve palette colours. The fallback (when m_palette is default-
    // constructed) uses the QPalette::Active group's default roles.
    const QColor highlight     = m_palette.color(QPalette::Active, QPalette::Highlight);
    const QColor linkVisited   = m_palette.color(QPalette::Active, QPalette::LinkVisited);
    const QColor link          = m_palette.color(QPalette::Active, QPalette::Link);
    const QColor windowText    = m_palette.color(QPalette::Active, QPalette::WindowText);
    const QColor text          = m_palette.color(QPalette::Active, QPalette::Text);
    const QColor placeholder   = m_palette.color(QPalette::Active, QPalette::PlaceholderText);

    m_fmtBlockKw = QTextCharFormat{};
    m_fmtBlockKw.setForeground(highlight.isValid() ? highlight : QColor("#1565C0"));
    m_fmtBlockKw.setFontWeight(QFont::Bold);

    m_fmtObjectType = QTextCharFormat{};
    m_fmtObjectType.setForeground(linkVisited.isValid() ? linkVisited : QColor("#6A1B9A"));
    m_fmtObjectType.setFontWeight(QFont::Bold);

    m_fmtVariable = QTextCharFormat{};
    m_fmtVariable.setForeground(link.isValid() ? link : QColor("#00695C"));
    m_fmtVariable.setFontItalic(true);

    m_fmtOperator = QTextCharFormat{};
    m_fmtOperator.setForeground(windowText.isValid() ? windowText : QColor("#212121"));

    m_fmtNumber = QTextCharFormat{};
    m_fmtNumber.setForeground(text.isValid() ? text : QColor("#37474F"));

    m_fmtComment = QTextCharFormat{};
    m_fmtComment.setForeground(placeholder.isValid() ? placeholder : QColor("#9E9E9E"));
    m_fmtComment.setFontItalic(true);
}

void RuleSyntaxHighlighter::highlightBlock(const QString &text)
{
    if (text.isEmpty()) return;

    // ── Comment: ';' to EOL. Apply first; comment text wins over keywords.
    const int semi = text.indexOf(QLatin1Char(';'));
    int contentEnd = semi >= 0 ? semi : text.size();
    if (semi >= 0)
        setFormat(semi, text.size() - semi, m_fmtComment);

    // Pre-build the upper-case lookup sets once per call (cheap).
    static const QSet<QString> kBlock    = toUpperSet(blockKwSet());
    static const QSet<QString> kObjType  = toUpperSet(objectTypeSet());
    static const QSet<QString> kVariable = toUpperSet(variableSet());

    // ── Walk tokens up to contentEnd.
    int i = 0;
    while (i < contentEnd) {
        // Skip whitespace.
        while (i < contentEnd && text.at(i).isSpace()) ++i;
        if (i >= contentEnd) break;

        const QChar c = text.at(i);

        // Operator tokens: =, <, >, <=, >=, <>
        if (c == QLatin1Char('=') || c == QLatin1Char('<') || c == QLatin1Char('>')) {
            int len = 1;
            if (i + 1 < contentEnd) {
                const QChar nc = text.at(i + 1);
                if ((c == QLatin1Char('<') && (nc == QLatin1Char('=') || nc == QLatin1Char('>'))) ||
                    (c == QLatin1Char('>') &&  nc == QLatin1Char('='))) {
                    len = 2;
                }
            }
            setFormat(i, len, m_fmtOperator);
            i += len;
            continue;
        }

        // Number: starts with a digit, optional decimal point, or HH:MM[:SS].
        if (c.isDigit() ||
            (c == QLatin1Char('.') && i + 1 < contentEnd && text.at(i + 1).isDigit()))
        {
            int j = i;
            while (j < contentEnd) {
                const QChar cj = text.at(j);
                if (cj.isDigit() || cj == QLatin1Char('.') || cj == QLatin1Char(':'))
                    ++j;
                else break;
            }
            setFormat(i, j - i, m_fmtNumber);
            i = j;
            continue;
        }

        // Identifier: letters, digits, underscore, hyphen — everything else
        // up to whitespace or an operator character.
        int j = i;
        while (j < contentEnd) {
            const QChar cj = text.at(j);
            if (cj.isSpace() ||
                cj == QLatin1Char('=') || cj == QLatin1Char('<') || cj == QLatin1Char('>') ||
                cj == QLatin1Char(';'))
                break;
            ++j;
        }
        if (j > i) {
            const QString tok = text.mid(i, j - i).toUpper();
            if (kBlock.contains(tok))
                setFormat(i, j - i, m_fmtBlockKw);
            else if (kObjType.contains(tok))
                setFormat(i, j - i, m_fmtObjectType);
            else if (kVariable.contains(tok))
                setFormat(i, j - i, m_fmtVariable);
            else if (tok == QStringLiteral("IS") || tok == QStringLiteral("NOT"))
                setFormat(i, j - i, m_fmtOperator);
            // else: leave default — it's an identifier (NODE name, etc.)
        }
        i = j > i ? j : i + 1;   // safety: always advance.
    }
}

} // namespace openswmmvis::controls
