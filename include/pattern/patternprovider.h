/*!
 * \file   patternprovider.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BQ Phase 6.7.2 — MVC model for a single SWMM time pattern.
 *
 * One PatternProvider per `[PATTERNS]` entry in the project. Owned by a
 * project-scoped PatternRegistry. Acts as the **single source of truth** for
 * that pattern: the editor dialog's grid, bar-chart preview, Object Browser
 * property pane, and any DWF inflow picker all subscribe to the provider's
 * Qt signals (per [[feedback_mvc_synchronized_uis]]).
 *
 * Patterns are fixed-size dimensionless multiplier arrays whose count is
 * determined by the pattern type:
 *
 *   - **Monthly** — 12 factors (one per calendar month)
 *   - **Daily**   —  7 factors (one per day of week, Sun … Sat)
 *   - **Hourly**  — 24 factors (one per hour of day, weekday)
 *   - **Weekend** — 24 factors (one per hour of day, weekend)
 *
 * SWMM's legacy convention is `avg = 1.0` (sum = N), but the editor's
 * `normalize()` lets callers rescale to any target sum (default 1.0 per the
 * user-confirmed UX in Slice BQ Phase 6.7.2).
 *
 * Mutations are intended to flow through `QUndoCommand` subclasses; this
 * class is undo-stack agnostic and exposes the raw apply* methods.
 */
#ifndef OPENSWMMVIS_PATTERN_PATTERNPROVIDER_H
#define OPENSWMMVIS_PATTERN_PATTERNPROVIDER_H

#include <QObject>
#include <QString>
#include <QVector>

namespace openswmmvis::pattern {

/*! \brief Pattern shape — fixed factor count per type.
 *  Numeric values intentionally match the engine's `PatternType` enum
 *  (MONTHLY=0, DAILY=1, HOURLY=2, WEEKEND=3) so casts to/from `int` are safe. */
enum class PatternType {
    Monthly = 0,    ///< 12 factors, one per calendar month.
    Daily   = 1,    ///<  7 factors, one per day of week (Sun..Sat).
    Hourly  = 2,    ///< 24 factors, one per hour of day (weekday).
    Weekend = 3,    ///< 24 factors, one per hour of day (weekend).
};

class PatternProvider : public QObject
{
    Q_OBJECT

public:
    /*! \brief Construct an empty pattern of the given type. All factors
     *  start at 1.0 (the SWMM "no effect" identity value). */
    PatternProvider(QString name, PatternType type, QObject *parent = nullptr);
    ~PatternProvider() override;

    // ── Identity / metadata ─────────────────────────────────────────────────

    /*! \brief Pattern ID. Uniqueness within a project is owned by the registry. */
    QString name() const noexcept { return m_name; }

    /*! \brief Pattern shape (Monthly / Daily / Hourly / Weekend). */
    PatternType type() const noexcept { return m_type; }

    /*! \brief How many factors does this type need? Static helper. */
    static int factorCountFor(PatternType t) noexcept;

    /*! \brief Convenience: factorCountFor(type()). */
    int factorCount() const noexcept { return factorCountFor(m_type); }

    /*! \brief Human-readable label for a factor row index (1..factorCount-1).
     *  Returns "Jan".."Dec" / "Sun".."Sat" / "00:00".."23:00" / "00:00".."23:00".
     *  Static so UI code can label headers without holding a provider. */
    static QString rowLabel(PatternType t, int i);

    // ── Factor access ───────────────────────────────────────────────────────

    /*! \brief Read a single factor by index. Caller must ensure `0 ≤ i < factorCount()`. */
    double factor(int i) const { return m_factors.at(i); }

    /*! \brief All factors (const ref). Size is always factorCount(). */
    const QVector<double>& factors() const noexcept { return m_factors; }

    /*! \brief Sum of all factors. Used by the UI status strip + normalize. */
    double sumOfFactors() const noexcept;

    // ── Mutators (return false + emit mutationRejected on invariant violation) ──

    /*! \brief Replace one factor. Validates `0 ≤ i < factorCount()` and
     *  `v >= 0` (negative multipliers are nonsensical for SWMM patterns).
     *  Emits `factorChanged(i)` on success. */
    bool setFactor(int i, double v, QString *reasonOut = nullptr);

    /*! \brief Live variant of setFactor for drag-edit: does NOT push undo
     *  per-frame (caller handles batching). */
    bool setFactorLive(int i, double v);

    /*! \brief Replace the entire factor array. Size of \p f must equal
     *  factorCount(); each value must be `>= 0`. Atomic: either applied or rejected. */
    bool setAllFactors(QVector<double> f, QString *reasonOut = nullptr);

    /*! \brief Rescale every factor so their sum equals \p targetSum.
     *  No-op (returns false) if the current sum is zero (would divide by
     *  zero). Emits `factorsChanged()` on success. */
    bool normalize(double targetSum = 1.0, QString *reasonOut = nullptr);

    /*! \brief Rename the pattern. Uniqueness is the registry's responsibility;
     *  this just stores + notifies. */
    void setName(QString newName);

    /*! \brief Switch pattern type. Resets all factors to 1.0 because the
     *  factor count changes — there's no meaningful mapping from 12 monthly
     *  factors to 24 hourly factors. Emits `typeChanged` + `factorsChanged`. */
    void setType(PatternType t);

signals:
    /*! \brief One factor at index \p i had its value changed. */
    void factorChanged(int i);

    /*! \brief All factors were replaced (setAllFactors / setType / normalize).
     *  Subscribers re-read the whole array. */
    void factorsChanged();

    /*! \brief Pattern was renamed; dependent-reference cascading is the
     *  registry's job (subscribers should not rewrite refs from this signal). */
    void nameChanged(QString prev, QString now);

    /*! \brief Pattern type changed (factors reset to 1.0). */
    void typeChanged(PatternType prev, PatternType now);

    /*! \brief A mutator was refused because it would have violated an invariant.
     *  Surfaced to UI so the status bar can show the offending state. */
    void mutationRejected(QString reason);

private:
    QString          m_name;
    PatternType      m_type;
    QVector<double>  m_factors;     ///< Always factorCount() in size; defaults to 1.0.
};

} // namespace openswmmvis::pattern

#endif // OPENSWMMVIS_PATTERN_PATTERNPROVIDER_H
