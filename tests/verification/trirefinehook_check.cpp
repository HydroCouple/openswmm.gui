/*!
 * \file   trirefinehook_check.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Standalone verification for the Triangle refinement hook.
 *
 * Links the REAL src/mesh/trirefinehook.cpp (no Qt, no GDAL, no Triangle) and
 * drives triunsuitable() directly, standing in for Triangle's testtriangle()
 * call at triangle.c:7366.
 *
 * Build & run (from this directory):
 *   g++ -std=c++17 -O2 -I../../include -I../../vendor/triangle \
 *       -o trirefinehook_check trirefinehook_check.cpp ../../src/mesh/trirefinehook.cpp \
 *       $(pkg-config --cflags --libs Qt6Core)
 *   ./trirefinehook_check > trirefinehook_check.out.txt
 *
 * Qt is needed only for qint64/QtGlobal, so a shim is used when Qt headers are
 * unavailable (see QTGLOBAL_SHIM below).
 *
 * Checks:
 *   1. A null hook is a no-op: nothing is ever unsuitable.
 *   2. An empty hook is a no-op — installing one cannot change the mesh.
 *   3. The size function grades by CENTROID and honours "<= 0 means
 *      unconstrained", matching Triangle's own per-region convention.
 *   4. Cancellation latches, then reports every triangle suitable, so the
 *      refinement queue drains and Triangle can free its pools normally.
 *      Cancellation is detected within one poll stride.
 *   5. Once cancelled, the size function and progress callback stop being
 *      consulted — no work is done on the way out.
 *   6. Progress fires on a coarse stride and reports a monotone count.
 *   7. The guard restores the previous hook (nesting) and resets counters.
 *   8. Hook state is per-thread: concurrent triangulations cannot see each
 *      other's cancellation.
 */

#include "mesh/trirefinehook.h"

#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

extern "C" int triunsuitable(double *triorg, double *tridest, double *triapex,
                             double area);

