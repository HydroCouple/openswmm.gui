// GPL-3.0-or-later
#pragma once
#include "render/isublayer.h"
#include "render/sublayerstyle.h"
#include "render/classificationscheme.h"
#include <QColor>
namespace OpenSWMM::Render {
class FlowStreakStyle : public SublayerStyle {
    Q_OBJECT
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY styleChanged)
    Q_CLASSINFO("group:color","Appearance")
    Q_PROPERTY(double widthPx READ widthPx WRITE setWidthPx NOTIFY styleChanged)
    Q_CLASSINFO("group:widthPx","Appearance")
    Q_PROPERTY(double headWidthPx READ headWidthPx WRITE setHeadWidthPx NOTIFY styleChanged)
    Q_CLASSINFO("group:headWidthPx","Appearance")
    Q_PROPERTY(double trailLengthPx READ trailLengthPx WRITE setTrailLengthPx NOTIFY styleChanged)
    Q_CLASSINFO("group:trailLengthPx","Appearance")
    Q_PROPERTY(bool fade READ fade WRITE setFade NOTIFY styleChanged)
    Q_CLASSINFO("group:fade","Appearance")
    Q_PROPERTY(bool taper READ taper WRITE setTaper NOTIFY styleChanged)
    Q_CLASSINFO("group:taper","Appearance")
    Q_PROPERTY(double spacingPx READ spacingPx WRITE setSpacingPx NOTIFY styleChanged)
    Q_CLASSINFO("group:spacingPx","Distribution")
    Q_PROPERTY(double pathLengthPx READ pathLengthPx WRITE setPathLengthPx NOTIFY styleChanged)
    Q_CLASSINFO("group:pathLengthPx","Distribution")
    Q_PROPERTY(double dryDepthCutoff READ dryDepthCutoff WRITE setDryDepthCutoff NOTIFY styleChanged)
    Q_CLASSINFO("group:dryDepthCutoff","Wet support")
    Q_PROPERTY(bool animated READ animated WRITE setAnimated NOTIFY styleChanged)
    Q_CLASSINFO("group:animated","Motion")
    Q_PROPERTY(double speedMultiplier READ speedMultiplier WRITE setSpeedMultiplier NOTIFY styleChanged)
    Q_CLASSINFO("group:speedMultiplier","Motion")
    Q_PROPERTY(int animationTimestepMs READ animationTimestepMs WRITE setAnimationTimestepMs NOTIFY styleChanged)
    Q_CLASSINFO("group:animationTimestepMs","Motion")
    Q_PROPERTY(bool colorByMagnitude READ colorByMagnitude WRITE setColorByMagnitude NOTIFY styleChanged)
    Q_CLASSINFO("group:colorByMagnitude","Colour")
public:
    explicit FlowStreakStyle(QObject *parent=nullptr);
    QColor color() const { return m_color; }
    void setColor(QColor value);
    double widthPx() const { return m_widthPx; }
    void setWidthPx(double value);
    double headWidthPx() const { return m_headWidthPx; }
    void setHeadWidthPx(double value);
    double widthAtFraction(double tailToHead) const;
    double trailLengthPx() const { return m_trailLengthPx; }
    void setTrailLengthPx(double value);
    bool fade() const { return m_fade; }
    void setFade(bool value);
    bool taper() const { return m_taper; }
    void setTaper(bool value);
    double spacingPx() const { return m_spacingPx; }
    void setSpacingPx(double value);
    double pathLengthPx() const { return m_pathLengthPx; }
    void setPathLengthPx(double value);
    double dryDepthCutoff() const { return m_dryDepthCutoff; }
    void setDryDepthCutoff(double value);
    bool animated() const { return m_animated; }
    void setAnimated(bool value);
    double speedMultiplier() const { return m_speedMultiplier; }
    void setSpeedMultiplier(double value);
    int animationTimestepMs() const { return m_animationTimestepMs; }
    void setAnimationTimestepMs(int value);
    bool colorByMagnitude() const { return m_colorByMagnitude; }
    void setColorByMagnitude(bool value);
    const ClassificationScheme &scheme() const { return m_scheme; }
    void setScheme(const ClassificationScheme &value);
    QColor colorForSpeed(double speed) const;
    QJsonObject toJson() const override;
    void fromJson(const QJsonObject &) override;
    // Only these settings affect path preparation, not paint or phase.
    QJsonObject pathSettings() const;
private:
    ClassificationScheme m_scheme;
    QColor m_color = QColor(Qt::white);
    double m_widthPx = 1.5;
    double m_headWidthPx = 1.5;
    double m_trailLengthPx = 80.0;
    bool m_fade = true;
    bool m_taper = true;
    double m_spacingPx = 30.0;
    double m_pathLengthPx = 350.0;
    double m_dryDepthCutoff = 0.001;
    bool m_animated = true;
    double m_speedMultiplier = 2.0;
    int m_animationTimestepMs = 16;
    bool m_colorByMagnitude = false;
};
class FlowStreakSublayer : public ISublayer {
    Q_OBJECT
public:
    explicit FlowStreakSublayer(QObject *parent=nullptr);
    Kind kind() const override { return LineKind; }
    QString id() const override { return QStringLiteral("results2d.flowStreaks"); }
    QString displayName() const override { return tr("Flow Streaks"); }
    bool isVisible() const override { return m_visible; }
    void setVisible(bool) override;
    qreal opacity() const override { return m_opacity; }
    void setOpacity(qreal) override;
    bool isDynamic() const override { return true; }
    SublayerStyle *style() override { return m_style; }
    FlowStreakStyle *streakStyle() const { return m_style; }
    QList<LegendSymbolItem> legendSymbolItems() const override;
    QSGNode *buildOrUpdateNode(QSGNode *existing,const SublayerContext &) override { return existing; }
private:
    FlowStreakStyle *m_style;
    bool m_visible=false;
    qreal m_opacity=1.;
};
} // namespace OpenSWMM::Render
