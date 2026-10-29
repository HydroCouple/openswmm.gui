// SPDX-License-Identifier: GPL-3.0-or-later
#include <meshkit/events.hpp>
#include <cstdlib>
#include <iostream>
#include <thread>

static void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

int main()
{
    using namespace meshkit;
    EventChannel channel(8), other;
    require(channel.job() != other.job(), "jobs must be isolated");
    Event progress;
    progress.kind = EventKind::Progress;
    progress.stage = "refinement";
    for (int i = 0; i < 6'000'000; ++i) {
        progress.counts.triangles = i;
        progress.counts.quads = 42;
        channel.publish(progress);
    }
    auto batch = channel.drain();
    require(batch.events.size() == 1 && batch.dropped == 0, "progress flood must coalesce");
    require(batch.events[0].counts.cells() == 6'000'041, "typed cells must not be vertex counts");
    std::thread worker([&] {
        Event event;
        for (int i = 0; i < 1000; ++i) {
            event.message = std::to_string(i);
            channel.publish(event);
        }
    });
    worker.join();
    batch = channel.drain();
    require(batch.events.size() == 8 && batch.dropped == 992,
            "bounded diagnostics must report loss");
    for (std::size_t i = 1; i < batch.events.size(); ++i)
        require(batch.events[i].sequence > batch.events[i - 1].sequence, "sequence must increase");
    progress.percent = 70;
    channel.publish(progress);
    channel.drain();
    progress.percent = 45;
    channel.publish(progress);
    require(channel.drain().events[0].percent == 70,
            "second pass must not regress overall progress");
    Event completed;
    completed.kind = EventKind::Completed;
    completed.status = RunStatus::ResourceLimit;
    channel.publish(completed);
    channel.publish(progress);
    batch = channel.drain();
    require(batch.events.size() == 1 && batch.events[0].status == RunStatus::ResourceLimit,
            "terminal event must survive late progress");
    require(Counts{}.cells() == -1, "missing counts are unknown, not zero");
    std::cout
        << "Event isolation, six-million-update coalescing, overflow, counters and terminal state passed.\n";
}
