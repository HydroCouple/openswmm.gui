/*!
 * \file   trirefinehook.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Triangle refinement hook — cancellation, progress, and graded element sizing
 * without modifying vendor/triangle/.
 *
 * Shewchuk's Triangle exposes exactly one extension point during quality
 * refinement: the `triunsuitable()` callback, invoked from testtriangle() for
 * every candidate triangle when the `-u` switch is present.  Upstream sanctions
 * supplying it externally by defining EXTERNAL_TEST (see the comment block at
 * triangle.c:1363-1384), which is how it is wired here — triangle.c itself is
 * untouched.
 *
 * Three things ride on this one hook:
 *
 *  1. CANCELLATION.  Triangle's refinement loop is otherwise uninterruptible:
 *     a long `-q`/`-a` run cannot be stopped, so the GUI's Stop button is dead
 *     for its whole duration.  The hook polls a caller-supplied predicate and,
 *     once cancelled, reports every triangle as suitable.  The bad-triangle
 *     queue then drains and Triangle returns through its normal exit path.
 *
 *     This is deliberately NOT a longjmp out of triangulate().  Triangle's
 *     memory pools are freed by triangledeinit() at the very end of
 *     triangulate(); jumping out past it leaks them, and they are the dominant
 *     allocation in the whole pipeline (~200 bytes per output vertex).  Draining
 *     is slightly slower to respond but leaks nothing.
 *
 *  2. PROGRESS.  The hook fires often enough during refinement to drive a
 *     progress indicator, which Triangle otherwise provides no way to observe.
 *
 *  3. GRADED SIZING.  A single global `-a<area>` cap forces the SAME element
 *     size everywhere, which is the primary driver of output vertex count on
 *     large domains.  A size function lets the caller ask for fine elements
 *     only where they matter and coarse ones elsewhere.  This is the largest
 *     available lever on final mesh size.
 *
 * NOTE: `triunsuitable()` takes no user-data pointer, so the active hook has to
 * be reachable from a free function.  It is stored thread-locally rather than
 * globally so that triangulating several domains (or, later, several tiles)
 * concurrently stays correct.
 */
#ifndef OPENSWMMVIS_MESH_TRIREFINEHOOK_H
#define OPENSWMMVIS_MESH_TRIREFINEHOOK_H

#include <QtGlobal>

#include <functional>

namespace mesh {

/*! \brief Callbacks consulted by Triangle's `-u` user test.
 *
 * Every member is optional.  A hook with all members empty is equivalent to
 * not passing `-u` at all (every triangle is reported suitable), so installing
 * one never changes the mesh by itself.
 */
struct RefineHook
{
    /*! Polled periodically during refinement.  Returning true stops further
     *  refinement; generate() then reports failure rather than a partial mesh.
     *  Must be cheap and thread-safe (typically reads an atomic). */
    std::function<bool()> isCancelled;

    /*! Maximum permitted triangle area at map coordinate (x, y), evaluated at
     *  the candidate triangle's centroid.  Return <= 0 to leave the triangle
     *  unconstrained.  When set, this SUPERSEDES GenerationOptions::maxArea —
     *  MeshGenerator omits the global `-a<area>` switch so the two cannot
     *  fight.
     *
     *  CAREFUL with per-region bounds (corrected 2026-08-17; this comment
     *  previously claimed they were "unaffected and still applied", which is
     *  wrong).  Triangle honours RegionMarker::maxArea only when its internal
     *  `vararea` flag is set, and that flag is set only by a BARE `a` switch
     *  with no number (vendor/triangle/triangle.c:3449-3469, tested at 7390).
     *  So the two configurations differ:
     *
     *    - maxArea > 0 and NO size function → `a<area>` is emitted, `vararea`
     *      stays clear, and per-region bounds are silently IGNORED;
     *    - size function installed → no numeric `a`, the bare `a` branch runs,
     *      and per-region bounds take effect.
     *
     *  Installing a size function therefore switches region area bounds on for
     *  the first time on a model that has them.  Callers that care should clamp
     *  RegionMarker::maxArea to their own floor before addRegion(); the mesh
     *  generation dialog does exactly that. */
    std::function<double(double x, double y)> targetAreaAt;

    /*! Called every few hundred thousand tests with the running test count.
     *  Purely advisory — there is no way to know the total in advance, so this
     *  suits a busy indicator or a log line, not a percentage. */
    std::function<void(qint64 testCount)> onProgress;
};

/*!
 * \brief Installs \p hook as the active refinement hook for the current thread.
 *
 * Scope the guard around the triangulate() call.  Nesting is supported: the
 * previous hook is restored on destruction.  Passing nullptr disables the hook
 * for the guarded scope.
 */
class RefineHookGuard
{
public:
    explicit RefineHookGuard(const RefineHook *hook) noexcept;
    ~RefineHookGuard();

    RefineHookGuard(const RefineHookGuard &)            = delete;
    RefineHookGuard &operator=(const RefineHookGuard &) = delete;

private:
    const RefineHook *m_prev;
};

/*! \brief True if the hook active on this thread observed cancellation.
 *
 * Valid until the next RefineHookGuard is constructed on this thread.  Because
 * cancellation drains rather than aborts, triangulate() still returns success —
 * callers must consult this to tell a completed mesh from an abandoned one. */
[[nodiscard]] bool refineHookWasCancelled() noexcept;

/*! \brief Number of triunsuitable() invocations since the active guard was
 *         constructed.  Useful for logging refinement cost. */
[[nodiscard]] qint64 refineHookTestCount() noexcept;

} // namespace mesh

#endif // OPENSWMMVIS_MESH_TRIREFINEHOOK_H
