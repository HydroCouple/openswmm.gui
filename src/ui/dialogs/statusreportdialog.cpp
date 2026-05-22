/*!
 * \file   statusreportdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/statusreportdialog.h"

#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTextEdit>
#include <QVBoxLayout>

namespace openswmmvis::ui {

StatusReportDialog::StatusReportDialog(const QString &rptPath, QWidget *parent)
    : QDialog(parent), m_path(rptPath)
{
    setWindowTitle(tr("Status Report — %1").arg(rptPath));
    resize(900, 700);

    QString err;
    const auto sections = openswmmvis::io::RptParser::parse(m_path, &err);
    if (sections.isEmpty()) {
        QMessageBox::warning(this, tr("Couldn't parse .rpt"),
            tr("Could not read %1:\n%2").arg(m_path, err));
    }
    buildUi(sections);
}

StatusReportDialog::~StatusReportDialog() = default;

void StatusReportDialog::buildUi(const QVector<openswmmvis::io::RptSection> &sections)
{
    auto *root = new QVBoxLayout(this);

    // Continuity warning banner.
    m_continuityBanner = new QLabel(this);
    m_continuityBanner->setStyleSheet(
        QStringLiteral("QLabel { background-color: #ffe7e3; color: #8a1f00; "
                       "padding: 6px; border-radius: 3px; }"));
    m_continuityBanner->setWordWrap(true);
    if (openswmmvis::io::RptParser::hasHighContinuityError(sections)) {
        m_continuityBanner->setText(
            tr("⚠ Continuity error above 10 % detected. Check the "
               "Runoff / Routing Continuity sections."));
    } else {
        m_continuityBanner->hide();
    }
    root->addWidget(m_continuityBanner);

    // Find toolbar.
    auto *findRow = new QHBoxLayout;
    findRow->addWidget(new QLabel(tr("Find:"), this));
    m_findEdit = new QLineEdit(this);
    findRow->addWidget(m_findEdit, 1);
    auto *findNext = new QPushButton(tr("Next"), this);
    findRow->addWidget(findNext);
    root->addLayout(findRow);
    connect(findNext, &QPushButton::clicked, this, &StatusReportDialog::onFindNext);
    connect(m_findEdit, &QLineEdit::returnPressed, this, &StatusReportDialog::onFindNext);

    // Tabs.
    m_tabs = new QTabWidget(this);
    const QFont monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    for (const auto &s : sections) {
        if (s.title.isEmpty() && s.body.trimmed().isEmpty()) continue;
        auto *edit = new QTextEdit;
        edit->setReadOnly(true);
        edit->setFont(monoFont);
        edit->setLineWrapMode(QTextEdit::NoWrap);
        edit->setPlainText(s.body);
        QString label = s.title.isEmpty() ? tr("(untitled)") : s.title;
        if (label.length() > 28) label = label.left(25) + QStringLiteral("…");
        m_tabs->addTab(edit, label);
        m_tabs->setTabToolTip(m_tabs->count() - 1, s.title);
    }
    root->addWidget(m_tabs, 1);

    // Close button.
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::accept);
    root->addWidget(bb);
}

void StatusReportDialog::onFindNext()
{
    const QString needle = m_findEdit->text();
    if (needle.isEmpty() || !m_tabs) return;
    auto *edit = qobject_cast<QTextEdit*>(m_tabs->currentWidget());
    if (!edit) return;
    if (!edit->find(needle)) {
        // Wrap to start.
        QTextCursor c = edit->textCursor();
        c.movePosition(QTextCursor::Start);
        edit->setTextCursor(c);
        edit->find(needle);
    }
}

} // namespace openswmmvis::ui
