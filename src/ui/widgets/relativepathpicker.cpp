/*!
 * \file   relativepathpicker.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "ui/widgets/relativepathpicker.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QToolButton>

namespace openswmmvis::ui {

namespace {

// Returns the canonical form of `dir` (no trailing slash, native form).
QString canonAnchor(QString dir) {
    if (dir.isEmpty()) return dir;
    while (dir.size() > 1 && (dir.endsWith('/') || dir.endsWith('\\')))
        dir.chop(1);
    return QDir::cleanPath(dir);
}

// Resolve `p` against `anchor`. If `p` is absolute it's returned cleaned;
// otherwise `anchor + "/" + p` is built when `anchor` is non-empty, else
// `p` is returned verbatim.
QString resolveAgainst(const QString &p, const QString &anchor) {
    if (p.isEmpty()) return p;
    QFileInfo fi(p);
    if (fi.isAbsolute()) return QDir::cleanPath(p);
    if (anchor.isEmpty()) return p;
    return QDir::cleanPath(anchor + QLatin1Char('/') + p);
}

} // namespace

RelativePathPicker::RelativePathPicker(QWidget *parent)
    : QWidget(parent)
    , m_edit(new QLineEdit(this))
    , m_browse(new QToolButton(this))
{
    m_browse->setText(QStringLiteral("…"));
    m_browse->setToolTip(tr("Browse for file"));
    m_browse->setAutoRaise(true);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->addWidget(m_edit, /*stretch=*/1);
    layout->addWidget(m_browse, /*stretch=*/0);

    connect(m_browse, &QToolButton::clicked,
            this, &RelativePathPicker::onBrowseClicked);
    connect(m_edit, &QLineEdit::editingFinished,
            this, &RelativePathPicker::onLineEditFinished);
}

void RelativePathPicker::setProjectAnchor(const QString &dir)
{
    const QString canon = canonAnchor(dir);
    if (canon == m_anchor) return;
    m_anchor = canon;
    refreshDisplay();
}

QString RelativePathPicker::displayPath() const
{
    return m_edit ? m_edit->text() : QString{};
}

bool RelativePathPicker::isDisplayedRelatively() const
{
    if (m_absolute.isEmpty()) return false;
    const QString shown = displayPath();
    if (shown.isEmpty()) return false;
    return QFileInfo(shown).isRelative();
}

void RelativePathPicker::setPath(const QString &p)
{
    const QString next = resolveAgainst(p, m_anchor);
    if (next == m_absolute) return;
    m_absolute = next;
    refreshDisplay();
    emit pathChanged(m_absolute);
}

void RelativePathPicker::refreshDisplay()
{
    if (!m_edit) return;
    if (m_absolute.isEmpty()) {
        m_edit->setText(QString{});
        m_edit->setToolTip(tr("No file selected"));
        return;
    }
    QString shown;
    if (!m_anchor.isEmpty()) {
        QDir d(m_anchor);
        const QString rel = d.relativeFilePath(m_absolute);
        // QDir::relativeFilePath returns the absolute form unchanged when
        // the path is on a different drive / cannot be expressed relative.
        const QFileInfo fi(rel);
        shown = fi.isRelative() ? rel : m_absolute;
    } else {
        shown = m_absolute;
    }
    m_edit->setText(shown);
    m_edit->setToolTip(tr("Resolved: %1").arg(m_absolute));
}

void RelativePathPicker::onBrowseClicked()
{
    QString start = !m_absolute.isEmpty() ? QFileInfo(m_absolute).absolutePath()
                                          : (m_anchor.isEmpty()
                                              ? QDir::homePath() : m_anchor);
    QString picked;
    if (m_mode == QFileDialog::AcceptSave) {
        picked = QFileDialog::getSaveFileName(this, m_caption, start, m_filter);
    } else {
        picked = QFileDialog::getOpenFileName(this, m_caption, start, m_filter);
    }
    if (picked.isEmpty()) return;
    setPath(picked);
}

void RelativePathPicker::onLineEditFinished()
{
    // User typed a path into the line edit. Resolve it against the anchor
    // (so a relative entry like "data/rain.dat" lands on the right
    // absolute), and refresh the display.
    const QString typed = m_edit->text();
    setPath(typed);
}

} // namespace openswmmvis::ui
