/*!
 * \file   reactionexpressionedit.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/widgets/reactionexpressionedit.h"

#include <openswmm/engine/openswmm_engine.h>

#include <QAbstractItemView>
#include <QCompleter>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QScrollBar>
#include <QStringListModel>
#include <QTimer>

namespace openswmmvis::ui {

// ─────────────────────────────────────────────────────────────────────────────
// Highlighter
// ─────────────────────────────────────────────────────────────────────────────

QStringList ReactionSyntaxHighlighter::hydVarNames()
{
    QStringList out;
    const int n = swmm_reaction_hydvar_count();
    for (int i = 0; i < n; ++i) {
        char name[16], desc[128];
        if (swmm_reaction_hydvar_get(i, name, 16, desc, 128) == SWMM_OK)
            out << QString::fromUtf8(name);
    }
    return out;
}

QStringList ReactionSyntaxHighlighter::functionNames()
{
    QStringList out;
    const int n = swmm_reaction_function_count();
    for (int i = 0; i < n; ++i) {
        char name[16];
        int arity = 0;
        if (swmm_reaction_function_get(i, name, 16, &arity) == SWMM_OK)
            out << QString::fromUtf8(name);
    }
    return out;
}

ReactionSyntaxHighlighter::ReactionSyntaxHighlighter(QTextDocument *doc)
    : QSyntaxHighlighter(doc)
{
    rebuildFormats_();
}

void ReactionSyntaxHighlighter::setModelIdentifiers(const QSet<QString> &names)
{
    m_modelIdents = names;
    rehighlight();
}

void ReactionSyntaxHighlighter::setPalette(const QPalette &palette)
{
    m_palette = palette;
    rebuildFormats_();
    rehighlight();
}

void ReactionSyntaxHighlighter::rebuildFormats_()
{
    // Palette roles only (theme-live — the TreatmentSyntaxHighlighter
    // convention): hydraulic variables = Link italic, model identifiers =
    // Link bold, functions = LinkVisited bold, operators = WindowText,
    // numbers = Text, unrecognized identifiers = PlaceholderText italic.
    const QColor link        = m_palette.color(QPalette::Active, QPalette::Link);
    const QColor linkVisited = m_palette.color(QPalette::Active, QPalette::LinkVisited);
    const QColor windowText  = m_palette.color(QPalette::Active, QPalette::WindowText);
    const QColor text        = m_palette.color(QPalette::Active, QPalette::Text);
    const QColor placeholder = m_palette.color(QPalette::Active, QPalette::PlaceholderText);

    m_fmtVariable = QTextCharFormat{};
    m_fmtVariable.setForeground(link.isValid() ? link : QColor("#00695C"));
    m_fmtVariable.setFontItalic(true);

    m_fmtModelIdent = QTextCharFormat{};
    m_fmtModelIdent.setForeground(link.isValid() ? link : QColor("#00695C"));
    m_fmtModelIdent.setFontWeight(QFont::Bold);

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

void ReactionSyntaxHighlighter::highlightBlock(const QString &text)
{
    if (text.isEmpty()) return;

    static const QSet<QString> kHydVars = [] {
        QSet<QString> s;
        for (const QString &v : hydVarNames()) s.insert(v.toUpper());
        return s;
    }();
    static const QSet<QString> kFuncs = [] {
        QSet<QString> s;
        for (const QString &f : functionNames()) s.insert(f.toUpper());
        return s;
    }();

    int i = 0;
    const int n = text.size();
    while (i < n) {
        const QChar c = text.at(i);
        if (c.isSpace()) { ++i; continue; }

        if (QStringLiteral("+-*/^(),").contains(c)) {
            setFormat(i, 1, m_fmtOperator);
            ++i;
            continue;
        }

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

        if (c.isLetter() || c == QLatin1Char('_')) {
            int j = i;
            while (j < n && (text.at(j).isLetterOrNumber()
                             || text.at(j) == QLatin1Char('_')))
                ++j;
            const QString word = text.mid(i, j - i);
            // Model identifiers match case-sensitively (the compiler's
            // rule); functions and hydraulic variables case-insensitively.
            if (m_modelIdents.contains(word))
                setFormat(i, j - i, m_fmtModelIdent);
            else if (kFuncs.contains(word.toUpper()))
                setFormat(i, j - i, m_fmtFunction);
            else if (kHydVars.contains(word.toUpper()))
                setFormat(i, j - i, m_fmtVariable);
            else
                setFormat(i, j - i, m_fmtUnknown);
            i = j;
            continue;
        }

        setFormat(i, 1, m_fmtUnknown);
        ++i;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Single-line editor
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Species + coefficients + terms + pollutants from the live engine.
QSet<QString> modelIdentifiers(void *engineHandle)
{
    QSet<QString> out;
    if (!engineHandle) return out;
    auto e = static_cast<SWMM_Engine>(engineHandle);

    char name[128];
    const int ns = swmm_reaction_species_count(e);
    for (int i = 0; i < ns; ++i) {
        char units[32];
        int wall = 0;
        double a = 0, r = 0;
        if (swmm_reaction_species_get(e, i, name, 128, &wall, units, 32,
                                      &a, &r) == SWMM_OK)
            out.insert(QString::fromUtf8(name));
    }
    const int nc = swmm_reaction_coeff_count(e);
    for (int i = 0; i < nc; ++i) {
        int is_param = 0;
        double v = 0;
        if (swmm_reaction_coeff_get(e, i, name, 128, &is_param, &v)
                == SWMM_OK)
            out.insert(QString::fromUtf8(name));
    }
    const int nt = swmm_reaction_term_count(e);
    for (int i = 0; i < nt; ++i) {
        char expr[512];
        if (swmm_reaction_term_get(e, i, name, 128, expr, 512) == SWMM_OK)
            out.insert(QString::fromUtf8(name));
    }
    const int np = swmm_pollutant_count(e);
    for (int i = 0; i < np; ++i)
        if (const char *id = swmm_pollutant_id(e, i))
            out.insert(QString::fromUtf8(id));
    return out;
}

} // namespace

