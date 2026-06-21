/*!
 * \file   classificationscheme.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice US.1 (UNIFIED_STYLING_AND_UI_CONSISTENCY_PLAN.md S1) — the
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
    enum class ClassMode : int
    {
        Continuous = 0,
        Classified = 1,
    };

    /*! \enum LabelFormat
     *  \brief How numeric class-edge values are rendered in legend / editor
     *         labels — fixed decimal places (GIS "precision") or significant
     *         figures. Mirrors standard GIS classification-label controls. */
    enum class LabelFormat : int
    {
        Decimals          = 0,   //!< QString::number(v, 'f', labelPrecision)
        SignificantFigures = 1,  //!< QString::number(v, 'g', labelPrecision)
    };

    ClassificationScheme();

    // ── Mode ───────────────────────────────────────────────────────────
    [[nodiscard]] ClassMode mode() const { return m_mode; }
    void setMode(ClassMode m);

    // ── Classification (embedded binner) ───────────────────────────────
    [[nodiscard]] const IntervalBinner &binner() const { return m_binner; }
    void setBinner(const IntervalBinner &b);

    [[nodiscard]] BinMethod method() const { return m_binner.method(); }
    void setMethod(BinMethod m);

    [[nodiscard]] int classCount() const { return m_binner.binCount(); }
    void setClassCount(int n);

    [[nodiscard]] const QVector<double> &manualBreaks() const { return m_binner.manualBreaks(); }
    void setManualBreaks(QVector<double> breaks);

    // ── Colour source ──────────────────────────────────────────────────
    /*! Named built-in ramp (RasterColorRamp::builtin). Empty = use the
     *  two-colour lowColor→highColor gradient. */
    [[nodiscard]] QString rampName() const { return m_rampName; }
    void setRampName(const QString &name);

    [[nodiscard]] bool invertRamp() const { return m_invertRamp; }
    void setInvertRamp(bool on);

    /*! Two-colour gradient endpoints, used only when rampName is empty.
     *  Subsumes the bags' shallow/deep + low/high colour pairs. */
    [[nodiscard]] QColor lowColor() const { return m_lowColor; }
    void setLowColor(const QColor &c);
    [[nodiscard]] QColor highColor() const { return m_highColor; }
    void setHighColor(const QColor &c);

    /*! The ramp actually sampled: builtin(rampName) when named, otherwise
     *  a two-stop lowColor→highColor ramp. Inversion is NOT baked in —
     *  colorAtF / colorForClass apply it. */
    [[nodiscard]] RasterColorRamp resolvedRamp() const;

    // ── Range ──────────────────────────────────────────────────────────
    [[nodiscard]] bool useCustomRange() const { return m_useCustomRange; }
    void setUseCustomRange(bool on);
    [[nodiscard]] double rangeMin() const { return m_rangeMin; }
    void setRangeMin(double v);
    [[nodiscard]] double rangeMax() const { return m_rangeMax; }
    void setRangeMax(double v);

    [[nodiscard]] RangeMode rangeMode() const { return m_rangeMode; }
    void setRangeMode(RangeMode m);

    // ── Label number format (Decimals vs significant figures) ──────────
    [[nodiscard]] LabelFormat labelFormat() const { return m_labelFormat; }
    void setLabelFormat(LabelFormat f);
    [[nodiscard]] int labelPrecision() const { return m_labelPrecision; }
    void setLabelPrecision(int digits);

    /*! Format a numeric value as a label per labelFormat()/labelPrecision().
     *  'f' (fixed decimals) for Decimals, 'g' (significant figures) for
     *  SignificantFigures. */
    [[nodiscard]] QString formatValue(double v) const;

    /*! (lo, hi) the scheme classifies over: the custom range when enabled
     *  and non-degenerate, otherwise (dataMin, dataMax). */
    [[nodiscard]] QPair<double, double> effectiveRange(double dataMin, double dataMax) const;

    // ── Per-class overrides ────────────────────────────────────────────
    /*! Sparse user overrides keyed by class index. Dormant when the class
     *  count drops below the index; re-emerge if it grows again (matches
     *  GraduatedRenderer's override semantics). */
    [[nodiscard]] QColor colorOverride(int classIndex) const;          /*!< invalid QColor = none */
    void setColorOverride(int classIndex, const QColor &c);
    void clearColorOverride(int classIndex);
    [[nodiscard]] QString labelOverride(int classIndex) const;         /*!< empty = none */
    void setLabelOverride(int classIndex, const QString &label);
    void clearLabelOverride(int classIndex);
    void clearOverrides();
    [[nodiscard]] const QHash<int, QColor>  &colorOverrides() const { return m_colorOverrides; }
    [[nodiscard]] const QHash<int, QString> &labelOverrides() const { return m_labelOverrides; }

    // ── Computation ────────────────────────────────────────────────────
    /*!
     * \brief Class edges over the effective range — classCount()+1 (for
     *        Manual: breaks-in-range + 2) ascending values INCLUSIVE of
     *        both endpoints. Empty when the range is degenerate.
     *
     *        Range-driven methods (EqualInterval, Logarithmic,
     *        Exponential, Manual) compute over (lo, hi) directly — for
     *        EqualInterval this reproduces evenlySpacedLevelsInclusive
     *        exactly, so default-styled output is unchanged. Data-driven
     *        methods (Quantile, NaturalBreaks, StdDev) classify the
     *        in-range finite samples; with no samples they degrade to
     *        equal spacing. All breaks are clamped into [lo, hi].
     */
    [[nodiscard]] QVector<double> levelEdges(double dataMin, double dataMax,
                                             const QVector<double> &samples = {}) const;

    /*! levelEdges minus the two endpoints — the isoline level set. */
    [[nodiscard]] QVector<double> interiorLevels(double dataMin, double dataMax,
                                                 const QVector<double> &samples = {}) const;

    /*! 0-based class index for a value, given edges from levelEdges().
     *  Clamped to [0, edges.size()-2]; 0 when edges are degenerate. */
    [[nodiscard]] static int classIndexFor(double value, const QVector<double> &edges);

    /*! Continuous colour at normalised position f in [0,1] (clamped),
     *  inversion applied. */
    [[nodiscard]] QColor colorAtF(double f) const;

    /*! Discrete class colour: the user override when present, otherwise
     *  the ramp sampled at the class midpoint (i + 0.5) / count. */
    [[nodiscard]] QColor colorForClass(int classIndex) const;
    [[nodiscard]] QColor colorForClass(int classIndex, int count) const;

    /*! Continuous colour for a raw value over the effective range. */
    [[nodiscard]] QColor colorForValue(double value, double dataMin, double dataMax) const;

    /*!
     * \brief One legend row per class — value-labelled "lo – hi" (label
     *        overrides win), SimpleFill swatch from colorForClass, classKey
     *        = class index. Callers stamp sublayerId / opacity before
     *        returning rows from ISublayer::legendSymbolItems().
     */
    [[nodiscard]] QList<LegendSymbolItem> legendItems(double dataMin, double dataMax,
                                                      const QVector<double> &samples = {}) const;

    // ── Change tracking ────────────────────────────────────────────────
    /*! Content-change stamp for render-cache keys. Restamped from a
     *  process-global counter on every mutating setter; equal content
     *  copies share it, distinct mutations never do. */
    [[nodiscard]] quint64 revision() const { return m_revision; }

    // ── Persistence ────────────────────────────────────────────────────
    [[nodiscard]] QJsonObject toJson() const;
    static ClassificationScheme fromJson(const QJsonObject &j);

    [[nodiscard]] bool operator==(const ClassificationScheme &o) const;
    [[nodiscard]] bool operator!=(const ClassificationScheme &o) const { return !(*this == o); }

private:
    void bump();

    ClassMode        m_mode = ClassMode::Classified;
    IntervalBinner   m_binner;                       // EqualInterval, 5 bins
    QString          m_rampName = QStringLiteral("viridis");
    bool             m_invertRamp = false;
    QColor           m_lowColor  = QColor(Qt::white);
    QColor           m_highColor = QColor(Qt::black);
    bool             m_useCustomRange = false;
    double           m_rangeMin = 0.0;
    double           m_rangeMax = 1.0;
    RangeMode        m_rangeMode = RangeMode::FixedOverRun;
    // Default reproduces the historic legend formatting (`'g', 3`).
    LabelFormat      m_labelFormat    = LabelFormat::SignificantFigures;
    int              m_labelPrecision = 3;
    QHash<int, QColor>  m_colorOverrides;
    QHash<int, QString> m_labelOverrides;
    quint64          m_revision = 0;
};

} // namespace OpenSWMM::Render

Q_DECLARE_METATYPE(OpenSWMM::Render::ClassificationScheme)

#endif // OPENSWMM_RENDER_CLASSIFICATIONSCHEME_H
