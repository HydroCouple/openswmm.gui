/*!
 * \file   contourbandsublayer.h
 * \brief  Dynamic marching-squares filled-contour-bands sublayer over a 2D mesh.
 *         Plan §3 — fifth sublayer in the SWMM2DResultsLayer default mix.
 *         Slice S5.5.
 */
#ifndef OPENSWMM_RENDER_SUBLAYERS_CONTOURBANDSUBLAYER_H
#define OPENSWMM_RENDER_SUBLAYERS_CONTOURBANDSUBLAYER_H

#include "render/isublayer.h"
#include "render/sublayerstyle.h"

#include <QColor>
#include <QJsonObject>
#include <QString>

namespace OpenSWMM::Render
{

class ContourBandStyle : public SublayerStyle
{
    Q_OBJECT
    Q_PROPERTY(QString attribute     READ attribute     WRITE setAttribute     NOTIFY styleChanged)
    Q_PROPERTY(int     bandCount     READ bandCount     WRITE setBandCount     NOTIFY styleChanged)
    Q_PROPERTY(QColor  lowColor      READ lowColor      WRITE setLowColor      NOTIFY styleChanged)
    Q_PROPERTY(QColor  highColor     READ highColor     WRITE setHighColor     NOTIFY styleChanged)
    Q_PROPERTY(QColor  belowMinColor READ belowMinColor WRITE setBelowMinColor NOTIFY styleChanged)
    Q_PROPERTY(QColor  aboveMaxColor READ aboveMaxColor WRITE setAboveMaxColor NOTIFY styleChanged)
    Q_PROPERTY(bool    smoothBands   READ smoothBands   WRITE setSmoothBands   NOTIFY styleChanged)

    Q_CLASSINFO("group:attribute",     "Classification")
    Q_CLASSINFO("group:bandCount",     "Classification")
    Q_CLASSINFO("group:lowColor",      "Ramp")
    Q_CLASSINFO("group:highColor",     "Ramp")
    Q_CLASSINFO("group:belowMinColor", "Out of range")
    Q_CLASSINFO("group:aboveMaxColor", "Out of range")
    Q_CLASSINFO("group:smoothBands",   "Rendering")

public:
    explicit ContourBandStyle(QObject *parent = nullptr) : SublayerStyle(parent) {}

    [[nodiscard]] QString attribute() const     { return m_attribute; }
    [[nodiscard]] int     bandCount() const     { return m_bandCount; }
    [[nodiscard]] QColor  lowColor() const      { return m_lowColor; }
    [[nodiscard]] QColor  highColor() const     { return m_highColor; }
    [[nodiscard]] QColor  belowMinColor() const { return m_belowMinColor; }
    [[nodiscard]] QColor  aboveMaxColor() const { return m_aboveMaxColor; }
    [[nodiscard]] bool    smoothBands() const   { return m_smoothBands; }

    void setAttribute(const QString &v);
    void setBandCount(int v);
    void setLowColor(const QColor &v);
    void setHighColor(const QColor &v);
    void setBelowMinColor(const QColor &v);
    void setAboveMaxColor(const QColor &v);
    void setSmoothBands(bool v);

    [[nodiscard]] QJsonObject toJson() const override;
    void fromJson(const QJsonObject &j) override;

private:
    QString m_attribute     = QStringLiteral("depth");
    int     m_bandCount     = 8;
    QColor  m_lowColor      = QColor( 60, 100, 200, 200);
    QColor  m_highColor     = QColor(200, 220, 255, 200);
    QColor  m_belowMinColor = QColor(  0,   0,   0,   0);
    QColor  m_aboveMaxColor = QColor(255, 255, 255, 200);
    bool    m_smoothBands   = true;
};

class ContourBandSublayer : public ISublayer
{
    Q_OBJECT
public:
    explicit ContourBandSublayer(QString id_, QObject *parent = nullptr);

    Kind    kind() const override        { return ContourBandKind; }
    QString id() const override          { return m_id; }
    QString displayName() const override { return tr("Contour bands"); }

    bool  isVisible() const override     { return m_visible; }
    void  setVisible(bool v) override;
    qreal opacity() const override       { return m_opacity; }
    void  setOpacity(qreal o) override;

    bool isDynamic() const override      { return true; }

    SublayerStyle *style() override      { return m_style; }

    QList<LegendSymbolItem> legendSymbolItems() const override;
    QSGNode *buildOrUpdateNode(QSGNode *existing, const SublayerContext &) override;

    [[nodiscard]] ContourBandStyle *bandStyle() const { return m_style; }

private:
    QString            m_id;
    bool               m_visible = false; // off by default per plan §3
    qreal              m_opacity = 0.85;
    ContourBandStyle  *m_style;
};

} // namespace OpenSWMM::Render

#endif
