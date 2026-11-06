// SPDX-License-Identifier: Apache-2.0
// Apache-2.0 relicensing authorized by the project rights holder, 2026-10-29.
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <limits>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace meshkit
{

enum class Severity { Info, Warning, Error };
enum class EventKind { Stage, Progress, Diagnostic, StageCompleted, Completed };
enum class RunStatus {
    Running,
    Complete,
    CompleteWithExceptions,
    ResourceLimit,
    Cancelled,
    Failed,
    Infeasible
};

inline const char *statusName(RunStatus status) noexcept
{
    switch (status) {
    case RunStatus::Running:
        return "running";
    case RunStatus::Complete:
        return "complete";
    case RunStatus::CompleteWithExceptions:
        return "complete_with_exceptions";
    case RunStatus::ResourceLimit:
        return "resource_limit";
    case RunStatus::Cancelled:
        return "cancelled";
    case RunStatus::Failed:
        return "failed";
    case RunStatus::Infeasible:
        return "infeasible";
    }
    return "failed";
}

struct Counts {
    // -1 means unavailable. Working vertices can include unused/dead slots;
    // delivered vertices are supplied only once the assembled output is known.
    std::int64_t vertices = -1, workingVertices = -1, triangles = -1, quads = -1;
    std::int64_t inserted = -1, removed = -1;
    std::int64_t cells() const noexcept
    {
        return triangles < 0 || quads < 0 ||
                       triangles > std::numeric_limits<std::int64_t>::max() - quads
                   ? -1
                   : triangles + quads;
    }
};

struct Event {
    std::uint64_t job = 0, sequence = 0;
    std::int64_t elapsedMs = 0;
    EventKind kind = EventKind::Diagnostic;
    Severity severity = Severity::Info;
    RunStatus status = RunStatus::Running;
    std::string stage, code, message;
    Counts counts;
    int percent = -1;
};

// A per-job bounded, thread-safe mailbox. Producers never wait for a UI/event
// loop and retain no QObject. Consumers poll/drain on their own thread.
// Progress is coalesced; overflow is explicitly counted, never hidden.
class EventChannel
{
public:
    struct Batch {
        std::vector<Event> events;
        std::uint64_t dropped = 0; // cumulative non-coalesced overflow
    };

    explicit EventChannel(std::size_t capacity = 2048)
        : capacity_(std::max<std::size_t>(capacity, 2)), job_(nextJob_.fetch_add(1)),
          started_(Clock::now())
    {
    }

    std::uint64_t job() const noexcept { return job_; }

    void publish(Event event)
    {
        // Bounded strings as well as bounded row count. Callers supply UTF-8.
        truncate(event.message, 8192);
        truncate(event.stage, 256);
        truncate(event.code, 128);
        std::lock_guard<std::mutex> guard(mutex_);
        if (finished_)
            return;
        event.job = job_;
        event.sequence = ++sequence_;
        event.elapsedMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started_).count();
        if (event.percent >= 0) {
            percent_ = std::max(percent_, std::clamp(event.percent, 0, 100));
            event.percent = percent_;
        }
        if (event.kind == EventKind::Progress && !pending_.empty() &&
            pending_.back().kind == EventKind::Progress && pending_.back().stage == event.stage) {
            pending_.back() = std::move(event);
            return;
        }
        if (pending_.size() == capacity_) {
            const auto progress =
                std::find_if(pending_.begin(), pending_.end(),
                             [](const Event &e) { return e.kind == EventKind::Progress; });
            if (progress != pending_.end())
                pending_.erase(progress);
            else
                pending_.pop_front();
            ++dropped_;
        }
        finished_ = event.kind == EventKind::Completed;
        pending_.push_back(std::move(event));
    }

    Batch drain()
    {
        std::lock_guard<std::mutex> guard(mutex_);
        Batch batch;
        batch.dropped = dropped_;
        batch.events.reserve(pending_.size());
        while (!pending_.empty()) {
            batch.events.push_back(std::move(pending_.front()));
            pending_.pop_front();
        }
        return batch;
    }

private:
    static void truncate(std::string &s, std::size_t limit)
    {
        if (s.size() <= limit)
            return;
        while (limit > 0 && (static_cast<unsigned char>(s[limit]) & 0xc0) == 0x80)
            --limit;
        s.resize(limit);
        s += "…";
    }
    using Clock = std::chrono::steady_clock;
    const std::size_t capacity_;
    const std::uint64_t job_;
    const Clock::time_point started_;
    inline static std::atomic<std::uint64_t> nextJob_{1};
    std::mutex mutex_;
    std::deque<Event> pending_;
    std::uint64_t sequence_ = 0, dropped_ = 0;
    int percent_ = 0;
    bool finished_ = false;
};

} // namespace meshkit
