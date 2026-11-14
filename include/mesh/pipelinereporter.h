// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <meshkit/events.hpp>
#include <QDebug>
#include <QElapsedTimer>
#include <QString>
#include <memory>

namespace mesh
{

// Qt adapter only. The per-job transport itself is application independent.
class PipelineReporter
{
public:
    explicit PipelineReporter(std::shared_ptr<meshkit::EventChannel> channel)
        : channel_(std::move(channel))
    {
        clock_.start();
    }

    void progress(int percent, const QString &message, const QString &stage,
                  meshkit::Counts counts = {})
    {
        const bool changed = stage_ != stage;
        stage_ = stage;
        if (!changed && clock_.elapsed() - lastProgress_ < 100)
            return;
        lastProgress_ = clock_.elapsed();
        emitEvent(changed ? meshkit::EventKind::Stage : meshkit::EventKind::Progress,
                  meshkit::Severity::Info, message, "progress", counts, percent);
    }

    void diagnostic(meshkit::Severity severity, const QString &message,
                    const char *code = "diagnostic")
    {
        warned_ = warned_ || severity != meshkit::Severity::Info;
        emitEvent(meshkit::EventKind::Diagnostic, severity, message, code);
    }
    bool hasWarnings() const noexcept { return warned_; }

    void stageCompleted(const QString &stage, qint64 milliseconds)
    {
        emitEvent(
            meshkit::EventKind::StageCompleted, meshkit::Severity::Info,
            QStringLiteral("%1 completed in %2 s").arg(stage).arg(milliseconds / 1000.0, 0, 'f', 3),
            "stage_completed", {}, -1, meshkit::RunStatus::Running, stage);
    }

    void complete(meshkit::RunStatus status, const QString &message, meshkit::Counts counts = {})
    {
        const auto severity = status == meshkit::RunStatus::Failed     ? meshkit::Severity::Error
                              : status == meshkit::RunStatus::Complete ? meshkit::Severity::Info
                                                                       : meshkit::Severity::Warning;
        emitEvent(meshkit::EventKind::Completed, severity, message, meshkit::statusName(status),
                  counts,
                  status == meshkit::RunStatus::Complete ||
                          status == meshkit::RunStatus::CompleteWithExceptions
                      ? 100
                      : -1,
                  status);
    }

    // Preserve ordinary diagnostic output while delivering the same message to
    // this job. No global Qt handler and no cross-job/GUI object capture.
    class Line
    {
    public:
        Line(PipelineReporter &owner, meshkit::Severity severity)
            : owner_(owner), severity_(severity), stream_(std::make_unique<QDebug>(&message_))
        {
        }
        ~Line() noexcept
        {
            stream_.reset();
            try {
                if (severity_ == meshkit::Severity::Error)
                    qCritical().noquote() << message_;
                else if (severity_ == meshkit::Severity::Warning)
                    qWarning().noquote() << message_;
                else
                    qInfo().noquote() << message_;
                owner_.diagnostic(severity_, message_);
            } catch (...) { /* Diagnostic allocation must not terminate the worker. */
            }
        }
        Line(const Line &) = delete;
        Line &operator=(const Line &) = delete;
        Line &noquote()
        {
            stream_->noquote();
            return *this;
        }
        Line &nospace()
        {
            stream_->nospace();
            return *this;
        }
        template <class T> Line &operator<<(const T &value)
        {
            *stream_ << value;
            return *this;
        }

    private:
        PipelineReporter &owner_;
        meshkit::Severity severity_;
        QString message_;
        std::unique_ptr<QDebug> stream_;
    };
    Line line(meshkit::Severity severity) { return Line(*this, severity); }

private:
    void emitEvent(meshkit::EventKind kind, meshkit::Severity severity, const QString &message,
                   const char *code, meshkit::Counts counts = {}, int percent = -1,
                   meshkit::RunStatus status = meshkit::RunStatus::Running,
                   const QString &eventStage = {})
    {
        if (!channel_)
            return;
        meshkit::Event event;
        event.kind = kind;
        event.severity = severity;
        event.status = status;
        event.stage = (eventStage.isEmpty() ? stage_ : eventStage).toUtf8().toStdString();
        event.code = code;
        event.message = message.toUtf8().toStdString();
        event.counts = counts;
        event.percent = percent;
        channel_->publish(std::move(event));
    }
    std::shared_ptr<meshkit::EventChannel> channel_;
    QString stage_;
    QElapsedTimer clock_;
    qint64 lastProgress_ = -100;
    bool warned_ = false;
};
}
