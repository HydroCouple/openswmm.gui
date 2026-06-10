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
#include "layers/swmmmodellayer.h"
#include "layers/swmmresultslayer.h"
#include "map/legendclasseditcommands.h"
#include "map/legendcontent.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "render/ifeaturerenderer.h"
#include "render/irasterrenderer.h"
#include "render/legendoverlaystyle.h"
#include "render/legendsymbolitem.h"
#include "ui/dialogs/legendpropertiesdialog.h"

#include <QApplication>
#include <QClipboard>
#include <QColorDialog>
#include <QContextMenuEvent>
#include <QEvent>
#include <QFontMetrics>
#include <QImage>
#include <QLinearGradient>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>

namespace openswmmvis::ui {

using Style = OpenSWMM::Render::LegendOverlayStyle;

namespace {

constexpr int  kSwatchPadding = 6;
constexpr int  kLayerSpacing  = 6;
constexpr int  kMaxLabelWidth = 220;

// Gap B1 — both helpers delegate to the canonical LegendContent copy so the
// on-canvas legend, the dock tree and the per-class edit routing read the
// SAME rows (results layers now aggregate their per-kind renderers instead
// of showing the dormant layer-level renderer).
QColor firstSymbolColor(const OpenSWMM::Render::SymbolStyle &style)
{
    return openswmmvis::map::LegendContent::firstSymbolColor(style);
}

QList<OpenSWMM::Render::LegendSymbolItem> legendItemsFor(OpenSWMMVisLayer *layer)
{
    return openswmmvis::map::LegendContent::legendItemsFor(layer);
}

// Slice BB Phase 8.6.10 / 8.6.16 — apply per-item overrides on top of the
// renderer-supplied items so the on-canvas legend + the dock tree show
// identical state. Mutates `items` in place.
void applyItemOverrides(QList<OpenSWMM::Render::LegendSymbolItem> &items,
                        OpenSWMMVisLayer *layer,
                        const OpenSWMM::Render::LegendOverlayStyle *style)
{
    if (!style || !layer) return;
    const QString lk = OpenSWMM::Render::LegendOverlayStyle::itemKey(layer, {});
    for (auto &row : items) {
        if (row.classKey.isEmpty()) continue;
        const auto ov = style->itemOverride(lk, row.classKey);
        if (!ov.visible)             row.visible = false;
        if (!ov.userLabel.isEmpty()) row.userLabel = ov.userLabel;
    }
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

    // Default to a self-owned style; replaced via setStyle() once a
    // project-scoped instance lands with Phase 8.6.12 persistence.
    m_style = new Style(this);
    m_ownsStyle = true;
    connect(m_style, &Style::changed, this, &LegendOverlay::onStyleChanged);

    setCanvas(canvas);
    hide();   // user toggles via actionShowLegend
}

LegendOverlay::~LegendOverlay()
{
    if (!m_ownsStyle && m_style) {
        disconnect(m_style, nullptr, this, nullptr);
    }
    // Self-owned style is destroyed via Qt parent-child cleanup.
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
        // its bounds and re-anchors per style().anchor() on layout changes.
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

void LegendOverlay::setStyle(Style *style, bool takeOwnership)
{
    if (m_style == style) return;
    if (m_style) {
        disconnect(m_style, nullptr, this, nullptr);
        if (m_ownsStyle) m_style->deleteLater();
    }
    m_style = style;
    m_ownsStyle = takeOwnership;
    if (m_style) {
        if (takeOwnership && m_style->parent() != this)
            m_style->setParent(this);
        connect(m_style, &Style::changed, this, &LegendOverlay::onStyleChanged);
    }
    onStyleChanged();
}

void LegendOverlay::onStyleChanged()
{
    setWindowOpacity(m_style ? m_style->opacity() : 1.0);
    recomputeLayout();
    update();
}

void LegendOverlay::connectLayer(OpenSWMMVisLayer *layer)
{
    if (!layer) return;
    // Qt 6 asserts on Qt::UniqueConnection with non-PMF slots; clear any
    // prior connections from this layer to us so repeated calls don't stack.
    disconnect(layer, nullptr, this, nullptr);
    auto refresh = [this]() { recomputeLayout(); update(); };
    connect(layer, &OpenSWMMVisLayer::visibilityChanged, this, refresh);
    connect(layer, &OpenSWMMVisLayer::nameChanged,       this, refresh);
    connect(layer, &OpenSWMMVisLayer::repaintRequested,  this, refresh);
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
    if (!m_canvas || !m_style) {
        resize(0, 0);
        return;
    }

    const int padding    = m_style->padding();
    const int rowSpacing = m_style->rowSpacing();
    const int swatchSize = m_style->swatchSize();

    const QFontMetrics fm(m_style->itemFont());
    const QFontMetrics hfm(m_style->layerHeaderFont());
    const QFontMetrics tfm(m_style->titleFont());
    const int rowH = std::max(fm.height(), swatchSize);

    int contentW  = 0;
    int contentH  = padding;
    bool first    = true;

    if (m_style->showTitle() && !m_style->title().isEmpty()) {
        contentW = std::max(contentW,
            std::min(tfm.horizontalAdvance(m_style->title()), kMaxLabelWidth));
        contentH += tfm.height();
        contentH += rowSpacing;
    }

    for (int i = m_canvas->layers().size() - 1; i >= 0; --i)
    {
        OpenSWMMVisLayer *layer = m_canvas->layers().at(i);
        if (!layer || !layer->isVisible()) continue;

        auto rows = legendItemsFor(layer);
        applyItemOverrides(rows, layer, m_style);
        if (!first) contentH += kLayerSpacing;
        first = false;

        // Layer header
        contentW = std::max(contentW,
            std::min(hfm.horizontalAdvance(layer->name()), kMaxLabelWidth));
        contentH += hfm.height();

        for (const auto &row : rows) {
            if (!row.visible) continue;
            const int labelW = std::min(fm.horizontalAdvance(row.effectiveLabel()),
                                        kMaxLabelWidth);
            contentW = std::max(contentW, swatchSize + kSwatchPadding + labelW);
            contentH += rowH + rowSpacing;
        }
    }
    contentH += padding;

    if (first && !(m_style->showTitle() && !m_style->title().isEmpty())) {
        resize(0, 0);
        return;
    }

    resize(contentW + 2 * padding, contentH);

    if (!m_positioned) {
        anchorToCanvas();
        m_positioned = true;
    } else if (m_style->anchor() != Style::Anchor::Free) {
        anchorToCanvas();
    } else {
        clampInsideCanvas();
    }
}

void LegendOverlay::anchorToCanvas()
{
    if (!m_canvas || !m_style) return;
    const int margin = 12;
    const int cw = m_canvas->width();
    const int ch = m_canvas->height();
    const int w  = width();
    const int h  = height();

    int x = 0, y = 0;
    switch (m_style->anchor()) {
    case Style::Anchor::TopLeft:     x = margin;            y = margin;            break;
    case Style::Anchor::Top:         x = (cw - w) / 2;      y = margin;            break;
    case Style::Anchor::TopRight:    x = cw - w - margin;   y = margin;            break;
    case Style::Anchor::Right:       x = cw - w - margin;   y = (ch - h) / 2;      break;
    case Style::Anchor::BottomRight: x = cw - w - margin;   y = ch - h - margin;   break;
    case Style::Anchor::Bottom:      x = (cw - w) / 2;      y = ch - h - margin;   break;
    case Style::Anchor::BottomLeft:  x = margin;            y = ch - h - margin;   break;
    case Style::Anchor::Left:        x = margin;            y = (ch - h) / 2;      break;
    case Style::Anchor::Free:        // honour current pos; just clamp.
        clampInsideCanvas();
        return;
    }
    move(std::clamp(x, 0, std::max(0, cw - w)),
         std::clamp(y, 0, std::max(0, ch - h)));
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
    if (!m_canvas || !m_style || width() == 0 || height() == 0) return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int padding    = m_style->padding();
    const int rowSpacing = m_style->rowSpacing();
    const int swatchSize = m_style->swatchSize();

    // ── Background + frame ────────────────────────────────────────────
    QPainterPath box;
    const qreal inset = m_style->showFrame() ? 0.5 : 0.0;
    box.addRoundedRect(
        QRectF(inset, inset, width() - 2 * inset, height() - 2 * inset),
        m_style->cornerRadius(), m_style->cornerRadius());

    switch (m_style->backgroundMode()) {
    case Style::BackgroundMode::None:
        break;
    case Style::BackgroundMode::Solid:
        p.fillPath(box, m_style->backgroundColor());
        break;
    case Style::BackgroundMode::Gradient: {
        QLinearGradient g;
        if (m_style->gradientOrientation() == Qt::Horizontal)
            g = QLinearGradient(0, 0, width(), 0);
        else
            g = QLinearGradient(0, 0, 0, height());
        g.setColorAt(0.0, m_style->backgroundColor());
        g.setColorAt(1.0, m_style->gradientEndColor());
        p.fillPath(box, g);
        break;
    }
    }

    if (m_style->showFrame()) {
        p.setPen(QPen(m_style->frameColor(), m_style->frameWidth()));
        p.drawPath(box);
    }

    // ── Content ───────────────────────────────────────────────────────
    const QFontMetrics fm(m_style->itemFont());
    const QFontMetrics hfm(m_style->layerHeaderFont());
    const QFontMetrics tfm(m_style->titleFont());
    const int rowH = std::max(fm.height(), swatchSize);

    int y = padding;
    m_layerBands.clear();

    if (m_style->showTitle() && !m_style->title().isEmpty()) {
        p.setFont(m_style->titleFont());
        p.setPen(m_style->titleColor());
        p.drawText(QRect(padding, y, width() - 2 * padding, tfm.height()),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   tfm.elidedText(m_style->title(), Qt::ElideRight,
                                  width() - 2 * padding));
        y += tfm.height() + rowSpacing;
    }

    bool first = true;
    for (int i = m_canvas->layers().size() - 1; i >= 0; --i)
    {
        OpenSWMMVisLayer *layer = m_canvas->layers().at(i);
        if (!layer || !layer->isVisible()) continue;

        if (!first) y += kLayerSpacing;
        first = false;

        LayerBand band;
        band.layer = layer;
        band.yTop  = y;

        // Layer header
        p.setFont(m_style->layerHeaderFont());
        p.setPen(m_style->layerHeaderColor());
        p.drawText(QRect(padding, y, width() - 2 * padding, hfm.height()),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   hfm.elidedText(layer->name(), Qt::ElideRight,
                                  width() - 2 * padding));
        y += hfm.height();

        p.setFont(m_style->itemFont());
        auto paintRows = legendItemsFor(layer);
        applyItemOverrides(paintRows, layer, m_style);
        for (const auto &row : paintRows) {
            if (!row.visible) continue;
            const int itemTop = y;
            const QColor c = firstSymbolColor(row.symbol);
            const QRect swatchRect(padding,
                                   y + (rowH - swatchSize) / 2,
                                   swatchSize, swatchSize);
            p.setBrush(c);
            p.setPen(QPen(c.darker(140), 1.0));
            p.drawRect(swatchRect);

            const QRect textRect(padding + swatchSize + kSwatchPadding, y,
                                 width() - padding - swatchSize - kSwatchPadding - padding,
                                 rowH);
            p.setPen(m_style->itemColor());
            p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                       fm.elidedText(row.effectiveLabel(), Qt::ElideRight,
                                     textRect.width()));
            y += rowH + rowSpacing;

            // Record an item band so contextMenuEvent can map cursor-y →
            // (layer, classKey) for the per-swatch Change color… action.
            ItemBand ib;
            ib.classKey = row.classKey;
            ib.yTop     = itemTop;
            ib.yBottom  = y;
            band.items.append(ib);
        }

        band.yBottom = y;
        m_layerBands.append(band);
    }
}

OpenSWMMVisLayer *LegendOverlay::layerAtY(int y) const
{
    for (const auto &band : m_layerBands) {
        if (y >= band.yTop && y < band.yBottom)
            return band.layer.data();
    }
    return nullptr;
}

QPair<OpenSWMMVisLayer *, QString> LegendOverlay::itemAtY(int y) const
{
    for (const auto &band : m_layerBands) {
        if (!band.layer) continue;
        for (const auto &ib : band.items) {
            if (y >= ib.yTop && y < ib.yBottom)
                return { band.layer.data(), ib.classKey };
        }
    }
    return { nullptr, {} };
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
        // Dragging implies the user wants free placement.
        if (m_style && m_style->anchor() != Style::Anchor::Free)
            m_style->setAnchor(Style::Anchor::Free);
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

void LegendOverlay::contextMenuEvent(QContextMenuEvent *event)
{
    if (!m_canvas || !m_style) {
        QWidget::contextMenuEvent(event);
        return;
    }

    QMenu menu(this);

    // ── Per-swatch hit ────────────────────────────────────────────────
    // Resolves first; takes precedence over the layer-header menu so a
    // right-click directly on a row offers the per-class actions inline.
    const auto [itemLayer, classKey] = itemAtY(event->pos().y());
    if (itemLayer && !classKey.isEmpty()) {
        // Gap B1 — dispatch through LegendContent so multi-kind layers
        // (SWMMModelLayer) and results layers route through their
        // kind-qualified facades. The old qobject_cast chain permanently
        // disabled "Change color…" for model-layer rows on the overlay
        // even though the same edit worked from the dock.
        namespace LC = openswmmvis::map::LegendContent;
        const bool canEditColor = LC::supportsClassEdit(
            itemLayer, OpenSWMM::Render::ClassEditKind::Color);

        QAction *changeColor = menu.addAction(tr("Change color…"));
        changeColor->setEnabled(canEditColor);
        if (canEditColor) {
            // Snapshot the layer + classKey at menu-build time so the lambda
            // doesn't capture stale references after the menu closes.
            QPointer<OpenSWMMVisLayer> layerPtr = itemLayer;
            const QString classKeyCopy = classKey;
            // Best-effort starting colour for the colour picker — current
            // override if any, else first symbol color from the legend row.
            QColor seed = LC::colorForClass(itemLayer, classKey);
            if (!seed.isValid()) {
                // Walk the canonical rows to find this class's colour.
                for (const auto &row : LC::legendItemsFor(itemLayer)) {
                    if (row.classKey == classKey) {
                        seed = firstSymbolColor(row.symbol);
                        break;
                    }
                }
            }
            connect(changeColor, &QAction::triggered, this,
                    [this, layerPtr, classKeyCopy, seed]() {
                if (!layerPtr || !m_canvas) return;
                const QColor picked = QColorDialog::getColor(
                    seed.isValid() ? seed : QColor(Qt::white),
                    this,
                    tr("Change legend color"),
                    QColorDialog::ShowAlphaChannel);
                if (!picked.isValid()) return;   // user cancelled.
                if (auto *stack = m_canvas->undoStack()) {
                    stack->push(new openswmmvis::map::SetRendererClassColorCommand(
                        layerPtr.data(), classKeyCopy, picked));
                }
            });
        }
        menu.addSeparator();
    }

    OpenSWMMVisLayer *hitLayer = layerAtY(event->pos().y());
    if (hitLayer) {
        QAction *hideLayer = menu.addAction(
            tr("Hide layer “%1”").arg(hitLayer->name()));
        connect(hideLayer, &QAction::triggered, this,
                [hitLayer]() { hitLayer->setVisible(false); });

        // Move up / down within MapCanvas.
        const int idx = m_canvas->layers().indexOf(hitLayer);
        const int total = m_canvas->layers().size();
        QAction *moveUp = menu.addAction(tr("Move layer up"));
        moveUp->setEnabled(idx >= 0 && idx < total - 1);
        QPointer<MapCanvas> canvasPtr = m_canvas;
        connect(moveUp, &QAction::triggered, this, [canvasPtr, idx]() {
            if (canvasPtr) canvasPtr->moveLayer(idx, idx + 1);
        });
        QAction *moveDown = menu.addAction(tr("Move layer down"));
        moveDown->setEnabled(idx > 0);
        connect(moveDown, &QAction::triggered, this, [canvasPtr, idx]() {
            if (canvasPtr) canvasPtr->moveLayer(idx, idx - 1);
        });

        menu.addSeparator();
    }

    QAction *props = menu.addAction(tr("Properties…"));
    connect(props, &QAction::triggered, this, &LegendOverlay::openPropertiesDialog);

    menu.addSeparator();

    QAction *copyImg = menu.addAction(tr("Copy legend as image"));
    connect(copyImg, &QAction::triggered, this, &LegendOverlay::copyLegendImage);

    QAction *resetLayoutAct = menu.addAction(tr("Reset layout"));
    connect(resetLayoutAct, &QAction::triggered, this, &LegendOverlay::resetLayout);

    menu.addSeparator();

    QAction *hideLegend = menu.addAction(tr("Hide legend"));
    connect(hideLegend, &QAction::triggered, this, [this]() {
        hide();
        emit hideRequested();
    });

    menu.exec(event->globalPos());
    event->accept();
}

void LegendOverlay::openPropertiesDialog()
{
    auto *dlg = new LegendPropertiesDialog(m_style, this);
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void LegendOverlay::copyLegendImage()
{
    if (width() == 0 || height() == 0) return;
    QImage img(size(), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    render(&img);
    QApplication::clipboard()->setImage(img);
}

void LegendOverlay::resetLayout()
{
    if (!m_style) return;
    m_style->resetToDefaults();
    m_positioned = false;
    recomputeLayout();
    update();
}

bool LegendOverlay::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_canvas && event->type() == QEvent::Resize) {
        if (m_style && m_style->anchor() != Style::Anchor::Free)
            anchorToCanvas();
        else
            clampInsideCanvas();
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace openswmmvis::ui
