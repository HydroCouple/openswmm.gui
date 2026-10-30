/**
 * @file      isolinesublayer.h
 * @brief     Dynamic marching-squares isoline sublayer over a 2D mesh.
 *         Plan §3 — fourth sublayer in the SWMM2DResultsLayer default mix.
 *         Slice S5.4.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMM_RENDER_SUBLAYERS_ISOLINESUBLAYER_H
#define OPENSWMM_RENDER_SUBLAYERS_ISOLINESUBLAYER_H

#include "render/classificationscheme.h"
#include "render/isublayer.h"
#include "render/sublayerstyle.h"

#include <QColor>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <Qt>

#include <vector>

namespace OpenSWMM::Render
{

class IsolineStyle : public SublayerStyle
{
    Q_OBJECT
public:
    /*! VS.8 — how the iso-levels are generated. Count spreads
     *  isoValueCount levels evenly over the data range (legacy);
     *  FixedInterval emits levels at baseLevel + k·levelInterval (the
     *  ArcGIS/QGIS "contour interval + base contour" idiom). */
    enum class LevelMode { Count = 0, FixedInterval = 1 };
    Q_ENUM(LevelMode)

private:
    Q_PROPERTY(QString attribute READ attribute WRITE setAttribute NOTIFY styleChanged)
    Q_PROPERTY(LevelMode levelMode READ levelMode WRITE setLevelMode NOTIFY styleChanged)
    Q_PROPERTY(int isoValueCount READ isoValueCount WRITE setIsoValueCount NOTIFY styleChanged)
    Q_PROPERTY(double levelInterval READ levelInterval WRITE setLevelInterval NOTIFY styleChanged)
    Q_PROPERTY(double baseLevel READ baseLevel WRITE setBaseLevel NOTIFY styleChanged)
    Q_PROPERTY(double lineWidthPx READ lineWidthPx WRITE setLineWidthPx NOTIFY styleChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY styleChanged)
    Q_PROPERTY(Qt::PenStyle dashPattern READ dashPattern WRITE setDashPattern NOTIFY styleChanged)
    // VS.8 — index contours: every Nth level draws with indexWidthPx
    // (0 = off). Standard topographic-map emphasis.
    Q_PROPERTY(int indexEvery READ indexEvery WRITE setIndexEvery NOTIFY styleChanged)
    Q_PROPERTY(double indexWidthPx READ indexWidthPx WRITE setIndexWidthPx NOTIFY styleChanged)
    Q_PROPERTY(bool labels READ labels WRITE setLabels NOTIFY styleChanged)
    Q_PROPERTY(int labelDecimals READ labelDecimals WRITE setLabelDecimals NOTIFY styleChanged)
    Q_PROPERTY(double labelFontPt READ labelFontPt WRITE setLabelFontPt NOTIFY styleChanged)
    Q_PROPERTY(bool labelHalo READ labelHalo WRITE setLabelHalo NOTIFY styleChanged)

    Q_CLASSINFO("group:attribute", "Classification")
    Q_CLASSINFO("group:levelMode", "Classification")
    Q_CLASSINFO("group:isoValueCount", "Classification")
    Q_CLASSINFO("group:levelInterval", "Classification")
    Q_CLASSINFO("group:baseLevel", "Classification")
    Q_CLASSINFO("group:lineWidthPx", "Symbology")
    Q_CLASSINFO("group:color", "Symbology")
    Q_CLASSINFO("group:dashPattern", "Symbology")
    Q_CLASSINFO("group:indexEvery", "Symbology")
    Q_CLASSINFO("group:indexWidthPx", "Symbology")
    Q_CLASSINFO("group:labels", "Labels")
    Q_CLASSINFO("group:labelDecimals", "Labels")
    Q_CLASSINFO("group:labelFontPt", "Labels")
    Q_CLASSINFO("group:labelHalo", "Labels")

