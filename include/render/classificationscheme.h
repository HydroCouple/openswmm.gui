/**
 * @file      classificationscheme.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice US.1 (UNIFIED_STYLING_AND_UI_CONSISTENCY_PLAN.md S1) — the
 *         shared classification value type embedded in style bags.
 *
 *         A ClassificationScheme answers one question for every classified
 *         visual (1D graduated symbols, 2D depth fill, contour bands,
 *         isolines, mesh elevation fill): "given a data range and an
 *         optional sample of values, what are the class edges and what
 *         colour does each class get?"
 *
 *         It composes the existing primitives rather than replacing them:
 *           - IntervalBinner   — break computation (EqualInterval, Quantile,
 *                                Manual, NaturalBreaks, StdDev, Log, Exp)
 *           - RasterColorRamp  — named built-in / two-colour gradient source
 *           - RangeMode        — animation range behaviour (P2 spine)
 *
 *         `levelEdges()` is the canonical replacement for the
 *         Contour::evenlySpacedLevels* helpers at the QSG renderer call
 *         sites: it returns N+1 ascending edges INCLUSIVE of both range
 *         endpoints (N bands need N+1 edges; isoline passes draw the
 *         interior edges via `interiorLevels()`).
 *
 *         `revision()` is a content-change stamp for QSG cache keys: every
 *         mutating setter restamps from a process-global counter, so two
 *         schemes with different content virtually never share a revision,
 *         while copies of an unchanged scheme do (cache stays warm).
 *
 *         Value type (no QObject) — style bags store it by value and emit
 *         their own styleChanged() when it is replaced.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_CLASSIFICATIONSCHEME_H
#define OPENSWMM_RENDER_CLASSIFICATIONSCHEME_H

#include "render/attributesource.h"
#include "render/colorramp.h"
#include "render/intervalbinner.h"
#include "render/legendsymbolitem.h"

#include <QColor>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QMetaType>
#include <QPair>
#include <QString>
#include <QVector>

namespace OpenSWMM::Render
{

/*!
 * \class ClassificationScheme
 * \brief Shared classification model: mode + binner + ramp + range +
 *        per-class overrides, with a revision stamp for render caches.
 */
class ClassificationScheme
{
public:
    /*! \enum ClassMode
     *  \brief Continuous = smooth ramp sampling (no discrete classes);
     *         Classified = discrete bands from the embedded binner. */
    enum class ClassMode : int {
        Continuous = 0,
        Classified = 1,
    };

    /*! \enum LabelFormat
     *  \brief How numeric class-edge values are rendered in legend / editor
     *         labels — fixed decimal places (GIS "precision") or significant
     *         figures. Mirrors standard GIS classification-label controls. */
    enum class LabelFormat : int {
        Decimals = 0,           //!< QString::number(v, 'f', labelPrecision)
        SignificantFigures = 1, //!< QString::number(v, 'g', labelPrecision)
    };

    /** @brief Construct five equal-interval classes with a viridis ramp.
     * Uses automatic bounds, a fixed-over-run range mode, significant-figure
     * labels with precision three, no overrides, and revision zero. */
    ClassificationScheme();

    // ── Mode ───────────────────────────────────────────────────────────
    /** @brief Read the requested continuous or discrete rendering mode.
     * @return Stored mode; computations such as levelEdges() remain available in either mode. */
    [[nodiscard]] ClassMode mode() const { return m_mode; }
    /** @brief Change the rendering mode and revision when the value differs.
     * @param m Mode to store; the underlying enum value is not validated. */
    void setMode(ClassMode m);

    // ── Classification (embedded binner) ───────────────────────────────
    /** @brief Inspect the embedded break configuration.
     * @return Borrowed reference valid for this scheme's lifetime; later setters can change it. */
    [[nodiscard]] const IntervalBinner &binner() const { return m_binner; }
    /** @brief Copy a complete binner and restamp if its configuration differs.
     * @param b Binner to copy, including method, count, and manual breaks.
     * Does not apply setManualBreaks() validation; use validationError() before rendering. */
    void setBinner(const IntervalBinner &b);

    /** @brief Read the embedded break algorithm.
     * @return Configured method, independent of mode(). */
    [[nodiscard]] BinMethod method() const { return m_binner.method(); }
    /** @brief Change the break algorithm, retaining its count and manual breaks.
     * @param m Algorithm to store without enum validation; a change restamps the revision. */
    void setMethod(BinMethod m);

    /** @brief Read the requested number of classes.
     * @return Binner count; Manual classification can produce a different actual count. */
    [[nodiscard]] int classCount() const { return m_binner.binCount(); }
    /** @brief Set a class count with a minimum of one.
     * @param n Requested count, clamped to at least one with no upper limit.
     * Restamps only when the clamped count changes. */
    void setClassCount(int n);

