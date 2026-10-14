// GPL-3.0-or-later
#pragma once
#include "render/isublayer.h"
#include "render/sublayerstyle.h"
#include "render/classificationscheme.h"
#include <QColor>
#include <limits>
namespace OpenSWMM::Render {
class FlowStreakStyle : public SublayerStyle {
    Q_OBJECT
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY styleChanged)
    Q_CLASSINFO("group:color","Appearance")
    Q_PROPERTY(double widthPx READ widthPx WRITE setWidthPx NOTIFY styleChanged)
    Q_CLASSINFO("group:widthPx","Appearance")
    Q_PROPERTY(double headWidthPx READ headWidthPx WRITE setHeadWidthPx NOTIFY styleChanged)
    Q_CLASSINFO("group:headWidthPx","Appearance")
    Q_PROPERTY(double trailDurationSeconds READ trailDurationSeconds WRITE setTrailDurationSeconds NOTIFY styleChanged)
    Q_CLASSINFO("group:trailDurationSeconds","Appearance")
    Q_PROPERTY(double spacingPx READ spacingPx WRITE setSpacingPx NOTIFY styleChanged)
    Q_CLASSINFO("group:spacingPx","Distribution")
    Q_PROPERTY(bool animated READ animated WRITE setAnimated NOTIFY styleChanged)
    Q_CLASSINFO("group:animated","Motion")
    Q_PROPERTY(double speedMultiplier READ speedMultiplier WRITE setSpeedMultiplier NOTIFY styleChanged)
    Q_CLASSINFO("group:speedMultiplier","Motion")
    Q_PROPERTY(double minimumLifetimeSeconds READ minimumLifetimeSeconds WRITE setMinimumLifetimeSeconds NOTIFY styleChanged)
    Q_CLASSINFO("group:minimumLifetimeSeconds","Motion")
    Q_PROPERTY(double maximumLifetimeSeconds READ maximumLifetimeSeconds WRITE setMaximumLifetimeSeconds NOTIFY styleChanged)
    Q_CLASSINFO("group:maximumLifetimeSeconds","Motion")
    Q_PROPERTY(double lifetimeFadeSeconds READ lifetimeFadeSeconds WRITE setLifetimeFadeSeconds NOTIFY styleChanged)
    Q_CLASSINFO("group:lifetimeFadeSeconds","Motion")
    Q_PROPERTY(bool colorByMagnitude READ colorByMagnitude WRITE setColorByMagnitude NOTIFY styleChanged)
    Q_CLASSINFO("group:colorByMagnitude","Colour")
public:
    explicit FlowStreakStyle(QObject *parent=nullptr);
    // Rendering uniforms use floats; impose only their numeric range, not
    // arbitrary tuning ceilings. Keep editors and persisted values consistent.
    static constexpr double maximumNumericValue() { return std::numeric_limits<float>::max(); }
    QColor color() const { return m_color; }
    void setColor(QColor value);
    double widthPx() const { return m_widthPx; }
    void setWidthPx(double value);
    double headWidthPx() const { return m_headWidthPx; }
    void setHeadWidthPx(double value);
    double widthAtFraction(double tailToHead) const;
    double trailDurationSeconds() const { return m_trailDurationSeconds; }
    void setTrailDurationSeconds(double value);
    double spacingPx() const { return m_spacingPx; }
    void setSpacingPx(double value);
    bool animated() const { return m_animated; }
    void setAnimated(bool value);
    double speedMultiplier() const { return m_speedMultiplier; }
    bool motionWasMigrated() const {return m_motionWasMigrated;}
    void setSpeedMultiplier(double value);
    double minimumLifetimeSeconds() const { return m_minimumLifetimeSeconds; }
    void setMinimumLifetimeSeconds(double value);
    double maximumLifetimeSeconds() const { return m_maximumLifetimeSeconds; }
    void setMaximumLifetimeSeconds(double value);
    double lifetimeFadeSeconds() const { return m_lifetimeFadeSeconds; }
    void setLifetimeFadeSeconds(double value);
    bool colorByMagnitude() const { return m_colorByMagnitude; }
    void setColorByMagnitude(bool value);
    const ClassificationScheme &scheme() const { return m_scheme; }
    void setScheme(const ClassificationScheme &value);
    QColor colorForSpeed(double speed) const;
    QJsonObject toJson() const override;
    void fromJson(const QJsonObject &) override;
private:
    ClassificationScheme m_scheme;
    QColor m_color = QColor(255,255,255,217);
    double m_widthPx = 1.5;
    double m_headWidthPx = 2.5;
    double m_trailDurationSeconds = 1.5;
    double m_spacingPx = 40.0;
    bool m_animated = true;
    double m_speedMultiplier = 1.0;
    bool m_motionWasMigrated=false;
    double m_minimumLifetimeSeconds = 20.0;
    double m_maximumLifetimeSeconds = 40.0;
    double m_lifetimeFadeSeconds = 0.6;
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