public:
    // Slice US.2 — level count + classification method now live in a shared
    // ClassificationScheme; isoValueCount forwards to its class count. The
    // FixedInterval params + symbology + label knobs stay on the bag.
    /** @brief Construct a Classified EqualInterval scheme with eight stored classes.
     * @param parent Optional QObject owner; destruction of the parent destroys this style.
     * All ordinary access occurs on the owning Qt thread. */
    explicit IsolineStyle(QObject *parent = nullptr);

    /** @brief Read the scalar attribute key.
     * @return Stored/forwarded value, initially "depth". */
    [[nodiscard]] QString attribute() const { return m_attribute; }
    /** @brief Read the level-generation mode.
     * @return Stored/forwarded value, initially Count. */
    [[nodiscard]] LevelMode levelMode() const { return m_levelMode; }
    /** @brief Read the scheme class count used to derive interior levels.
     * @return Stored/forwarded value, initially 8. */
    [[nodiscard]] int isoValueCount() const { return m_scheme.classCount(); }
    /** @brief Read the fixed contour interval in attribute units.
     * @return Stored/forwarded value, initially 0.5. */
    [[nodiscard]] double levelInterval() const { return m_levelInterval; }
    /** @brief Read the fixed-interval reference contour in attribute units.
     * @return Stored/forwarded value, initially 0. */
    [[nodiscard]] double baseLevel() const { return m_baseLevel; }
    /** @brief Read the ordinary line width in logical screen pixels.
     * @return Stored/forwarded value, initially 1. */
    [[nodiscard]] double lineWidthPx() const { return m_lineWidthPx; }
    /** @brief Read the isoline color.
     * @return Stored/forwarded value, initially RGBA(10,10,10,220). */
    [[nodiscard]] QColor color() const { return m_color; }
    /** @brief Read the line Qt::PenStyle enum.
     * @return Stored/forwarded value, initially Qt::SolidLine. */
    [[nodiscard]] Qt::PenStyle dashPattern() const { return m_dashPattern; }
    /** @brief Read the emphasized-contour cadence (zero disables).
     * @return Stored/forwarded value, initially 0. */
    [[nodiscard]] int indexEvery() const { return m_indexEvery; }
    /** @brief Read the emphasized-contour width in logical screen pixels.
     * @return Stored/forwarded value, initially 2. */
    [[nodiscard]] double indexWidthPx() const { return m_indexWidthPx; }
    /** @brief Read the contour-label visibility switch.
     * @return Stored/forwarded value, initially false. */
    [[nodiscard]] bool labels() const { return m_labels; }
    /** @brief Read the fixed-point label precision.
     * @return Stored/forwarded value, initially 2. */
    [[nodiscard]] int labelDecimals() const { return m_labelDecimals; }
    /** @brief Read the contour-label font size in points.
     * @return Stored/forwarded value, initially 9. */
    [[nodiscard]] double labelFontPt() const { return m_labelFontPt; }
    /** @brief Read the contour-label halo switch.
     * @return Stored/forwarded value, initially true. */
    [[nodiscard]] bool labelHalo() const { return m_labelHalo; }

    /** @brief Set the scalar attribute key and notify on a change.
     * @param v New value. Stores the exact name without checking availability.
     * Uses exact equality to suppress unchanged assignments; otherwise emits
     * styleChanged through setDirty(). No synchronous contour work is performed. */
    void setAttribute(const QString &v);
    /** @brief Set the level-generation mode and notify on a change.
     * @param v New value. Stores the enum without validation; any non-Count value follows the fixed-interval path.
     * Uses enum equality to suppress unchanged assignments; otherwise emits
     * styleChanged through setDirty(). No synchronous contour work is performed. */
    void setLevelMode(LevelMode v);
    /** @brief Set the scheme class count used to derive interior levels and notify on a change.
     * @param v New value. Clamps to at least one before forwarding to scheme.setClassCount(); does not change the mode/method.
     * Uses integer equality to suppress unchanged assignments; otherwise emits
     * styleChanged through setDirty(). No synchronous contour work is performed. */
    void setIsoValueCount(int v);
    /** @brief Set the fixed contour interval in attribute units and notify on a change.
     * @param v New value. Replaces nonpositive values with 1e-6; smaller positive intervals are accepted. NaN is not sanitized.
     * Uses qFuzzyCompare(current+1, input+1) to suppress unchanged assignments; otherwise emits
     * styleChanged through setDirty(). No synchronous contour work is performed. */
    void setLevelInterval(double v);
    /** @brief Set the fixed-interval reference contour in attribute units and notify on a change.
     * @param v New value. Stores raw input without finite-value checks.
     * Uses qFuzzyCompare(current+1, input+1) to suppress unchanged assignments; otherwise emits
     * styleChanged through setDirty(). No synchronous contour work is performed. */
    void setBaseLevel(double v);
    /** @brief Set the ordinary line width in logical screen pixels and notify on a change.
     * @param v New value. Clamps negatives to zero; NaN is not sanitized.
     * Uses qFuzzyCompare(current+1, input+1) to suppress unchanged assignments; otherwise emits
     * styleChanged through setDirty(). No synchronous contour work is performed. */
    void setLineWidthPx(double v);
    /** @brief Set the isoline color and notify on a change.
     * @param v New value. Accepts invalid QColor values without validation.
     * Uses QColor equality to suppress unchanged assignments; otherwise emits
     * styleChanged through setDirty(). No synchronous contour work is performed. */
    void setColor(const QColor &v);
    /** @brief Set the line Qt::PenStyle enum and notify on a change.
     * @param v New value. Stores the enum without integer validation.
     * Uses enum equality to suppress unchanged assignments; otherwise emits
     * styleChanged through setDirty(). No synchronous contour work is performed. */
    void setDashPattern(Qt::PenStyle v);
    /** @brief Set the emphasized-contour cadence (zero disables) and notify on a change.
     * @param v New value. Clamps negative values to zero.
     * Uses integer equality to suppress unchanged assignments; otherwise emits
     * styleChanged through setDirty(). No synchronous contour work is performed. */
    void setIndexEvery(int v);
    /** @brief Set the emphasized-contour width in logical screen pixels and notify on a change.
     * @param v New value. Clamps values below 0.1 to 0.1; NaN is not sanitized.
     * Uses qFuzzyCompare(current+1, input+1) to suppress unchanged assignments; otherwise emits
     * styleChanged through setDirty(). No synchronous contour work is performed. */
    void setIndexWidthPx(double v);
    /** @brief Set the contour-label visibility switch and notify on a change.
     * @param v New value. Changes only the rendering hint; no labels are generated here.
     * Uses Boolean equality to suppress unchanged assignments; otherwise emits
     * styleChanged through setDirty(). No synchronous contour work is performed. */
    void setLabels(bool v);
    /** @brief Set the fixed-point label precision and notify on a change.
     * @param v New value. Clamps to [0,9].
     * Uses integer equality to suppress unchanged assignments; otherwise emits
     * styleChanged through setDirty(). No synchronous contour work is performed. */
    void setLabelDecimals(int v);
    /** @brief Set the contour-label font size in points and notify on a change.
     * @param v New value. Clamps finite values to [4,72]; no explicit NaN check occurs.
     * Uses qFuzzyCompare(current+1, input+1) to suppress unchanged assignments; otherwise emits
     * styleChanged through setDirty(). No synchronous contour work is performed. */
    void setLabelFontPt(double v);
    /** @brief Set the contour-label halo switch and notify on a change.
     * @param v New value. Changes only the rendering hint.
     * Uses Boolean equality to suppress unchanged assignments; otherwise emits
     * styleChanged through setDirty(). No synchronous contour work is performed. */
    void setLabelHalo(bool v);

    /*! VS.8 / US.2 — generate the iso-level values for a data range according
     *  to levelMode. Count mode classifies via the embedded scheme (method-
     *  aware: EqualInterval mirrors Contour::evenlySpacedLevels; Quantile /
     *  Jenks / StdDev bin against \p samples when supplied). FixedInterval
     *  mode emits baseLevel + k·levelInterval for every k whose level lands
     *  strictly inside (vMin, vMax). Capped at 256 levels as a runaway guard
     *  for tiny intervals. */
    /** @param vMin Data lower bound in attribute units; if !(vMax>vMin), returns empty
     * before consulting a custom range. Supply finite bounds for meaningful levels.
     * @param vMax Data upper bound in the same unit.
     * @param samples Optional values for data-dependent Count classification; borrowed
     * only during the call. Scheme rules, custom ranges and manual edges apply in Count mode.
     * @return Count mode returns scheme.interiorLevels(), whose count follows its
     * classification method rather than promising exactly isoValueCount values. Fixed
     * mode uses max(levelInterval,1e-12), keeps levels strictly inside (vMin,vMax), and
     * stops after 256 values. baseLevel/interval must be finite for meaningful output.
     * The 256 cap applies only to the fixed-interval path. */
    [[nodiscard]] std::vector<double> levelsForRange(double vMin, double vMax,
                                                     const QVector<double> &samples = {}) const;

    /*! Slice US.2 — the embedded classification scheme (level count + method
     *  + custom range). The ramp fields are unused — isolines paint a single
     *  line colour. */
    /** @return Borrowed owned scheme, replaced by setScheme()/fromJson(); no mutable access. */
    [[nodiscard]] const ClassificationScheme &scheme() const { return m_scheme; }
    /** @brief Copy a complete level-classification scheme and notify on inequality.
     * @param s Source copied without validation; other isoline fields are retained.
     * ClassificationScheme equality determines whether the replacement is suppressed. */
    void setScheme(const ClassificationScheme &s);

    /** @brief Serialize isoline classification, geometry and label hints.
     * @return Independent object; dashPattern uses its Qt enum key, color uses ARGB,
     * and the complete classification is included. No configuration validation occurs. */
    [[nodiscard]] QJsonObject toJson() const override;
    /** @brief Read a partial isoline style and notify once unconditionally.
     * @param j Borrowed object; absent/mistyped scalar fields retain current values.
     * A present classification key replaces the scheme through its reader, including
     * a nonobject as an empty object. Numeric clamps match setters; levelMode is
     * restricted to known enum values here, unlike setLevelMode(). Invalid/empty color
     * and unknown Qt dash tokens are ignored. Raw baseLevel has no finiteness check.
     * Individual setters are bypassed; observers see the final state at styleChanged(). */
    void fromJson(const QJsonObject &j) override;

