/**
 * @file      legendoverlay.cpp
 * @brief     LegendOverlay is a child QWidget of MapCanvas.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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
#include "render/legendlayout.h"
#include "render/legendoverlaystyle.h"
#include "render/legendpainter.h"
#include "render/legendsymbolitem.h"
#include "render/mapboxchrome.h"
#include "ui/accessibility/customaccessible.h"
#include "ui/dialogs/legendpropertiesdialog.h"

#include <QApplication>
#include <QClipboard>
#include <QColorDialog>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QImage>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace openswmmvis::ui
{

using Style = OpenSWMM::Render::LegendOverlayStyle;

namespace
{

/**
 * @brief Delegate swatch seed selection to the shared legend-content authority.
 * @param[in] style Symbol style.
 * @return Shared LegendContent color/fallback result.
 */
QColor firstSymbolColor(const OpenSWMM::Render::SymbolStyle &style)
{
    return openswmmvis::map::LegendContent::firstSymbolColor(style);
}

} // namespace

LegendOverlay::LegendOverlay(MapCanvas *canvas) : QWidget(canvas)
{
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);
    setCursor(Qt::OpenHandCursor);
    // Slice US.B2 — track mouse moves without a button so the cursor reflects
    // the resize grip the pointer is hovering over.
    setMouseTracking(true);
    // Allow paint with transparency over the canvas's framebuffer + scene.
    setAttribute(Qt::WA_TranslucentBackground, true);

    // Default to a self-owned style; replaced via setStyle() once a
    // project-scoped instance lands with Phase 8.6.12 persistence.
    m_style = new Style(this);
    m_ownsStyle = true;
    connect(m_style, &Style::changed, this, &LegendOverlay::onStyleChanged);

    setCanvas(canvas);
    setAccessibleName(tr("Map legend"));
    hide(); // user toggles via actionShowLegend
}

