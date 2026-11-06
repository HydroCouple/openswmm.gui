// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <meshkit/events.hpp>
#include <QWidget>
#include <memory>
#include <functional>

class QTimer;
class QTableView;
class QLabel;
class QPushButton;
class QPlainTextEdit;
class MeshRunLogModel;
class MeshRunLogFilter;

class MeshRunLogWidget : public QWidget
{
public:
    explicit MeshRunLogWidget(QWidget *parent = nullptr);
    void beginRun(std::shared_ptr<meshkit::EventChannel> channel);
    void drain();
    void finish();
    void addNote(meshkit::Severity severity, const QString &message);
    void setHighlightAction(std::function<void()> action);
    QString text() const;
    bool exportLog(const QString &path, QString *error = nullptr) const;

private:
    MeshRunLogModel *model_;
    MeshRunLogFilter *filter_;
    QTableView *view_;
    QLabel *retention_;
    QTimer *timer_;
    QPushButton *highlight_;
    QPlainTextEdit *details_;
    std::function<void()> highlightAction_;
    std::shared_ptr<meshkit::EventChannel> channel_;
    bool follow_ = true;
    std::uint64_t lost_ = 0;
};