private:
    QString m_attribute = QStringLiteral("depth");
    LevelMode m_levelMode = LevelMode::Count;
    double m_levelInterval = 0.5;
    double m_baseLevel = 0.0;
    double m_lineWidthPx = 1.0;
    QColor m_color = QColor(10, 10, 10, 220);
    Qt::PenStyle m_dashPattern = Qt::SolidLine;
    int m_indexEvery = 0;
    double m_indexWidthPx = 2.0;
    bool m_labels = false;
    int m_labelDecimals = 2;
    double m_labelFontPt = 9.0;
    bool m_labelHalo = true;
    ClassificationScheme m_scheme;
};

class IsolineSublayer : public ISublayer
{
    Q_OBJECT
public:
    /** @brief Construct an initially hidden isoline pass with opacity one and an owned style.
     * @param id_ Stable identifier moved into storage without checking uniqueness.
     * @param parent Optional QObject owner; destroys the pass and child style.
     * Style changes forward to invalidated(); ordinary access is on the owning Qt thread. */
    explicit IsolineSublayer(QString id_, QObject *parent = nullptr);

    /** @brief Identify this rendering pass.
     * @return IsolineKind. */
    Kind kind() const override { return IsolineKind; }
    /** @brief Copy the construction-time host identifier.
     * @return Stored id without validation. */
    QString id() const override { return m_id; }
    // Terrain isolines trace bed ELEVATION; results isolines trace water DEPTH.
    /** @brief Describe the classified attribute for the UI.
     * @return Elevation Isolines for a case-insensitive elevation key, otherwise Depth Isolines. */
    QString displayName() const override
    {
        const QString a = m_style ? m_style->attribute() : QString();
        if (a.compare(QLatin1String("elevation"), Qt::CaseInsensitive) == 0)
            return tr("Elevation Isolines");
        return tr("Depth Isolines");
    }

