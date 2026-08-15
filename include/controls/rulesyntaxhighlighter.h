/*!
 * \file   rulesyntaxhighlighter.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BR Phase 6.8.2 — QSyntaxHighlighter for SWMM control rules.
 *
 * Attached to `RuleCodeEditor`'s `QTextDocument`. Token classes + palette
 * roles:
 *
 *   - Block keyword (`RULE`, `IF`, `THEN`, `ELSE`, `AND`, `OR`, `NOT`,
 *     `PRIORITY`)                                            — bold, Highlight
 *   - Object type   (`NODE`, `LINK`, `CONDUIT`, `PUMP`,
 *                    `ORIFICE`, `WEIR`, `OUTLET`, `SUBCATCH`,
 *                    `SIMULATION`, `GAGE`)                   — bold, LinkVisited
 *   - Variable      (`DEPTH`, `HEAD`, `FLOW`, `VOLUME`,
 *                    `INFLOW`, `SETTING`, `STATUS`,
 *                    `CLOCKTIME`, `DATE`, `MONTH`, `DAY`,
 *                    `TIME`, `OVERFLOW`)                     — italic, Link
 *   - Operator      (`=`, `<`, `>`, `<=`, `>=`, `<>`, `IS`)  — WindowText
 *   - Number        (decimal + clock `HH:MM[:SS]`)           — Text
 *   - Comment       (`;` to EOL)                             — italic, Placeholder
 *
 * The keyword/object/variable vocabularies are aligned with the engine's
 * `Controls.cpp` tokeniser (the `attr ==` / `obj_type ==` ladders at lines
 * 650–736 of that file) so the GUI and engine cannot drift. A small
 * exposed `keywordList()` accessor lets `RuleCodeEditor`'s `QCompleter`
 * reuse the same vocabulary without duplication.
 *
 * Theming. Colours come from the application palette via `QPalette`
 * colour roles rather than hard-coded RGB, so dark / light themes Just
 * Work. The highlighter rebuilds its `QTextCharFormat`s on `setPalette`
 * (called by the editor when the palette changes).
 */
#ifndef OPENSWMMVIS_CONTROLS_RULESYNTAXHIGHLIGHTER_H
#define OPENSWMMVIS_CONTROLS_RULESYNTAXHIGHLIGHTER_H

#include <QPalette>
#include <QStringList>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

namespace openswmmvis::controls {

class RuleSyntaxHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    explicit RuleSyntaxHighlighter(QTextDocument *parent = nullptr);
    ~RuleSyntaxHighlighter() override;

    /*! \brief Rebuild text formats from the supplied palette. Editor
     *  calls this whenever the application palette changes (theme
     *  switch). */
    void setPalette(const QPalette &palette);

    // ── Vocabulary accessors — shared with the completer ────────────────

    static QStringList blockKeywords();   ///< RULE, IF, THEN, ELSE, AND, OR, NOT, PRIORITY
    static QStringList objectTypes();     ///< NODE, LINK, CONDUIT, PUMP, ORIFICE, WEIR, OUTLET, SUBCATCH, SIMULATION, GAGE
    static QStringList variableNames();   ///< DEPTH, HEAD, FLOW, VOLUME, INFLOW, SETTING, STATUS, CLOCKTIME, DATE, MONTH, DAY, TIME, OVERFLOW
    static QStringList operatorTokens();  ///< =, <, >, <=, >=, <>, IS

    /*! \brief Every keyword the completer can offer (block + object +
     *  variable). The completer's context-keyed picker still subsets
     *  these by preceding token, but the registry of valid tokens is
     *  owned here. */
    static QStringList allKeywords();

protected:
    void highlightBlock(const QString &text) override;

private:
    void rebuildFormats_();

    QPalette m_palette;

    QTextCharFormat m_fmtBlockKw;
    QTextCharFormat m_fmtObjectType;
    QTextCharFormat m_fmtVariable;
    QTextCharFormat m_fmtOperator;
    QTextCharFormat m_fmtNumber;
    QTextCharFormat m_fmtComment;
};

} // namespace openswmmvis::controls

#endif // OPENSWMMVIS_CONTROLS_RULESYNTAXHIGHLIGHTER_H
