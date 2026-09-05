/*!
 * \file   gwfexpressionedit.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/widgets/gwfexpressionedit.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_subcatchments.h>

#include <QAbstractItemView>
#include <QCompleter>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QScrollBar>
#include <QStandardItemModel>
#include <QTimer>

namespace openswmmvis::ui {

namespace {

// Headless fallback only (null engine handle) — mirrors the engine's
// Groundwater.hpp GW_VAR_NAMES / MathExpr.cpp func_map at the time of
// writing. With an engine handle the lists come from the engine itself.
QStringList fallbackVariables()
{
    return { QStringLiteral("HGW"), QStringLiteral("HSW"), QStringLiteral("HCB"),
             QStringLiteral("HGS"), QStringLiteral("KS"),  QStringLiteral("K"),
             QStringLiteral("THETA"), QStringLiteral("PHI"), QStringLiteral("FI"),
             QStringLiteral("FU"),  QStringLiteral("A") };
}

QStringList fallbackFunctions()
{
    return { QStringLiteral("abs"),  QStringLiteral("sgn"),  QStringLiteral("sqrt"),
             QStringLiteral("log"),  QStringLiteral("exp"),  QStringLiteral("sin"),
             QStringLiteral("cos"),  QStringLiteral("tan"),  QStringLiteral("asin"),
             QStringLiteral("acos"), QStringLiteral("atan"), QStringLiteral("step"),
             QStringLiteral("min"),  QStringLiteral("max"),  QStringLiteral("cot"),
             QStringLiteral("sinh"), QStringLiteral("cosh"), QStringLiteral("tanh"),
             QStringLiteral("coth"), QStringLiteral("log10"), QStringLiteral("acot") };
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Highlighter
// ─────────────────────────────────────────────────────────────────────────────

GwfSyntaxHighlighter::GwfSyntaxHighlighter(QTextDocument *doc,
                                           const QStringList &variables,
                                           const QStringList &functions)
    : QSyntaxHighlighter(doc)
{
    for (const QString &v : variables) m_vars.insert(v.toUpper());
    for (const QString &f : functions) m_funcs.insert(f.toLower());
    rebuildFormats_();
}

void GwfSyntaxHighlighter::setPalette(const QPalette &palette)
{
    m_palette = palette;
    rebuildFormats_();
    rehighlight();
}

void GwfSyntaxHighlighter::rebuildFormats_()
{
    // Palette roles only (theme-live, matching TreatmentSyntaxHighlighter):
    // variables = Link, functions = LinkVisited, operators = WindowText,
    // numbers = Text, unrecognized identifiers = PlaceholderText italic.
    const QColor link        = m_palette.color(QPalette::Active, QPalette::Link);
    const QColor linkVisited = m_palette.color(QPalette::Active, QPalette::LinkVisited);
    const QColor windowText  = m_palette.color(QPalette::Active, QPalette::WindowText);
    const QColor text        = m_palette.color(QPalette::Active, QPalette::Text);
    const QColor placeholder = m_palette.color(QPalette::Active, QPalette::PlaceholderText);

    m_fmtVariable = QTextCharFormat{};
    m_fmtVariable.setForeground(link.isValid() ? link : QColor("#00695C"));
    m_fmtVariable.setFontItalic(true);

    m_fmtFunction = QTextCharFormat{};
    m_fmtFunction.setForeground(linkVisited.isValid() ? linkVisited
                                                      : QColor("#6A1B9A"));
    m_fmtFunction.setFontWeight(QFont::Bold);

    m_fmtOperator = QTextCharFormat{};
    m_fmtOperator.setForeground(windowText.isValid() ? windowText
                                                     : QColor("#212121"));

    m_fmtNumber = QTextCharFormat{};
    m_fmtNumber.setForeground(text.isValid() ? text : QColor("#37474F"));

    m_fmtUnknown = QTextCharFormat{};
    m_fmtUnknown.setForeground(placeholder.isValid() ? placeholder
                                                     : QColor("#9E9E9E"));
    m_fmtUnknown.setFontItalic(true);
}

void GwfSyntaxHighlighter::highlightBlock(const QString &text)
{
    if (text.isEmpty()) return;

    int i = 0;
    const int n = text.size();
    while (i < n) {
        const QChar c = text.at(i);
        if (c.isSpace()) { ++i; continue; }

        // Operators + parens + comma.
        if (QStringLiteral("+-*/^(),").contains(c)) {
            setFormat(i, 1, m_fmtOperator);
            ++i;
            continue;
        }

        // Number (digits, decimal point, exponent).
        if (c.isDigit() || (c == QLatin1Char('.') && i + 1 < n
                            && text.at(i + 1).isDigit())) {
            int j = i;
            while (j < n) {
                const QChar cj = text.at(j);
                if (cj.isDigit() || cj == QLatin1Char('.')
                    || cj == QLatin1Char('e') || cj == QLatin1Char('E')
                    || ((cj == QLatin1Char('+') || cj == QLatin1Char('-'))
                        && j > i
                        && (text.at(j - 1) == QLatin1Char('e')
                            || text.at(j - 1) == QLatin1Char('E'))))
                    ++j;
                else break;
            }
            setFormat(i, j - i, m_fmtNumber);
            i = j;
            continue;
        }

        // Identifier.
        if (c.isLetter() || c == QLatin1Char('_')) {
            int j = i;
            while (j < n && (text.at(j).isLetterOrNumber()
                             || text.at(j) == QLatin1Char('_')))
                ++j;
            const QString word = text.mid(i, j - i);
            if (m_funcs.contains(word.toLower()))
                setFormat(i, j - i, m_fmtFunction);
            else if (m_vars.contains(word.toUpper()))
                setFormat(i, j - i, m_fmtVariable);
            else
                setFormat(i, j - i, m_fmtUnknown);
            i = j;
            continue;
        }

        // Anything else: flag as unknown (the validator rejects it too).
        setFormat(i, 1, m_fmtUnknown);
        ++i;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Single-line editor