    /** @brief Inspect stored interior manual boundaries in data units.
     * @return Borrowed vector valid for the scheme's lifetime; setters can replace its contents. */
    [[nodiscard]] const QVector<double> &manualBreaks() const { return m_binner.manualBreaks(); }
    /** @brief Replace manual breaks only when the entire sequence is valid.
     * @param breaks Finite, strictly increasing boundaries in the classified quantity's units.
     * Empty is legal. Invalid input is ignored without sorting, partial replacement,
     * or a revision change. Equal input is also a no-op; classCount() is retained. */
    void setManualBreaks(QVector<double> breaks);

    // ── Colour source ──────────────────────────────────────────────────
    /** @brief Read the ramp's built-in key or custom display label.
     * @return Stored text verbatim; empty selects the two-color fallback when no usable custom ramp exists. */
    [[nodiscard]] QString rampName() const { return m_rampName; }
    /** @brief Change the ramp label and discard an existing custom payload.
     * @param name Built-in key, custom label, or empty text; no trimming or validation occurs.
     * An exactly equal label is a no-op that preserves the custom payload. */
    void setRampName(const QString &name);

    /** @brief Read whether sampling reverses the normalized ramp position.
     * @return True when sampling uses one minus the requested position. */
    [[nodiscard]] bool invertRamp() const { return m_invertRamp; }
    /** @brief Change ramp sampling direction and restamp on change.
     * @param on True to reverse sampling; stored ramp stops remain unchanged. */
    void setInvertRamp(bool on);

    /** @brief Read the low endpoint of the fallback gradient.
     * @return Stored color, used when no usable custom or named ramp is selected. */
    [[nodiscard]] QColor lowColor() const { return m_lowColor; }
    /** @brief Change the fallback gradient's low endpoint and restamp on change.
     * @param c Color to copy; invalid colors are accepted without validation. */
    void setLowColor(const QColor &c);
    /** @brief Read the high endpoint of the fallback gradient.
     * @return Stored color, independent of the inversion flag. */
    [[nodiscard]] QColor highColor() const { return m_highColor; }
    /** @brief Change the fallback gradient's high endpoint and restamp on change.
     * @param c Color to copy; invalid colors are accepted without validation. */
    void setHighColor(const QColor &c);

    /*! User-authored (custom) ramp payload. `builtin(name)` only knows the
     *  built-in catalogue and silently degrades unknown names to grayscale,
     *  so custom ramps must travel WITH the scheme: the editor stores the
     *  full ramp here (stops + interp) alongside its display label in
     *  rampName(). Serialized into the scheme JSON so `.oswp` projects stay
     *  portable across machines and survive custom-ramp deletion/rename. */
    /** @return True when a custom payload is stored, even if it has no stops. */
    [[nodiscard]] bool hasCustomRamp() const { return m_hasCustomRamp; }
    /** @brief Inspect the embedded custom payload without resolving a fallback.
     * @return Borrowed ramp valid for the scheme's lifetime; meaningful when hasCustomRamp() is true. */
    [[nodiscard]] const RasterColorRamp &customRamp() const { return m_customRamp; }
    /** @brief Copy a custom ramp and its portable display label.
     * @param ramp Payload to copy without stop validation or normalization.
     * @param name Label stored verbatim in rampName(), including empty text.
     * Restamps unless an existing custom payload matches its stops, interpolation,
     * clamp flags, and label. Differences only in ramp min/max are ignored by this
     * equality check and therefore do not replace an otherwise equal payload. */
    void setCustomRamp(const RasterColorRamp &ramp, const QString &name);
    /** @brief Discard a stored custom ramp, retaining its name for built-in lookup.
     * Restamps only if a custom payload was present. */
    void clearCustomRamp();

    /*! The ramp actually sampled: the embedded custom ramp when present,
     *  else builtin(rampName) when named, otherwise a two-stop
     *  lowColor→highColor ramp. Inversion is NOT baked in —
     *  colorAtF / colorForClass apply it.
     * @return Owned ramp value. An empty custom payload falls through to the
     * named/two-color fallback; unknown built-in names use grayscale. */
    [[nodiscard]] RasterColorRamp resolvedRamp() const;

