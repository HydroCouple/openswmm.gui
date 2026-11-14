/**
 * @file toolrunspanel.cpp
 * @brief Literal run inspection, filtering, cancellation and validated output navigation.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "ui/panels/toolrunspanel.h"
#include "plugins/extensiontoolruns.h"
#include <QComboBox>
#include <QDateTime>
#include <QFileInfo>
#include <QHeaderView>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QShowEvent>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>

using namespace OpenSWMM::Sdk;
namespace openswmmvis
{
namespace
{
QString statusText(const ToolRunState &state)
{
    if (!state.isFinished())
        return state.cancelRequested ? QObject::tr("Canceling") : QObject::tr("Running");
    switch (state.outcome) {
    case ToolRunOutcome::Succeeded: return QObject::tr("Completed");
    case ToolRunOutcome::Warning: return QObject::tr("Completed with warnings");
    case ToolRunOutcome::Failed: return QObject::tr("Failed");
    case ToolRunOutcome::Cancelled: return QObject::tr("Canceled");
    }
    return {};
}
}
ToolRunsPanel::ToolRunsPanel(ToolRunStore *store, const QString &pluginId,
                             const QString &toolId, QWidget *parent)
    : QWidget(parent), mStore(store), mPluginId(pluginId), mToolId(toolId)
{
    setObjectName("toolRunsPanel");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    mProjectFilter = new QComboBox(this);
    mProjectFilter->setObjectName("toolRunProjectFilter");
    mProjectFilter->setAccessibleName(tr("Filter history by model session"));
    mProjectFilter->setVisible(pluginId.isEmpty());
    layout->addWidget(mProjectFilter);
    mToolFilter = new QComboBox(this);
    mToolFilter->setObjectName("toolRunToolFilter");
    mToolFilter->setAccessibleName(tr("Filter history by tool"));
    mToolFilter->setVisible(pluginId.isEmpty());
    layout->addWidget(mToolFilter);
    auto *row = new QHBoxLayout;
    mRun = new QComboBox(this);
    mRun->setObjectName("toolRunSelector");
    mRun->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    mRun->setAccessibleName(tr("Execution history"));
    row->addWidget(mRun, 1);
    mCancel = new QPushButton(tr("Cancel"), this);
    mCancel->setObjectName("cancelToolRun");
    row->addWidget(mCancel);
    mRemove = new QPushButton(tr("Remove entry"), this);
    row->addWidget(mRemove);
    layout->addLayout(row);
    mSummary = new QLabel(this);
    mSummary->setObjectName("toolRunSummary");
    mSummary->setTextFormat(Qt::PlainText);
    mSummary->setWordWrap(true);
    mSummary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(mSummary);
    mElapsedTimer = new QTimer(this);
    mElapsedTimer->setObjectName("toolRunElapsedTimer");
    mElapsedTimer->setInterval(1000);
    connect(mElapsedTimer, &QTimer::timeout, this, &ToolRunsPanel::updateTimingSummary);
    auto *tabs = new QTabWidget(this);
    mResults = new QTableWidget(0, 3, tabs);
    mResults->setObjectName("toolRunResults");
    mResults->setHorizontalHeaderLabels({tr("Result"), tr("Value or file"), tr("Action")});
    mResults->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    mResults->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mResults->setWordWrap(false);
    mResults->verticalHeader()->hide();
    tabs->addTab(mResults, tr("Results"));
    mMessages = new QPlainTextEdit(tabs);
    mMessages->setObjectName("toolRunMessages");
    mMessages->setReadOnly(true);
    mMessages->setMaximumBlockCount(1100);
    tabs->addTab(mMessages, tr("Messages"));
    layout->addWidget(tabs, 1);
    connect(mRun, &QComboBox::currentIndexChanged, this, &ToolRunsPanel::showRun);
    connect(mProjectFilter, &QComboBox::currentIndexChanged, this, &ToolRunsPanel::refresh);
    connect(mToolFilter, &QComboBox::currentIndexChanged, this, &ToolRunsPanel::refresh);
    connect(mCancel, &QPushButton::clicked, this, [this] {
        if (mStore)
            mStore->requestCancel(mRun->currentData().toUuid());
    });
    connect(mRemove, &QPushButton::clicked, this, [this] {
        if (mStore)
            mStore->remove(mRun->currentData().toUuid());
    });
    if (mStore)
        connect(mStore, &ToolRunStore::changed, this, &ToolRunsPanel::refresh);
    refresh();
}
void ToolRunsPanel::refresh()
{
    if (!mStore)
        return;
    const auto previous = mRun->currentData().toUuid();
    const auto project = mProjectFilter->currentData().toUuid();
    const auto tool = mToolFilter->currentData().toString();
    const auto states = mStore->runs();
    const QSignalBlocker projectBlocker(mProjectFilter);
    mProjectFilter->clear();
    mProjectFilter->addItem(tr("All projects"), QUuid{});
    for (const auto &state : states) {
        const auto session = state.request.sessionId;
        if (!session.isNull() && mProjectFilter->findData(session) < 0)
            mProjectFilter->addItem(state.modelTitle.isEmpty()
                ? tr("Model %1").arg(session.toString(QUuid::WithoutBraces).left(8))
                : state.modelTitle, session);
    }
    mProjectFilter->setCurrentIndex(qMax(0, mProjectFilter->findData(project)));
    const QSignalBlocker toolBlocker(mToolFilter);
    mToolFilter->clear();
    mToolFilter->addItem(tr("All tools"), QString{});
    for (const auto &state : states) {
        const auto key = state.pluginId + '\n' + state.request.toolId;
        if (mToolFilter->findData(key) < 0)
            mToolFilter->addItem(state.pluginId + " / " + state.request.toolId, key);
    }
    mToolFilter->setCurrentIndex(qMax(0, mToolFilter->findData(tool)));
    const QSignalBlocker runBlocker(mRun);
    mRun->clear();
    for (const auto &state : states) {
        if ((!mPluginId.isEmpty() && state.pluginId != mPluginId) ||
            (!mToolId.isEmpty() && state.request.toolId != mToolId) ||
            (!tool.isEmpty() && state.pluginId + '\n' + state.request.toolId != tool) ||
            (!project.isNull() && state.request.sessionId != project))
            continue;
        mRun->addItem(QStringLiteral("%1 · %2 · %3 · %4")
            .arg(state.startedAt.toLocalTime().toString("yyyy-MM-dd HH:mm:ss"), state.request.toolId,
                 state.request.title, statusText(state)), state.id);
        mRun->setItemData(mRun->count() - 1, toolRunTimestamp(state.startedAt), Qt::ToolTipRole);
    }
    const int retained = mRun->findData(previous);
    mRun->setCurrentIndex(retained >= 0 ? retained : mRun->count() > 0 ? 0 : -1);
    showRun();
}
void ToolRunsPanel::showRun()
{
    const auto state = mStore ? mStore->run(mRun->currentData().toUuid()) : ToolRunState{};
    mCancel->setEnabled(!state.id.isNull() && !state.isFinished() && state.cancellable && !state.cancelRequested);
    mRemove->setEnabled(state.isFinished());
    mResults->setRowCount(0);
    mMessages->clear();
    mElapsedTimer->stop();
    mStartedAt = state.startedAt;
    mFinishedAt = state.finishedAt;
    if (state.id.isNull()) {
        mSummary->setText(tr("No executions yet. Open a tool and choose Preview, Download or Run."));
        return;
    }
    mSummaryPrefix = state.pluginId + " · " + statusText(state);
    if (!state.request.inputs.isEmpty())
        mSummaryPrefix += "\n" + state.request.inputs;
    if (!state.stage.isEmpty())
        mSummaryPrefix += "\n" + state.stage;
    updateTimingSummary();
    if (!state.isFinished() && isVisible())
        mElapsedTimer->start();
    for (const auto &result : state.results) {
        const int row = mResults->rowCount();
        mResults->insertRow(row);
        mResults->setItem(row, 0, new QTableWidgetItem(result.title));
        mResults->setItem(row, 1, new QTableWidgetItem(result.path.isEmpty() ? result.value : result.path));
        if (result.kind == ToolResultKind::Value)
            continue;
        auto *button = new QPushButton(result.kind == ToolResultKind::Layer
            ? tr("Show in Layers") : tr("Reveal in folder"), mResults);
        const bool available = mStore && mStore->navigate(result);
        button->setEnabled(available);
        if (!available)
            button->setToolTip(tr("This output is currently unavailable. Its saved description is retained."));
        connect(button, &QPushButton::clicked, this, [this, result] {
            if (mStore)
                mStore->navigate(result, true);
            showRun();
        });
        mResults->setCellWidget(row, 2, button);
    }
    QStringList lines;
    if (state.messagesTruncated)
        lines.append(tr("Earlier messages were removed from this bounded history."));
    for (const auto &message : state.messages)
        lines.append(QStringLiteral("%1 [%2] %3")
            .arg(toolRunTimestamp(message.at), message.level, message.text));
    mMessages->setPlainText(lines.join('\n'));
}
void ToolRunsPanel::updateTimingSummary()
{
    if (!mStartedAt.isValid())
        return;
    QString summary = mSummaryPrefix + tr("\nStarted %1").arg(toolRunTimestamp(mStartedAt));
    if (mFinishedAt.isValid())
        summary += tr("\nFinished %1\nDuration %2")
            .arg(toolRunTimestamp(mFinishedAt), toolRunDuration(mStartedAt, mFinishedAt));
    else
        summary += tr("\nElapsed %1")
            .arg(toolRunDuration(mStartedAt, QDateTime::currentDateTimeUtc()));
    mSummary->setText(summary);
}
void ToolRunsPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (mStartedAt.isValid() && !mFinishedAt.isValid()) {
        updateTimingSummary();
        mElapsedTimer->start();
    }
}
void ToolRunsPanel::hideEvent(QHideEvent *event)
{
    mElapsedTimer->stop();
    QWidget::hideEvent(event);
}
QWidget *createToolRunsPanel(QMainWindow *window, const QString &pluginId,
                            const QString &toolId, QWidget *parent)
{
    auto *store = ToolRunStore::forWindow(window);
    if (toolId.isEmpty())
        return new ToolRunsPanel(store, pluginId, toolId, parent);
    auto *container = new QWidget(parent);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *toggle = new QToolButton(container);
    toggle->setText(QObject::tr("Results and messages"));
    toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toggle->setArrowType(Qt::RightArrow);
    toggle->setCheckable(true);
    toggle->setAccessibleName(QObject::tr("Show results and messages for this tool"));
    layout->addWidget(toggle);
    auto *panel = new ToolRunsPanel(store, pluginId, toolId, container);
    panel->hide();
    layout->addWidget(panel);
    QObject::connect(toggle, &QToolButton::toggled, panel, [toggle, panel](bool open) {
        toggle->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
        panel->setVisible(open);
    });
    return container;
}
}
