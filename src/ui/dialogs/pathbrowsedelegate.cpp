/*!
 * \file   pathbrowsedelegate.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/pathbrowsedelegate.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QToolButton>

namespace {
constexpr const char *kPathEditObjectName = "pathBrowseDelegateLineEdit";
} // namespace

PathBrowseDelegate::PathBrowseDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

PathBrowseDelegate::PathBrowseDelegate(Mode mode,
                                       QString title,
                                       QString filter,
                                       QString placeholder,
                                       QObject *parent)
    : QStyledItemDelegate(parent),
      m_mode(mode),
      m_title(std::move(title)),
      m_filter(std::move(filter)),
      m_placeholder(std::move(placeholder))
{
}

QWidget *PathBrowseDelegate::createEditor(QWidget *parent,
                                          const QStyleOptionViewItem & /*opt*/,
                                          const QModelIndex & /*idx*/) const
{
    auto *host = new QWidget(parent);
    host->setFocusPolicy(Qt::StrongFocus);

    auto *lay = new QHBoxLayout(host);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(2);

    auto *edit = new QLineEdit(host);
    edit->setObjectName(QString::fromLatin1(kPathEditObjectName));
    if (!m_placeholder.isEmpty()) edit->setPlaceholderText(m_placeholder);
    edit->setFrame(false);

    auto *btn = new QToolButton(host);
    btn->setText(QStringLiteral("…"));
    btn->setToolTip(m_title.isEmpty() ? tr("Browse for file") : m_title);

    lay->addWidget(edit, 1);
    lay->addWidget(btn, 0);
    host->setFocusProxy(edit);

    // Browse → open the configured file picker and push the result through
    // the delegate's commitData channel.
    QObject::connect(btn, &QToolButton::clicked, host,
        [this, host, edit] {
            const QString cur = edit->text().trimmed();
            const QString title = m_title.isEmpty()
                ? tr("Choose file") : m_title;
            const QString filter = m_filter.isEmpty()
                ? tr("All Files (*)") : m_filter;
            const QString p = (m_mode == SaveFile)
                ? QFileDialog::getSaveFileName(host, title, cur, filter)
                : QFileDialog::getOpenFileName(host, title, cur, filter);
            if (p.isEmpty()) return;
            edit->setText(p);
            auto *self = const_cast<PathBrowseDelegate *>(this);
            emit self->commitData(host);
        });

    // Typed edits commit when the user tabs away or presses Enter.
    QObject::connect(edit, &QLineEdit::editingFinished, host,
        [this, host] {
            auto *self = const_cast<PathBrowseDelegate *>(this);
            emit self->commitData(host);
        });

    return host;
}

void PathBrowseDelegate::setEditorData(QWidget *editor,
                                       const QModelIndex &idx) const
{
    auto *edit = editor
        ? editor->findChild<QLineEdit *>(QString::fromLatin1(kPathEditObjectName))
        : nullptr;
    if (!edit) return;
    QSignalBlocker block(edit);
    edit->setText(idx.data(Qt::EditRole).toString());
}

void PathBrowseDelegate::setModelData(QWidget *editor,
                                      QAbstractItemModel *model,
                                      const QModelIndex &idx) const
{
    auto *edit = editor
        ? editor->findChild<QLineEdit *>(QString::fromLatin1(kPathEditObjectName))
        : nullptr;
    if (!edit || !model) return;
    model->setData(idx, edit->text(), Qt::EditRole);
}