    // ── Range ──────────────────────────────────────────────────────────
    /** @brief Read whether explicit range endpoints take precedence.
     * @return True to use rangeMin()/rangeMax(), including invalid stored bounds. */
    [[nodiscard]] bool useCustomRange() const { return m_useCustomRange; }
    /** @brief Choose explicit or caller-provided bounds and restamp on change.
     * @param on True to use explicit bounds; no validation is performed here. */
    void setUseCustomRange(bool on);
    /** @brief Read the explicit lower bound.
     * @return Stored value in the classified quantity's units. */
    [[nodiscard]] double rangeMin() const { return m_rangeMin; }
    /** @brief Store an explicit lower bound and restamp when exact equality fails.
     * @param v Lower bound in data units; no finiteness or ordering check occurs. */
    void setRangeMin(double v);
    /** @brief Read the explicit upper bound.
     * @return Stored value in the classified quantity's units. */
    [[nodiscard]] double rangeMax() const { return m_rangeMax; }
    /** @brief Store an explicit upper bound and restamp when exact equality fails.
     * @param v Upper bound in data units; validate the completed pair with validationError(). */
    void setRangeMax(double v);

    /** @brief Read the range policy used by animation consumers.
     * @return Stored policy; effectiveRange() itself does not aggregate animation frames. */
    [[nodiscard]] RangeMode rangeMode() const { return m_rangeMode; }
    /** @brief Store an animation range policy and restamp on change.
     * @param m Policy to store without enum validation. */
    void setRangeMode(RangeMode m);

    // ── Label number format (Decimals vs significant figures) ──────────
    /** @brief Read the numeric label format.
     * @return Fixed-decimal or significant-figure selection. */
    [[nodiscard]] LabelFormat labelFormat() const { return m_labelFormat; }
    /** @brief Store a label format and restamp on change.
     * @param f Format to store; any value other than Decimals uses significant figures in formatValue(). */
    void setLabelFormat(LabelFormat f);
    /** @brief Read the stored numeric precision.
     * @return Precision in [0,12]; significant-figure formatting uses at least one. */
    [[nodiscard]] int labelPrecision() const { return m_labelPrecision; }
    /** @brief Set numeric precision, clamping it before comparing and restamping.
     * @param digits Decimal places or significant figures, clamped to [0,12]. */
    void setLabelPrecision(int digits);

    /*! Format a numeric value as a label per labelFormat()/labelPrecision().
     *  'f' (fixed decimals) for Decimals, 'g' (significant figures) for
     *  SignificantFigures.
     * @param v Number to format in its existing units, without conversion or suffix.
     * @return QString::number output using a decimal point, with precision clamped
     * to at least one for significant figures. Nonfinite values are passed through. */
    [[nodiscard]] QString formatValue(double v) const;

    /** @brief Validate completed bounds and the applicable break configuration.
     * @param dataMin Automatic lower bound in data units; ignored for custom ranges.
     * @param dataMax Automatic upper bound in the same units; ignored for custom ranges.
     * @return Empty on success; otherwise the first translated error for nonfinite
     * bounds, reversed/constant explicit bounds, nonpositive classified log bounds,
     * or invalid manual breaks. Constant automatic bounds are legal. Does not alter
     * the scheme or validate enum values, colors, sample data, or ramp stops. */
    [[nodiscard]] QString validationError(double dataMin, double dataMax) const;

    /** @brief Choose stored custom bounds or the caller's data bounds verbatim.
     * @param dataMin Automatic lower bound in data units.
     * @param dataMax Automatic upper bound in the same units.
     * @return Custom pair when enabled, otherwise (dataMin,dataMax), with no
     * finiteness/order check and no fallback for invalid custom bounds. */
    [[nodiscard]] QPair<double, double> effectiveRange(double dataMin, double dataMax) const;

    // ── Per-class overrides ────────────────────────────────────────────
    /** @brief Read a sparse class-color override without checking the current count.
     * @param classIndex Integer key, including dormant indices outside the active classes.
     * @return Stored color or an invalid QColor when absent. */
    [[nodiscard]] QColor colorOverride(int classIndex) const; /*!< invalid QColor = none */
    /** @brief Store or remove a sparse class-color override and restamp on change.
     * @param classIndex Integer key; no range check occurs and negative keys are accepted.
     * @param c Valid color to copy; an invalid color clears this key.
     * Overrides survive reductions in class count and become visible again if used later. */
    void setColorOverride(int classIndex, const QColor &c);
    /** @brief Remove a color override and restamp only when the key existed.
     * @param classIndex Integer key to erase, regardless of current class count. */
    void clearColorOverride(int classIndex);
    /** @brief Read a sparse user label without checking the current count.
     * @param classIndex Integer key to look up.
     * @return Stored text, or empty text when absent. */
    [[nodiscard]] QString labelOverride(int classIndex) const; /*!< empty = none */
    /** @brief Store or remove a user label and restamp on change.
     * @param classIndex Integer key, accepted without range validation.
     * @param label Text copied verbatim; empty clears the key and whitespace is retained. */
    void setLabelOverride(int classIndex, const QString &label);
    /** @brief Remove a label override and restamp only when the key existed.
     * @param classIndex Integer key to erase, including dormant keys. */
    void clearLabelOverride(int classIndex);
    /** @brief Clear both override maps, restamping only if either was nonempty. */
    void clearOverrides();
    /** @brief Inspect all color overrides, including dormant or negative keys.
     * @return Borrowed hash valid for this scheme's lifetime; setters can mutate its contents. */
    [[nodiscard]] const QHash<int, QColor> &colorOverrides() const { return m_colorOverrides; }
    /** @brief Inspect all user-label overrides.
     * @return Borrowed hash valid for this scheme's lifetime; setters can mutate its contents. */
    [[nodiscard]] const QHash<int, QString> &labelOverrides() const { return m_labelOverrides; }