    /** @brief Read local visibility.
     * @return Stored flag, initially false; parent visibility is not consulted. */
    bool isVisible() const override { return m_visible; }
    /** @brief Set visibility and emit invalidated() on a Boolean change.
     * @param v New flag; no synchronous contour construction occurs. */
    void setVisible(bool v) override;
    /** @brief Read the local opacity multiplier.
     * @return Stored value, initially one. */
    qreal opacity() const override { return m_opacity; }
    /** @brief Replace clamped opacity and invalidate on a change.
     * @param o Finite multiplier clamped to [0,1]; NaN is not explicitly rejected.
     * Uses qFuzzyCompare(current+1, input+1) to suppress unchanged assignments. */
    void setOpacity(qreal o) override;

    /** @brief Report dependence on animated data.
     * @return True without starting a worker or rebuilding contours. */
    bool isDynamic() const override { return true; }

    /** @brief Borrow the owned style through the base interface.
     * @return Child pointer valid until sublayer destruction; do not delete/reparent. */
    SublayerStyle *style() override { return m_style; }

    /** @brief Build one line preview using attribute, stored class count and pen properties.
     * @return Single row with id/opacity and line color/width/dash. Fixed-interval
     * levels, emphasized contours and label settings are not expanded; visibility is ignored. */
    QList<LegendSymbolItem> legendSymbolItems() const override;
    /** @brief Leave node construction to the owning host renderer.
     * @param existing Borrowed node returned unchanged, including null.
     * @param ctx Unused rendering context.
     * @return existing; no allocation or ownership transfer occurs. */
    QSGNode *buildOrUpdateNode(QSGNode *existing, const SublayerContext &ctx) override;

    /** @brief Borrow the concrete owned isoline style.
     * @return Child pointer valid until sublayer destruction; do not delete/reparent. */
    [[nodiscard]] IsolineStyle *isolineStyle() const { return m_style; }

private:
    QString m_id;
    bool m_visible = false; // off by default per plan §3
    qreal m_opacity = 1.0;
    IsolineStyle *m_style;
};

} // namespace OpenSWMM::Render

#endif
