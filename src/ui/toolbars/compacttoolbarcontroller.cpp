#include "ui/toolbars/compacttoolbarcontroller.h"

#include <QMainWindow>
#include <QSettings>
#include <QTabBar>
#include <QToolBar>
#include <QWidget>

namespace {
const QString kSettingsGroup = QStringLiteral("SWMMVis::MainWindow");
const QString kLastTabKey    = QStringLiteral("CompactToolbarTab");
}

namespace openswmmvis::ui {

CompactToolbarController::CompactToolbarController(QMainWindow *window)
    : QObject(window),
      mWindow(window)
{
    mStrip = new QToolBar(tr("Toolbar Tabs"), window);
    mStrip->setObjectName(QStringLiteral("toolBarTabStrip"));
    mStrip->setMovable(false);
    mStrip->setFloatable(false);

    mTabBar = new QTabBar(mStrip);
    mTabBar->setObjectName(QStringLiteral("compactToolbarTabBar"));
    mTabBar->setDocumentMode(true);
    mTabBar->setDrawBase(false);
    mTabBar->setExpanding(false);
    mTabBar->setFocusPolicy(Qt::TabFocus);
    mTabBar->setAccessibleName(tr("Toolbar tabs"));
    mStrip->addWidget(mTabBar);

    // Expanding spacer keeps future corner widgets (command palette
    // launcher) right-aligned.
    auto *spacer = new QWidget(mStrip);
    spacer->setObjectName(QStringLiteral("compactToolbarSpacer"));
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    mStrip->addWidget(spacer);

    connect(mTabBar, &QTabBar::currentChanged, this, [this](int index) {
        if (!mFinalized || index < 0 || index >= mTabs.size())
            return;
        applyVisibility();
        QSettings settings;
        settings.beginGroup(kSettingsGroup);
        settings.setValue(kLastTabKey, mTabs[index].id);
        settings.endGroup();
        emit currentTabChanged(mTabs[index].id);
    });
}

void CompactToolbarController::addTab(const QString &id, const QString &title,
                                      const QList<QToolBar *> &toolbars,
                                      bool contextual)
{
    Q_ASSERT(!mFinalized);
    Tab tab;
    tab.id = id;
    tab.title = title;
    tab.bars = toolbars;
    tab.contextual = contextual;
    mTabs.append(tab);
    const int index = mTabBar->addTab(title);
    mTabBar->setTabData(index, id);
}

void CompactToolbarController::finalize()
{
    if (mFinalized || !mWindow)
        return;

    // Row 1: the strip; row 2: every tab's toolbars after one break.
    // Re-adding an already-added toolbar (the .ui-authored and subclass
    // bars) moves it into place; setMovable(false) locks the layout.
    mWindow->addToolBar(Qt::TopToolBarArea, mStrip);
    mWindow->addToolBarBreak(Qt::TopToolBarArea);
    for (const Tab &tab : mTabs) {
        for (QToolBar *bar : tab.bars) {
            if (!bar)
                continue;
            mWindow->removeToolBar(bar);
            mWindow->addToolBar(Qt::TopToolBarArea, bar);
            bar->setMovable(false);
        }
    }

    for (int i = 0; i < mTabs.size(); ++i) {
        if (mTabs[i].contextual)
            mTabBar->setTabVisible(i, false);
    }

    mFinalized = true;

    // Restore the persisted tab (fall back to the first visible one).
    QSettings settings;
    settings.beginGroup(kSettingsGroup);
    const QString last = settings.value(kLastTabKey).toString();
    settings.endGroup();
    int target = indexOf(last);
    if (target < 0 || !mTabBar->isTabVisible(target)) {
        target = 0;
        while (target < mTabBar->count() && !mTabBar->isTabVisible(target))
            ++target;
    }
    if (target >= 0 && target < mTabBar->count()
        && mTabBar->currentIndex() != target) {
        mTabBar->setCurrentIndex(target);   // triggers applyVisibility
    } else {
        applyVisibility();
    }
}

void CompactToolbarController::setTabVisible(const QString &id, bool visible)
{
    const int index = indexOf(id);
    if (index < 0)
        return;
    if (mTabBar->isTabVisible(index) == visible)
        return;
    // QTabBar auto-selects a neighboring visible tab when the current one
    // is hidden (currentChanged fires and re-applies visibility); the
    // explicit apply below covers the non-current case.
    mTabBar->setTabVisible(index, visible);
    applyVisibility();
}

void CompactToolbarController::setCurrentTab(const QString &id)
{
    const int index = indexOf(id);
    if (index >= 0 && mTabBar->isTabVisible(index))
        mTabBar->setCurrentIndex(index);
}

QString CompactToolbarController::currentTab() const
{
    const int index = mTabBar->currentIndex();
    return (index >= 0 && index < mTabs.size()) ? mTabs[index].id : QString();
}

bool CompactToolbarController::isTabVisible(const QString &id) const
{
    const int index = indexOf(id);
    return index >= 0 && mTabBar->isTabVisible(index);
}

QStringList CompactToolbarController::tabIds() const
{
    QStringList out;
    out.reserve(mTabs.size());
    for (const Tab &tab : mTabs)
        out.append(tab.id);
    return out;
}

QList<QToolBar *> CompactToolbarController::toolbarsForTab(const QString &id) const
{
    const int index = indexOf(id);
    return index >= 0 ? mTabs[index].bars : QList<QToolBar *>{};
}

int CompactToolbarController::indexOf(const QString &id) const
{
    for (int i = 0; i < mTabs.size(); ++i) {
        if (mTabs[i].id == id)
            return i;
    }
    return -1;
}

void CompactToolbarController::applyVisibility()
{
    const int current = mTabBar->currentIndex();
    for (int i = 0; i < mTabs.size(); ++i) {
        const bool show = (i == current) && mTabBar->isTabVisible(i);
        for (QToolBar *bar : mTabs[i].bars) {
            if (bar)
                bar->setVisible(show);
        }
    }
}

}   // namespace openswmmvis::ui