    // ── Computation ────────────────────────────────────────────────────
    /*!
     * \brief Class edges over the effective range — classCount()+1 (for
     *        Manual: breaks-in-range + 2) ascending values INCLUSIVE of
     *        both endpoints. Empty when validation fails or the range is degenerate.
     *
     *        Range-driven methods (EqualInterval, Logarithmic,
     *        Exponential, Manual) compute over (lo, hi) directly — for
     *        EqualInterval this reproduces evenlySpacedLevelsInclusive
     *        exactly, so default-styled output is unchanged. Data-driven
     *        methods (Quantile, NaturalBreaks, StdDev) classify the
     *        in-range finite samples; with no samples they classify the two
     *        endpoints. Empty computed breaks fall back to equal spacing.
     *        All computed breaks are clamped into [lo, hi]; duplicate edges
     *        can remain and produce zero-width bands.
     * @param dataMin Automatic lower bound in data units; ignored for custom ranges.
     * @param dataMax Automatic upper bound in the same units; ignored for custom ranges.
     * @param samples Optional data values in the same units. Only finite values
     * within the effective range feed data-driven algorithms; range-only methods ignore them.
     * @return Owned edge vector. Manual mode inserts only stored breaks strictly
     * inside the range, so its length can differ from classCount()+1.
     */
    [[nodiscard]] QVector<double> levelEdges(double dataMin, double dataMax,
                                             const QVector<double> &samples = {}) const;

    /** @brief Compute the isoline levels by removing the two outer edges.
     * @param dataMin Automatic lower bound in data units.
     * @param dataMax Automatic upper bound in the same units.
     * @param samples Optional data samples, with the filtering rules of levelEdges().
     * @return Interior edges, retaining duplicates; empty for invalid/constant
     * ranges or when the edge vector contains at most its two endpoints. */
    [[nodiscard]] QVector<double> interiorLevels(double dataMin, double dataMax,
                                                 const QVector<double> &samples = {}) const;

    /*! 0-based class index for a value, given edges from levelEdges().
     *  Below-minimum or non-finite values return -1 (hidden).
     *  Empty edges (a constant field) return 0 for finite values.
     *  Values above the maximum belong to the final class.
     * @param value Scalar in the edges' units; nonfinite values are hidden.
     * @param edges Finite, nondecreasing boundaries, normally from levelEdges().
     * @return -1 for nonfinite/below-range values, otherwise an index in
     * [0,edges.size()-2]. Vectors with fewer than two entries return zero
     * for any finite value. Equality to an interior edge selects the upper class. */
    [[nodiscard]] static int classIndexFor(double value, const QVector<double> &edges);

    /** @brief Sample the resolved ramp with clamping and optional inversion.
     * @param f Finite, dimensionless ramp position; values outside [0,1] are clamped.
     * @return Sampled color after applying invertRamp(). */
    [[nodiscard]] QColor colorAtF(double f) const;

    /** @brief Sample a discrete class using the configured class count.
     * @param classIndex Class key; negative values produce transparent.
     * @return Override when present, otherwise a ramp midpoint color with the
     * index clamped to the active count. Delegates to the explicit-count overload. */
    [[nodiscard]] QColor colorForClass(int classIndex) const;
    /** @brief Sample a discrete class using an explicit actual band count.
     * @param classIndex Override key and sampling index; negative returns transparent.
     * @param count Actual band count, clamped to at least one for ramp sampling.
     * @return Exact color override when present, even above count; otherwise the
     * ramp color at (clampedIndex+0.5)/count with inversion applied. */
    [[nodiscard]] QColor colorForClass(int classIndex, int count) const;