QString LegendOverlay::summaryText() const
{
    if (!m_canvas || !m_style)
        return {};
    using Kind = OpenSWMM::Render::LegendEntry::Kind;
    QStringList parts;
    if (m_style->showTitle() && !m_style->title().isEmpty())
        parts.append(m_style->title());
    // The lines the box draws: each heading with its rows' labels.
    const auto entries = openswmmvis::map::LegendContent::buildEntries(m_canvas, m_style);
    QString heading;
    QStringList labels;
    const auto flush = [&]() {
        if (heading.isNull())
            return;
        parts.append(labels.isEmpty()
                         ? heading
                         : tr("%1: %2").arg(heading, labels.join(QStringLiteral(", "))));
        labels.clear();
    };
    for (const auto &entry : entries) {
        if (entry.kind == Kind::GroupHeading) {
            flush();
            heading = entry.label;
        } else if (!entry.label.isEmpty()) {
            labels.append(entry.label);
        }
    }
    flush();
    return parts.join(QStringLiteral("; "));
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
    if (m_canvas == canvas)
        return;

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
    if (m_style == style)
        return;
    if (m_style) {
        disconnect(m_style, nullptr, this, nullptr);
        if (m_ownsStyle)
            m_style->deleteLater();
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
    recomputeLayout();
    update();
}

void LegendOverlay::connectLayer(OpenSWMMVisLayer *layer)
{
    if (!layer)
        return;
    // Qt 6 asserts on Qt::UniqueConnection with non-PMF slots; clear any
    // prior connections from this layer to us so repeated calls don't stack.
    disconnect(layer, nullptr, this, nullptr);
    auto refresh = [this]() {
        recomputeLayout();
        update();
    };
    connect(layer, &OpenSWMMVisLayer::visibilityChanged, this, refresh);
    connect(layer, &OpenSWMMVisLayer::nameChanged, this, refresh);
    connect(layer, &OpenSWMMVisLayer::repaintRequested, this, refresh);
}

void LegendOverlay::disconnectLayer(OpenSWMMVisLayer *layer)
{
    if (!layer)
        return;
    disconnect(layer, nullptr, this, nullptr);
}

void LegendOverlay::recomputeLayout()
{
    if (!m_canvas || !m_style) {
        m_entries.clear();
        m_layout = {};
        resize(0, 0);
        return;
    }

    // A live resize owns the geometry; don't fight it.
    if (m_resizing)
        return;

    m_entries = openswmmvis::map::LegendContent::buildEntries(m_canvas, m_style);
    m_layout = OpenSWMM::Render::LegendLayout::compute(m_entries, *m_style);
    if (m_layout.isEmpty()) {
        m_contentHeight = 0;
        resize(0, 0);
        return;
    }
    const int contentH = int(std::ceil(m_layout.contentSize.height()));
    m_contentHeight = contentH;

    // An explicit size overrides the fitted size on each axis (0 = fit).
    const int boxW = m_layout.width;
    int boxH = m_style->explicitHeight() > 0 ? m_style->explicitHeight() : contentH;
    // Keep the box on the map so it can be dragged; taller rows scroll inside it.
    const int margin = OpenSWMM::Render::kMapBoxAnchorMargin;
    if (m_canvas->height() > 0)
        boxH = std::min(boxH, std::max(48, m_canvas->height() - 2 * margin));
    resize(boxW, boxH);
    m_scrollY = std::clamp(m_scrollY, 0, std::max(0, contentH - boxH));

    if (!m_positioned) {
        // Restore a saved free position; otherwise anchor.
        if (m_style->anchor() == Style::Anchor::Free && !m_style->freePosition().isNull()) {
            move(m_style->freePosition());
            clampInsideCanvas();
        } else {
            anchorToCanvas();
        }
        m_positioned = true;
    } else if (m_style->anchor() != Style::Anchor::Free) {
        anchorToCanvas();
    } else {
        clampInsideCanvas();
    }
}

void LegendOverlay::anchorToCanvas()
{
    if (!m_canvas || !m_style)
        return;
    if (m_style->anchor() == Style::Anchor::Free) {
        clampInsideCanvas(); // honor the current position
        return;
    }
    move(OpenSWMM::Render::anchoredMapBoxPosition(size(), m_canvas->size(), *m_style));
}

void LegendOverlay::clampInsideCanvas()
{
    if (!m_canvas)
        return;
    const int maxX = std::max(0, m_canvas->width() - width());
    const int maxY = std::max(0, m_canvas->height() - height());
    move(std::clamp(x(), 0, maxX), std::clamp(y(), 0, maxY));
}

void LegendOverlay::paintEvent(QPaintEvent * /*event*/)
{
    if (!m_canvas || !m_style || width() == 0 || height() == 0)
        return;

    QPainter p(this);
    paintLegend(p, size(), m_scrollY);
    // A thin thumb on the right edge shows the rows scroll (mouse wheel).
    const int maxScroll = m_contentHeight - height();
    if (maxScroll > 0) {
        const int track = height() - 8;
        const int thumb = std::max(16, track * height() / m_contentHeight);
        QColor c = m_style->itemColor();
        c.setAlpha(110);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawRoundedRect(QRectF(width() - 6, 4 + (track - thumb) * m_scrollY / maxScroll, 3, thumb),
                          1.5, 1.5);
    }
}

OpenSWMM::Render::LegendLayoutResult LegendOverlay::layoutForWidth(int width) const
{
    if (m_layout.width == width || !m_style)
        return m_layout;
    // During a live resize the box is wider or narrower than the style says.
    return OpenSWMM::Render::LegendLayout::compute(m_entries, *m_style, width);
}

void LegendOverlay::paintLegend(QPainter &p, const QSize &size, int scrollY) const
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setOpacity(std::clamp<qreal>(m_style->opacity(), 0.0, 1.0));
    const QRectF box(QPointF(0, 0), QSizeF(size));
    OpenSWMM::Render::paintMapBoxChrome(p, box, *m_style);

    // Rows scroll inside the box; the background and frame stay put.
    p.setClipPath(OpenSWMM::Render::mapBoxPath(box, *m_style));
    p.translate(0, -scrollY);
    OpenSWMM::Render::LegendPainter::paint(p, layoutForWidth(size.width()), m_entries, *m_style);
    p.restore();
}

