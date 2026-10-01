#ifndef OPENSWMM_RENDER_RESULTSCALARSUBLAYER_H
#define OPENSWMM_RENDER_RESULTSCALARSUBLAYER_H

#include "io/mesh2dscalarframe.h"
#include "render/sublayers/scalarfillsublayer.h"
#include <memory>

namespace OpenSWMM::Render {

// Scientific data colors remain independent of the application's chrome theme.
class ResultScalarStyle : public ScalarFillStyle
{
    Q_OBJECT
    Q_PROPERTY(QColor missingColor READ missingColor WRITE setMissingColor NOTIFY styleChanged)
    Q_PROPERTY(QColor waterlessColor READ waterlessColor WRITE setWaterlessColor NOTIFY styleChanged)
    Q_PROPERTY(QColor notApplicableColor READ notApplicableColor WRITE setNotApplicableColor NOTIFY styleChanged)
    Q_CLASSINFO("group:missingColor", "Data availability")
    Q_CLASSINFO("group:waterlessColor", "Data availability")
    Q_CLASSINFO("group:notApplicableColor", "Data availability")
public:
    explicit ResultScalarStyle(QObject *parent = nullptr);
    QColor missingColor() const { return m_missing; }
    QColor waterlessColor() const { return m_waterless; }
    QColor notApplicableColor() const { return m_notApplicable; }
    void setMissingColor(const QColor &color);
    void setWaterlessColor(const QColor &color);
    void setNotApplicableColor(const QColor &color);
    QJsonObject toJson() const override;
    void fromJson(const QJsonObject &json) override;
private:
    QColor m_missing{145, 145, 145, 150};
    QColor m_waterless{230, 230, 230, 90};
    QColor m_notApplicable{Qt::transparent};
};

// Additional flat cell-result pass. attribute() stores the source's semantic
// variable key, never a species-array index. Host layer owns frame acquisition,
// persistence and CPU geometry; the QSG renderer uses the same cellColors().
class ResultScalarSublayer : public ISublayer
{
    Q_OBJECT
public:
    explicit ResultScalarSublayer(QString id, QObject *parent = nullptr);
    Kind kind() const override { return ColorRampFillKind; }
    QString id() const override { return m_id; }
    QString displayName() const override;
    bool isVisible() const override { return m_visible; }
    void setVisible(bool value) override;
    qreal opacity() const override { return m_opacity; }
    void setOpacity(qreal value) override;
    bool isDynamic() const override { return true; }
    SublayerStyle *style() override { return m_style; }
    ResultScalarStyle *fillStyle() const { return m_style; }
    QString variableKey() const { return m_style->attribute(); }
    void setVariableKey(const QString &key) { m_style->setAttribute(key); }
    const openswmmvis::io::Mesh2DResultVariable &descriptor() const { return m_descriptor; }
    void setDescriptor(const openswmmvis::io::Mesh2DResultVariable &descriptor);
    // Render-thread publication only transfers immutable data: no QObject
    // mutation or signals. GUI legends atomically load the displayed frame.
    std::shared_ptr<const openswmmvis::io::Mesh2DScalarFrame> presentedFrame() const;
    void publishPresentedFrame(std::shared_ptr<const openswmmvis::io::Mesh2DScalarFrame> frame);
    QVector<QColor> cellColors(const openswmmvis::io::Mesh2DScalarFrame &frame) const;
    QList<LegendSymbolItem> legendSymbolItems() const override;
    QList<LegendSymbolItem> legendSymbolItems(const openswmmvis::io::Mesh2DScalarFrame &frame) const;
    QSGNode *buildOrUpdateNode(QSGNode *existing, const SublayerContext &) override { return existing; }
private:
    QString unitsLabel(const openswmmvis::io::Mesh2DResultVariable &) const;
    std::shared_ptr<const openswmmvis::io::Mesh2DScalarFrame> m_presentedFrame;
    QString m_id;
    bool m_visible = true;
    qreal m_opacity = 1;
    ResultScalarStyle *m_style;
    openswmmvis::io::Mesh2DResultVariable m_descriptor;
};

} // namespace OpenSWMM::Render
#endif