    /** @brief Map a raw scalar to the continuous ramp over the effective bounds.
     * @param value Data value in the bounds' units; nonfinite or below-minimum values are transparent.
     * @param dataMin Automatic finite lower bound, ignored when custom bounds are enabled.
     * @param dataMax Automatic finite upper bound in the same units, ignored for custom bounds.
     * @return Sampled color, clamped at the high end; a nonincreasing range samples
     * normalized position zero for visible values. Does not call validationError();
     * callers must supply finite effective bounds. Class overrides and mode() are ignored. */
    [[nodiscard]] QColor colorForValue(double value, double dataMin, double dataMax) const;

    /*!
     * \brief One legend row per class — value-labelled "lo – hi" (label
     *        and separate user-label overrides), SimpleFill swatch from
     *        colorForClass, classKey = class index. Callers stamp sublayerId / opacity before
     *        returning rows from ISublayer::legendSymbolItems().
     * @param dataMin Automatic lower bound in data units.
     * @param dataMax Automatic upper bound in the same units.
     * @param samples Optional data values used by levelEdges().
     * @return Owned rows in ascending class order. If no edges are available,
     * including a validation failure, returns classCount() rows labelled Class 1,
     * Class 2, etc. User labels populate userLabel separately from the numeric label.
     */
    [[nodiscard]] QList<LegendSymbolItem> legendItems(double dataMin, double dataMax,
                                                      const QVector<double> &samples = {}) const;

    // ── Change tracking ────────────────────────────────────────────────
    /** @brief Read the cache revision, preserved by copying the scheme.
     * @return Zero for a default-constructed, unmodified scheme; otherwise a
     * process-global atomic stamp assigned when configuration changes or JSON is
     * loaded. No-op setters retain it. The counter can eventually wrap. */
    [[nodiscard]] quint64 revision() const { return m_revision; }

    // ── Persistence ────────────────────────────────────────────────────
    /** @brief Serialize configuration, custom payload, and sparse overrides.
     * @return Owned JSON object with colors as ARGB strings; revision is omitted.
     * Values are serialized without first validating bounds or enum selections. */
    [[nodiscard]] QJsonObject toJson() const;
    /** @brief Load configuration with defaults for absent or mistyped properties.
     * @param j Scheme JSON; unknown fields are ignored. Color strings must parse,
     * precision is clamped to [0,12], and override keys must be nonnegative integers.
     * @return Newly stamped value. Custom ramps are retained only if they contain
     * stops. Range bounds are accepted without ordering checks; binner loading
     * follows IntervalBinner::fromJson(). Use validationError() before rendering. */
    static ClassificationScheme fromJson(const QJsonObject &j);

    /** @brief Compare configuration, excluding revision stamps.
     * @param o Scheme to compare without modification.
     * @return True for equal configuration, using exact scalar/color/map equality.
     * Custom-ramp min/max are ignored; its stops, interpolation and clamp flags
     * matter only when a custom payload is present. NaN bounds compare unequal. */
    [[nodiscard]] bool operator==(const ClassificationScheme &o) const;
    /** @brief Test whether stored configurations differ.
     * @param o Scheme to compare without modification.
     * @return Logical negation of operator==(), with the same revision exclusions. */
    [[nodiscard]] bool operator!=(const ClassificationScheme &o) const { return !(*this == o); }

private:
    /** @brief Assign the next process-global atomic cache stamp.
     * Uses relaxed ordering; this does not synchronize access to scheme fields. */
    void bump();

    ClassMode m_mode = ClassMode::Classified;
    IntervalBinner m_binner; // EqualInterval, 5 bins
    QString m_rampName = QStringLiteral("viridis");
    RasterColorRamp m_customRamp; // valid only when m_hasCustomRamp
    bool m_hasCustomRamp = false;
    bool m_invertRamp = false;
    QColor m_lowColor = QColor(Qt::white);
    QColor m_highColor = QColor(Qt::black);
    bool m_useCustomRange = false;
    double m_rangeMin = 0.0;
    double m_rangeMax = 1.0;
    RangeMode m_rangeMode = RangeMode::FixedOverRun;
    // Default reproduces the historic legend formatting (`'g', 3`).
    LabelFormat m_labelFormat = LabelFormat::SignificantFigures;
    int m_labelPrecision = 3;
    QHash<int, QColor> m_colorOverrides;
    QHash<int, QString> m_labelOverrides;
    quint64 m_revision = 0;
};

} // namespace OpenSWMM::Render

Q_DECLARE_METATYPE(OpenSWMM::Render::ClassificationScheme)

#endif // OPENSWMM_RENDER_CLASSIFICATIONSCHEME_H
