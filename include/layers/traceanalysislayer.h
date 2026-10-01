// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "layers/openswmmvislayer.h"
#include "output/tracedata.h"
#include "render/colorramp.h"
#include "render/isublayerhost.h"
#include <QPainterPath>

namespace openswmmvis::trace
{
// Every visual channel has its own field, range, transform and palette.
struct Channel
{
    QString field = QStringLiteral("ratio"), transform = QStringLiteral("linear");
    bool automatic = true;
    double minimum = 0, maximum = 1, minimumSize = 2, maximumSize = 14;
    int classes = 0;
    RasterColorRamp ramp = RasterColorRamp::viridis();
    QJsonObject toJson() const;
    static Channel fromJson(const QJsonObject &);
};
class TraceStyle final : public OpenSWMM::Render::SublayerStyle
{
    Q_OBJECT
    Q_PROPERTY(double taper READ taper WRITE setTaper NOTIFY styleChanged)
    Q_PROPERTY(bool arrows READ arrows WRITE setArrows NOTIFY styleChanged)
    Q_PROPERTY(bool labels READ labels WRITE setLabels NOTIFY styleChanged)
  public:
    explicit TraceStyle(QObject *p = nullptr) : SublayerStyle(p) {}
    Channel linkColor, linkWidth, nodeColor, nodeSize;
    double taper() const { return m_taper; }
    void setTaper(double);
    bool arrows() const { return m_arrows; }
    void setArrows(bool);
    bool labels() const { return m_labels; }
    void setLabels(bool);
    void changed() { setDirty(); }
    QJsonObject toJson() const override;
    void fromJson(const QJsonObject &) override;

  private:
    double m_taper = .3;
    bool m_arrows = true, m_labels = false;
};
// Pure pixel-space geometry, shared by painting and hit testing. Duplicate
// vertices are discarded; segment joins are bevelled to avoid miter spikes.
QPainterPath taperedPath(const QVector<QPointF> &, double startWidth, double endWidth);
QString fieldLabel(const QString &, bool node = false);
class TraceAnalysisLayer final : public OpenSWMMVisLayer, public OpenSWMM::Render::ISublayerHost
{
    Q_OBJECT
  public:
    explicit TraceAnalysisLayer(std::shared_ptr<Result>, OpenSWMMVisWorkspace * = nullptr);
    std::shared_ptr<Result> result() const { return m_result; }
    TraceStyle *traceStyle() const { return m_style; }
    QList<OpenSWMM::Render::ISublayer *> sublayers() const override { return m_parts; }
    std::vector<std::unique_ptr<openswmmvis::ui::ILayerStyleSubject>> styleSubjects() override;
    QString sourceDescription() const override;
    void populateScene(QGraphicsScene *, const MapExtent &,
                       const SpatialReferenceSystem *) override;
    void paint(QPainter *, const QVector<QPointF> &, const QVector<QVector<QPointF>> &) const;
    double value(bool node, int index, const QString &field) const;
    QPair<double, double> range(bool node, const Channel &) const;
    double normalized(bool node, int index, const Channel &) const;
    QList<OpenSWMM::Render::LegendSymbolItem> legend(bool node) const;
    bool hitTest(QPointF pixel, const QTransform &mapToPixel, const SpatialReferenceSystem *canvas,
                 bool *node, int *index) const;
    void highlight(bool node, int index);
    QJsonObject savedStyle() const;
    void restoreStyle(const QJsonObject &);
  signals:
    void featurePicked(bool node, int index);

  private:
    std::shared_ptr<Result> m_result;
    TraceStyle *m_style;
    QList<OpenSWMM::Render::ISublayer *> m_parts;
    bool m_highlightNode = true;
    int m_highlight = -1;
};
} // namespace openswmmvis::trace
