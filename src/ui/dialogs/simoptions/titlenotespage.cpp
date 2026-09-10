/*!
 * \file   titlenotespage.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/simoptions/titlenotespage.h"

#include <QAction>
#include <QFont>
#include <QKeySequence>
#include <QSignalBlocker>
#include <QSize>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextListFormat>
#include <QToolBar>
#include <QVBoxLayout>

#include "swmmvisprojectwindow.h"

namespace openswmmvis::ui
{

TitleNotesPage::TitleNotesPage(SimOptionsContext &ctx, QWidget *parent)
    : SimOptionsPage(ctx, parent)
{
    buildUi();
}

QString TitleNotesPage::title() const
{
    return tr("Title / Notes");
}

void TitleNotesPage::buildUi()
{
    auto *vlay = new QVBoxLayout(this);

    auto *toolbar = new QToolBar(this);
    toolbar->setIconSize(QSize(16, 16));

    m_boldAction = toolbar->addAction(tr("Bold"));
    m_boldAction->setShortcut(QKeySequence::Bold);
    m_boldAction->setCheckable(true);
    m_boldAction->setToolTip(tr("Bold (Ctrl+B)"));

    m_italicAction = toolbar->addAction(tr("Italic"));
    m_italicAction->setShortcut(QKeySequence::Italic);
    m_italicAction->setCheckable(true);
    m_italicAction->setToolTip(tr("Italic (Ctrl+I)"));

    m_underlineAction = toolbar->addAction(tr("Underline"));
    m_underlineAction->setShortcut(QKeySequence::Underline);
    m_underlineAction->setCheckable(true);
    m_underlineAction->setToolTip(tr("Underline (Ctrl+U)"));

    toolbar->addSeparator();
    auto *bulletAction = toolbar->addAction(tr("Bulleted list"));
    bulletAction->setToolTip(tr("Insert bulleted list"));
    auto *numberedAction = toolbar->addAction(tr("Numbered list"));
    numberedAction->setToolTip(tr("Insert numbered list"));

    vlay->addWidget(toolbar);

    m_notesEdit = new QTextEdit(this);
    m_notesEdit->setAcceptRichText(true);
    m_notesEdit->setPlaceholderText(
        tr("Enter project title and notes (mirrors the SWMM [TITLE] section)."));
    tagOption(m_notesEdit, "[TITLE]");
    vlay->addWidget(m_notesEdit, 1);

    connect(m_boldAction, &QAction::triggered, this, [this](bool checked) {
        if (!m_notesEdit) return;
        QTextCharFormat fmt;
        fmt.setFontWeight(checked ? QFont::Bold : QFont::Normal);
        m_notesEdit->mergeCurrentCharFormat(fmt);
    });
    connect(m_italicAction, &QAction::triggered, this, [this](bool checked) {
        if (!m_notesEdit) return;
        QTextCharFormat fmt;
        fmt.setFontItalic(checked);
        m_notesEdit->mergeCurrentCharFormat(fmt);
    });
    connect(m_underlineAction, &QAction::triggered, this, [this](bool checked) {
        if (!m_notesEdit) return;
        QTextCharFormat fmt;
        fmt.setFontUnderline(checked);
        m_notesEdit->mergeCurrentCharFormat(fmt);
    });
    auto applyListStyle = [this](QTextListFormat::Style style) {
        if (!m_notesEdit) return;
        QTextCursor c = m_notesEdit->textCursor();
        c.createList(style);
    };
    connect(bulletAction,   &QAction::triggered, this,
            [applyListStyle]() { applyListStyle(QTextListFormat::ListDisc); });
    connect(numberedAction, &QAction::triggered, this,
            [applyListStyle]() { applyListStyle(QTextListFormat::ListDecimal); });

    connect(m_notesEdit, &QTextEdit::currentCharFormatChanged, this,
            [this](const QTextCharFormat &fmt) {
                if (m_boldAction)
                    m_boldAction->setChecked(fmt.fontWeight() >= QFont::Bold);
                if (m_italicAction)
                    m_italicAction->setChecked(fmt.fontItalic());
                if (m_underlineAction)
                    m_underlineAction->setChecked(fmt.fontUnderline());
            });
}

void TitleNotesPage::read()
{
    if (!m_notesEdit) return;
    QSignalBlocker blk(m_notesEdit);
    // Prefer the .oswp-persisted rich HTML when available — it preserves
    // formatting that the engine's plain-text [TITLE] cannot.
    SWMMVisProjectWindow *pw = ctx_.projectWindow();
    const QString persistedHtml = pw ? pw->notesHtml() : QString();
    if (!persistedHtml.isEmpty()) {
        m_notesEdit->setHtml(persistedHtml);
    } else if (SWMM_Engine e = ctx_.engine()) {
        int count = 0;
        QStringList lines;
        if (swmm_title_get_count(e, &count) == 0 && count > 0) {
            lines.reserve(count);
            for (int i = 0; i < count; ++i) {
                char buf[1024] = {0};
                if (swmm_title_get_line(e, i, buf, sizeof(buf)) == 0)
                    lines << QString::fromUtf8(buf);
            }
        }
        m_notesEdit->setPlainText(lines.join(QChar('\n')));
    } else {
        m_notesEdit->clear();
    }
    m_initialHtml = m_notesEdit->toHtml();
}

int TitleNotesPage::write()
{
    if (!m_notesEdit) return 0;
    const QString currentHtml = m_notesEdit->toHtml();
    if (currentHtml == m_initialHtml) return 0;

    int n = 0;
    if (SWMM_Engine e = ctx_.engine()) {
        const QString plain = m_notesEdit->toPlainText();
        if (swmm_title_clear(e) == 0) {
            const QByteArray utf8 = plain.toUtf8();
            if (swmm_title_set(e, utf8.constData()) == 0)
                ++n;
        }
    }
    if (SWMMVisProjectWindow *pw = ctx_.projectWindow()) {
        const QString plain = m_notesEdit->toPlainText();
        // Drop the HTML if the document only carries plain text — keeps
        // .oswp tidy and matches the "no notes" empty case.
        pw->setNotesHtml(plain.isEmpty() ? QString() : currentHtml);
    }
    m_initialHtml = currentHtml;
    return n;
}

} // namespace openswmmvis::ui
