/*!
 * \file   statusreportdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice GUI-2026-05-30 §6 — two-panel Report Viewer implementation.
 */
#include "ui/dialogs/statusreportdialog.h"
#include "ui/theme/themehelpers.h"
#include "ui/theme/iconfactory.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QVBoxLayout>

namespace openswmmvis::ui {

namespace {

// Section header lines in a SWMM .rpt are surrounded by runs of '*' or '-'.
// Continuity-error fail line we want to mark in red:
//   "  Continuity Error (%) ............ XX.XX"
// with XX.XX > 10.0 triggers attention.
constexpr double kContinuityErrorThresholdPct = 10.0;

// ---------------------------------------------------------------------------
// RptSyntaxHighlighter
// ---------------------------------------------------------------------------
class RptSyntaxHighlighter final : public QSyntaxHighlighter
{
public:
    explicit RptSyntaxHighlighter(QTextDocument *doc)
        : QSyntaxHighlighter(doc)
    {
        m_headerFmt.setForeground(QColor(0x1f, 0x4e, 0x79)); // muted accent blue
        m_headerFmt.setFontWeight(QFont::Bold);

        m_dividerFmt.setForeground(QColor(0x88, 0x88, 0x88));

        m_unitsFmt.setForeground(QColor(0x4a, 0x7a, 0x4a));  // muted green

        m_errorFmt.setForeground(QColor(0xb0, 0x1c, 0x00));
        m_errorFmt.setFontWeight(QFont::Bold);
    }

protected:
    void highlightBlock(const QString &text) override
    {
        // Asterisk dividers and dash dividers — section delimiters.
        static const QRegularExpression dividerRx(
            QStringLiteral("^[\\*\\-]{3,}\\s*$"));
        if (dividerRx.match(text).hasMatch()) {
            setFormat(0, text.length(), m_dividerFmt);
            return;
        }

        // Section-title line — bold, accent.  We treat a non-empty line that
        // starts at column 0 (no leading whitespace) and is shorter than
        // 80 chars as a candidate title.  Conservative heuristic that
        // catches the centred-title pattern in SWMM .rpt output.
        if (!text.isEmpty()
            && !text.startsWith(QLatin1Char(' '))
            && text.length() < 80
            && !text.trimmed().contains(QLatin1Char('=')))
        {
            // Bias against pure tabular rows: titles rarely contain '...'.
            if (!text.contains(QStringLiteral(".."))) {
                setFormat(0, text.length(), m_headerFmt);
                return;
            }
        }

        // Continuity error highlight: looks for "Continuity Error (%)" or
        // "Continuity Error" followed by a number > threshold.
        static const QRegularExpression contRx(
            QStringLiteral(R"(Continuity\s+Error\s*\(%\)\s*\.*\s*(-?\d+(?:\.\d+)?))"));
        QRegularExpressionMatch m = contRx.match(text);
        if (m.hasMatch()) {
            const double val = m.captured(1).toDouble();
            if (std::abs(val) > kContinuityErrorThresholdPct) {
                setFormat(0, text.length(), m_errorFmt);
                return;
            }
        }

        // Generic units / trailing token highlight — light touch.
        static const QRegularExpression unitsRx(
            QStringLiteral(R"(\b(cfs|cms|gpm|mgd|cu\s*ft|cu\s*m|mm|inches|hours|days)\b)"));
        auto it = unitsRx.globalMatch(text);
        while (it.hasNext()) {
            const auto mu = it.next();
            setFormat(int(mu.capturedStart()), int(mu.capturedLength()),
                      m_unitsFmt);
        }
    }

private:
    QTextCharFormat m_headerFmt;
    QTextCharFormat m_dividerFmt;
    QTextCharFormat m_unitsFmt;
    QTextCharFormat m_errorFmt;
};

QString readReportText(const QString &path, QString *errorOut = nullptr)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (errorOut) *errorOut = f.errorString();
        return {};
    }

    QString text = QString::fromUtf8(f.readAll());
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    if (!text.isEmpty() && !text.endsWith(QLatin1Char('\n')))
        text += QLatin1Char('\n');
    return text;
}

