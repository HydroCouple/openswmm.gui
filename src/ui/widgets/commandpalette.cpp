#include "ui/widgets/commandpalette.h"

#include "ui/dialogs/dialoglayoutwatcher.h"
#include "ui/widgets/commandpalettemodel.h"

#include <QAction>
#include <QEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListView>
#include <QMainWindow>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

namespace openswmmvis::ui {

namespace {

/*! Icon + title left, category chip + shortcut hint right; grayed when
 *  the action is disabled. */
class PaletteDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        if (!index.data(CommandPaletteModel::EnabledRole).toBool())
            opt.state &= ~QStyle::State_Enabled;

        // Base row (selection background, icon, title).
        opt.text.clear();
        opt.widget->style()->drawControl(QStyle::CE_ItemViewItem, &opt,
                                         painter, opt.widget);

        const QRect r = option.rect.adjusted(6, 0, -8, 0);
        const bool selected = option.state & QStyle::State_Selected;
        const QPalette &pal = option.palette;
        const QPalette::ColorGroup group =
            index.data(CommandPaletteModel::EnabledRole).toBool()
                ? QPalette::Active
                : QPalette::Disabled;
        const QColor fg = pal.color(group, selected ? QPalette::HighlightedText
                                                    : QPalette::Text);
        const QColor hint = pal.color(group, selected ? QPalette::HighlightedText
                                                      : QPalette::PlaceholderText);

        painter->save();
        const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
        int x = r.left();
        if (!icon.isNull()) {
            const QSize is(18, 18);
            icon.paint(painter, QRect(QPoint(x, r.center().y() - is.height() / 2), is),
                       Qt::AlignCenter,
                       group == QPalette::Disabled ? QIcon::Disabled : QIcon::Normal);
        }
        x += 26;

        const QString shortcut = index.data(CommandPaletteModel::ShortcutRole).toString();
        const QString category = index.data(CommandPaletteModel::CategoryRole).toString();
        QFontMetrics fm(option.font);

        int rightEdge = r.right();
        if (!shortcut.isEmpty()) {
            const int w = fm.horizontalAdvance(shortcut);
            painter->setPen(hint);
            painter->drawText(QRect(rightEdge - w, r.top(), w, r.height()),
                              Qt::AlignVCenter | Qt::AlignRight, shortcut);
            rightEdge -= w + 14;
        }
        if (!category.isEmpty()) {
            const int w = fm.horizontalAdvance(category);
            painter->setPen(hint);
            painter->drawText(QRect(rightEdge - w, r.top(), w, r.height()),
                              Qt::AlignVCenter | Qt::AlignRight, category);
            rightEdge -= w + 14;
        }

        painter->setPen(fg);
        const QString title = fm.elidedText(index.data(Qt::DisplayRole).toString(),
                                            Qt::ElideRight, rightEdge - x);
        painter->drawText(QRect(x, r.top(), rightEdge - x, r.height()),
                          Qt::AlignVCenter | Qt::AlignLeft, title);
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override
    {
        QSize s = QStyledItemDelegate::sizeHint(option, index);
        s.setHeight(qMax(s.height(), 26));
        return s;
    }
};

}   // namespace

CommandPalette::CommandPalette(QMainWindow *parent)
    : QDialog(parent, Qt::FramelessWindowHint | Qt::Dialog)
{
    setObjectName(QStringLiteral("commandPalette"));
    // Deliberately fixed-size: opt out of automatic layout persistence.
    setProperty(kNoLayoutPersistenceProp, true);
    setModal(false);
    setFixedWidth(560);
    setAccessibleName(tr("Command palette"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    mFilter = new QLineEdit(this);
    mFilter->setObjectName(QStringLiteral("commandPaletteFilter"));
    mFilter->setPlaceholderText(tr("Type a command…"));
    mFilter->setClearButtonEnabled(true);
    mFilter->setAccessibleName(tr("Command filter"));
    layout->addWidget(mFilter);

    mModel = new CommandPaletteModel(this);
    mList = new QListView(this);
    mList->setObjectName(QStringLiteral("commandPaletteList"));
    mList->setModel(mModel);
    mList->setItemDelegate(new PaletteDelegate(mList));
    mList->setUniformItemSizes(true);
    mList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mList->setSelectionMode(QAbstractItemView::SingleSelection);
    mList->setFocusPolicy(Qt::NoFocus);   // the filter keeps focus; keys forward
    mList->setFixedHeight(330);
    mList->setAccessibleName(tr("Command list"));
    layout->addWidget(mList);

    mFilter->installEventFilter(this);
    connect(mFilter, &QLineEdit::textChanged, this, [this](const QString &text) {
        mModel->setFilterPattern(text);
        if (mModel->rowCount() > 0)
            mList->setCurrentIndex(mModel->index(0, 0));
    });
    connect(mList, &QListView::activated, this,
            [this](const QModelIndex &) { triggerCurrent(); });
}

void CommandPalette::popup()
{
    mModel->reload();
    mFilter->clear();
    mModel->setFilterPattern(QString());
    if (mModel->rowCount() > 0)
        mList->setCurrentIndex(mModel->index(0, 0));

    if (auto *parent = parentWidget()) {
        const QPoint center = parent->mapToGlobal(
            QPoint(parent->width() / 2, parent->height() / 4));
        move(center.x() - width() / 2, center.y());
    }
    show();
    raise();
    activateWindow();
    mFilter->setFocus();
}

bool CommandPalette::event(QEvent *event)
{
    if (event->type() == QEvent::WindowDeactivate)
        hide();
    return QDialog::event(event);
}

bool CommandPalette::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == mFilter && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        switch (keyEvent->key()) {
        case Qt::Key_Down:
            moveSelection(+1);
            return true;
        case Qt::Key_Up:
            moveSelection(-1);
            return true;
        case Qt::Key_PageDown:
            moveSelection(+8);
            return true;
        case Qt::Key_PageUp:
            moveSelection(-8);
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            triggerCurrent();
            return true;
        case Qt::Key_Escape:
            hide();
            return true;
        default:
            break;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void CommandPalette::triggerCurrent()
{
    const QModelIndex index = mList->currentIndex();
    QAction *action = mModel->actionAt(index);
    if (!action || !action->isEnabled())
        return;
    hide();
    action->trigger();
}

void CommandPalette::moveSelection(int delta)
{
    const int count = mModel->rowCount();
    if (count == 0)
        return;
    int row = mList->currentIndex().isValid() ? mList->currentIndex().row() : 0;
    row = qBound(0, row + delta, count - 1);
    mList->setCurrentIndex(mModel->index(row, 0));
}

}   // namespace openswmmvis::ui