namespace {

int gFailures = 0;

void check(bool ok, const char *what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++gFailures;
}

// Stand-in for Triangle's testtriangle(): hand the hook a triangle with the
// given centroid and area.  Corner layout is irrelevant to the hook beyond the
// centroid, so an isoceles triangle about (cx, cy) is enough.
int ask(double cx, double cy, double area)
{
    double o[2] = { cx - 1.0, cy - 1.0 };
    double d[2] = { cx + 1.0, cy - 1.0 };
    double a[2] = { cx,       cy + 2.0 };
    return triunsuitable(o, d, a, area);
}

// The stride constants inside trirefinehook.cpp are private; these mirror them
// so the test can reason about latency without exporting implementation detail.
constexpr long long kCancelPollStride = 4096;
constexpr long long kProgressStride   = 1 << 18;

void test1_nullHook()
{
    std::printf("\n[1] No hook installed\n");
    // No guard in scope at all.
    bool any = false;
    for (int i = 0; i < 10000; ++i)
        if (ask(i, i, 1e9)) any = true;
    check(!any, "nothing is unsuitable when no hook is installed");
}

void test2_emptyHook()
{
    std::printf("\n[2] Empty hook installed\n");
    const mesh::RefineHook hook;                 // all members empty
    const mesh::RefineHookGuard g(&hook);

    bool any = false;
    for (int i = 0; i < 10000; ++i)
        if (ask(i, i, 1e9)) any = true;

    check(!any, "an empty hook never marks a triangle unsuitable");
    check(!mesh::refineHookWasCancelled(), "not cancelled");
    check(mesh::refineHookTestCount() == 10000, "test count tracks invocations");
}

void test3_sizeFunctionGrading()
{
    std::printf("\n[3] Size function grading\n");

    // Fine (target 10) left of x=100, coarse (target 1000) right of it,
    // unconstrained (target 0) beyond x=200.
    int nCalls = 0;
    double lastX = 0, lastY = 0;
    mesh::RefineHook hook;
    hook.targetAreaAt = [&](double x, double y) {
        ++nCalls; lastX = x; lastY = y;
        if (x > 200.0) return 0.0;
        return (x < 100.0) ? 10.0 : 1000.0;
    };
    const mesh::RefineHookGuard g(&hook);

    check(ask(50.0, 0.0, 50.0)  == 1, "fine zone: area 50 > target 10 -> refine");
    check(ask(50.0, 0.0, 5.0)   == 0, "fine zone: area 5 <= target 10 -> keep");
    check(ask(150.0, 0.0, 50.0) == 0, "coarse zone: area 50 <= target 1000 -> keep");
    check(ask(150.0, 0.0, 5000.0) == 1, "coarse zone: area 5000 > target 1000 -> refine");
    check(ask(300.0, 0.0, 1e12) == 0, "target <= 0 means unconstrained");

    // Centroid, not any single corner.  ask() builds corners at cx-1, cx+1, cx
    // and cy-1, cy-1, cy+2 -> centroid is exactly (cx, cy).
    ask(42.0, 7.0, 1.0);
    const bool centroid = (lastX > 41.999 && lastX < 42.001)
                       && (lastY >  6.999 && lastY <  7.001);
    check(centroid, "size function is evaluated at the centroid");
    check(nCalls == 6, "size function called once per test");
}

void test4_cancellationDrains()
{
    std::printf("\n[4] Cancellation latches and then drains\n");

    std::atomic<bool> stop{false};
    long long cancelPolls = 0;
    long long sizeCalls   = 0;

    mesh::RefineHook hook;
    hook.isCancelled  = [&] { ++cancelPolls; return stop.load(); };
    hook.targetAreaAt = [&](double, double) { ++sizeCalls; return 1.0; };
    const mesh::RefineHookGuard g(&hook);

    // Phase 1: not cancelled — everything oversized must be refined.
    int refinedBefore = 0;
    for (int i = 0; i < 20000; ++i) refinedBefore += ask(i, 0.0, 100.0);
    check(refinedBefore == 20000, "before cancel, oversized triangles are refined");
    check(!mesh::refineHookWasCancelled(), "not yet cancelled");

    const long long pollsAtSignal = cancelPolls;
    const long long testsAtSignal = mesh::refineHookTestCount();
    stop.store(true);

    // Phase 2: cancelled — detect within one stride, then report all suitable.
    long long detectedAt   = -1;
    long long refinedAfterDetect = 0;   // must stay 0
    for (int i = 0; i < 20000; ++i) {
        const int r = ask(i, 0.0, 100.0);
        const bool cancelledNow = mesh::refineHookWasCancelled();
        if (detectedAt < 0 && cancelledNow)
            detectedAt = mesh::refineHookTestCount();
        if (cancelledNow) refinedAfterDetect += r;
    }

    check(mesh::refineHookWasCancelled(), "cancellation latched");

    const long long latency = detectedAt - testsAtSignal;
    std::printf("      detected %lld tests after the signal (stride %lld)\n",
                latency, kCancelPollStride);
    check(latency > 0 && latency <= kCancelPollStride,
          "detected within one poll stride");

    // Every test from detection onward must report suitable (0). This is what
    // lets Triangle's bad-triangle queue drain to empty and unwind through
    // triangledeinit() instead of leaking the mesh pools.
    check(refinedAfterDetect == 0,
          "no triangle is marked unsuitable once cancelled");

    std::printf("      cancel polls before signal: %lld over %lld tests\n",
                pollsAtSignal, testsAtSignal);
    check(pollsAtSignal <= testsAtSignal / kCancelPollStride + 1,
          "cancel predicate is polled on a stride, not every test");
    check(sizeCalls > 0, "size function was consulted while running");
}

void test5_noWorkAfterCancel()
{
    std::printf("\n[5] No work after cancellation\n");

    std::atomic<bool> stop{true};      // cancelled from the very first poll
    long long sizeCalls = 0, progressCalls = 0;

    mesh::RefineHook hook;
    hook.isCancelled  = [&] { return stop.load(); };
    hook.targetAreaAt = [&](double, double) { ++sizeCalls; return 1.0; };
    hook.onProgress   = [&](qint64) { ++progressCalls; };
    const mesh::RefineHookGuard g(&hook);

    // Run well past both strides.
    long long refined = 0, refinedAfterDetect = 0;
    const long long n = kProgressStride * 3;
    for (long long i = 0; i < n; ++i) {
        const int r = ask(0.0, 0.0, 1e9);
        refined += r;
        if (mesh::refineHookWasCancelled()) refinedAfterDetect += r;
    }

    check(mesh::refineHookWasCancelled(), "cancelled");

    // Cancellation is polled on a stride, so the tests BEFORE the first poll
    // are still evaluated normally — that is the intended bounded latency, not
    // a leak. What matters is that the work stops at detection and that the
    // pre-detection work is bounded by one stride.
    std::printf("      refined %lld total, %lld after detection (bound %lld)\n",
                refined, refinedAfterDetect, kCancelPollStride);
    check(refined < kCancelPollStride,
          "pre-detection refinement is bounded by one poll stride");
    check(refinedAfterDetect == 0,
          "every triangle reported suitable once cancellation is detected");

    // The size function may be consulted only for the tests BEFORE the first
    // poll detected cancellation (i.e. at most one stride's worth).
    std::printf("      size fn calls %lld (bound %lld), progress calls %lld\n",
                sizeCalls, kCancelPollStride, progressCalls);
    check(sizeCalls <= kCancelPollStride, "size fn stops being consulted");
    check(progressCalls == 0, "progress stops being reported");
}

void test6_progressStride()
{
    std::printf("\n[6] Progress reporting\n");

    std::vector<long long> reports;
    mesh::RefineHook hook;
    hook.onProgress = [&](qint64 n) { reports.push_back(n); };
    const mesh::RefineHookGuard g(&hook);

    const long long n = kProgressStride * 4 + 7;
    for (long long i = 0; i < n; ++i) ask(0.0, 0.0, 1.0);

    std::printf("      %zu report(s) over %lld tests\n", reports.size(), n);
    check(reports.size() == 4, "one report per progress stride");

    bool monotone = true;
    for (std::size_t i = 1; i < reports.size(); ++i)
        if (reports[i] <= reports[i - 1]) monotone = false;
    check(monotone, "reported counts are strictly increasing");
    check(!reports.empty() && reports.front() == kProgressStride,
          "first report lands on the stride boundary");
}

void test7_guardNestingAndReset()
{
    std::printf("\n[7] Guard nesting and counter reset\n");

    mesh::RefineHook outer;
    outer.targetAreaAt = [](double, double) { return 1.0; };   // refines
    mesh::RefineHook inner;                                     // empty: never refines

    const mesh::RefineHookGuard gOuter(&outer);
    for (int i = 0; i < 100; ++i) ask(0, 0, 1e6);
    check(mesh::refineHookTestCount() == 100, "outer counted 100");
    check(ask(0, 0, 1e6) == 1, "outer hook is active");

    {
        const mesh::RefineHookGuard gInner(&inner);
        check(mesh::refineHookTestCount() == 0, "guard resets the counter");
        check(ask(0, 0, 1e6) == 0, "inner hook is active");
    }

    check(ask(0, 0, 1e6) == 1, "outer hook restored after inner guard dies");

    {
        const mesh::RefineHookGuard gNull(nullptr);
        check(ask(0, 0, 1e6) == 0, "nullptr guard disables the hook");
    }
    check(ask(0, 0, 1e6) == 1, "outer hook restored after null guard dies");
}

void test8_threadIsolation()
{
    std::printf("\n[8] Per-thread isolation\n");

    std::atomic<bool> aCancelledBLeaked{false};
    std::atomic<bool> bSawOwnCancel{false};
    std::atomic<bool> aRefinedThroughout{true};

    // Thread A: never cancels, always expects "refine".
    std::thread ta([&] {
        mesh::RefineHook h;
        h.isCancelled  = [] { return false; };
        h.targetAreaAt = [](double, double) { return 1.0; };
        const mesh::RefineHookGuard g(&h);
        for (int i = 0; i < 200000; ++i) {
            if (ask(0, 0, 1e6) != 1) aRefinedThroughout.store(false);
            if (mesh::refineHookWasCancelled()) aCancelledBLeaked.store(true);
        }
    });

    // Thread B: cancels immediately.
    std::thread tb([&] {
        mesh::RefineHook h;
        h.isCancelled  = [] { return true; };
        h.targetAreaAt = [](double, double) { return 1.0; };
        const mesh::RefineHookGuard g(&h);
        for (int i = 0; i < 200000; ++i) ask(0, 0, 1e6);
        bSawOwnCancel.store(mesh::refineHookWasCancelled());
    });

    ta.join(); tb.join();

    check(bSawOwnCancel.load(), "thread B observed its own cancellation");
    check(!aCancelledBLeaked.load(), "thread A never saw B's cancellation");
    check(aRefinedThroughout.load(), "thread A's size function stayed in effect");
}

} // namespace

int main()
{
    std::printf("Triangle refinement hook verification\n");
    std::printf("=====================================\n");

    test1_nullHook();
    test2_emptyHook();
    test3_sizeFunctionGrading();
    test4_cancellationDrains();
    test5_noWorkAfterCancel();
    test6_progressStride();
    test7_guardNestingAndReset();
    test8_threadIsolation();

    std::printf("\n=====================================\n");
    std::printf("%s (%d failure%s)\n", gFailures ? "FAILED" : "ALL CHECKS PASSED",
                gFailures, gFailures == 1 ? "" : "s");
    return gFailures ? 1 : 0;
}