// ─────────────────────────────────────────────────────────────────────────────

GwfExpressionEdit::GwfExpressionEdit(void *engineHandle, QWidget *parent)
    : QTextEdit(parent), m_engine(engineHandle)
{
    setAcceptRichText(false);
    setLineWrapMode(QTextEdit::NoWrap);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setTabChangesFocus(true);
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    // Single-line height.
    const int h = fontMetrics().height() + 10;
    setFixedHeight(h);

    loadVocabulary_();

    m_highlighter = new GwfSyntaxHighlighter(document(), m_varNames, m_funcNames);
    m_highlighter->setPalette(palette());

    // One-column model: DisplayRole = "NAME — description" (what the popup
    // shows), UserRole = bare name (what the completer matches on and
    // inserts). QStandardItem aliases Display/Edit, hence UserRole.
    auto *model = new QStandardItemModel(this);
    for (int i = 0; i < m_varNames.size(); ++i) {
        const QString &name = m_varNames.at(i);
        const QString desc  = i < m_varDescs.size() ? m_varDescs.at(i) : QString();
        auto *item = new QStandardItem(
            desc.isEmpty() ? name : QStringLiteral("%1 — %2").arg(name, desc));
        item->setData(name, Qt::UserRole);
        model->appendRow(item);
    }
    for (const QString &f : m_funcNames) {
        auto *item = new QStandardItem(f + QStringLiteral("()"));
        item->setData(f, Qt::UserRole);
        model->appendRow(item);
    }

    m_completer = new QCompleter(model, this);
    m_completer->setCompletionRole(Qt::UserRole);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setWidget(this);
    connect(m_completer, qOverload<const QString &>(&QCompleter::activated),
            this, &GwfExpressionEdit::insertCompletion_);

    // Debounced live validation (250 ms — same cadence as the treatment edit).
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(250);
    connect(m_debounce, &QTimer::timeout,
            this, &GwfExpressionEdit::validateNow);
    connect(this, &QTextEdit::textChanged,
            this, [this]() { m_debounce->start(); });
}