const OpenSWMM::Render::LegendEntry *LegendOverlay::entryAt(const QPoint &pos) const
{
    const auto *box = m_layout.boxAt(QPointF(pos.x(), pos.y() + m_scrollY));
    if (!box || box->entryIndex < 0 || box->entryIndex >= m_entries.size())
        return nullptr;
    return &m_entries.at(box->entryIndex);
}

int LegendOverlay::edgesAt(const QPoint &pos) const
{
    constexpr int kGrip = 6; // px reach of an edge/corner grip
    int edges = 0;
    if (pos.x() <= kGrip)
        edges |= 1; // Left
    if (pos.x() >= width() - kGrip)
        edges |= 2; // Right
    if (pos.y() <= kGrip)
        edges |= 4; // Top
    if (pos.y() >= height() - kGrip)
        edges |= 8; // Bottom
    return edges;
}

void LegendOverlay::updateCursor(int edges)
{
    switch (edges) {
    case 1:
    case 2:
        setCursor(Qt::SizeHorCursor);
        break; // L / R
    case 4:
    case 8:
        setCursor(Qt::SizeVerCursor);
        break; // T / B
    case (1 | 4):
    case (2 | 8):
        setCursor(Qt::SizeFDiagCursor);
        break; // TL / BR
    case (2 | 4):
    case (1 | 8):
        setCursor(Qt::SizeBDiagCursor);
        break; // TR / BL
    default:
        setCursor(Qt::OpenHandCursor);
        break; // interior
    }
}

void LegendOverlay::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        // Selecting the legend shows its style in the Properties panel.
        if (m_canvas && m_style)
            m_canvas->selectOverlayStyle(m_style, tr("Legend"));
        const int edges = edgesAt(event->pos());
        if (edges != 0) {
            // Slice US.B2 — begin an interactive resize from this edge/corner.
            m_resizing = true;
            m_resizeEdges = edges;
            m_resizeStartGeom = geometry();
            m_resizeStartGlobal = event->globalPosition().toPoint();
        } else {
            m_dragging = true;
            m_dragStartOffset = event->pos();
            setCursor(Qt::ClosedHandCursor);
        }
        event->accept();
    } else {
        QWidget::mousePressEvent(event);
    }
}

void LegendOverlay::mouseMoveEvent(QMouseEvent *event)
{
    if (m_resizing && (event->buttons() & Qt::LeftButton)) {
        // Resize the dragged edge(s); the opposite edge stays pinned. The
        // floors mirror LegendOverlayStyle's explicit-size clamps.
        const QPoint d = event->globalPosition().toPoint() - m_resizeStartGlobal;
        QRect g = m_resizeStartGeom;
        if (m_resizeEdges & 1)
            g.setLeft(g.left() + d.x());
        if (m_resizeEdges & 2)
            g.setRight(g.right() + d.x());
        if (m_resizeEdges & 4)
            g.setTop(g.top() + d.y());
        if (m_resizeEdges & 8)
            g.setBottom(g.bottom() + d.y());
        if (g.width() < 80) {
            if (m_resizeEdges & 1)
                g.setLeft(g.right() - 80);
            else
                g.setWidth(80);
        }
        if (g.height() < 48) {
            if (m_resizeEdges & 4)
                g.setTop(g.bottom() - 48);
            else
                g.setHeight(48);
        }
        setGeometry(g);
        event->accept();
        return;
    }
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        QPoint newPos = mapToParent(event->pos()) - m_dragStartOffset;
        if (m_canvas) {
            newPos.setX(std::clamp(newPos.x(), 0, std::max(0, m_canvas->width() - width())));
            newPos.setY(std::clamp(newPos.y(), 0, std::max(0, m_canvas->height() - height())));
        }
        move(newPos);
        // Dragging implies the user wants free placement.
        if (m_style && m_style->anchor() != Style::Anchor::Free)
            m_style->setAnchor(Style::Anchor::Free);
        event->accept();
        return;
    }
    // Hover: reflect the grip under the cursor.
    updateCursor(edgesAt(event->pos()));
    QWidget::mouseMoveEvent(event);
}