QString joinedSectionBodies(const QVector<openswmmvis::io::RptSection> &sections)
{
    QString text;
    for (const auto &s : sections)
        text += s.body;
    return text;
}

// ---------------------------------------------------------------------------
// RptViewer — QPlainTextEdit with a line-number gutter (the canonical Qt
// "Code Editor" margin). The report renders wholesale with NoWrap, so one
// text block == one file line and the gutter is a 1:1 line map of the .rpt:
// the last gutter number equals the file's line count, which makes "is
// anything truncated?" answerable at a glance against any external editor.
// ---------------------------------------------------------------------------
class RptViewer : public QPlainTextEdit
{
public:
    explicit RptViewer(QWidget *parent = nullptr) : QPlainTextEdit(parent)
    {
        m_gutter = new Gutter(this);
        connect(this, &QPlainTextEdit::blockCountChanged,
                this, [this](int) { applyGutterWidth(); });
        connect(this, &QPlainTextEdit::updateRequest,
                this, [this](const QRect &r, int dy) {
                    if (dy != 0)
                        m_gutter->scroll(0, dy);
                    else
                        m_gutter->update(0, r.y(), m_gutter->width(), r.height());
                    if (r.contains(viewport()->rect()))
                        applyGutterWidth();
                });
        applyGutterWidth();
    }

    [[nodiscard]] int gutterWidth() const
    {
        int digits = 1;
        for (int m = qMax(1, blockCount()); m >= 10; m /= 10)
            ++digits;
        return 12 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
    }

protected:
    void resizeEvent(QResizeEvent *e) override
    {
        QPlainTextEdit::resizeEvent(e);
        const QRect cr = contentsRect();
        m_gutter->setGeometry(QRect(cr.left(), cr.top(), gutterWidth(), cr.height()));
    }

private:
    class Gutter : public QWidget
    {
    public:
        explicit Gutter(RptViewer *v) : QWidget(v), m_v(v) {}
        [[nodiscard]] QSize sizeHint() const override
        { return QSize(m_v->gutterWidth(), 0); }
    protected:
        void paintEvent(QPaintEvent *e) override { m_v->paintGutter(e); }
    private:
        RptViewer *m_v;
    };

    void applyGutterWidth() { setViewportMargins(gutterWidth(), 0, 0, 0); }

    void paintGutter(QPaintEvent *e)
    {
        QPainter p(m_gutter);
        p.fillRect(e->rect(), palette().color(QPalette::Window));
        p.setPen(palette().color(QPalette::PlaceholderText));

        QTextBlock block = firstVisibleBlock();
        int num    = block.blockNumber() + 1;
        int top    = qRound(blockBoundingGeometry(block)
                                .translated(contentOffset()).top());
        int bottom = top + qRound(blockBoundingRect(block).height());
        const int w = m_gutter->width() - 6;

        while (block.isValid() && top <= e->rect().bottom())
        {
            if (block.isVisible() && bottom >= e->rect().top())
                p.drawText(0, top, w, fontMetrics().height(),
                           Qt::AlignRight, QString::number(num));
            block  = block.next();
            top    = bottom;
            bottom = top + qRound(blockBoundingRect(block).height());
            ++num;
        }
    }

    Gutter *m_gutter = nullptr;
    friend class Gutter;
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// StatusReportDialog
// ---------------------------------------------------------------------------

StatusReportDialog::StatusReportDialog(const QString &rptPath, QWidget *parent)
    : StatusReportDialog(QVector<ReportSource>{
          { QFileInfo(rptPath).fileName(), rptPath } }, 0, parent)
{
}

StatusReportDialog::StatusReportDialog(const QVector<ReportSource> &sources,
                                       int initialIndex, QWidget *parent)
    : QDialog(parent), m_sources(sources)
{
    // Iteration 2 (D3) — naming wires the app-wide layout persistence
    // (the delegating rptPath ctor funnels through here too).
    setObjectName(QStringLiteral("StatusReportDialog"));
    resize(1100, 760);
    buildUi();
    if (!m_sources.isEmpty()) {
        const int idx = qBound(0, initialIndex, int(m_sources.size()) - 1);
        if (m_runCombo) {
            QSignalBlocker b(m_runCombo);
            m_runCombo->setCurrentIndex(idx);
        }
        loadReport(idx);
    }
}

StatusReportDialog::~StatusReportDialog() = default;

void StatusReportDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(4);

