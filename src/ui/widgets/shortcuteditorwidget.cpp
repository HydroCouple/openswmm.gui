#include "ui/widgets/shortcuteditorwidget.h"

#include "ui/actioncatalog.h"
#include "ui/actionregistry.h"
#include "ui/theme/themehelpers.h"

#include <QAction>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace openswmmvis::ui {

namespace {

enum Column { ColCommand = 0, ColShortcut, ColDefault };
constexpr int kIdRole = Qt::UserRole + 1;

/*! Sequences the platform reserves for window/system management —
 *  rebinding over them works but is warned against (Qt::CTRL is Cmd on
 *  macOS, where these are sacred; on Win/Linux they're merely common). */
bool isReservedSequence(const QKeySequence &seq)
{
    static const QList<QKeySequence> kReserved = {
        QKeySequence(QStringLiteral("Ctrl+Q")), QKeySequence(QStringLiteral("Ctrl+W")),
        QKeySequence(QStringLiteral("Ctrl+H")), QKeySequence(QStringLiteral("Ctrl+M")),
        QKeySequence(QStringLiteral("Ctrl+Tab")),
        QKeySequence(QStringLiteral("Ctrl+Space")),
    };
    return kReserved.contains(seq);
}

QString cleanText(const QAction *action)
{
    QString text = action->text();
    text.remove(QLatin1Char('&'));
    return text;
}

}   // namespace

ShortcutEditorWidget::ShortcutEditorWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);

    mFilter = new QLineEdit(this);
    mFilter->setObjectName(QStringLiteral("shortcutEditorFilter"));
    mFilter->setPlaceholderText(tr("Filter commands…"));
    mFilter->setClearButtonEnabled(true);
    mFilter->setAccessibleName(tr("Filter commands"));
    layout->addWidget(mFilter);

    mTree = new QTreeWidget(this);
    mTree->setObjectName(QStringLiteral("shortcutEditorTree"));
    mTree->setColumnCount(3);
    mTree->setHeaderLabels({tr("Command"), tr("Shortcut"), tr("Default")});
    mTree->header()->setSectionResizeMode(ColCommand, QHeaderView::Stretch);
    mTree->setRootIsDecorated(true);
    mTree->setUniformRowHeights(true);
    mTree->setAccessibleName(tr("Command shortcuts"));
    layout->addWidget(mTree, 1);

    auto *editRow = new QHBoxLayout();
    layout->addLayout(editRow);
    auto *editLabel = new QLabel(tr("&Shortcut:"), this);
    mSequenceEdit = new QKeySequenceEdit(this);
    mSequenceEdit->setObjectName(QStringLiteral("shortcutEditorSequence"));
    editLabel->setBuddy(mSequenceEdit);
    editRow->addWidget(editLabel);
    editRow->addWidget(mSequenceEdit, 1);
    mAssign = new QPushButton(tr("&Assign"), this);
    mClear = new QPushButton(tr("C&lear"), this);
    mReset = new QPushButton(tr("&Reset"), this);
    editRow->addWidget(mAssign);
    editRow->addWidget(mClear);
    editRow->addWidget(mReset);

    auto *bottomRow = new QHBoxLayout();
    layout->addLayout(bottomRow);
    mConflict = new QLabel(this);
    mConflict->setObjectName(QStringLiteral("shortcutEditorConflict"));
    mConflict->setWordWrap(true);
    bottomRow->addWidget(mConflict, 1);
    mResetAll = new QPushButton(tr("Reset A&ll to Defaults"), this);
    bottomRow->addWidget(mResetAll);

    connect(mFilter, &QLineEdit::textChanged,
            this, &ShortcutEditorWidget::onFilterChanged);
    connect(mTree, &QTreeWidget::currentItemChanged,
            this, [this](QTreeWidgetItem *, QTreeWidgetItem *) {
                onSelectionChanged();
            });
    connect(mSequenceEdit, &QKeySequenceEdit::keySequenceChanged,
            this, [this](const QKeySequence &) { onSequenceChanged(); });
    connect(mAssign,  &QPushButton::clicked, this, &ShortcutEditorWidget::onAssign);
    connect(mClear,   &QPushButton::clicked, this, &ShortcutEditorWidget::onClear);
    connect(mReset,   &QPushButton::clicked, this, &ShortcutEditorWidget::onReset);
    connect(mResetAll, &QPushButton::clicked, this, &ShortcutEditorWidget::onResetAll);

    // External changes (another editor instance, palette-driven resets)
    // refresh the visible rows.
    connect(ActionRegistry::instance(), &ActionRegistry::shortcutChanged,
            this, [this](const QString &) { reload(); });

    reload();
    onSelectionChanged();
}

void ShortcutEditorWidget::reload()
{
    const QString selected = selectedCommandId();
    mTree->clear();

    auto *registry = ActionRegistry::instance();
    QHash<QString, QTreeWidgetItem *> categoryItems;
    const QStringList ids = registry->registeredIds();   // catalog order
    for (const QString &id : ids) {
        QAction *action = registry->action(id);
        const ActionCatalogEntry *entry = registry->catalogEntry(id);
        if (!action || !entry || action->text().isEmpty())
            continue;
        const QString category = QString::fromLatin1(entry->category);
        QTreeWidgetItem *parent = categoryItems.value(category, nullptr);
        if (!parent) {
            parent = new QTreeWidgetItem(mTree, {category});
            parent->setFlags(Qt::ItemIsEnabled);
            parent->setExpanded(true);
            categoryItems.insert(category, parent);
        }
        auto *item = new QTreeWidgetItem(parent);
        item->setData(0, kIdRole, id);
        item->setText(ColCommand, cleanText(action));
        refreshItem(item);
    }
    mTree->resizeColumnToContents(ColShortcut);
    if (!selected.isEmpty())
        selectCommand(selected);
    if (!mFilter->text().isEmpty())
        onFilterChanged(mFilter->text());
}

