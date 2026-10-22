/**
 * @file      rulesyntaxhighlighter.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BR Phase 6.8.2 — QSyntaxHighlighter for SWMM control rules.
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
 *                    `TIME`, `OVERFLOW`, `INTENSITY`,
 *                    `n-HR_DEPTH`)                          — italic, Link
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_CONTROLS_RULESYNTAXHIGHLIGHTER_H
#define OPENSWMMVIS_CONTROLS_RULESYNTAXHIGHLIGHTER_H

#include <QPalette>
#include <QStringList>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

namespace openswmmvis::controls
{

class RuleSyntaxHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    /**
     * @brief Attach rule token highlighting to a text document.
     * @param[in] parent Optional document owner/target; formatting follows GUI-thread QTextDocument rules.
     */
    explicit RuleSyntaxHighlighter(QTextDocument *parent = nullptr);
    /**
     * @brief Detach/destroy highlighting state through QSyntaxHighlighter.
     */
    ~RuleSyntaxHighlighter() override;

    /*! \brief Rebuild text formats from the supplied palette. Editor
     *  calls this whenever the application palette changes (theme
     *  switch).
     * @param[in] palette Palette copied to derive token formats from Active color roles.
     * @details Rebuilds formats and immediately rehighlights the document.
     */
    void setPalette(const QPalette &palette);

    // ── Vocabulary accessors — shared with the completer ────────────────

    /**
     * @brief Copy canonical rule-control keywords for highlighting/completion.
     * @return Uppercase block vocabulary in stable declaration order.
     */
    static QStringList blockKeywords(); ///< RULE, IF, THEN, ELSE, AND, OR, NOT, PRIORITY
    /**
     * @brief Copy recognized control-rule object categories.
     * @return Canonical uppercase object tokens; no project object names are included.
     */
    static QStringList
    objectTypes(); ///< NODE, LINK, CONDUIT, PUMP, ORIFICE, WEIR, OUTLET, SUBCATCH, SIMULATION, GAGE
    /**
     * @brief Copy recognized variable names for highlighting/completion.
     * @return Canonical uppercase variable vocabulary with representative rainfall-window attributes; parameterized windows are recognized separately.
     */
    static QStringList
    variableNames(); ///< Includes INTENSITY and representative n-HR_DEPTH attributes
    /**
     * @brief Copy recognized rule comparison tokens.
     * @return Symbolic comparisons plus IS.
     */
    static QStringList operatorTokens(); ///< =, <, >, <=, >=, <>, IS

    /*! \brief Every keyword the completer can offer (block + object +
     *  variable). The completer's context-keyed picker still subsets
     *  these by preceding token, but the registry of valid tokens is
     *  owned here.
     * @return Block keywords followed by object types and variable names, excluding operatorTokens(); caller owns the copy.
     */
    static QStringList allKeywords();

protected:
    /**
     * @brief Apply lexical styles to one document block, without validating rule grammar.
     * @param[in] text Current block text supplied by Qt.
     * @details Semicolon comments win over all other token classes. Matching is case-insensitive; numeric n-HR_DEPTH forms receive variable styling without checking the engine window limit.
     */
    void highlightBlock(const QString &text) override;

private:
    /**
     * @brief Rebuild character formats from cached palette colors and fixed fallback colors.
     * @details Does not itself request document rehighlighting.
     */
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