void LegendOverlay::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_resizing) {
        // Commit the size through the style so it persists + syncs the
        // properties dialog. Keep the resize guard until ALL setters finish:
        // each emits changed(), which otherwise restores the old geometry.
        const QRect resized = geometry();
        if (m_style) {
            if (m_style->anchor() != Style::Anchor::Free)
                m_style->setAnchor(Style::Anchor::Free);
            m_style->setExplicitWidth(resized.width());
            m_style->setExplicitHeight(resized.height());
            m_style->setFreePosition(resized.topLeft());
        }
        m_resizing = false;
        m_resizeEdges = 0;
        recomputeLayout();
        updateCursor(edgesAt(event->pos()));
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        setCursor(Qt::OpenHandCursor);
        // Persist the free position so save/reopen restores it.
        if (m_style && m_style->anchor() == Style::Anchor::Free)
            m_style->setFreePosition(pos());
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void LegendOverlay::wheelEvent(QWheelEvent *event)
{
    const int maxScroll = std::max(0, m_contentHeight - height());
    if (maxScroll == 0) {
        QWidget::wheelEvent(event); // nothing to scroll: the map gets the wheel
        return;
    }
    // Trackpads report pixels; a mouse notch (120) scrolls 40 px.
    const int dy = !event->pixelDelta().isNull() ? event->pixelDelta().y()
                                                  : event->angleDelta().y() * 40 / 120;
    m_scrollY = std::clamp(m_scrollY - dy, 0, maxScroll);
    update();
    event->accept();
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
    const OpenSWMM::Render::LegendEntry *hit = entryAt(event->pos());
    OpenSWMMVisLayer *itemLayer =
        hit ? qobject_cast<OpenSWMMVisLayer *>(hit->layer.data()) : nullptr;
    const QString classKey = hit ? hit->classKey : QString();
    if (itemLayer && !classKey.isEmpty()) {
        // Gap B1 — dispatch through LegendContent so multi-kind layers
        // (SWMMModelLayer) and results layers route through their
        // kind-qualified facades. The old qobject_cast chain permanently
        // disabled "Change color…" for model-layer rows on the overlay
        // even though the same edit worked from the dock.
        namespace LC = openswmmvis::map::LegendContent;
        const bool canEditColor =
            LC::supportsClassEdit(itemLayer, OpenSWMM::Render::ClassEditKind::Color);

        QAction *changeColor = menu.addAction(tr("Change color…"));
        changeColor->setEnabled(canEditColor);
        if (canEditColor) {
            // Snapshot the layer + classKey at menu-build time so the lambda
            // doesn't capture stale references after the menu closes.
            QPointer<OpenSWMMVisLayer> layerPtr = itemLayer;
            const QString classKeyCopy = classKey;
            // Best-effort starting color for the color picker — current
            // override if any, else first symbol color from the legend row.
            QColor seed = LC::colorForClass(itemLayer, classKey);
            if (!seed.isValid()) {
                // Walk the canonical rows to find this class's color.
                for (const auto &row : LC::legendItemsFor(itemLayer)) {
                    if (row.classKey == classKey) {
                        seed = firstSymbolColor(row.symbol);
                        break;
                    }
                }
            }
            connect(changeColor, &QAction::triggered, this, [this, layerPtr, classKeyCopy, seed]() {
                if (!layerPtr || !m_canvas)
                    return;
                const QColor picked = QColorDialog::getColor(
                    seed.isValid() ? seed : QColor(Qt::white), this, tr("Change legend color"),
                    QColorDialog::ShowAlphaChannel);
                if (!picked.isValid())
                    return; // user canceled.
                if (auto *stack = m_canvas->undoStack()) {
                    stack->push(new openswmmvis::map::SetRendererClassColorCommand(
                        layerPtr.data(), classKeyCopy, picked));
                }
            });
        }
        menu.addSeparator();
    }

    OpenSWMMVisLayer *hitLayer = itemLayer;
    if (hitLayer) {
        QAction *hideLayer = menu.addAction(tr("Hide layer “%1”").arg(hitLayer->name()));
        connect(hideLayer, &QAction::triggered, this,
                [hitLayer]() { hitLayer->setVisible(false); });

        // Move up / down within MapCanvas.
        const int idx = m_canvas->layers().indexOf(hitLayer);
        const int total = m_canvas->layers().size();
        QAction *moveUp = menu.addAction(tr("Move layer up"));
        moveUp->setEnabled(idx >= 0 && idx < total - 1);
        QPointer<MapCanvas> canvasPtr = m_canvas;
        connect(moveUp, &QAction::triggered, this, [canvasPtr, idx]() {
            if (canvasPtr)
                canvasPtr->moveLayer(idx, idx + 1);
        });
        QAction *moveDown = menu.addAction(tr("Move layer down"));
        moveDown->setEnabled(idx > 0);
        connect(moveDown, &QAction::triggered, this, [canvasPtr, idx]() {
            if (canvasPtr)
                canvasPtr->moveLayer(idx, idx - 1);
        });

        menu.addSeparator();
    }

    QAction *props = menu.addAction(tr("Properties…"));
    connect(props, &QAction::triggered, this, &LegendOverlay::openPropertiesDialog);

    menu.addSeparator();

    QAction *copyImg = menu.addAction(tr("Copy legend as image"));
    connect(copyImg, &QAction::triggered, this, &LegendOverlay::copyLegendImage);

    // Slice US.B2 — drop any manual resize back to natural auto-fit.
    QAction *autoSizeAct = menu.addAction(tr("Auto-size legend"));
    autoSizeAct->setEnabled(m_style &&
                            (m_style->explicitWidth() > 0 || m_style->explicitHeight() > 0));
    connect(autoSizeAct, &QAction::triggered, this, &LegendOverlay::autoSize);

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

namespace
{

/** The painted legend to screen readers: its rows as text (plan B2). */
class LegendOverlayAccessible : public accessibility::LiveWidgetAccessible
{
public:
    explicit LegendOverlayAccessible(LegendOverlay *legend)
        : LiveWidgetAccessible(legend, QAccessible::Graphic)
    {
    }

protected:
    QString descriptionText() const override
    {
        return static_cast<LegendOverlay *>(widget())->summaryText();
    }
};

void registerLegendOverlayAccessible()
{
    QAccessible::installFactory(
        &accessibility::createAccessible<LegendOverlay, LegendOverlayAccessible>);
}

} // namespace

Q_COREAPP_STARTUP_FUNCTION(registerLegendOverlayAccessible)

void LegendOverlay::openPropertiesDialog()
{
    auto *dlg = new LegendPropertiesDialog(m_style, this);
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void LegendOverlay::copyLegendImage()
{
    if (width() == 0 || height() == 0)
        return;
    // The whole legend, also the rows scrolled out of view.
    const QSize full(width(), std::max(height(), m_contentHeight));
    QImage img(full, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    paintLegend(p, full, 0);
    p.end();
    QApplication::clipboard()->setImage(img);
}

void LegendOverlay::resetLayout()
{
    if (!m_style)
        return;
    m_style->resetToDefaults();
    m_positioned = false;
    recomputeLayout();
    update();
}

void LegendOverlay::autoSize()
{
    // Slice US.B2 — clear explicit width/height; the next recomputeLayout
    // fits the box to its content again. Position is left untouched.
    if (!m_style)
        return;
    m_style->setExplicitWidth(0);
    m_style->setExplicitHeight(0);
    recomputeLayout();
    update();
}

bool LegendOverlay::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_canvas && event->type() == QEvent::Resize) {
        // Re-measure: the height cap follows the map; placement re-anchors or clamps.
        recomputeLayout();
        update();
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace openswmmvis::ui
