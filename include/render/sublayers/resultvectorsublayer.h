/**
 * @file      resultvectorsublayer.h
 * @brief     Arrows and streaks for one vector result (an x/y component pair).
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMM_RENDER_RESULTVECTORSUBLAYER_H
#define OPENSWMM_RENDER_RESULTVECTORSUBLAYER_H

#include "io/mesh2dresultvector.h"
#include "render/classificationscheme.h"
#include "render/isublayer.h"
#include "render/sublayerstyle.h"

#include <QColor>
#include <QPointF>
#include <atomic>
#include <vector>

namespace OpenSWMM::Render
{

/*!
 * \class ResultVectorStyle
 * \brief Arrow and streak displays of a vector result. Arrow size and the color of both
 *        displays bind to the vector magnitude, a constant, or a scalar variable of the same
 *        domain (a catalog key).
 */
class ResultVectorStyle : public SublayerStyle
{
    Q_OBJECT
public:
    /// Magnitude and Scalar map the frame's range onto [min, max] length; FixedScale draws
    /// scalePxPerUnit pixels per unit of |v| (clamped to [min, max]), the same in every frame.
    enum class SizeMode { Magnitude = 0, Constant = 1, Scalar = 2, FixedScale = 3 };
    Q_ENUM(SizeMode)
    enum class ColorMode { Constant = 0, Magnitude = 1, Scalar = 2 };
    Q_ENUM(ColorMode)
    enum class LengthScaling { Linear = 0, SquareRoot = 1, Log = 2 };
    Q_ENUM(LengthScaling)

private:
    Q_PROPERTY(QString vectorKey READ vectorKey WRITE setVectorKey NOTIFY styleChanged)
    Q_PROPERTY(bool arrows READ arrows WRITE setArrows NOTIFY styleChanged)
    Q_PROPERTY(bool streaks READ streaks WRITE setStreaks NOTIFY styleChanged)
    Q_PROPERTY(double spacingPx READ spacingPx WRITE setSpacingPx NOTIFY styleChanged)
    Q_PROPERTY(double minLengthPx READ minLengthPx WRITE setMinLengthPx NOTIFY styleChanged)
    Q_PROPERTY(double maxLengthPx READ maxLengthPx WRITE setMaxLengthPx NOTIFY styleChanged)
    Q_PROPERTY(double headSizePx READ headSizePx WRITE setHeadSizePx NOTIFY styleChanged)
    Q_PROPERTY(double shaftWidthPx READ shaftWidthPx WRITE setShaftWidthPx NOTIFY styleChanged)
    Q_PROPERTY(
        LengthScaling lengthScaling READ lengthScaling WRITE setLengthScaling NOTIFY styleChanged)
    Q_PROPERTY(SizeMode sizeMode READ sizeMode WRITE setSizeMode NOTIFY styleChanged)
    Q_PROPERTY(
        double scalePxPerUnit READ scalePxPerUnit WRITE setScalePxPerUnit NOTIFY styleChanged)
    Q_PROPERTY(
        double dryDepthCutoff READ dryDepthCutoff WRITE setDryDepthCutoff NOTIFY styleChanged)
    Q_PROPERTY(QString sizeKey READ sizeKey WRITE setSizeKey NOTIFY styleChanged)
    Q_PROPERTY(bool sizeUseCustomRange READ sizeUseCustomRange WRITE setSizeUseCustomRange NOTIFY
                   styleChanged)
    Q_PROPERTY(double sizeRangeMin READ sizeRangeMin WRITE setSizeRangeMin NOTIFY styleChanged)
    Q_PROPERTY(double sizeRangeMax READ sizeRangeMax WRITE setSizeRangeMax NOTIFY styleChanged)
    Q_PROPERTY(ColorMode colorMode READ colorMode WRITE setColorMode NOTIFY styleChanged)
    Q_PROPERTY(QString colorKey READ colorKey WRITE setColorKey NOTIFY styleChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY styleChanged)
    Q_PROPERTY(double streakLengthPx READ streakLengthPx WRITE setStreakLengthPx NOTIFY styleChanged)
    Q_PROPERTY(double streakWidthPx READ streakWidthPx WRITE setStreakWidthPx NOTIFY styleChanged)
    Q_PROPERTY(
        double streakSpacingPx READ streakSpacingPx WRITE setStreakSpacingPx NOTIFY styleChanged)
    Q_CLASSINFO("group:arrows", "Display")
    Q_CLASSINFO("group:streaks", "Display")
    Q_CLASSINFO("group:spacingPx", "Arrows")
    Q_CLASSINFO("group:minLengthPx", "Arrows")
    Q_CLASSINFO("group:maxLengthPx", "Arrows")
    Q_CLASSINFO("group:headSizePx", "Arrows")
    Q_CLASSINFO("group:shaftWidthPx", "Arrows")
    Q_CLASSINFO("group:lengthScaling", "Size")
    Q_CLASSINFO("group:sizeMode", "Size")
    Q_CLASSINFO("group:scalePxPerUnit", "Size")
    Q_CLASSINFO("group:dryDepthCutoff", "Display")
    Q_CLASSINFO("group:sizeKey", "Size")
    Q_CLASSINFO("group:sizeUseCustomRange", "Size")
    Q_CLASSINFO("group:sizeRangeMin", "Size")
    Q_CLASSINFO("group:sizeRangeMax", "Size")
    Q_CLASSINFO("group:colorMode", "Colour")
    Q_CLASSINFO("group:colorKey", "Colour")
    Q_CLASSINFO("group:color", "Colour")
    Q_CLASSINFO("group:streakLengthPx", "Streaks")
    Q_CLASSINFO("group:streakWidthPx", "Streaks")
    Q_CLASSINFO("group:streakSpacingPx", "Streaks")

public:
    /** @brief Arrows on, streaks off, size and color by magnitude (continuous viridis).
     * @param parent Optional QObject owner. */
    explicit ResultVectorStyle(QObject *parent = nullptr);