    // Run selector — one entry per loaded run's report; hidden for a
    // single source so the classic single-report layout is unchanged.
    if (m_sources.size() > 1) {
        auto *runRow = new QHBoxLayout;
        runRow->setContentsMargins(0, 0, 0, 0);
        runRow->addWidget(new QLabel(tr("Run:"), this));
        m_runCombo = new QComboBox(this);
        for (const auto &src : m_sources) {
            m_runCombo->addItem(src.label);
            m_runCombo->setItemData(m_runCombo->count() - 1, src.path,
                                    Qt::ToolTipRole);
        }
        runRow->addWidget(m_runCombo, 1);
        root->addLayout(runRow);
        connect(m_runCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, [this](int idx) { loadReport(idx); });
    }

    // Continuity-error banner (text + visibility set per report in loadReport).
    m_continuityBanner = new QLabel(this);
    m_continuityBanner->setStyleSheet(
        openswmmvis::ui::theme::bannerStyle(openswmmvis::ui::theme::Banner::Error)
        + QStringLiteral(" padding: 6px; border-radius: 3px; font-weight: bold;"));
    m_continuityBanner->setWordWrap(true);
    m_continuityBanner->hide();
    root->addWidget(m_continuityBanner);

    // ── Splitter ───────────────────────────────────────────────────────
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setObjectName(QStringLiteral("main"));
    m_splitter->setChildrenCollapsible(false);

    // Left pane: filter + section list.
    auto *leftWrap = new QWidget(m_splitter);
    auto *leftLay  = new QVBoxLayout(leftWrap);
    leftLay->setContentsMargins(0, 0, 0, 0);
    leftLay->setSpacing(2);

    m_sectionFilter = new QLineEdit(leftWrap);
    m_sectionFilter->setPlaceholderText(tr("Filter sections…"));
    m_sectionFilter->setClearButtonEnabled(true);
    leftLay->addWidget(m_sectionFilter);

