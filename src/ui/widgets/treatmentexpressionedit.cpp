/*!
 * \file   treatmentexpressionedit.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/widgets/treatmentexpressionedit.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_quality.h>

#include <QAbstractItemView>
#include <QCompleter>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QScrollBar>
#include <QSet>
#include <QStringListModel>
#include <QTimer>

namespace openswmmvis::ui {

// ─────────────────────────────────────────────────────────────────────────────
// Highlighter
// ─────────────────────────────────────────────────────────────────────────────

QStringList TreatmentSyntaxHighlighter::variableNames()
{
    return { QStringLiteral("C"), QStringLiteral("R"), QStringLiteral("DT"),
             QStringLiteral("HRT"), QStringLiteral("Q"), QStringLiteral("V"),
             QStringLiteral("D"), QStringLiteral("AREA") };
}

QStringList TreatmentSyntaxHighlighter::functionNames()
{
    return { QStringLiteral("exp"), QStringLiteral("log"),
             QStringLiteral("ln"), QStringLiteral("sqrt"),
             QStringLiteral("min"), QStringLiteral("max"),
             QStringLiteral("abs"), QStringLiteral("sgn"),
             QStringLiteral("step") };
}

QStringList TreatmentSyntaxHighlighter::allKeywords()
{
    return variableNames() + functionNames();
}

TreatmentSyntaxHighlighter::TreatmentSyntaxHighlighter(QTextDocument *doc)
    : QSyntaxHighlighter(doc)
{
    rebuildFormats_();
}

void TreatmentSyntaxHighlighter::setPalette(const QPalette &palette)
{
    m_palette = palette;
    rebuildFormats_();
    rehighlight();
}

void TreatmentSyntaxHighlighter::rebuildFormats_()
{
    // Palette roles only (theme-live, matching RuleSyntaxHighlighter):
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

void TreatmentSyntaxHighlighter::highlightBlock(const QString &text)
{
    if (text.isEmpty()) return;

    static const QSet<QString> kVars = [] {
        QSet<QString> s;
        for (const QString &v : variableNames()) s.insert(v.toUpper());
        return s;
    }();
    static const QSet<QString> kFuncs = [] {
        QSet<QString> s;
        for (const QString &f : functionNames()) s.insert(f.toLower());
        return s;
    }();

    int i = 0;
    const int n = text.size();
    while (i < n) {
        const QChar c = text.at(i);
        if (c.isSpace()) { ++i; continue; }

        // Operators + parens + comma.
        if (QStringLiteral("+-*/^(),=").contains(c)) {
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
            if (kFuncs.contains(word.toLower()))
                setFormat(i, j - i, m_fmtFunction);
            else if (kVars.contains(word.toUpper()))
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

TreatmentExpressionEdit::TreatmentExpressionEdit(void *engineHandle,
                                                 QWidget *parent)
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

    m_highlighter = new TreatmentSyntaxHighlighter(document());
    m_highlighter->setPalette(palette());

    m_completer = new QCompleter(this);
    m_completer->setModel(new QStringListModel(
        TreatmentSyntaxHighlighter::allKeywords(), m_completer));
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setWidget(this);
    connect(m_completer, qOverload<const QString &>(&QCompleter::activated),
            this, &TreatmentExpressionEdit::insertCompletion_);

    // Debounced live validation (250 ms — same cadence as RuleValidator).
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(250);
    connect(m_debounce, &QTimer::timeout,
            this, &TreatmentExpressionEdit::validateNow);
    connect(this, &QTextEdit::textChanged,
            this, [this]() { m_debounce->start(); });
}

void TreatmentExpressionEdit::setExpression(const QString &text)
{
    setPlainText(text);
    validateNow();
}

void TreatmentExpressionEdit::validateNow()
{
    const QString expr = toPlainText().trimmed();
    if (expr.isEmpty()) {
        // Empty clears the expression — not an error.
        emit validationChanged(true, QString(), -1);
        return;
    }
    if (!m_engine) {
        emit validationChanged(true, QString(), -1);
        return;
    }
    char errbuf[512] = {};
    int col = -1;
    const int rc = swmm_treatment_validate_expression(
        static_cast<SWMM_Engine>(m_engine), expr.toUtf8().constData(),
        errbuf, sizeof(errbuf), &col);
    emit validationChanged(rc == SWMM_OK,
                           QString::fromUtf8(errbuf), col);
}

QString TreatmentExpressionEdit::textUnderCursor_() const
{
    QTextCursor tc = textCursor();
    tc.select(QTextCursor::WordUnderCursor);
    return tc.selectedText();
}

void TreatmentExpressionEdit::showCompleter_(const QString &prefix)
{
    m_completer->setCompletionPrefix(prefix);
    QRect cr = cursorRect();
    cr.setWidth(m_completer->popup()->sizeHintForColumn(0)
                + m_completer->popup()->verticalScrollBar()->sizeHint().width());
    m_completer->complete(cr);
}

void TreatmentExpressionEdit::insertCompletion_(const QString &completion)
{
    if (m_completer->widget() != this) return;
    QTextCursor tc = textCursor();
    tc.select(QTextCursor::WordUnderCursor);
    tc.insertText(completion);
    setTextCursor(tc);
}

void TreatmentExpressionEdit::focusInEvent(QFocusEvent *e)
{
    if (m_completer) m_completer->setWidget(this);
    QTextEdit::focusInEvent(e);
}

void TreatmentExpressionEdit::keyPressEvent(QKeyEvent *e)
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

// ─────────────────────────────────────────────────────────────────────────────
// Delegate
// ─────────────────────────────────────────────────────────────────────────────

TreatmentExpressionDelegate::TreatmentExpressionDelegate(void *engineHandle,
                                                         QObject *parent)
    : QStyledItemDelegate(parent), m_engine(engineHandle)
{
}

QWidget *TreatmentExpressionDelegate::createEditor(
    QWidget *parent, const QStyleOptionViewItem & /*option*/,
    const QModelIndex & /*index*/) const
{
    auto *editor = new TreatmentExpressionEdit(m_engine, parent);
    connect(editor, &TreatmentExpressionEdit::validationChanged,
            this, &TreatmentExpressionDelegate::validationChanged);
    connect(editor, &TreatmentExpressionEdit::editingFinished, this,
            [this, editor]() {
                auto *self = const_cast<TreatmentExpressionDelegate *>(this);
                emit self->commitData(editor);
                emit self->closeEditor(editor);
            });
    return editor;
}

void TreatmentExpressionDelegate::setEditorData(QWidget *editor,
                                                const QModelIndex &index) const
{
    if (auto *e = qobject_cast<TreatmentExpressionEdit *>(editor)) {
        e->setExpression(index.data(Qt::EditRole).toString());
        return;
    }
    QStyledItemDelegate::setEditorData(editor, index);
}

void TreatmentExpressionDelegate::setModelData(QWidget *editor,
                                               QAbstractItemModel *model,
                                               const QModelIndex &index) const
{
    if (auto *e = qobject_cast<TreatmentExpressionEdit *>(editor)) {
        model->setData(index, e->expression().trimmed(), Qt::EditRole);
        return;
    }
    QStyledItemDelegate::setModelData(editor, model, index);
}

} // namespace openswmmvis::ui
