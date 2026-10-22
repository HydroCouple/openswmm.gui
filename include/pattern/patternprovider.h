/**
 * @file      patternprovider.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BQ Phase 6.7.2 — MVC model for a single SWMM time pattern.
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_PATTERN_PATTERNPROVIDER_H
#define OPENSWMMVIS_PATTERN_PATTERNPROVIDER_H

#include <QObject>
#include <QString>
#include <QVector>

namespace openswmmvis::pattern
{

/*! \brief Pattern shape — fixed factor count per type.
 *  Numeric values intentionally match the engine's `PatternType` enum
 *  (MONTHLY=0, DAILY=1, HOURLY=2, WEEKEND=3) so casts to/from `int` are safe. */
enum class PatternType {
    Monthly = 0, ///< 12 factors, one per calendar month.
    Daily = 1,   ///<  7 factors, one per day of week (Sun..Sat).
    Hourly = 2,  ///< 24 factors, one per hour of day (weekday).
    Weekend = 3, ///< 24 factors, one per hour of day (weekend).
};

class PatternProvider : public QObject
{
    Q_OBJECT

public:
    /*! \brief Construct an empty pattern of the given type. All factors
     *  start at 1.0 (the SWMM "no effect" identity value).
     * @param[in] name Initial identifier; registry enforces project uniqueness.
     * @param[in] type Supported pattern shape determining the initial factor count.
     * @param[in] parent Optional QObject owner.
     * @details Call only on the QObject owning thread; factors are local until registry mirroring/save.
     */
    PatternProvider(QString name, PatternType type, QObject *parent = nullptr);
    /**
     * @brief Destroy the local factor storage and QObject connections.
     */
    ~PatternProvider() override;

    // ── Identity / metadata ─────────────────────────────────────────────────

    /*! \brief Pattern ID. Uniqueness within a project is owned by the registry.
     * @return Copied identifier, without normalization.
     */
    QString name() const noexcept { return m_name; }

    /*! \brief Pattern shape (Monthly / Daily / Hourly / Weekend).
     * @return Current pattern shape.
     */
    PatternType type() const noexcept { return m_type; }

    /*! \brief How many factors does this type need? Static helper.
     * @param[in] t Supported pattern shape.
     * @return 12 monthly, 7 daily, 24 hourly/weekend, or zero for an unsupported enum value.
     */
    static int factorCountFor(PatternType t) noexcept;

    /*! \brief Convenience: factorCountFor(type()).
     * @return Required factor count for the current type.
     */
    int factorCount() const noexcept { return factorCountFor(m_type); }

    /*! \brief Human-readable label for a factor row index (0..factorCount-1).
     *  Returns "Jan".."Dec" / "Sun".."Sat" / "00:00".."23:00" / "00:00".."23:00".
     *  Static so UI code can label headers without holding a provider.
     * @param[in] t Pattern shape selecting calendar/time labels.
     * @param[in] i Zero-based factor index.
     * @return Translated month/day abbreviation or hour label; empty for an invalid type/index.
     */
    static QString rowLabel(PatternType t, int i);

    // ── Factor access ───────────────────────────────────────────────────────

    /*! \brief Read a single factor by index. Caller must ensure `0 ≤ i < factorCount()`.
     * @param[in] i In-range zero-based factor index; bounds are the caller responsibility.
     * @return Dimensionless multiplier at i.
     */
    double factor(int i) const { return m_factors.at(i); }

    /*! \brief All factors (const ref). Size is always factorCount().
     * @return Borrowed factor vector, invalidated by replacement/type changes or destruction; do not retain across edits.
     */
    const QVector<double> &factors() const noexcept { return m_factors; }

    /*! \brief Sum of all factors. Used by the UI status strip + normalize.
     * @return Arithmetic sum of dimensionless factors; no normalization or finiteness filtering occurs.
     */
    double sumOfFactors() const noexcept;

    // ── Mutators (return false + emit mutationRejected on invariant violation) ──

    /*! \brief Replace one factor. Validates `0 ≤ i < factorCount()` and
     *  `v >= 0` (negative multipliers are nonsensical for SWMM patterns).
     *  Emits `factorChanged(i)` on success.
     * @param[in] i Zero-based factor index.
     * @param[in] v Dimensionless nonnegative multiplier; finiteness is not checked independently.
     * @param[out] reasonOut Optional diagnostic assigned on rejection, unchanged on success.
     * @return True for a change or equal-value no-op; false with mutationRejected() for invalid index or negative value.
     */
    bool setFactor(int i, double v, QString *reasonOut = nullptr);