void GwfExpressionEdit::loadVocabulary_()
{
    if (!m_engine) {
        m_varNames  = fallbackVariables();
        m_varDescs  = QStringList();
        m_funcNames = fallbackFunctions();
        return;
    }
    auto *e = static_cast<SWMM_Engine>(m_engine);
    char buf[256];
    const int nv = swmm_gwf_variable_count(e);
    for (int i = 0; i < nv; ++i) {
        buf[0] = '\0';
        if (swmm_gwf_variable_name(e, i, buf, sizeof(buf)) != SWMM_OK || !buf[0])
            continue;
        m_varNames << QString::fromUtf8(buf);
        buf[0] = '\0';
        swmm_gwf_variable_description(e, i, buf, sizeof(buf));
        m_varDescs << QString::fromUtf8(buf);
    }
    const int nf = swmm_gwf_function_count(e);
    for (int i = 0; i < nf; ++i) {
        buf[0] = '\0';
        if (swmm_gwf_function_name(e, i, buf, sizeof(buf)) == SWMM_OK && buf[0])
            m_funcNames << QString::fromUtf8(buf);
    }
}

void GwfExpressionEdit::setExpression(const QString &text)
{
    setPlainText(text);
    validateNow();
}

void GwfExpressionEdit::validateNow()
{
    const QString expr = toPlainText().trimmed();
    if (expr.isEmpty() || !m_engine) {
        // Empty clears the expression — not an error (the engine validator
        // reports an empty string as invalid, so short-circuit here).
        m_valid = true;
        emit validationChanged(true, QString(), -1);
        return;
    }
    char errbuf[512] = {};
    int col = -1;
    const int rc = swmm_gwf_validate_expression(
        static_cast<SWMM_Engine>(m_engine), expr.toUtf8().constData(),
        errbuf, sizeof(errbuf), &col);
    m_valid = (rc == SWMM_OK);
    emit validationChanged(m_valid, QString::fromUtf8(errbuf), col);
}

void GwfExpressionEdit::insertAtCursor(const QString &text)
{
    QTextCursor tc = textCursor();
    tc.insertText(text);
    setTextCursor(tc);
    setFocus();
}

QString GwfExpressionEdit::textUnderCursor_() const
{
    QTextCursor tc = textCursor();
    tc.select(QTextCursor::WordUnderCursor);
    return tc.selectedText();
}

void GwfExpressionEdit::showCompleter_(const QString &prefix)
{
    m_completer->setCompletionPrefix(prefix);
    QRect cr = cursorRect();
    cr.setWidth(m_completer->popup()->sizeHintForColumn(0)
                + m_completer->popup()->verticalScrollBar()->sizeHint().width());
    m_completer->complete(cr);
}

void GwfExpressionEdit::insertCompletion_(const QString &completion)
{
    if (m_completer->widget() != this) return;
    QTextCursor tc = textCursor();
    tc.select(QTextCursor::WordUnderCursor);
    tc.insertText(completion);
    setTextCursor(tc);
}

void GwfExpressionEdit::focusInEvent(QFocusEvent *e)
{
    if (m_completer) m_completer->setWidget(this);
    QTextEdit::focusInEvent(e);
}

void GwfExpressionEdit::keyPressEvent(QKeyEvent *e)
{
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

    // Single line: Enter commits instead of inserting a newline.
    if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
        emit editingFinished();
        return;
    }

    // Ctrl+Space force-triggers the popup.
    if (e->key() == Qt::Key_Space
        && e->modifiers().testFlag(Qt::ControlModifier)) {
        showCompleter_(textUnderCursor_());
        return;
    }

    QTextEdit::keyPressEvent(e);

    if (!m_completer) return;
    const QString prefix = textUnderCursor_();
    const QString typed  = e->text();
    const bool wordChar  = !typed.isEmpty()
                           && (typed.at(0).isLetterOrNumber()
                               || typed.at(0) == QLatin1Char('_'));
    if (!wordChar || prefix.size() < 2) {
        if (m_completer->popup()) m_completer->popup()->hide();
        return;
    }
    if (prefix != m_completer->completionPrefix())
        showCompleter_(prefix);
}

} // namespace openswmmvis::ui
