// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "layers/openswmmvislayer.h"
#include "output/tracedata.h"
#include "render/classificationscheme.h"
#include "render/isublayerhost.h"
#include "render/labelconfig.h"
#include <QPainterPath>
class SWMMResultsLayer;

namespace openswmmvis::trace
{
// Every visual channel has its own field, range, transform and palette.
struct Channel
{
    QString field = QStringLiteral("ratio"), transform = QStringLiteral("linear");
    bool automatic = true;
    double minimum = 0, maximum = 1, minimumSize = 2, maximumSize = 14;
    bool proportional = true; // Link widths only: a shared zero-based linear scale.
    OpenSWMM::Render::ClassificationScheme colors;
    Channel();
    QJsonObject toJson() const;
    static Channel fromJson(const QJsonObject &);
};
struct TraceLabels
{
    OpenSWMM::Render::LabelConfig appearance;
    bool showId = true, percent = true;
    int precision = 2;
    TraceLabels();
    QJsonObject toJson() const;
    static TraceLabels fromJson(const QJsonObject &);
};
class TraceStyle final : public OpenSWMM::Render::SublayerStyle
{
    Q_OBJECT
    Q_PROPERTY(bool arrows READ arrows WRITE setArrows NOTIFY styleChanged)
    Q_PROPERTY(bool labels READ labels WRITE setLabels NOTIFY styleChanged)
  public:
    explicit TraceStyle(QObject *p = nullptr) : SublayerStyle(p) { linkWidth.minimumSize = 0; }
    Channel linkColor, linkWidth, nodeColor, nodeSize;
    TraceLabels linkLabels, nodeLabels;
    double taper() const { return m_taper; }
    void setTaper(double);
    bool arrows() const { return m_arrows; }
    void setArrows(bool);
    bool labels() const { return nodeLabels.appearance.enabled; }
    void setLabels(bool);
    void changed() { setDirty(); }
    QJsonObject toJson() const override;
    void fromJson(const QJsonObject &) override;

  private:
    double m_taper = 1;
    bool m_arrows = true;
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
    double widthReference() const { return widthReference(m_style->linkWidth); }
    double widthReference(const Channel &) const;
    QVector<double> linkWidths() const;
    QVector<double> nodeSizes() const;
    QVector<double> samples(bool node, const Channel &) const;
    QVector<QColor> colors(bool node) const;
    QString labelText(bool node, int index) const;
    QJsonObject savedStyle() const;
    void restoreStyle(const QJsonObject &);
  signals:
    void featurePicked(bool node, int index);

  private:
    QVector<double> colorEdges(bool node, const Channel &) const;
    struct ColorCache
    {
        QString field;
        quint64 revision = 0;
        QVector<double> edges;
    };
    mutable ColorCache m_nodeColorCache, m_linkColorCache;
    std::shared_ptr<Result> m_result;
    TraceStyle *m_style;
    QList<OpenSWMM::Render::ISublayer *> m_parts;
    bool m_highlightNode = true;
    int m_highlight = -1;
};
// A static child of one immutable output run. The output owns both this
// tree row and its painter; no separate top-level analysis layer is needed.
class TraceSublayer final : public OpenSWMM::Render::ISublayer
{
  public:
    TraceSublayer(SWMMResultsLayer *, std::shared_ptr<Result>, bool travel);
    Kind kind() const override { return LineKind; }
    QString id() const override;
    QString displayName() const override;
    bool isVisible() const override { return m_layer->isVisible(); }
    void setVisible(bool v) override { m_layer->setVisible(v); }
    qreal opacity() const override { return m_layer->opacity(); }
    void setOpacity(qreal v) override { m_layer->setOpacity(v); }
    bool isDynamic() const override { return false; }
    OpenSWMM::Render::SublayerStyle *style() override { return m_layer->traceStyle(); }
    QList<OpenSWMM::Render::LegendSymbolItem> legendSymbolItems() const override;
    QSGNode *buildOrUpdateNode(QSGNode *, const OpenSWMM::Render::SublayerContext &) override
    {
        return nullptr;
    }
    TraceAnalysisLayer *layer() const { return m_layer; }
    bool travel() const { return m_travel; }

  private:
    TraceAnalysisLayer *m_layer;
    bool m_travel;
};
} // namespace openswmmvis::trace