    m_sectionsModel = new QStandardItemModel(this);
    m_sectionsProxy = new QSortFilterProxyModel(this);
    m_sectionsProxy->setSourceModel(m_sectionsModel);
    m_sectionsProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);

    m_sectionList = new QListView(leftWrap);
    m_sectionList->setModel(m_sectionsProxy);
    m_sectionList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_sectionList->setUniformItemSizes(true);
    leftLay->addWidget(m_sectionList, 1);

    m_splitter->addWidget(leftWrap);

    // Right pane: search row + text viewer.
    auto *rightWrap = new QWidget(m_splitter);
    auto *rightLay  = new QVBoxLayout(rightWrap);
    rightLay->setContentsMargins(0, 0, 0, 0);
    rightLay->setSpacing(2);

    auto *searchRow = new QHBoxLayout;
    searchRow->setContentsMargins(0, 0, 0, 0);
    searchRow->addWidget(new QLabel(tr("Search:"), rightWrap));
    m_searchEdit = new QLineEdit(rightWrap);
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setPlaceholderText(tr("type to search …  Enter = next, Shift+Enter = prev"));
    searchRow->addWidget(m_searchEdit, 1);

    auto *btnPrev = new QPushButton(rightWrap);
    btnPrev->setIcon(openswmmvis::ui::IconFactory::icon(QStringLiteral("FindPrev")));
    auto *btnNext = new QPushButton(rightWrap);
    btnNext->setIcon(openswmmvis::ui::IconFactory::icon(QStringLiteral("FindNext")));
    btnPrev->setMinimumWidth(28);
    btnNext->setMinimumWidth(28);
    btnPrev->setToolTip(tr("Previous match (Shift+Enter)"));
    btnNext->setToolTip(tr("Next match (Enter)"));
    searchRow->addWidget(btnPrev);
    searchRow->addWidget(btnNext);

    m_regexToggle = new QCheckBox(tr("Regex"), rightWrap);
    searchRow->addWidget(m_regexToggle);

    m_searchStatus = new QLabel(rightWrap);
    m_searchStatus->setMinimumWidth(80);
    m_searchStatus->setStyleSheet(openswmmvis::ui::theme::hintStyle());
    searchRow->addWidget(m_searchStatus);

    rightLay->addLayout(searchRow);

    m_viewer = new RptViewer(rightWrap);
    m_viewer->setReadOnly(true);
    m_viewer->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_viewer->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_viewer->setUndoRedoEnabled(false);
    rightLay->addWidget(m_viewer, 1);

    m_splitter->addWidget(rightWrap);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 4);
    root->addWidget(m_splitter, 1);

    // Close button.
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::accept);
    root->addWidget(bb);

    // Wire signals.
    connect(m_sectionFilter, &QLineEdit::textChanged,
            this, &StatusReportDialog::onSectionFilterChanged);
    connect(m_sectionList, &QListView::clicked,
            this, &StatusReportDialog::onSectionActivated);
    connect(m_sectionList, &QListView::activated,
            this, &StatusReportDialog::onSectionActivated);

    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &StatusReportDialog::onSearchChanged);
    connect(m_searchEdit, &QLineEdit::returnPressed,
            this, &StatusReportDialog::onSearchNext);
    connect(btnNext, &QPushButton::clicked,
            this, &StatusReportDialog::onSearchNext);
    connect(btnPrev, &QPushButton::clicked,
            this, &StatusReportDialog::onSearchPrev);
    connect(m_regexToggle, &QCheckBox::toggled,
            this, [this](bool){ updateMatchCounter(); });
}

void StatusReportDialog::loadReport(int sourceIndex)
{
    if (sourceIndex < 0 || sourceIndex >= m_sources.size()) return;
    m_path = m_sources[sourceIndex].path;
    setWindowTitle(tr("Report Viewer — %1").arg(m_path));

    QString err;
    const auto sections = openswmmvis::io::RptParser::parse(m_path, &err);
    QString rawErr;
    QString reportText = readReportText(m_path, &rawErr);
    if (reportText.isEmpty() && !sections.isEmpty())
        reportText = joinedSectionBodies(sections);

    // Diagnostic for "report viewer truncates sections": pinpoint which layer
    // drops content. Logs the on-disk file size/line count vs. the number of
    // sections the parser produced. If file lines >> parsed coverage the parse
    // dropped it; if the file itself is short the engine/run wrote a partial
    // .rpt. populateText() logs the char count it hands the widget.
    {
        QFile f(m_path);
        qint64 bytes = f.size();
        int fileLines = 0;
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            while (!f.atEnd()) { f.readLine(); ++fileLines; }
            f.close();
        }
        qDebug().noquote()
            << "[ReportViewer] load" << m_path
            << "bytes=" << bytes << "fileLines=" << fileLines
            << "rawChars=" << reportText.length()
            << "sectionsParsed=" << sections.size()
            << (err.isEmpty() ? QString() : QStringLiteral("parseErr=") + err)
            << (rawErr.isEmpty() ? QString() : QStringLiteral("rawErr=") + rawErr);
    }

    if (sections.isEmpty() && reportText.isEmpty()) {
        QMessageBox::warning(this, tr("Couldn't parse .rpt"),
            tr("Could not read %1:\n%2").arg(
                m_path, err.isEmpty() ? rawErr : err));
    }

    if (openswmmvis::io::RptParser::hasHighContinuityError(sections)) {
        m_continuityBanner->setText(
            tr("⚠ Continuity error above %1 % detected. "
               "Check the Runoff / Routing Continuity sections.")
                .arg(kContinuityErrorThresholdPct, 0, 'f', 0));
        m_continuityBanner->show();
    } else {
        m_continuityBanner->hide();
    }

    populateText(sections, reportText);
}