    QString vectorKey() const { return m_vectorKey; }
    bool arrows() const { return m_arrows; }
    bool streaks() const { return m_streaks; }
    double spacingPx() const { return m_spacingPx; }
    double minLengthPx() const { return m_minLengthPx; }
    double maxLengthPx() const { return m_maxLengthPx; }
    double headSizePx() const { return m_headSizePx; }
    double shaftWidthPx() const { return m_shaftWidthPx; }
    LengthScaling lengthScaling() const { return m_lengthScaling; }
    SizeMode sizeMode() const { return m_sizeMode; }
    /** @return Pixels per unit of |v| in FixedScale size mode; initially 20 (px per m/s). */
    double scalePxPerUnit() const { return m_scalePxPerUnit; }
    /** @return Surface vectors skip cells shallower than this depth (m); initially 0.01. */
    double dryDepthCutoff() const { return m_dryDepthCutoff; }
    QString sizeKey() const { return m_sizeKey; }
    bool sizeUseCustomRange() const { return m_sizeUseCustomRange; }
    double sizeRangeMin() const { return m_sizeRangeMin; }
    double sizeRangeMax() const { return m_sizeRangeMax; }
    ColorMode colorMode() const { return m_colorMode; }
    QString colorKey() const { return m_colorKey; }
    QColor color() const { return m_color; }
    double streakLengthPx() const { return m_streakLengthPx; }
    double streakWidthPx() const { return m_streakWidthPx; }
    double streakSpacingPx() const { return m_streakSpacingPx; }
    /** @return Color classification (ramp, classes, custom range) for Magnitude/Scalar color. */
    const ClassificationScheme &colorScheme() const { return m_colorScheme; }

    void setVectorKey(const QString &key);
    void setArrows(bool on);
    void setStreaks(bool on);
    void setSpacingPx(double px);
    void setMinLengthPx(double px);
    void setMaxLengthPx(double px);
    void setHeadSizePx(double px);
    void setShaftWidthPx(double px);
    void setLengthScaling(LengthScaling scaling);
    void setSizeMode(SizeMode mode);
    void setScalePxPerUnit(double px);
    void setDryDepthCutoff(double metres);
    void setSizeKey(const QString &key);
    void setSizeUseCustomRange(bool on);
    void setSizeRangeMin(double value);
    void setSizeRangeMax(double value);
    void setColorMode(ColorMode mode);
    void setColorKey(const QString &key);
    void setColor(const QColor &color);
    void setStreakLengthPx(double px);
    void setStreakWidthPx(double px);
    void setStreakSpacingPx(double px);
    void setColorScheme(const ClassificationScheme &scheme);

    /**
     * @param value Bound size value (|v| for Magnitude and FixedScale).
     * @param lo,hi Size range in the bound value's units (unused by Constant and FixedScale).
     * @return Arrow length in pixels within [minLengthPx, maxLengthPx]; maxLengthPx for Constant;
     *   scalePxPerUnit · f(value) for FixedScale (f per length scaling: linear, square root or
     *   log1p, as the former velocity arrows), 0 for a still cell.
     */
    double lengthPxFor(double value, double lo, double hi) const;

