/**
 * @file toolrunspanel.h
 * @brief Shared run-history view and per-tool result/message inspector.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include <QWidget>
#include <QPointer>
#include <QDateTime>

class QMainWindow;
class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;
class QPlainTextEdit;
class QTimer;
class QShowEvent;
class QHideEvent;
namespace openswmmvis
{
class ToolRunStore;
/** @brief MVC projection of the window history, optionally scoped to one tool. */
class ToolRunsPanel final : public QWidget
{
    Q_OBJECT
public:
    /** @brief Bind copied session history.
     * @param[in] store Window-owned model.
     * @param[in] pluginId Optional owner filter.
     * @param[in] toolId Optional local tool filter paired with owner.
     * @param[in] parent Widget owner. */
    ToolRunsPanel(ToolRunStore *store, const QString &pluginId = {},
                  const QString &toolId = {}, QWidget *parent = nullptr);
protected:
    /** @brief Resume elapsed display when the selected active run becomes visible.
     * @param[in] event Native widget exposure. */
    void showEvent(QShowEvent *event) override;
    /** @brief Pause display-only timing while the inspector is hidden.
     * @param[in] event Native widget concealment. */
    void hideEvent(QHideEvent *event) override;
private:
    void refresh();
    void showRun();
    void updateTimingSummary();
    QPointer<ToolRunStore> mStore;
    QString mPluginId;
    QString mToolId;
    QComboBox *mProjectFilter;
    QComboBox *mToolFilter;
    QComboBox *mRun;
    QLabel *mSummary;
    QPushButton *mCancel;
    QPushButton *mRemove;
    QTableWidget *mResults;
    QPlainTextEdit *mMessages;
    QTimer *mElapsedTimer; ///< Updates only cached timing text once a second while visible.
    QDateTime mStartedAt; ///< Selected run start for display-only elapsed updates.
    QDateTime mFinishedAt; ///< Selected run end; invalid during active work.
    QString mSummaryPrefix; ///< Cached literal details, independent of message/result widgets.
};
/** @brief Create a projection of the unique main-window history.
 * @param[in] window Store owner.
 * @param[in] pluginId Optional owner filter.
 * @param[in] toolId Optional tool filter.
 * @param[in] parent Widget owner.
 * @return New host-owned widget. */
QWidget *createToolRunsPanel(QMainWindow *window, const QString &pluginId = {},
                            const QString &toolId = {}, QWidget *parent = nullptr);
}
