/*!
 * \file   trirefinehook.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "mesh/trirefinehook.h"

namespace {

// Active hook + per-run counters.  thread_local so concurrent triangulations
// (several domains now, mesh tiles later) cannot see each other's state.
thread_local const mesh::RefineHook *tHook      = nullptr;
thread_local qint64                  tTests     = 0;
thread_local bool                    tCancelled = false;

// Countdown thresholds rather than modulo: triunsuitable() sits in Triangle's
// innermost refinement loop and an integer division per call is measurable.
thread_local qint64 tNextCancelPoll = 0;
thread_local qint64 tNextProgress   = 0;

// Cancellation latency vs polling cost.  At a few million tests per second a
// 4096-test stride puts the response time well under a frame.
constexpr qint64 kCancelPollStride = 4096;

// Progress is advisory only (no total is knowable), so keep it rare.
constexpr qint64 kProgressStride = 1 << 18;   // 262144

} // namespace

namespace mesh {

RefineHookGuard::RefineHookGuard(const RefineHook *hook) noexcept
    : m_prev(tHook)
{
    tHook           = hook;
    tTests          = 0;
    tCancelled      = false;
    tNextCancelPoll = kCancelPollStride;
    tNextProgress   = kProgressStride;
}

RefineHookGuard::~RefineHookGuard()
{
    tHook = m_prev;
}

bool   refineHookWasCancelled() noexcept { return tCancelled; }
qint64 refineHookTestCount()    noexcept { return tTests; }

} // namespace mesh

// ---------------------------------------------------------------------------
// Triangle's user test.  triangle.c is compiled with EXTERNAL_TEST, which
// replaces its own definition with a declaration (triangle.c:1386-1390) and
// leaves this symbol to be resolved at final link.
//
// The parameter types must match Triangle's: `vertex` is `REAL *`, and REAL is
// double (triangle.h:267).  triangle.c declares it K&R-style with unspecified
// arguments under EXTERNAL_TEST, so C linkage is what matters here.
//
// Returns 1 if the triangle is too large and must be refined, 0 otherwise.
// ---------------------------------------------------------------------------

extern "C" int triunsuitable(double *triorg, double *tridest, double *triapex,
                             double area)
{
    const mesh::RefineHook *h = tHook;
    if (!h) return 0;                 // no hook installed — nothing is unsuitable

    const qint64 n = ++tTests;

    if (!tCancelled && n >= tNextCancelPoll) {
        tNextCancelPoll = n + kCancelPollStride;
        if (h->isCancelled && h->isCancelled())
            tCancelled = true;
    }

    // Cancelled: report everything suitable so the bad-triangle queue drains
    // and triangulate() unwinds through triangledeinit(), freeing its pools.
    // See the header for why this is not a longjmp.
    if (tCancelled) return 0;

    if (h->onProgress && n >= tNextProgress) {
        tNextProgress = n + kProgressStride;
        h->onProgress(n);
    }

    if (!h->targetAreaAt) return 0;

    // Evaluate the size function at the centroid.  Triangle hands us the three
    // corners; the centroid is the cheapest stable representative point, and
    // using it keeps the decision independent of vertex ordering.
    const double cx = (triorg[0] + tridest[0] + triapex[0]) / 3.0;
    const double cy = (triorg[1] + tridest[1] + triapex[1]) / 3.0;

    const double target = h->targetAreaAt(cx, cy);

    // Non-positive target means "unconstrained here", matching Triangle's own
    // treatment of non-positive per-region area bounds (triangle.c:7356-7358).
    return (target > 0.0 && area > target) ? 1 : 0;
}