    /** @return Every property, the color scheme included. */
    QJsonObject toJson() const override;
    /** @brief Restore from toJson(); missing keys keep current values; one final styleChanged. */
    void fromJson(const QJsonObject &json) override;

private:
    QString m_vectorKey;
    bool m_arrows = true, m_streaks = false;
    double m_spacingPx = 30, m_minLengthPx = 6, m_maxLengthPx = 36, m_headSizePx = 8,
           m_shaftWidthPx = 2;
    LengthScaling m_lengthScaling = LengthScaling::Linear;
    SizeMode m_sizeMode = SizeMode::Magnitude;
    double m_scalePxPerUnit = 20, m_dryDepthCutoff = 0.01;
    QString m_sizeKey;
    bool m_sizeUseCustomRange = false;
    double m_sizeRangeMin = 0, m_sizeRangeMax = 1;
    ColorMode m_colorMode = ColorMode::Magnitude;
    QString m_colorKey;
    QColor m_color{20, 20, 20, 220};
    double m_streakLengthPx = 48, m_streakWidthPx = 1.6, m_streakSpacingPx = 28;
    ClassificationScheme m_colorScheme;
};

/// One arrow in scene coordinates.
struct ResultVectorArrow {
    QPointF tail, tip;
    QColor color;
};
/// One streak polyline in scene coordinates, oldest point first.
struct ResultVectorStreak {
    std::vector<QPointF> points;
    QColor color;
};
/// Display geometry of one vector row for one frame and view.
struct ResultVectorGeometry {
    std::vector<ResultVectorArrow> arrows;
    std::vector<ResultVectorStreak> streaks;
    QString error;
};

/*!
 * \class ResultVectorSublayer
 * \brief One vector result row. The host layer reads the component frames and builds
 *        ResultVectorGeometry; this object owns identity, visibility, opacity and style.
 */
class ResultVectorSublayer : public ISublayer
{
    Q_OBJECT
public:
    /** @param id Stable row ID ("results2d.vector." prefix). @param parent QObject owner. */
    explicit ResultVectorSublayer(QString id, QObject *parent = nullptr);
    Kind kind() const override { return VectorGlyphKind; }
    QString id() const override { return m_id; }
    /** @return Vector label and units, with the displays shown, or an unavailable notice. */
    QString displayName() const override;
    bool isVisible() const override { return m_visible; }
    void setVisible(bool value) override;
    qreal opacity() const override { return m_opacity; }
    void setOpacity(qreal value) override;
    bool isDynamic() const override { return true; }
    SublayerStyle *style() override { return m_style; }
    ResultVectorStyle *vectorStyle() const { return m_style; }
    QString vectorKey() const { return m_style->vectorKey(); }
    const openswmmvis::io::Mesh2DResultVector &descriptor() const { return m_descriptor; }
    /** @brief Replace the source metadata and invalidate. */
    void setDescriptor(const openswmmvis::io::Mesh2DResultVector &descriptor);
    /** @return Legend rows without a frame range (an arrow swatch in the constant color). */
    QList<LegendSymbolItem> legendSymbolItems() const override;
    /**
     * @param colorMin,colorMax Range of the color-bound value for this frame (NaN when unknown).
     * @param colorUnits Units of the color-bound value.
     * @return Arrow/streak swatch rows: one for Constant color, else ramp or class swatches.
     */
    QList<LegendSymbolItem> legendSymbolItems(double colorMin, double colorMax,
                                              const QString &colorUnits) const;
    /** @return Color for a bound value over [lo, hi] with the color scheme (opacity applied). */
    QColor colorFor(double value, double lo, double hi) const;
    /** @return Report time the scene graph last drew this row at, or -1. Atomic, so the GUI
     *   thread may read what the render thread published. */
    int presentedTime() const { return m_presentedTime.load(std::memory_order_acquire); }
    /** @brief Record the report time the scene graph drew this row at (render thread; no
     *   signals). @param time Report time index, or -1 to clear. */
    void publishPresentedTime(int time) { m_presentedTime.store(time, std::memory_order_release); }
    QSGNode *buildOrUpdateNode(QSGNode *existing, const SublayerContext &) override
    {
        return existing;
    }

private:
    QString m_id;
    bool m_visible = true;
    qreal m_opacity = 1;
    ResultVectorStyle *m_style;
    openswmmvis::io::Mesh2DResultVector m_descriptor;
    std::atomic<int> m_presentedTime{-1};
};

} // namespace OpenSWMM::Render
#endif
