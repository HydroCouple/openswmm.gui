/**
 * @file      qsg2dasyncresult.h
 * @brief     Transfer of asynchronously prepared 2D scene graph results.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * QSG-2D-1M Phase 7 — cancellation-safe double buffer for asynchronously
 * recomputed derived render geometry (contour bands, isoline segments,
 * velocity samples, far-LOD aggregates).
 *
 * Contract (all calls on the GUI thread — workers hand their result back
 * via a queued invocation before publishing):
 *
 *   1. When inputs change, the owner calls beginJob() and captures the
 *      returned generation token alongside the input snapshot it hands to
 *      the worker.
 *   2. While a job is in flight, the owner keeps rendering value() — the
 *      last published result (double buffering: stale-but-consistent
 *      output instead of a stall).
 *   3. The worker's completion handler calls tryPublish(gen, result).
 *      A result whose generation is no longer current is dropped, so a
 *      time/style change racing a slow worker can never apply stale
 *      output.
 *   4. invalidate() bumps the generation without starting a job — used
 *      when the derived product becomes meaningless (geometry swap).
 *
 * Header-only, no Qt dependency beyond the QtGlobal header. Unit-tested in
 * tests/unit/test_qsg2d_dirtystate.cpp (stale-result section).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_QSG2DASYNCRESULT_H
#define OPENSWMM_RENDER_QSG2DASYNCRESULT_H

#include <QtGlobal>

#include <utility>

namespace OpenSWMM::Render
{

/** @brief Keep a previously published product while a newer job is pending.
 * @tparam T Default-constructible, move-assignable result value. All methods run
 * on the GUI thread; generation checks do not provide thread synchronization.
 * Counter wraparound is not handled; tokens must remain unique over outstanding jobs. */
template <typename T> class Qsg2DAsyncResult
{
public:
    /*! Start a new job: invalidates any in-flight generation and returns
     *  the token the eventual tryPublish() must present.
     * @return Newly incremented generation. The previous published value is retained. */
    quint64 beginJob() { return ++m_submitted; }

    /*! Invalidate without starting a job — pending results become stale
     *  and the published value is cleared. */
    void invalidate()
    {
        ++m_submitted;
        m_published = 0;
        m_value = T{};
    }

    /** @brief Publish a completed product only for the current generation.
     * @param generation Token returned by beginJob(), checked on the GUI thread.
     * @param[in,out] value Result moved into storage on success; unchanged for a stale token.
     * @return True after moving and stamping the result, false for a stale generation.
     * This is a token comparison, not cancellation of worker execution. Use a
     * beginJob() token; zero is the unpublished sentinel, and equal tokens can be
     * published repeatedly. Exceptions from T's move assignment propagate. */
    bool tryPublish(quint64 generation, T &&value)
    {
        if (generation != m_submitted)
            return false; // stale — dropped
        m_value = std::move(value);
        m_published = generation;
        return true;
    }

    /*! True when a value has been published and no newer job superseded
     *  it. When false and hasValue() is true, value() is stale-but-
     *  renderable (double-buffer behaviour).
     * @return True only for a nonzero published generation equal to the newest submitted one. */
    [[nodiscard]] bool upToDate() const { return m_published != 0 && m_published == m_submitted; }

    /** @brief Test whether any nonzero generation has been published.
     * @return True even when a newer submitted job makes the retained value stale. */
    [[nodiscard]] bool hasValue() const { return m_published != 0; }
    /** @brief Test whether the newest generation lacks a matching publication.
     * @return True after beginJob() or invalidate() until a matching publication;
     * this does not prove that a worker is actually running. */
    [[nodiscard]] bool jobPending() const { return m_submitted != 0 && m_published != m_submitted; }

    /** @brief Inspect the retained product without copying it.
     * @return Borrowed value valid for this container's lifetime, replaced by
     * publication/invalidation. It is default-constructed when hasValue() is false. */
    [[nodiscard]] const T &value() const { return m_value; }
    /** @brief Read the newest submitted or invalidated generation.
     * @return Current token, initially zero; inspection does not start a job. */
    [[nodiscard]] quint64 currentGeneration() const { return m_submitted; }

private:
    T m_value{};
    quint64 m_submitted = 0; ///< newest generation handed to a worker
    quint64 m_published = 0; ///< generation of m_value (0 = none)
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_QSG2DASYNCRESULT_H