    /*! \brief Live variant of setFactor for drag-edit: does NOT push undo
     *  per-frame (caller handles batching).
     * @param[in] i Zero-based factor index.
     * @param[in] v Live drag value, clamped to zero when negative; no independent finiteness check.
     * @return False for an invalid index without mutationRejected(); true for change/equal-value no-op.
     * @details Emits factorChanged() only for a changed value; pushes no undo command.
     */
    bool setFactorLive(int i, double v);

    /*! \brief Replace the entire factor array. Size of \p f must equal
     *  factorCount(); each value must be `>= 0`. Atomic: either applied or rejected.
     * @param[in] f Complete dimensionless array, exactly factorCount() entries; negative values reject the whole replacement.
     * @param[out] reasonOut Optional rejection diagnostic, unchanged on success.
     * @return False with mutationRejected() for size/negative-value failure; true after replacing and emitting factorsChanged() even for an identical array.
     * @details No independent NaN/infinity rejection occurs here.
     */
    bool setAllFactors(QVector<double> f, QString *reasonOut = nullptr);

    /*! \brief Rescale every factor so their sum equals \p targetSum.
     *  No-op (returns false) if the current sum is zero (would divide by
     *  zero). Emits `factorsChanged()` on success.
     * @param[in] targetSum Positive desired sum; callers must supply a finite meaningful target.
     * @param[out] reasonOut Optional rejection diagnostic, unchanged on success.
     * @return False with mutationRejected() when current sum or target compares nonpositive; true after multiplying all entries and emitting factorsChanged().
     * @details Does not independently reject nonfinite factors, sums, or targets.
     */
    bool normalize(double targetSum = 1.0, QString *reasonOut = nullptr);

    /*! \brief Atomically swap the factor values at indices \p i and \p j.
     *  Used by the chart view's slot-reorder drag (horizontal drag of a
     *  vertex onto an adjacent slot). Emits a single `factorsChanged()`.
     *  Returns false (and emits `mutationRejected`) on out-of-range indices.
     * @param[in] i First zero-based index.
     * @param[in] j Second zero-based index.
     * @param[out] reasonOut Optional rejection diagnostic, unchanged on success.
     * @return True without notification when i==j; true with factorsChanged() for a valid swap; false for an invalid index.
     */
    bool swapFactors(int i, int j, QString *reasonOut = nullptr);

    /*! \brief Rename the pattern. Uniqueness is the registry's responsibility;
     *  this just stores + notifies.
     * @param[in] newName Name copied verbatim without emptiness/uniqueness validation.
     * @details Identical text is a no-op; use registry rename() for engine/reference synchronization.
     */
    void setName(QString newName);

    /*! \brief Switch pattern type. Resets all factors to 1.0 because the
     *  factor count changes — there's no meaningful mapping from 12 monthly
     *  factors to 24 hourly factors. Emits `typeChanged` + `factorsChanged`.
     * @param[in] t New pattern shape.
     * @details Identical type is a no-op; unsupported enum values produce zero factors. Type changes do not directly retype an existing engine pattern.
     */
    void setType(PatternType t);

signals:
    /*! \brief One factor at index \p i had its value changed.
     * @param[in] i Zero-based factor whose new value is already stored.
     */
    void factorChanged(int i);

    /*! \brief All factors were replaced (setAllFactors / setType / normalize).
     *  Subscribers re-read the whole array. */
    void factorsChanged();

    /*! \brief Pattern was renamed; dependent-reference cascading is the
     *  registry's job (subscribers should not rewrite refs from this signal).
     * @param[in] prev Previous identifier.
     * @param[in] now New identifier already stored.
     */
    void nameChanged(QString prev, QString now);

    /*! \brief Pattern type changed (factors reset to 1.0).
     * @param[in] prev Previous shape.
     * @param[in] now New shape whose factors have already reset.
     */
    void typeChanged(PatternType prev, PatternType now);

    /*! \brief A mutator was refused because it would have violated an invariant.
     *  Surfaced to UI so the status bar can show the offending state.
     * @param[in] reason Localized explanation; the rejected mutation did not apply.
     */
    void mutationRejected(QString reason);

private:
    QString m_name;
    PatternType m_type;
    QVector<double> m_factors; ///< Always factorCount() in size; defaults to 1.0.
};

} // namespace openswmmvis::pattern

#endif // OPENSWMMVIS_PATTERN_PATTERNPROVIDER_H
