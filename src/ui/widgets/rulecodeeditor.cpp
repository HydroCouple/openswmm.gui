/*!
 * \file   rulecodeeditor.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/widgets/rulecodeeditor.h"

#include "controls/rulesyntaxhighlighter.h"
#include "layers/swmmmodellayer.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QScrollBar>
#include <QStringListModel>
#include <QTextCursor>

namespace openswmmvis::ui {

using openswmmvis::controls::RuleSyntaxHighlighter;

RuleCodeEditor::RuleCodeEditor(QWidget *parent)
    : QTextEdit(parent)
{
    // Monospace font — content is code, not prose.
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(mono.pointSize() > 0 ? mono.pointSize() : 11);
    setFont(mono);
    setLineWrapMode(QTextEdit::NoWrap);
    setTabStopDistance(4 * QFontMetricsF(mono).horizontalAdvance(QLatin1Char(' ')));
    setAcceptRichText(false);   // we paint colour via the highlighter; user
                                 // typing should stay plain so paste from
                                 // word processors doesn't smuggle styles in.

    m_highlighter = new RuleSyntaxHighlighter(document());
    m_highlighter->setPalette(palette());

    m_complModel  = new QStringListModel(this);
    m_completer   = new QCompleter(m_complModel, this);
    m_completer->setWidget(this);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setModelSorting(QCompleter::UnsortedModel);
    connect(m_completer, QOverload<const QString &>::of(&QCompleter::activated),
            this, &RuleCodeEditor::insertCompletion_);
}

RuleCodeEditor::~RuleCodeEditor() = default;

void RuleCodeEditor::setModelLayer(SWMMModelLayer *layer)
{
    m_layer = layer;
    rebuildCompletions_();
}

void RuleCodeEditor::focusInEvent(QFocusEvent *e)
{
    if (m_completer) m_completer->setWidget(this);
    QTextEdit::focusInEvent(e);
}

QString RuleCodeEditor::textUnderCursor_() const
{
    QTextCursor c = textCursor();
    c.select(QTextCursor::WordUnderCursor);
    return c.selectedText();
}

void RuleCodeEditor::rebuildCompletions_()
{
    QStringList list = RuleSyntaxHighlighter::allKeywords();

    // Live model object names. The completer offers all node / link /
    // subcatchment names regardless of context — the user's typed prefix
    // does the filtering. A context-keyed picker (NODE → node names
    // only, LINK → link names only) would be more precise but is
    // deferred per the Phase 6.8.2 plan's "first cut" simplification.
    if (m_layer) {
        using L = SWMMModelLayer;
        static constexpr L::Category kSpatialCats[] = {
            L::CatJunctions, L::CatOutfalls, L::CatStorage, L::CatDividers,
            L::CatConduits,  L::CatPumps,    L::CatOrifices,
            L::CatWeirs,     L::CatOutlets,
            L::CatSubcatchments, L::CatRainGages
        };
        for (L::Category cat : kSpatialCats) {
            const int n = m_layer->categoryCount(cat);
            for (int i = 0; i < n; ++i) {
                const QString name = m_layer->objectNameAt(cat, i);
                if (!name.isEmpty()) list << name;
            }
        }
    }
    list.removeDuplicates();
    m_complModel->setStringList(list);
}

void RuleCodeEditor::showCompleter_(const QString &prefix)
{
    rebuildCompletions_();
    m_completer->setCompletionPrefix(prefix);
    // Snap the popup index to the first match so Enter immediately picks
    // the obvious candidate.
    QAbstractItemView *pop = m_completer->popup();
    if (!pop) return;
    pop->setCurrentIndex(m_completer->completionModel()->index(0, 0));

    QRect cr = cursorRect();
    cr.setWidth(pop->sizeHintForColumn(0)
                + pop->verticalScrollBar()->sizeHint().width());
    m_completer->complete(cr);
}

void RuleCodeEditor::keyPressEvent(QKeyEvent *e)
{
    // If the completer popup is open, route the navigation keys to it.
    QAbstractItemView *pop = m_completer ? m_completer->popup() : nullptr;
    if (pop && pop->isVisible()) {
        switch (e->key()) {
        case Qt::Key_Enter:
        case Qt::Key_Return:
        case Qt::Key_Escape:
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
            e->ignore();
            return;
        default:
            break;
        }
    }

    // Ctrl+Space force-triggers the popup at the current cursor.
    const bool forceShow = (e->key() == Qt::Key_Space)
                           && (e->modifiers().testFlag(Qt::ControlModifier));
    if (forceShow) {
        showCompleter_(textUnderCursor_());
        return;
    }

    QTextEdit::keyPressEvent(e);

    if (!m_completer) return;
    // Auto-trigger after 2 chars of prefix, on word characters only.
    const QString prefix = textUnderCursor_();
    const QString text   = e->text();
    const bool wordChar  = !text.isEmpty() && (text.at(0).isLetterOrNumber() || text.at(0) == QLatin1Char('_'));

    if (!wordChar || prefix.size() < 2) {
        if (m_completer->popup()) m_completer->popup()->hide();
        return;
    }
    if (prefix != m_completer->completionPrefix())
        showCompleter_(prefix);
}

void RuleCodeEditor::insertCompletion_(const QString &completion)
{
    if (!m_completer || m_completer->widget() != this) return;
    QTextCursor c = textCursor();
    const int extra = completion.length() - m_completer->completionPrefix().length();
    c.movePosition(QTextCursor::Left);
    c.movePosition(QTextCursor::EndOfWord);
    c.insertText(completion.right(extra));
    setTextCursor(c);
}

} // namespace openswmmvis::ui