void StatusReportDialog::populateText(
    const QVector<openswmmvis::io::RptSection> &sections,
    const QString &reportText)
{
    m_sectionsModel->removeRows(0, m_sectionsModel->rowCount());

    m_sectionAnchors.clear();
    m_sectionAnchors.reserve(sections.size() + 1);

    // Bookmarks are navigation only — the viewer always holds the whole file.
    auto addBookmark = [this, &reportText](const QString &label, int pos) {
        m_sectionAnchors.push_back(qBound(0, pos, reportText.length()));
        auto *item = new QStandardItem(label);
        item->setEditable(false);
        item->setToolTip(label);
        m_sectionsModel->appendRow(item);
    };

    int anchor = 0;
    for (int i = 0; i < sections.size(); ++i) {
        const openswmmvis::io::RptSection &s = sections.at(i);
        const int start = anchor;
        anchor += s.body.length();

        if (!s.title.isEmpty()) {
            addBookmark(s.title, start);
            continue;
        }

        // The block above the first '****' rule — engine banner, project
        // title/notes, then the WARNING/ERROR list — carries no star-delimited
        // title, so the parser hands it back untitled.  On a real report that
        // is ~15 % of the file, and labelling it "(untitled)" made it look as
        // though the report began at the first summary table.  Name it, and
        // add a second bookmark straight to the first warning.
        if (i != 0) {
            addBookmark(tr("(untitled)"), start);
            continue;
        }
        addBookmark(tr("Report Header & Notes"), start);

        // [ \t]*, not \s* — Qt's \s matches '\n', which would let a match start
        // on a blank line several lines above the warning it anchors on.
        static const QRegularExpression noticeRx(
            QStringLiteral("^[ \\t]*(?:WARNING|ERROR)\\b"),
            QRegularExpression::MultilineOption);
        int first = -1, count = 0;
        auto it = noticeRx.globalMatch(s.body);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            if (first < 0) first = int(m.capturedStart());
            ++count;
        }
        if (first >= 0)
            addBookmark(tr("Warnings & Errors (%1)").arg(count), start + first);
    }

    if (sections.isEmpty() && !reportText.isEmpty())
        addBookmark(tr("(full report)"), 0);

    m_viewer->setPlainText(reportText);

    qDebug().noquote()
        << "[ReportViewer] populated chars=" << reportText.length()
        << "sections=" << sections.size()
        << "viewerBlocks=" << m_viewer->document()->blockCount()
        << "viewerChars=" << m_viewer->document()->characterCount();

    // Highlighter installed after text so the initial highlight applies.
    // Created once — it stays attached to the (persistent) document across
    // run switches.
    if (!m_highlighter)
        m_highlighter = new RptSyntaxHighlighter(m_viewer->document());

    // Move cursor to top.
    QTextCursor c = m_viewer->textCursor();
    c.movePosition(QTextCursor::Start);
    m_viewer->setTextCursor(c);
}

void StatusReportDialog::onSectionFilterChanged(const QString &text)
{
    if (!m_sectionsProxy)
        return;

    const QStringList tokens =
        text.split(QRegularExpression(QStringLiteral("[\\s\\-]+")),
                   Qt::SkipEmptyParts);
    if (tokens.isEmpty()) {
        m_sectionsProxy->setFilterRegularExpression(QRegularExpression());
        return;
    }

    QString pattern;
    for (const QString &token : tokens) {
        pattern += QStringLiteral("(?=.*");
        pattern += QRegularExpression::escape(token);
        pattern += QLatin1Char(')');
    }
    pattern += QStringLiteral(".*");
    m_sectionsProxy->setFilterRegularExpression(
        QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption));
}

