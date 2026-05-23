/*!
 * \file   legendoverlay.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/widgets/legendoverlay.h"

#include "layers/gisrasterlayer.h"
#include "layers/gisvectorlayer.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "layers/swmm2dresultslayer.h"
#include "layers/swmmresultslayer.h"
#include "map/mapcanvas.h"
#include "render/ifeaturerenderer.h"
#include "render/irasterrenderer.h"
#include "render/legendsymbolitem.h"

#include <QEvent>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>

namespace openswmmvis::ui {

namespace {

constexpr int  kPadding       = 8;
constexpr int  kRowSpacing    = 2;
constexpr int  kSwatchSize    = 14;
constexpr int  kSwatchPadding = 6;
constexpr int  kLayerSpacing  = 6;
constexpr int  kMaxLabelWidth = 220;

QColor firstSymbolColor(const OpenSWMM::Render::SymbolStyle &style)
{
    for (const auto &sl : style.layers)
    {
        const auto it = sl.props.constFind(QStringLiteral("color"));
        if (it != sl.props.constEnd())
        {
            QColor c(it.value().toString());
            if (c.isValid()) return c;
        }
    }
    return QColor(Qt::gray);
}

QList<OpenSWMM::Render::LegendSymbolItem> legendItemsFor(OpenSWMMVisLayer *layer)
{
    using namespace OpenSWMM::Render;
    if (auto *l = qobject_cast<SWMMResultsLayer *>(layer); l && l->renderer())
        return l->renderer()->legendSymbolItems();
    if (auto *l = qobject_cast<SWMM2DResultsLayer *>(layer); l && l->renderer())
        return l->renderer()->legendSymbolItems();
    if (auto *l = qobject_cast<SWMM2DMeshLayer *>(layer); l && l->renderer())
        return l->renderer()->legendSymbolItems();
    if (auto *l = qobject_cast<GISVectorLayer *>(layer); l && l->renderer())
        return l->renderer()->legendSymbolItems();
    if (auto *l = qobject_cast<GISRasterLayer *>(layer); l && l->rasterRenderer())
        return l->rasterRenderer()->legendSymbolItems();
    return {};
}

} // namespace

LegendOverlay::LegendOverlay(MapCanvas *canvas)
    : QWidget(canvas)
{
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);
    setCursor(Qt::OpenHandCursor);
    // Allow paint with transparency over the canvas's framebuffer + scene.
    setAttribute(Qt::WA_TranslucentBackground, true);
    setCanvas(canvas);
    hide();   // user toggles via actionShowLegend
}

void LegendOverlay::setCanvas(MapCanvas *canvas)
{
    if (m_canvas == canvas) return;

    if (m_canvas) {
        m_canvas->removeEventFilter(this);
        disconnect(m_canvas, nullptr, this, nullptr);
        for (OpenSWMMVisLayer *l : m_canvas->layers())
            disconnectLayer(l);
    }

    m_canvas = canvas;
    if (canvas && parentWidget() != canvas)
        setParent(canvas);

    if (m_canvas) {
        // Watch the parent canvas for resize so the overlay stays inside
        // its bounds and (until the user drags) re-anchors to the
        // bottom-right corner on layout changes.
        m_canvas->installEventFilter(this);
        connect(m_canvas, &MapCanvas::layerAdded, this, [this](OpenSWMMVisLayer *l) {
            connectLayer(l);
            recomputeLayout();
            update();
        });
        connect(m_canvas, &MapCanvas::layerRemoved, this, [this](OpenSWMMVisLayer *l) {
            disconnectLayer(l);
            recomputeLayout();
            update();
        });
        connect(m_canvas, &MapCanvas::layerOrderChanged, this, [this]() {
            recomputeLayout();
            update();
        });
        for (OpenSWMMVisLayer *l : m_canvas->layers())
            connectLayer(l);
    }

    recomputeLayout();
    update();
}

void LegendOverlay::connectLayer(OpenSWMMVisLayer *layer)
{
    if (!layer) return;
    auto refresh = [this]() { recomputeLayout(); update(); };
    connect(layer, &OpenSWMMVisLayer::visibilityChanged, this, refresh,
            Qt::UniqueConnection);
    connect(layer, &OpenSWMMVisLayer::nameChanged,       this, refresh,
            Qt::UniqueConnection);
    connect(layer, &OpenSWMMVisLayer::repaintRequested,  this, refresh,
            Qt::UniqueConnection);
}

void LegendOverlay::disconnectLayer(OpenSWMMVisLayer *layer)
{
    if (!layer) return;
    disconnect(layer, nullptr, this, nullptr);
}

void LegendOverlay::recomputeLayout()
{
    // Walk visible layers, accumulate the box height + max content width.
    // Width is bounded so very long labels don't push the legend off-screen.
    if (!m_canvas) {
        resize(0, 0);
        return;
    }

    const QFontMetrics fm(font());
    const int rowH = std::max(fm.height(), kSwatchSize);
    int contentW  = 0;
    int contentH  = kPadding;
    bool first    = true;

    for (int i = m_canvas->layers().size() - 1; i >= 0; --i)
    {
        OpenSWMMVisLayer *layer = m_canvas->layers().at(i);
        if (!layer || !layer->isVisible()) continue;

        const auto rows = legendItemsFor(layer);
        if (!first) contentH += kLayerSpacing;
        first = false;

        // Layer header (bold)
        QFont hf = font();
        hf.setBold(true);
        contentW = std::max(contentW,
            std::min(QFontMetrics(hf).horizontalAdvance(layer->name()), kMaxLabelWidth));
        contentH += QFontMetrics(hf).height();

        for (const auto &row : rows) {
            if (!row.visible) continue;
            const int labelW = std::min(fm.horizontalAdvance(row.effectiveLabel()),
                                        kMaxLabelWidth);
            contentW = std::max(contentW, kSwatchSize + kSwatchPadding + labelW);
            contentH += rowH + kRowSpacing;
        }
    }
    contentH += kPadding;

    if (first) {                       // nothing visible
        resize(0, 0);
        return;
    }

    resize(contentW + 2 * kPadding, contentH);

    if (!m_positioned) {
        // First sizing — anchor to the bottom-right of the canvas.
        const int margin = 12;
        const int x = std::max(0, m_canvas->width()  - width()  - margin);
        const int y = std::max(0, m_canvas->height() - height() - margin);
        move(x, y);
        m_positioned = true;
    } else {
        clampInsideCanvas();
    }
}

void LegendOverlay::clampInsideCanvas()
{
    if (!m_canvas) return;
    const int maxX = std::max(0, m_canvas->width()  - width());
    const int maxY = std::max(0, m_canvas->height() - height());
    move(std::clamp(x(), 0, maxX), std::clamp(y(), 0, maxY));
}

void LegendOverlay::paintEvent(QPaintEvent * /*event*/)
{
    if (!m_canvas || width() == 0 || height() == 0) return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Translucent rounded background + 1 px border.
    QPainterPath box;
    box.addRoundedRect(QRectF(0.5, 0.5, width() - 1.0, height() - 1.0), 6.0, 6.0);
    p.fillPath(box, QColor(255, 255, 255, 225));
    p.setPen(QPen(QColor(80, 80, 80, 180), 1.0));
    p.drawPath(box);

    const QFontMetrics fm(font());
    QFont hf = font();
    hf.setBold(true);
    const QFontMetrics hfm(hf);
    const int rowH = std::max(fm.height(), kSwatchSize);

    int y = kPadding;
    bool first = true;
    p.setPen(Qt::black);

    for (int i = m_canvas->layers().size() - 1; i >= 0; --i)
    {
        OpenSWMMVisLayer *layer = m_canvas->layers().at(i);
        if (!layer || !layer->isVisible()) continue;

        if (!first) y += kLayerSpacing;
        first = false;

        p.setFont(hf);
        p.drawText(QRect(kPadding, y, width() - 2 * kPadding, hfm.height()),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   fm.elidedText(layer->name(), Qt::ElideRight,
                                 width() - 2 * kPadding));
        y += hfm.height();

        p.setFont(font());
        for (const auto &row : legendItemsFor(layer)) {
            if (!row.visible) continue;
            const QColor c = firstSymbolColor(row.symbol);
            const QRect swatchRect(kPadding,
                                   y + (rowH - kSwatchSize) / 2,
                                   kSwatchSize, kSwatchSize);
            p.setBrush(c);
            p.setPen(QPen(c.darker(140), 1.0));
            p.drawRect(swatchRect);

            const QRect textRect(kPadding + kSwatchSize + kSwatchPadding, y,
                                 width() - kPadding - kSwatchSize - kSwatchPadding - kPadding,
                                 rowH);
            p.setPen(Qt::black);
            p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                       fm.elidedText(row.effectiveLabel(), Qt::ElideRight,
                                     textRect.width()));
            y += rowH + kRowSpacing;
        }
    }
}

void LegendOverlay::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragStartOffset = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
    } else {
        QWidget::mousePressEvent(event);
    }
}

void LegendOverlay::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        QPoint newPos = mapToParent(event->pos()) - m_dragStartOffset;
        if (m_canvas) {
            newPos.setX(std::clamp(newPos.x(), 0, m_canvas->width()  - width()));
            newPos.setY(std::clamp(newPos.y(), 0, m_canvas->height() - height()));
        }
        move(newPos);
        event->accept();
    } else {
        QWidget::mouseMoveEvent(event);
    }
}

void LegendOverlay::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        setCursor(Qt::OpenHandCursor);
        event->accept();
    } else {
        QWidget::mouseReleaseEvent(event);
    }
}

bool LegendOverlay::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_canvas && event->type() == QEvent::Resize) {
        clampInsideCanvas();
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace openswmmvis::ui
