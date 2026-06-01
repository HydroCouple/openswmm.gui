/*!
 * \file   isolinesublayer.h
 * \brief  Dynamic marching-squares isoline sublayer over a 2D mesh.
 *         Plan §3 — fourth sublayer in the SWMM2DResultsLayer default mix.
 *         Slice S5.4.
 */
#ifndef OPENSWMM_RENDER_SUBLAYERS_ISOLINESUBLAYER_H
#define OPENSWMM_RENDER_SUBLAYERS_ISOLINESUBLAYER_H

#include "render/isublayer.h"
#include "render/sublayerstyle.h"

#include <QColor>
#include <QJsonObject>
#include <QString>
#include <Qt>

namespace OpenSWMM::Render
{

class IsolineStyle : public SublayerStyle
{
    Q_OBJECT
    Q_PROPERTY(QString      attribute     READ attribute     WRITE setAttribute     NOTIFY styleChanged)
    Q_PROPERTY(int          isoValueCount READ isoValueCount WRITE setIsoValueCount NOTIFY styleChanged)
    Q_PROPERTY(double       lineWidthPx   READ lineWidthPx   WRITE setLineWidthPx   NOTIFY styleChanged)
    Q_PROPERTY(QColor       color         READ color         WRITE setColor         NOTIFY styleChanged)
    Q_PROPERTY(Qt::PenStyle dashPattern   READ dashPattern   WRITE setDashPattern   NOTIFY styleChanged)
    Q_PROPERTY(bool         labels        READ labels        WRITE setLabels        NOTIFY styleChanged)

    Q_CLASSINFO("group:attribute",     "Classification")
    Q_CLASSINFO("group:isoValueCount", "Classification")
    Q_CLASSINFO("group:lineWidthPx",   "Symbology")
    Q_CLASSINFO("group:color",         "Symbology")
    Q_CLASSINFO("group:dashPattern",   "Symbology")
    Q_CLASSINFO("group:labels",        "Labels")

public:
    explicit IsolineStyle(QObject *parent = nullptr) : SublayerStyle(parent) {}

    [[nodiscard]] QString      attribute() const     { return m_attribute; }
    [[nodiscard]] int          isoValueCount() const { return m_isoValueCount; }
    [[nodiscard]] double       lineWidthPx() const   { return m_lineWidthPx; }
    [[nodiscard]] QColor       color() const         { return m_color; }
    [[nodiscard]] Qt::PenStyle dashPattern() const   { return m_dashPattern; }
    [[nodiscard]] bool         labels() const        { return m_labels; }

    void setAttribute(const QString &v);
    void setIsoValueCount(int v);
    void setLineWidthPx(double v);
    void setColor(const QColor &v);
    void setDashPattern(Qt::PenStyle v);
    void setLabels(bool v);

    [[nodiscard]] QJsonObject toJson() const override;
    void fromJson(const QJsonObject &j) override;

private:
    QString      m_attribute     = QStringLiteral("depth");
    int          m_isoValueCount = 8;
    double       m_lineWidthPx   = 1.0;
    QColor       m_color         = QColor(10, 10, 10, 220);
    Qt::PenStyle m_dashPattern   = Qt::SolidLine;
    bool         m_labels        = false;
};

class IsolineSublayer : public ISublayer
{
    Q_OBJECT
public:
    explicit IsolineSublayer(QString id_, QObject *parent = nullptr);

    Kind    kind() const override        { return IsolineKind; }
    QString id() const override          { return m_id; }
    QString displayName() const override { return tr("Isolines"); }

    bool  isVisible() const override     { return m_visible; }
    void  setVisible(bool v) override;
    qreal opacity() const override       { return m_opacity; }
    void  setOpacity(qreal o) override;

    bool isDynamic() const override      { return true; }

    SublayerStyle *style() override      { return m_style; }

    QList<LegendSymbolItem> legendSymbolItems() const override;
    QSGNode *buildOrUpdateNode(QSGNode *existing, const SublayerContext &) override;

    [[nodiscard]] IsolineStyle *isolineStyle() const { return m_style; }

private:
    QString       m_id;
    bool          m_visible = false; // off by default per plan §3
    qreal         m_opacity = 1.0;
    IsolineStyle *m_style;
};

} // namespace OpenSWMM::Render

#endif