ReactionExpressionEdit::ReactionExpressionEdit(void *engineHandle, int scope,
                                               QWidget *parent)
    : QTextEdit(parent), m_engine(engineHandle), m_scope(scope)
{
    setAcceptRichText(false);
    setLineWrapMode(QTextEdit::NoWrap);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setTabChangesFocus(true);
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    const int h = fontMetrics().height() + 10;
    setFixedHeight(h);

    m_highlighter = new ReactionSyntaxHighlighter(document());
    m_highlighter->setPalette(palette());

    m_completer = new QCompleter(this);
    m_completer->setModel(new QStringListModel(m_completer));
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setWidget(this);
    connect(m_completer, qOverload<const QString &>(&QCompleter::activated),
            this, &ReactionExpressionEdit::insertCompletion_);
    refreshVocabulary();

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(250);
    connect(m_debounce, &QTimer::timeout,
            this, &ReactionExpressionEdit::validateNow);
    connect(this, &QTextEdit::textChanged,
            this, [this]() { m_debounce->start(); });
}

void ReactionExpressionEdit::refreshVocabulary()
{
    const QSet<QString> idents = modelIdentifiers(m_engine);
    m_highlighter->setModelIdentifiers(idents);

    QStringList words(idents.cbegin(), idents.cend());
    words += ReactionSyntaxHighlighter::hydVarNames();
    words += ReactionSyntaxHighlighter::functionNames();
    words.sort(Qt::CaseInsensitive);
    if (auto *m = qobject_cast<QStringListModel *>(m_completer->model()))
        m->setStringList(words);
}

void ReactionExpressionEdit::setExpression(const QString &text)
{
    setPlainText(text);
    validateNow();
}

void ReactionExpressionEdit::validateNow()
{
    const QString expr = toPlainText().trimmed();
    if (expr.isEmpty() || !m_engine) {
        // Empty clears the expression — not an error.
        emit validationChanged(true, QString(), -1);
        return;
    }
    char errbuf[512] = {};
    int col = -1;
    const int rc = swmm_reaction_validate_expression(
        static_cast<SWMM_Engine>(m_engine), m_scope,
        expr.toUtf8().constData(), errbuf, sizeof(errbuf), &col);
    emit validationChanged(rc == SWMM_OK, QString::fromUtf8(errbuf), col);
}

QString ReactionExpressionEdit::textUnderCursor_() const
{
    QTextCursor tc = textCursor();
    tc.select(QTextCursor::WordUnderCursor);
    return tc.selectedText();
}

void ReactionExpressionEdit::showCompleter_(const QString &prefix)
{
    m_completer->setCompletionPrefix(prefix);
    QRect cr = cursorRect();
    cr.setWidth(m_completer->popup()->sizeHintForColumn(0)
                + m_completer->popup()->verticalScrollBar()->sizeHint().width());
    m_completer->complete(cr);
}

void ReactionExpressionEdit::insertCompletion_(const QString &completion)
{
    if (m_completer->widget() != this) return;
    QTextCursor tc = textCursor();
    tc.select(QTextCursor::WordUnderCursor);
    tc.insertText(completion);
    setTextCursor(tc);
}

void ReactionExpressionEdit::focusInEvent(QFocusEvent *e)
{
    if (m_completer) m_completer->setWidget(this);
    QTextEdit::focusInEvent(e);
}

void ReactionExpressionEdit::keyPressEvent(QKeyEvent *e)
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

    if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
        emit editingFinished();
        return;
    }

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

ReactionExpressionDelegate::ReactionExpressionDelegate(void *engineHandle,
                                                       int defaultScope,
                                                       QObject *parent)
    : QStyledItemDelegate(parent), m_engine(engineHandle),
      m_defaultScope(defaultScope)
{
}

QWidget *ReactionExpressionDelegate::createEditor(
    QWidget *parent, const QStyleOptionViewItem & /*option*/,
    const QModelIndex &index) const
{
    const QVariant sv = index.data(ScopeRole);
    const int scope = sv.isValid() ? sv.toInt() : m_defaultScope;
    auto *editor = new ReactionExpressionEdit(m_engine, scope, parent);
    connect(editor, &ReactionExpressionEdit::validationChanged,
            this, &ReactionExpressionDelegate::validationChanged);
    connect(editor, &ReactionExpressionEdit::editingFinished, this,
            [this, editor]() {
                auto *self = const_cast<ReactionExpressionDelegate *>(this);
                emit self->commitData(editor);
                emit self->closeEditor(editor);
            });
    return editor;
}

void ReactionExpressionDelegate::setEditorData(QWidget *editor,
                                               const QModelIndex &index) const
{
    if (auto *e = qobject_cast<ReactionExpressionEdit *>(editor)) {
        e->setExpression(index.data(Qt::EditRole).toString());
        return;
    }
    QStyledItemDelegate::setEditorData(editor, index);
}

void ReactionExpressionDelegate::setModelData(QWidget *editor,
                                              QAbstractItemModel *model,
                                              const QModelIndex &index) const
{
    if (auto *e = qobject_cast<ReactionExpressionEdit *>(editor)) {
        model->setData(index, e->expression().trimmed(), Qt::EditRole);
        return;
    }
    QStyledItemDelegate::setModelData(editor, model, index);
}

} // namespace openswmmvis::ui