void ShortcutEditorWidget::refreshItem(QTreeWidgetItem *item)
{
    auto *registry = ActionRegistry::instance();
    const QString id = item->data(0, kIdRole).toString();
    const auto effective = registry->effectiveShortcuts(id);
    const auto defaults  = registry->defaultShortcuts(id);
    const QString effectiveText = effective.isEmpty()
        ? QString()
        : effective.first().toString(QKeySequence::NativeText);
    const QString defaultText = defaults.isEmpty()
        ? QString()
        : defaults.first().toString(QKeySequence::NativeText);
    item->setText(ColShortcut, effectiveText);
    item->setText(ColDefault, defaultText);
    QFont font = item->font(ColShortcut);
    font.setBold(registry->hasUserShortcut(id));
    item->setFont(ColShortcut, font);
}

QString ShortcutEditorWidget::selectedCommandId() const
{
    const QTreeWidgetItem *item = mTree->currentItem();
    return item ? item->data(0, kIdRole).toString() : QString();
}

void ShortcutEditorWidget::selectCommand(const QString &id)
{
    for (int c = 0; c < mTree->topLevelItemCount(); ++c) {
        QTreeWidgetItem *category = mTree->topLevelItem(c);
        for (int i = 0; i < category->childCount(); ++i) {
            QTreeWidgetItem *item = category->child(i);
            if (item->data(0, kIdRole).toString() == id) {
                category->setExpanded(true);
                mTree->setCurrentItem(item);
                return;
            }
        }
    }
}

void ShortcutEditorWidget::onFilterChanged(const QString &text)
{
    const QString needle = text.trimmed();
    for (int c = 0; c < mTree->topLevelItemCount(); ++c) {
        QTreeWidgetItem *category = mTree->topLevelItem(c);
        int visibleChildren = 0;
        for (int i = 0; i < category->childCount(); ++i) {
            QTreeWidgetItem *item = category->child(i);
            const bool match = needle.isEmpty()
                || item->text(ColCommand).contains(needle, Qt::CaseInsensitive)
                || category->text(ColCommand).contains(needle, Qt::CaseInsensitive);
            item->setHidden(!match);
            if (match)
                ++visibleChildren;
        }
        category->setHidden(visibleChildren == 0);
    }
}

void ShortcutEditorWidget::onSelectionChanged()
{
    const QString id = selectedCommandId();
    const bool hasSelection = !id.isEmpty();
    mSequenceEdit->setEnabled(hasSelection);
    mAssign->setEnabled(false);
    mClear->setEnabled(hasSelection);
    mReset->setEnabled(hasSelection
                       && ActionRegistry::instance()->hasUserShortcut(id));
    mConflict->clear();
    if (hasSelection) {
        const auto effective = ActionRegistry::instance()->effectiveShortcuts(id);
        QSignalBlocker block(mSequenceEdit);
        mSequenceEdit->setKeySequence(
            effective.isEmpty() ? QKeySequence() : effective.first());
    } else {
        QSignalBlocker block(mSequenceEdit);
        mSequenceEdit->clear();
    }
}

void ShortcutEditorWidget::onSequenceChanged()
{
    const QString id = selectedCommandId();
    if (id.isEmpty())
        return;
    const QKeySequence seq = mSequenceEdit->keySequence();
    mConflict->clear();
    mConflict->setStyleSheet(QString());

    if (seq.isEmpty()) {
        mAssign->setEnabled(false);
        return;
    }
    const QString holder =
        ActionRegistry::instance()->conflictingActionId(seq, id);
    if (!holder.isEmpty()) {
        QAction *other = ActionRegistry::instance()->action(holder);
        mConflict->setStyleSheet(theme::errorTextStyle());
        mConflict->setText(tr("Conflicts with \"%1\" — clear that binding first.")
                               .arg(other ? cleanText(other) : holder));
        mAssign->setEnabled(false);
        return;
    }
    if (isReservedSequence(seq)) {
        mConflict->setStyleSheet(QString());
        mConflict->setText(tr("Note: %1 is a system shortcut on some platforms.")
                               .arg(seq.toString(QKeySequence::NativeText)));
    }
    mAssign->setEnabled(true);
}

void ShortcutEditorWidget::onAssign()
{
    const QString id = selectedCommandId();
    if (id.isEmpty())
        return;
    ActionRegistry::instance()->setUserShortcut(id, mSequenceEdit->keySequence());
    // shortcutChanged → reload() refreshes rows; restore selection focus.
    selectCommand(id);
}

void ShortcutEditorWidget::onClear()
{
    const QString id = selectedCommandId();
    if (id.isEmpty())
        return;
    ActionRegistry::instance()->setUserShortcut(id, QKeySequence());
    selectCommand(id);
}

void ShortcutEditorWidget::onReset()
{
    const QString id = selectedCommandId();
    if (id.isEmpty())
        return;
    ActionRegistry::instance()->resetShortcut(id);
    selectCommand(id);
}

void ShortcutEditorWidget::onResetAll()
{
    auto *registry = ActionRegistry::instance();
    const QStringList ids = registry->registeredIds();
    for (const QString &id : ids) {
        if (registry->hasUserShortcut(id))
            registry->resetShortcut(id);
    }
}

}   // namespace openswmmvis::ui
