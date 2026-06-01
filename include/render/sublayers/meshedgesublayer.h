/*!
 * \file   meshedgesublayer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Stylable wireframe-edge sublayer for the 2D mesh.
 *
 *         Static — isDynamic() == false. Edge geometry depends on the mesh
 *         topology, not on the animation period.
 *
 *         Replaces the hard-coded edge pass in SWMM2DMeshQSGRenderer with
 *         a property bag the user can edit through the layer-tree
 *         "Edit Sublayer Style..." dialog. The slope-driven thin/wide
 *         split shipped in AU.6 is preserved as an opt-in advanced toggle
 *         (useSlopeDrivenWidth); the default behaviour is a uniform
 *         lineWidthPx so the new style maps onto a single intuitive knob.
 */
#ifndef OPENSWMM_RENDER_SUBLAYERS_MESHEDGESUBLAYER_H
#define OPENSWMM_RENDER_SUBLAYERS_MESHEDGESUBLAYER_H

#include "render/isublayer.h"
#include "render/sublayerstyle.h"

#include <QColor>
#include <QJsonObject>
#include <QString>
#include <Qt>

namespace OpenSWMM::Render
{

class MeshEdgeStyle : public SublayerStyle
{
    Q_OBJECT
    Q_PROPERTY(QColor       color                READ color                WRITE setColor                NOTIFY styleChanged)
    Q_PROPERTY(double       lineWidthPx          READ lineWidthPx          WRITE setLineWidthPx          NOTIFY styleChanged)
    Q_PROPERTY(Qt::PenStyle dashPattern          READ dashPattern          WRITE setDashPattern          NOTIFY styleChanged)
    Q_PROPERTY(bool         useSlopeDrivenWidth  READ useSlopeDrivenWidth  WRITE setUseSlopeDrivenWidth  NOTIFY styleChanged)
    Q_PROPERTY(double       slopeBreak           READ slopeBreak           WRITE setSlopeBreak           NOTIFY styleChanged)
    Q_PROPERTY(double       wideWidthPx          READ wideWidthPx          WRITE setWideWidthPx          NOTIFY styleChanged)
    Q_PROPERTY(QColor       wideColor            READ wideColor            WRITE setWideColor            NOTIFY styleChanged)

    Q_CLASSINFO("group:color",               "Symbology")
    Q_CLASSINFO("group:lineWidthPx",         "Symbology")
    Q_CLASSINFO("group:dashPattern",         "Symbology")
    Q_CLASSINFO("group:useSlopeDrivenWidth", "Slope emphasis")
    Q_CLASSINFO("group:slopeBreak",          "Slope emphasis")
    Q_CLASSINFO("group:wideWidthPx",         "Slope emphasis")
    Q_CLASSINFO("group:wideColor",           "Slope emphasis")

public:
    explicit MeshEdgeStyle(QObject *parent = nullptr) : SublayerStyle(parent) {}

    [[nodiscard]] QColor       color() const               { return m_color; }
    [[nodiscard]] double       lineWidthPx() const         { return m_lineWidthPx; }
    [[nodiscard]] Qt::PenStyle dashPattern() const         { return m_dashPattern; }
    [[nodiscard]] bool         useSlopeDrivenWidth() const { return m_useSlopeDrivenWidth; }
    [[nodiscard]] double       slopeBreak() const          { return m_slopeBreak; }
    [[nodiscard]] double       wideWidthPx() const         { return m_wideWidthPx; }
    [[nodiscard]] QColor       wideColor() const           { return m_wideColor; }

    void setColor(const QColor &v)              { if (m_color == v) return; m_color = v; setDirty(); }
    void setLineWidthPx(double v);
    void setDashPattern(Qt::PenStyle v)         { if (m_dashPattern == v) return; m_dashPattern = v; setDirty(); }
    void setUseSlopeDrivenWidth(bool v)         { if (m_useSlopeDrivenWidth == v) return; m_useSlopeDrivenWidth = v; setDirty(); }
    void setSlopeBreak(double v);
    void setWideWidthPx(double v);
    void setWideColor(const QColor &v)          { if (m_wideColor == v) return; m_wideColor = v; setDirty(); }

    [[nodiscard]] QJsonObject toJson() const override;
    void fromJson(const QJsonObject &j) override;

private:
    QColor       m_color               = QColor(0, 0, 0, 130);
    double       m_lineWidthPx         = 0.35;
    Qt::PenStyle m_dashPattern         = Qt::SolidLine;
    bool         m_useSlopeDrivenWidth = true;
    double       m_slopeBreak          = 0.35;   // fraction of maxSlope above which edges go wide
    double       m_wideWidthPx         = 0.90;
    QColor       m_wideColor           = QColor(0, 0, 0, 210);
};

class MeshEdgeSublayer : public ISublayer
{
    Q_OBJECT
public:
    explicit MeshEdgeSublayer(QString id_, QObject *parent = nullptr);

    Kind    kind() const override        { return LineKind; }
    QString id() const override          { return m_id; }
    QString displayName() const override { return tr("Mesh edges"); }

    bool  isVisible() const override     { return m_visible; }
    void  setVisible(bool v) override;
    qreal opacity() const override       { return m_opacity; }
    void  setOpacity(qreal o) override;

    bool isDynamic() const override      { return false; }

    SublayerStyle *style() override      { return m_style; }

    QList<LegendSymbolItem> legendSymbolItems() const override;
    QSGNode *buildOrUpdateNode(QSGNode *existing, const SublayerContext &) override;

    [[nodiscard]] MeshEdgeStyle *edgeStyle() const { return m_style; }

private:
    QString        m_id;
    bool           m_visible = true;
    qreal          m_opacity = 1.0;
    MeshEdgeStyle *m_style;
};

} // namespace OpenSWMM::Render

#endif