void StatusReportDialog::onSectionActivated(const QModelIndex &proxyIdx)
{
    if (!proxyIdx.isValid() || !m_sectionsProxy) return;
    const QModelIndex src = m_sectionsProxy->mapToSource(proxyIdx);
    const int row = src.row();
    if (row < 0 || row >= m_sectionAnchors.size()) return;
    const int pos = m_sectionAnchors[row];
    QTextCursor c = m_viewer->textCursor();
    c.setPosition(pos);
    m_viewer->setTextCursor(c);
    // Bookmark semantics: put the section title at the TOP of the viewport.
    // ensureCursorVisible() alone scrolls minimally, so a downward jump lands
    // the title on the BOTTOM edge — the section body sits below the fold,
    // which reads as "the section has headers but no content".
    //
    // QPlainTextEdit's vertical scrollbar is LINE-based (value == first
    // visible block), so scroll by setting it to the anchor's block number —
    // pixel math (cursorRect().top()) would overshoot by a pixel:line factor.
    // Clamped by the scrollbar near EOF, deterministic on the first click.
    if (QScrollBar *vs = m_viewer->verticalScrollBar())
        vs->setValue(m_viewer->document()->findBlock(pos).blockNumber());
}

void StatusReportDialog::onSearchChanged()
{
    // Incremental search from the current cursor position.  We don't
    // advance — just update the counter and re-run-find starting at the
    // beginning so the first match is highlighted live.
    QTextCursor c = m_viewer->textCursor();
    c.movePosition(QTextCursor::Start);
    m_viewer->setTextCursor(c);
    runFind(false);
    updateMatchCounter();
}

void StatusReportDialog::onSearchNext() { runFind(false); }
void StatusReportDialog::onSearchPrev() { runFind(true); }

void StatusReportDialog::runFind(bool backwards)
{
    const QString needle = m_searchEdit->text();
    if (needle.isEmpty()) {
        m_searchStatus->setText(QString());
        return;
    }

    QTextDocument::FindFlags flags;
    if (backwards) flags |= QTextDocument::FindBackward;

    bool found = false;
    if (m_regexToggle->isChecked()) {
        QRegularExpression rx(needle, QRegularExpression::CaseInsensitiveOption);
        if (!rx.isValid()) {
            m_searchStatus->setText(
                QStringLiteral("<span style='color:%1;'>%2</span>")
                    .arg(ThemeManager::instance()->colors().error.name(QColor::HexRgb))
                    .arg(tr("invalid regex")));
            return;
        }
        found = m_viewer->find(rx, flags);
        if (!found) {
            // Wrap around.
            QTextCursor c = m_viewer->textCursor();
            c.movePosition(backwards ? QTextCursor::End : QTextCursor::Start);
            m_viewer->setTextCursor(c);
            found = m_viewer->find(rx, flags);
        }
    } else {
        found = m_viewer->find(needle, flags);
        if (!found) {
            QTextCursor c = m_viewer->textCursor();
            c.movePosition(backwards ? QTextCursor::End : QTextCursor::Start);
            m_viewer->setTextCursor(c);
            found = m_viewer->find(needle, flags);
        }
    }

    if (!found)
        m_searchStatus->setText(tr("no match"));
    else
        updateMatchCounter();
}

void StatusReportDialog::updateMatchCounter()
{
    const QString needle = m_searchEdit->text();
    if (needle.isEmpty()) {
        m_searchStatus->setText(QString());
        return;
    }
    const QString body = m_viewer->toPlainText();
    int count = 0;
    if (m_regexToggle->isChecked()) {
        QRegularExpression rx(needle, QRegularExpression::CaseInsensitiveOption);
        if (!rx.isValid()) {
            m_searchStatus->setText(
                QStringLiteral("<span style='color:%1;'>%2</span>")
                    .arg(ThemeManager::instance()->colors().error.name(QColor::HexRgb))
                    .arg(tr("invalid regex")));
            return;
        }
        auto it = rx.globalMatch(body);
        while (it.hasNext()) { it.next(); ++count; }
    } else {
        int from = 0;
        while ((from = body.indexOf(needle, from, Qt::CaseInsensitive)) >= 0) {
            ++count;
            from += needle.length();
        }
    }
    m_searchStatus->setText(tr("%1 match%2").arg(count).arg(count == 1 ? "" : "es"));
}

} // namespace openswmmvis::ui
