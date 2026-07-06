/*!
 * \file   qsg2dasyncresult.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
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
 * Header-only, no Qt dependency beyond <QtGlobal>. Unit-tested in
 * tests/unit/test_qsg2d_dirtystate.cpp (stale-result section).
 */
#ifndef OPENSWMM_RENDER_QSG2DASYNCRESULT_H
#define OPENSWMM_RENDER_QSG2DASYNCRESULT_H

#include <QtGlobal>

#include <utility>

namespace OpenSWMM::Render
{

template <typename T>
class Qsg2DAsyncResult
{
public:
    /*! Start a new job: invalidates any in-flight generation and returns
     *  the token the eventual tryPublish() must present. */
    quint64 beginJob() { return ++m_submitted; }

    /*! Invalidate without starting a job — pending results become stale
     *  and the published value is cleared. */
    void invalidate()
    {
        ++m_submitted;
        m_published = 0;
        m_value     = T{};
    }

    /*! Accept \p value iff \p generation is still the newest job. */
    bool tryPublish(quint64 generation, T &&value)
    {
        if (generation != m_submitted) return false;   // stale — dropped
        m_value     = std::move(value);
        m_published = generation;
        return true;
    }

    /*! True when a value has been published and no newer job superseded
     *  it. When false and hasValue() is true, value() is stale-but-
     *  renderable (double-buffer behaviour). */
    [[nodiscard]] bool upToDate() const
    {
        return m_published != 0 && m_published == m_submitted;
    }

    [[nodiscard]] bool hasValue() const { return m_published != 0; }
    [[nodiscard]] bool jobPending() const
    {
        return m_submitted != 0 && m_published != m_submitted;
    }

    [[nodiscard]] const T &value() const { return m_value; }
    [[nodiscard]] quint64 currentGeneration() const { return m_submitted; }

private:
    T       m_value{};
    quint64 m_submitted = 0;   ///< newest generation handed to a worker
    quint64 m_published = 0;   ///< generation of m_value (0 = none)
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_QSG2DASYNCRESULT_H
