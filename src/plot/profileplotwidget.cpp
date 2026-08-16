/*!
 * \file   profileplotwidget.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "plot/profileplotwidget.h"
#include "ui/theme/thememanager.h"
#include "ui/theme/themetokens.h"

#include "plot/profileplotoptions.h"

#include <QFontMetricsF>
#include <QInputDialog>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRect>
#include <QResizeEvent>
#include <QRubberBand>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

// Visual constants ---------------------------------------------------------

constexpr int    kMarginLeft         = 64;
constexpr int    kMarginRight        = 16;
constexpr int    kMarginTop          = 16;
constexpr int    kMarginBottomBase   = 40;   // bottom margin without label rows
constexpr int    kLabelRowGap        = 4;    // visual gap between rows
constexpr double kPadFracY     = 0.0;    // no vertical padding — data range fills plot rect
constexpr double kPadFracX     = 0.02;
constexpr double kBeddingBelowFrac   = 0.05;  // bedding floor = minInvert − 5% range
// Each zero-length link (pump/weir/orifice/outlet) is allotted this fraction
// of the average conduit length as its visual gap.  Renders as a discrete
// glyph between the two connecting nodes without contributing to chainage.
constexpr double kZeroLengthGapFrac  = 0.40;

constexpr qreal  kConduitLineWidth  = 1.5;
constexpr qreal  kNodeBarWidth      = 1.5;
constexpr qreal  kHglLineWidth      = 2.2;
constexpr qreal  kEglLineWidth      = 1.6;
constexpr qreal  kEnvelopeAlpha     = 0.35;

// Half-width of the manhole-shaft "tube" drawn at each node (pixels).
// HGL lines and fills inset their end x-coords by this amount so the
// link rendering stops at the tube edge instead of bleeding into the
// node glyph. Duplicated as locals in paintNodes / hit-testing — kept
// in sync; this file-scope copy is the one the per-link HGL renderer
// consumes.
constexpr qreal  kHglPipeEdgeInsetPx = 3.5;
constexpr qreal  kAxisEdgeAlongPx    = 28.0;
constexpr qreal  kAxisLabelBandPx    = 48.0;

const QColor kSoilFillColor   (0xC6, 0xA9, 0x7A, 130);
const QColor kBeddingFillColor(0x9C, 0x82, 0x5A, 110);

// UI redesign P4 — plot chrome colors come from the theme tokens so the
// plot flips with the light/dark scheme (data fills stay hardcoded).
inline const openswmmvis::ui::ThemeColors &plotTheme()
{
    return openswmmvis::ui::ThemeManager::instance()->colors();
}


// Per-type fills (interior of the glyph / tube).  Picked from a friendly
// palette that stays distinguishable on the soil-coloured background.
QColor fillForNodeKind(ProfileBuilder::NodeKind k)
{
    switch (k) {
    case ProfileBuilder::NodeKind::Junction: return QColor(0xCF, 0xE2, 0xF3);  // light blue
    case ProfileBuilder::NodeKind::Outfall:  return QColor(0xF4, 0xCC, 0xCC);  // light red
    case ProfileBuilder::NodeKind::Storage:  return QColor(0xD9, 0xEA, 0xD3);  // light green
    case ProfileBuilder::NodeKind::Divider:  return QColor(0xFF, 0xE5, 0x99);  // light amber
    // No glyph is drawn for a virtual junction (see paintNodes) — the value
    // exists only so the switch stays exhaustive.
    case ProfileBuilder::NodeKind::VirtualJunction: return Qt::transparent;
    }
    return Qt::white;
}

QColor outlineForNodeKind(ProfileBuilder::NodeKind k)
{
    switch (k) {
    case ProfileBuilder::NodeKind::Junction: return QColor(0x1C, 0x4E, 0x7A);
    case ProfileBuilder::NodeKind::Outfall:  return QColor(0x8A, 0x21, 0x21);
    case ProfileBuilder::NodeKind::Storage:  return QColor(0x2D, 0x6A, 0x2D);
    case ProfileBuilder::NodeKind::Divider:  return QColor(0x9C, 0x6F, 0x14);
    case ProfileBuilder::NodeKind::VirtualJunction: return Qt::transparent;
    }
    return Qt::black;
}

QColor fillForLinkKind(ProfileBuilder::LinkKind k)
{
    switch (k) {
    case ProfileBuilder::LinkKind::Conduit: return QColor(0xEE, 0xEE, 0xEE);
    case ProfileBuilder::LinkKind::Pump:    return QColor(0xFF, 0xD6, 0xA5);  // peach
    case ProfileBuilder::LinkKind::Weir:    return QColor(0x5D, 0x40, 0x37);  // dark brown (masonry block)
    case ProfileBuilder::LinkKind::Orifice: return QColor(0xC9, 0xE4, 0xCA);  // mint
    case ProfileBuilder::LinkKind::Outlet:  return QColor(0xF5, 0xC2, 0xC7);  // pink
    }
    return Qt::white;
}

QColor outlineForLinkKind(ProfileBuilder::LinkKind k)
{
    switch (k) {
    case ProfileBuilder::LinkKind::Conduit: return QColor(0x33, 0x33, 0x33);
    case ProfileBuilder::LinkKind::Pump:    return QColor(0xC2, 0x70, 0x1A);
    case ProfileBuilder::LinkKind::Weir:    return QColor(0x3E, 0x2A, 0x1E);  // very dark brown
    case ProfileBuilder::LinkKind::Orifice: return QColor(0x2E, 0x6E, 0x39);
    case ProfileBuilder::LinkKind::Outlet:  return QColor(0xA3, 0x3D, 0x4C);
    }
    return Qt::black;
}

// Returns true when v is finite (i.e. not the initial +/-inf min/max sentinel).
inline bool isFinite(double v) { return std::isfinite(v); }

bool isMinEdge(ProfilePlotWidget::AxisEdge edge) noexcept
{
    return edge == ProfilePlotWidget::AxisEdge::XMinimum
        || edge == ProfilePlotWidget::AxisEdge::YMinimum;
}

QString labelForEdge(ProfilePlotWidget::AxisEdge edge)
{
    switch (edge) {
    case ProfilePlotWidget::AxisEdge::XMinimum: return QObject::tr("X minimum");
    case ProfilePlotWidget::AxisEdge::XMaximum: return QObject::tr("X maximum");
    case ProfilePlotWidget::AxisEdge::YMinimum: return QObject::tr("Y minimum");
    case ProfilePlotWidget::AxisEdge::YMaximum: return QObject::tr("Y maximum");
    case ProfilePlotWidget::AxisEdge::None: break;
    }
    return {};
}

bool parseDoubleLocaleAware(const QString &text, double &value)
{
    bool ok = false;
    value = QLocale().toDouble(text.trimmed(), &ok);
    if (!ok)
        value = text.trimmed().toDouble(&ok);
    return ok && std::isfinite(value);
}

// Flat-bottom structural links — render with the ground line dropping to
// the inlet invert across the link.  Conduits AND orifices are excluded
// because both enclose flow with a proper crown (the orifice's crown
// sits at sill + maxDepth — same convention as a conduit's crown).
bool linkKindIsExcavated(ProfileBuilder::LinkKind k)
{
    using K = ProfileBuilder::LinkKind;
    return k == K::Weir || k == K::Pump || k == K::Outlet;
}

// Links rendered with the conduit "tube" geometry: pipe-style invert and
// crown lines, ground rim above is uninterrupted.  Conduits use the
// sloped offset1/offset2 per end; orifices use a flat sill at
// (inletInvert + offset1) at both ends.
bool linkKindRendersAsConduit(ProfileBuilder::LinkKind k)
{
    using K = ProfileBuilder::LinkKind;
    return k == K::Conduit || k == K::Orifice;
}

// Effective inlet / outlet inverts for the bidirectional HGL waterfall
// rule. `inletInv` is the threshold the inlet HGL must exceed for the
// link to render; `outletInv` is the floor a forward waterfall lands on
// (and the rise a mirror waterfall starts from). Honours
// LinkStatic::reversed so the geometric upstream / downstream node
// always pin the polyline regardless of SWMM's topological direction.
// Returns false for Pump / Outlet (caller must skip).
bool hglInletOutletInv(const ProfileBuilder::LinkStatic &l,
                       const ProfileBuilder::NodeStatic &nodeI,
                       const ProfileBuilder::NodeStatic &nodeJ,
                       double &inletInv, double &outletInv)
{
    using K = ProfileBuilder::LinkKind;
    if (l.kind == K::Pump || l.kind == K::Outlet) return false;
    // inletIdx = i  → inlet is upstream node (nodeI).
    //          = i+1 → inlet is downstream node (nodeJ) when reversed.
    const auto &inletNode  = l.reversed ? nodeJ : nodeI;
    const auto &outletNode = l.reversed ? nodeI : nodeJ;
    if (l.kind == K::Weir) {
        // Weir crest is the single wet/dry threshold for both edges —
        // water below crest can't flow over from either side, so a dry
        // edge pins to the crest (not the bare receiving invert).
        const double crest = inletNode.invertElev + l.crestHeight;
        inletInv  = crest;
        outletInv = crest;
    } else if (l.kind == K::Orifice) {
        // Orifice has a flat sill at (inletInvert + offset1) — same
        // invert at both ends, matching the existing fill convention.
        const double sill = inletNode.invertElev + l.offset1;
        inletInv  = sill;
        outletInv = sill;
    } else { // Conduit
        // Path-oriented invert math: profilenetworkadapter_model stores
        // offset1/offset2 in path-traversal order (offset1 above nodeI,
        // offset2 above nodeJ) regardless of LinkStatic::reversed. That
        // makes each conduit edge's invert the local node invert plus the
        // local offset — adverse-slope pipes (nodeJ.invert + offset2 above
        // nodeI.invert + offset1) report distinct values here without any
        // reversed-flag swap. Callers use these at xU (= path-upstream
        // pixel) and xD (= path-downstream pixel), so writing them in
        // path orientation also matches how they're consumed.
        inletInv  = nodeI.invertElev + l.offset1;
        outletInv = nodeJ.invertElev + l.offset2;
    }
    return true;
}

// Closed-top crown elevation at each edge of a link, used by the HGL
// fill to clamp the water polygon to the pipe ceiling.  Returns false
// for links with no closed top (Weir / Pump / Outlet) — callers skip
// the clamp in that case so the fill follows the HGL line freely.
// Mirrors the crownUpstream/crownDownstream lambdas used by the soil
// fill so water and soil agree on the pipe ceiling.
bool hglEdgeCrown(const ProfileBuilder::LinkStatic &l,
                  const ProfileBuilder::NodeStatic &nodeI,
                  const ProfileBuilder::NodeStatic &nodeJ,
                  double &crownU, double &crownD)
{
    using K = ProfileBuilder::LinkKind;
    if (l.maxDepth <= 0.0) return false;
    if (l.kind == K::Conduit) {
        // Same path-oriented invariant as hglInletOutletInv: offset1 is at
        // nodeI, offset2 at nodeJ regardless of LinkStatic::reversed.
        crownU = nodeI.invertElev + l.offset1 + l.maxDepth;
        crownD = nodeJ.invertElev + l.offset2 + l.maxDepth;
        return true;
    }
    if (l.kind == K::Orifice) {
        const auto &inletNode = l.reversed ? nodeJ : nodeI;
        const double top = inletNode.invertElev + l.offset1 + l.maxDepth;
        crownU = top;
        crownD = top;
        return true;
    }
    return false;
}

// Returns the 2-point HGL polyline for one link.
//
// Per-edge wet/dry test: an edge is "wet" when the connected node's HGL
// equals or exceeds the link's invert at that edge; otherwise it's dry.
// The edge elevation is the node HGL when wet, the invert when dry.
// The line is a single straight segment between the two edge
// elevations — wet/wet gives a normal sloped HGL line, wet/dry tilts
// down to the dry-side invert (visual "waterfall" via the slope), and
// dry/dry traces the link bed.
//
// xU/xD are upstream/downstream node chainages (caller insets to pipe
// edges in pixel space after dataToPixel mapping). Caller must
// pre-skip Pump/Outlet and guard NaN inputs.
QVector<QPointF> hglPolylineForLink(double xU, double xD,
                                    double upHgl, double downHgl,
                                    double inletInv, double outletInv)
{
    const double topU = (upHgl   >= inletInv)  ? upHgl   : inletInv;
    const double topD = (downHgl >= outletInv) ? downHgl : outletInv;
    return { QPointF(xU, topU), QPointF(xD, topD) };
}

// Builds the per-link HGL fill top polyline (data coordinates) and
// returns the inlet/outlet invert elevations the caller uses to close
// the polygon along the link bed.  Implements the same two rules used
// for the HGL *line*: the bidirectional waterfall (dry edge pinned to
// invert) and the crown clamp (pressurized region tracks the crown,
// with a crossing vertex when HGL crosses the crown inside the link).
// Used by both the per-period (paintHglFill) and envelope
// (paintSeriesEnvelope) renderers so the fill always agrees with the
// line.  Returns false when no fill should be drawn (Pump / Outlet,
// NaN inputs, or both edges dry).
bool buildHglFillTop(const ProfileBuilder::LinkStatic &l,
                     const ProfileBuilder::NodeStatic &nodeI,
                     const ProfileBuilder::NodeStatic &nodeJ,
                     double xU, double xD, double vU, double vD,
                     QVector<QPointF> &top,
                     double &inletInv, double &outletInv)
{
    if (!isFinite(vU) || !isFinite(vD)) return false;
    if (!hglInletOutletInv(l, nodeI, nodeJ, inletInv, outletInv)) return false;
    if (vU < inletInv && vD < outletInv) return false;
    top = hglPolylineForLink(xU, xD, vU, vD, inletInv, outletInv);

    double crownU, crownD;
    if (hglEdgeCrown(l, nodeI, nodeJ, crownU, crownD)) {
        const double a = top[0].y();
        const double b = top[1].y();
        const bool upHigh = (a > crownU);
        const bool dnHigh = (b > crownD);
        if (upHigh && dnHigh) {
            top[0].setY(crownU);
            top[1].setY(crownD);
        } else if (upHigh || dnHigh) {
            const double denom = (b - a) - (crownD - crownU);
            const double t = (crownU - a) / denom;
            const double xCross = xU + t * (xD - xU);
            const double yCross = crownU + t * (crownD - crownU);
            QVector<QPointF> clamped;
            clamped.reserve(3);
            clamped.append(QPointF(xU, upHigh ? crownU : a));
            clamped.append(QPointF(xCross, yCross));
            clamped.append(QPointF(xD, dnHigh ? crownD : b));
            top = clamped;
        }
    }
    return true;
}

QPen makeLinePen(const QColor &c, qreal width, bool dashed = false,
                 bool longDash = false)
{
    QPen pen(c);
    pen.setWidthF(width);
    pen.setCosmetic(false);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    if (dashed) {
        QVector<qreal> dashes = longDash ? QVector<qreal>{ 12.0, 6.0 }
                                          : QVector<qreal>{ 6.0, 4.0 };
        pen.setDashPattern(dashes);
    }
    return pen;
}

QColor withAlphaF(const QColor &c, qreal alpha)
{
    QColor out = c;
    out.setAlphaF(alpha);
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ProfilePlotWidget::ProfilePlotWidget(QWidget *parent)
    : QWidget(parent)
{
    setAutoFillBackground(true);
    const auto applyPlotBackground = [this] {
        QPalette pal = palette();
        pal.setColor(QPalette::Window, plotTheme().plotBackground);
        setPalette(pal);
        update();
    };
    applyPlotBackground();
    connect(openswmmvis::ui::ThemeManager::instance(),
            &openswmmvis::ui::ThemeManager::themeChanged,
            this, applyPlotBackground);
    setMinimumSize(360, 240);
    setMouseTracking(true);
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void ProfilePlotWidget::setPath(const ProfileBuilder::PathStatic &path)
{
    m_path = path;
    recomputeBounds();
    update();
}

void ProfilePlotWidget::setSeries(const QVector<SeriesBinding> &series)
{
    m_series = series;
    recomputeBounds();
    update();
}

void ProfilePlotWidget::setCurrentPeriod(int seriesIdx, int period)
{
    m_currentSrc    = seriesIdx;
    m_currentPeriod = period;
    update();
}

void ProfilePlotWidget::setCurrentPeriod(int period)
{
    m_currentSrc    = (m_series.isEmpty() ? -1 : 0);
    m_currentPeriod = period;
    update();
}

// Static dispatch on OutputKind ─────────────────────────────────────────────
bool ProfilePlotWidget::isCurrentTimeKind(ProfileBuilder::OutputKind k)
{
    using K = ProfileBuilder::OutputKind;
    return k == K::HGL || k == K::EGL || k == K::WaterSurface;
}

const QVector<QVector<double>> &
ProfilePlotWidget::currentTimeArray(const ProfileBuilder::SourceDerived &d,
                                     ProfileBuilder::OutputKind k)
{
    using K = ProfileBuilder::OutputKind;
    static const QVector<QVector<double>> kEmpty;
    switch (k) {
    case K::HGL:          return d.hglByPeriod;
    case K::EGL:          return d.eglByPeriod;
    case K::WaterSurface: return d.waterSurfaceByPeriod;
    default:              return kEmpty;
    }
}

const QVector<double> &
ProfilePlotWidget::envelopeArray(const ProfileBuilder::SourceDerived &d,
                                  ProfileBuilder::OutputKind k)
{
    using K = ProfileBuilder::OutputKind;
    static const QVector<double> kEmpty;
    switch (k) {
    case K::MaxHGL:          return d.maxHgl;
    case K::MaxEGL:          return d.maxEgl;
    case K::MinHGL:          return d.minHgl;
    case K::MinEGL:          return d.minEgl;
    case K::MaxWaterSurface: return d.maxWaterSurface;
    case K::MinWaterSurface: return d.minWaterSurface;
    default:                 return kEmpty;
    }
}

void ProfilePlotWidget::setCurrentDateTime(const QDateTime &dt)
{
    if (m_currentDateTime == dt) return;
    m_currentDateTime = dt;
    update();
}

void ProfilePlotWidget::setLayerToggles(const LayerToggles &toggles)
{
    m_toggles = toggles;
    update();
}

void ProfilePlotWidget::setOptions(ProfilePlotOptions *options)
{
    if (m_options == options) return;
    if (m_options)
        disconnect(m_options.data(), nullptr, this, nullptr);
    m_options = options;
    if (m_options) {
        connect(m_options.data(), &ProfilePlotOptions::changed,
                this, [this] { update(); });
    }
    update();
}

// ── Theme accessors ────────────────────────────────────────────────────
QColor ProfilePlotWidget::themeNodeFill(ProfileBuilder::NodeKind k) const
{
    if (!m_options) return fillForNodeKind(k);
    using K = ProfileBuilder::NodeKind;
    switch (k) {
    case K::Junction: return m_options->junctionFill();
    case K::Outfall:  return m_options->outfallFill();
    case K::Storage:  return m_options->storageFill();
    case K::Divider:  return m_options->dividerFill();
    }
    return fillForNodeKind(k);
}
QColor ProfilePlotWidget::themeNodeOutline(ProfileBuilder::NodeKind k) const
{
    if (!m_options) return outlineForNodeKind(k);
    using K = ProfileBuilder::NodeKind;
    switch (k) {
    case K::Junction: return m_options->junctionOutline();
    case K::Outfall:  return m_options->outfallOutline();
    case K::Storage:  return m_options->storageOutline();
    case K::Divider:  return m_options->dividerOutline();
    }
    return outlineForNodeKind(k);
}
QPen ProfilePlotWidget::themeVirtualJunctionPen() const
{
    // The whole marker is one pen — a virtual junction has no fill and no rim
    // glyph, so there is no colour pair to merge (see themeNodeFill, which
    // routes it to Qt::transparent).
    if (m_options) return m_options->virtualJunctionOutlinePen();
    QPen pen(QColor(0x33, 0x33, 0x33), 1.0, Qt::CustomDashLine);
    pen.setDashPattern({ 4.0, 3.0 });
    pen.setCapStyle(Qt::FlatCap);
    pen.setJoinStyle(Qt::MiterJoin);
    return pen;
}
QColor ProfilePlotWidget::themeLinkFill(ProfileBuilder::LinkKind k) const
{
    if (!m_options) return fillForLinkKind(k);
    using K = ProfileBuilder::LinkKind;
    switch (k) {
    case K::Conduit: return m_options->conduitFill();
    case K::Pump:    return m_options->pumpFill();
    case K::Weir:    return m_options->weirFill();
    case K::Orifice: return m_options->orificeFill();
    case K::Outlet:  return m_options->outletFill();
    }
    return fillForLinkKind(k);
}
QColor ProfilePlotWidget::themeLinkOutline(ProfileBuilder::LinkKind k) const
{
    if (!m_options) return outlineForLinkKind(k);
    using K = ProfileBuilder::LinkKind;
    switch (k) {
    case K::Conduit: return m_options->conduitOutline();
    case K::Pump:    return m_options->pumpOutline();
    case K::Weir:    return m_options->weirOutline();
    case K::Orifice: return m_options->orificeOutline();
    case K::Outlet:  return m_options->outletOutline();
    }
    return outlineForLinkKind(k);
}
QColor ProfilePlotWidget::themeSoilFill() const
{
    return m_options ? m_options->soilFill() : kSoilFillColor;
}
QColor ProfilePlotWidget::themeBeddingFill() const
{
    return m_options ? m_options->beddingFill() : kBeddingFillColor;
}
QPen ProfilePlotWidget::themeConduitOutlinePen() const
{
    if (m_options) return m_options->conduitOutlinePen();
    return QPen(QColor(0x33, 0x33, 0x33), kConduitLineWidth, Qt::SolidLine);
}
QPen ProfilePlotWidget::themeLinkOutlinePen(ProfileBuilder::LinkKind k) const
{
    using K = ProfileBuilder::LinkKind;
    if (m_options) {
        switch (k) {
        case K::Conduit: return m_options->conduitOutlinePen();
        case K::Orifice: return m_options->orificeOutlinePen();
        case K::Weir:    return m_options->weirOutlinePen();
        case K::Pump:    return m_options->pumpOutlinePen();
        case K::Outlet:  return m_options->outletOutlinePen();
        }
    }
    return QPen(themeLinkOutline(k), 1.5, Qt::SolidLine);
}

ProfilePlotWidget::LayerToggles ProfilePlotWidget::layerToggles() const
{
    return m_toggles;
}

void ProfilePlotWidget::setAxisLabels(const QString &xLabel, const QString &yLabel)
{
    m_xLabel = xLabel;
    m_yLabel = yLabel;
    update();
}

// ---------------------------------------------------------------------------
// Bounds
// ---------------------------------------------------------------------------

void ProfilePlotWidget::recomputeBounds()
{
    m_virtualChainage.clear();
    m_virtualGap        = 0.0;
    m_haveBeddingFloor  = false;

    if (m_path.nodes.isEmpty()) {
        m_autoXMin = 0.0; m_autoXMax = 1.0;
        m_autoYMin = 0.0; m_autoYMax = 1.0;
        if (m_fitMode) { m_dataXMin = m_autoXMin; m_dataXMax = m_autoXMax;
                         m_dataYMin = m_autoYMin; m_dataYMax = m_autoYMax; }
        emitXRangeIfChanged();
        return;
    }

    // ── Virtual chainage: same as real chainage for conduits, plus a small
    // gap allowance for each zero-length link (pumps/weirs/orifices/outlets)
    // so they render as discrete glyphs the user can see and click.  The
    // axis tick labels continue to display *real* chainage at each node.
    double sumConduitLen = 0.0;
    int    conduitCount  = 0;
    for (const auto &l : m_path.links) {
        const bool hasLen = (l.kind == ProfileBuilder::LinkKind::Conduit)
                            && l.length > 0.0;
        if (hasLen) { sumConduitLen += l.length; ++conduitCount; }
    }
    const double avgConduitLen = (conduitCount > 0)
                                     ? sumConduitLen / conduitCount
                                     : 0.0;
    // Fallback gap: when every link is zero-length, use 1.0 unit; otherwise
    // a fraction of the average conduit length so the gap reads in scale.
    m_virtualGap = (avgConduitLen > 0.0)
                       ? avgConduitLen * kZeroLengthGapFrac
                       : 1.0;

    m_virtualChainage.resize(m_path.nodes.size());
    m_virtualChainage[0] = 0.0;
    for (int i = 0; i < m_path.links.size() && i + 1 < m_virtualChainage.size(); ++i) {
        const auto &l = m_path.links[i];
        const bool hasLen = (l.kind == ProfileBuilder::LinkKind::Conduit)
                            && l.length > 0.0;
        const double inc = hasLen ? l.length : m_virtualGap;
        m_virtualChainage[i + 1] = m_virtualChainage[i] + inc;
    }

    double xMin = 0.0;
    double xMax = m_virtualChainage.isEmpty() ? 1.0 : m_virtualChainage.last();
    if (xMax <= xMin) xMax = xMin + 1.0;

    double yMin =  std::numeric_limits<double>::infinity();
    double yMax = -std::numeric_limits<double>::infinity();
    for (const auto &n : m_path.nodes) {
        yMin = std::min(yMin, n.invertElev);
        yMax = std::max(yMax, ProfileBuilder::groundElev(n));
    }
    // The minimum invert across the path drives the bedding floor.  Cache
    // the floor elevation so paintSoilFill can use it (and so the y-axis
    // extends down to it rather than clipping the bedding block).
    double minInvert = std::numeric_limits<double>::infinity();
    for (const auto &n : m_path.nodes) {
        if (isFinite(n.invertElev))
            minInvert = std::min(minInvert, n.invertElev);
    }
    // Account for link end offsets (e.g. orifice/weir invert offsets) since
    // the user-facing "lowest elevation" includes them.
    for (int i = 0; i < m_path.links.size(); ++i) {
        const auto &l = m_path.links[i];
        if (l.kind != ProfileBuilder::LinkKind::Conduit) continue;
        const double zU = m_path.nodes[i    ].invertElev + l.offset1;
        const double zD = m_path.nodes[i + 1].invertElev + l.offset2;
        if (isFinite(zU)) minInvert = std::min(minInvert, zU);
        if (isFinite(zD)) minInvert = std::min(minInvert, zD);
    }
    // Use each series's envelope range to widen the y-extent.  When two
    // series share a SourceDerived (e.g. HGL + EGL on the same layer) the
    // same arrays get scanned twice — cheap, and avoids tracking dedup
    // state here.  All elevation-axis arrays contribute equally.
    for (const auto &s : m_series) {
        if (!s.derived) continue;
        const auto &d = *s.derived;
        for (double v : d.minHgl)          if (isFinite(v)) yMin = std::min(yMin, v);
        for (double v : d.maxHgl)          if (isFinite(v)) yMax = std::max(yMax, v);
        for (double v : d.minEgl)          if (isFinite(v)) yMin = std::min(yMin, v);
        for (double v : d.maxEgl)          if (isFinite(v)) yMax = std::max(yMax, v);
        for (double v : d.minWaterSurface) if (isFinite(v)) yMin = std::min(yMin, v);
        for (double v : d.maxWaterSurface) if (isFinite(v)) yMax = std::max(yMax, v);
    }
    for (const QPointF &s : m_path.terrainSamples) {
        if (isFinite(s.y())) {
            yMin = std::min(yMin, s.y());
            yMax = std::max(yMax, s.y());
        }
    }
    if (!isFinite(yMin) || !isFinite(yMax) || yMax <= yMin) {
        yMin = 0.0; yMax = 1.0;
    }

    // Bedding floor: lowest invert − 5% of the profile elevation range
    // (top of ground line minus lowest invert).  Cap so the data y-range
    // extends to include the bedding block.
    if (isFinite(minInvert) && minInvert < +std::numeric_limits<double>::infinity()) {
        const double topForRange = yMax;
        const double profileRange = std::max(topForRange - minInvert, 1.0);
        m_beddingFloorElev = minInvert - kBeddingBelowFrac * profileRange;
        m_haveBeddingFloor = true;
        yMin = std::min(yMin, m_beddingFloorElev);
    }

    const double rangeY = yMax - yMin;
    yMin -= rangeY * kPadFracY;
    yMax += rangeY * kPadFracY;
    const double rangeX = xMax - xMin;
    xMin -= rangeX * kPadFracX;
    xMax += rangeX * kPadFracX;

    m_autoXMin = xMin; m_autoXMax = xMax;
    m_autoYMin = yMin; m_autoYMax = yMax;
    if (m_fitMode) {
        m_dataXMin = m_autoXMin; m_dataXMax = m_autoXMax;
        m_dataYMin = m_autoYMin; m_dataYMax = m_autoYMax;
    }
    // A new path/series can move the fitted extent — the tracks pane must
    // learn about that just like any interactive range change.
    emitXRangeIfChanged();
}

// ── Virtual-chainage helpers ────────────────────────────────────────────

double ProfilePlotWidget::virtualX(int nodeIdx) const
{
    if (nodeIdx < 0) return 0.0;
    if (nodeIdx < m_virtualChainage.size())
        return m_virtualChainage[nodeIdx];
    // Fallback: use real chainage if the virtual table hasn't been built.
    if (nodeIdx < m_path.chainage.size())
        return m_path.chainage[nodeIdx];
    return 0.0;
}

bool ProfilePlotWidget::isVirtualNode(int nodeIdx) const
{
    return nodeIdx >= 0
        && nodeIdx < m_path.nodes.size()
        && m_path.nodes[nodeIdx].kind
               == ProfileBuilder::NodeKind::VirtualJunction;
}

void ProfilePlotWidget::hglEdgePixels(int linkIdx, qreal &pxU, qreal &pxD) const
{
    const qreal rawU = dataToPixel(virtualX(linkIdx),     0.0).x();
    const qreal rawD = dataToPixel(virtualX(linkIdx + 1), 0.0).x();
    const qreal midPx = 0.5 * (rawU + rawD);
    // No tube at a virtual junction, so no inset — the water surface must
    // run through it exactly as the pipe does.
    const qreal insetU = isVirtualNode(linkIdx)     ? qreal(0)
                                                    : kHglPipeEdgeInsetPx;
    const qreal insetD = isVirtualNode(linkIdx + 1) ? qreal(0)
                                                    : kHglPipeEdgeInsetPx;
    pxU = std::min<qreal>(rawU + insetU, midPx);
    pxD = std::max<qreal>(rawD - insetD, midPx);
}

double ProfilePlotWidget::virtualXAlongLink(int linkIdx, double frac) const
{
    if (linkIdx < 0 || linkIdx + 1 >= m_virtualChainage.size()) {
        if (linkIdx >= 0 && linkIdx + 1 < m_path.chainage.size()) {
            const double a = m_path.chainage[linkIdx];
            const double b = m_path.chainage[linkIdx + 1];
            return a + frac * (b - a);
        }
        return 0.0;
    }
    const double a = m_virtualChainage[linkIdx];
    const double b = m_virtualChainage[linkIdx + 1];
    return a + std::clamp(frac, 0.0, 1.0) * (b - a);
}

double ProfilePlotWidget::virtualToRealChainage(double vx) const
{
    if (m_virtualChainage.size() < 2 || m_path.chainage.size() < 2)
        return vx;
    if (vx <= m_virtualChainage.first()) return m_path.chainage.first();
    if (vx >= m_virtualChainage.last())  return m_path.chainage.last();
    // Find the link [i, i+1] containing vx and linearly interpolate the
    // real chainage across that span.  Zero-length links keep both ends
    // mapped to the same real chainage — so the label "stutters" at the
    // gap, signalling that the gap is non-distance.
    for (int i = 0; i + 1 < m_virtualChainage.size(); ++i) {
        const double va = m_virtualChainage[i];
        const double vb = m_virtualChainage[i + 1];
        if (vx >= va && vx <= vb) {
            const double span = vb - va;
            const double t = (span > 0.0) ? (vx - va) / span : 0.0;
            const double ra = m_path.chainage[i];
            const double rb = m_path.chainage[i + 1];
            return ra + t * (rb - ra);
        }
    }
    return vx;
}

// ---------------------------------------------------------------------------
// Zoom / pan
// ---------------------------------------------------------------------------

void ProfilePlotWidget::fitToExtent()
{
    m_fitMode = true;
    m_dataXMin = m_autoXMin; m_dataXMax = m_autoXMax;
    m_dataYMin = m_autoYMin; m_dataYMax = m_autoYMax;
    update();
    emitXRangeIfChanged();
}

void ProfilePlotWidget::zoomBy(double factor)
{
    if (factor <= 0.0) return;
    m_fitMode = false;
    const double cx = (m_dataXMin + m_dataXMax) / 2.0;
    const double cy = (m_dataYMin + m_dataYMax) / 2.0;
    const double halfX = (m_dataXMax - m_dataXMin) / 2.0 * factor;
    const double halfY = (m_dataYMax - m_dataYMin) / 2.0 * factor;
    m_dataXMin = cx - halfX; m_dataXMax = cx + halfX;
    m_dataYMin = cy - halfY; m_dataYMax = cy + halfY;
    update();
    emitXRangeIfChanged();
}

void ProfilePlotWidget::setVisibleXRange(double vxMin, double vxMax)
{
    if (!std::isfinite(vxMin) || !std::isfinite(vxMax) || vxMax <= vxMin)
        return;
    if (vxMin == m_dataXMin && vxMax == m_dataXMax)
        return;
    m_fitMode = false;
    m_dataXMin = vxMin;
    m_dataXMax = vxMax;
    update();
    emitXRangeIfChanged();
}

void ProfilePlotWidget::emitXRangeIfChanged()
{
    // NaN sentinel start values guarantee the first real range is emitted.
    if (m_dataXMin == m_lastEmittedXMin && m_dataXMax == m_lastEmittedXMax)
        return;
    m_lastEmittedXMin = m_dataXMin;
    m_lastEmittedXMax = m_dataXMax;
    emit visibleXRangeChanged(m_dataXMin, m_dataXMax);
}

int ProfilePlotWidget::chartLeftMarginPx()  { return kMarginLeft;  }
int ProfilePlotWidget::chartRightMarginPx() { return kMarginRight; }

void ProfilePlotWidget::setMode(Mode m)
{
    if (m_mode == m) return;
    m_mode = m;
    switch (m) {
    case Mode::Pan:      setCursor(Qt::OpenHandCursor);    break;
    case Mode::ZoomIn:   setCursor(Qt::CrossCursor);       break;
    case Mode::ZoomOut:  setCursor(Qt::CrossCursor);       break;
    case Mode::Identify: setCursor(Qt::ArrowCursor);       break;
    }
    // Tear down any in-progress drag / rubberband when the mode changes.
    m_panActive  = false;
    m_zoomActive = false;
    m_pressedAxisEdge = AxisEdge::None;
    if (m_rubberBand) m_rubberBand->hide();
}

ProfilePlotWidget::AxisEdge
ProfilePlotWidget::axisEdgeAt(const QPoint &widgetPos) const
{
    const QRectF r = plotRect();
    const QPointF p = widgetPos;

    const bool nearLeft  = std::abs(p.x() - r.left()) <= kAxisEdgeAlongPx;
    const bool nearRight = std::abs(p.x() - r.right()) <= kAxisEdgeAlongPx;
    const bool nearTop   = std::abs(p.y() - r.top()) <= kAxisEdgeAlongPx;
    const bool nearBot   = std::abs(p.y() - r.bottom()) <= kAxisEdgeAlongPx;

    const bool inBottomBand =
        p.y() >= r.bottom() && p.y() <= r.bottom() + kAxisLabelBandPx;
    if (inBottomBand) {
        if (nearLeft) return AxisEdge::XMinimum;
        if (nearRight) return AxisEdge::XMaximum;
    }

    const bool inLeftBand =
        p.x() <= r.left() && p.x() >= r.left() - kAxisLabelBandPx;
    if (inLeftBand) {
        if (nearBot) return AxisEdge::YMinimum;
        if (nearTop) return AxisEdge::YMaximum;
    }

    return AxisEdge::None;
}

bool ProfilePlotWidget::setAxisEdgeValue(AxisEdge edge, double value)
{
    if (!std::isfinite(value)) return false;

    switch (edge) {
    case AxisEdge::XMinimum:
        if (value >= m_dataXMax) return false;
        m_dataXMin = value;
        break;
    case AxisEdge::XMaximum:
        if (value <= m_dataXMin) return false;
        m_dataXMax = value;
        break;
    case AxisEdge::YMinimum:
        if (value >= m_dataYMax) return false;
        m_dataYMin = value;
        break;
    case AxisEdge::YMaximum:
        if (value <= m_dataYMin) return false;
        m_dataYMax = value;
        break;
    case AxisEdge::None:
        return false;
    }

    m_fitMode = false;
    update();
    emitXRangeIfChanged();
    return true;
}

QRectF ProfilePlotWidget::visibleDataRange() const
{
    return QRectF(QPointF(m_dataXMin, m_dataYMin),
                  QPointF(m_dataXMax, m_dataYMax)).normalized();
}

bool ProfilePlotWidget::editAxisEdge(AxisEdge edge)
{
    if (edge == AxisEdge::None) return false;
    const double current =
        edge == AxisEdge::XMinimum ? m_dataXMin :
        edge == AxisEdge::XMaximum ? m_dataXMax :
        edge == AxisEdge::YMinimum ? m_dataYMin :
                                     m_dataYMax;
    bool accepted = false;
    const QString text = QInputDialog::getText(
        this,
        tr("Edit Axis Range"),
        tr("%1:").arg(labelForEdge(edge)),
        QLineEdit::Normal,
        QLocale().toString(current, 'g', 15),
        &accepted);
    if (!accepted) return false;

    double value = 0.0;
    if (!parseDoubleLocaleAware(text, value)
        || !setAxisEdgeValue(edge, value)) {
        QMessageBox::warning(
            this,
            tr("Invalid Axis Range"),
            isMinEdge(edge)
                ? tr("The minimum must be a finite value less than the current maximum.")
                : tr("The maximum must be a finite value greater than the current minimum."));
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Coordinate transforms
// ---------------------------------------------------------------------------

namespace {

// Heuristic height (in pixels) needed for a row of labels rendered at the
// requested orientation.  Vertical labels need room for the longest name
// rotated 90°; diagonal needs `sin(angle)` of that; horizontal needs one line.
int labelRowHeightPx(const ProfilePlotWidget::LayerToggles::LabelOrientation o,
                     int longestNameChars,
                     int customAngleDeg,
                     const QFontMetricsF &fm)
{
    const int    charPx  = static_cast<int>(fm.horizontalAdvance(QLatin1Char('M')));
    const int    linePx  = static_cast<int>(fm.height()) + 2;
    const double textLen = longestNameChars * charPx;
    using LO = ProfilePlotWidget::LayerToggles;
    switch (o) {
    case LO::Vertical:
        return std::max(20, static_cast<int>(textLen) + 8);
    case LO::Diagonal: {
        const double rad = std::clamp(customAngleDeg, 1, 89) * M_PI / 180.0;
        return std::max(20, static_cast<int>(textLen * std::sin(rad)) + 8);
    }
    case LO::Horizontal:
        return linePx + 4;
    }
    return linePx + 4;
}

} // namespace

int ProfilePlotWidget::bottomMargin() const
{
    return kMarginBottomBase;
}

int ProfilePlotWidget::topMargin() const
{
    if (!m_toggles.showNodeLabels && !m_toggles.showLinkLabels)
        return kMarginTop;
    const QFontMetricsF fm(font());
    int longestNode = 0, longestLink = 0;
    for (const auto &n : m_path.nodes) longestNode = std::max(longestNode, static_cast<int>(n.name.size()));
    for (const auto &l : m_path.links) longestLink = std::max(longestLink, static_cast<int>(l.name.size()));
    int extra = 0;
    if (m_toggles.showLinkLabels) {
        extra += labelRowHeightPx(m_toggles.labelOrientation, longestLink,
                                  m_toggles.labelAngleDeg, fm)
                 + kLabelRowGap;
    }
    if (m_toggles.showNodeLabels) {
        extra += labelRowHeightPx(m_toggles.labelOrientation, longestNode,
                                  m_toggles.labelAngleDeg, fm)
                 + kLabelRowGap;
    }
    return kMarginTop + extra;
}

QRectF ProfilePlotWidget::plotRect() const
{
    return QRectF(kMarginLeft,
                  topMargin(),
                  std::max(1, width()  - kMarginLeft - kMarginRight),
                  std::max(1, height() - topMargin() - bottomMargin()));
}

QPointF ProfilePlotWidget::dataToPixel(double chainage, double elev) const
{
    const QRectF r = plotRect();
    const double xFrac = (chainage - m_dataXMin) / (m_dataXMax - m_dataXMin);
    const double yFrac = (elev     - m_dataYMin) / (m_dataYMax - m_dataYMin);
    const double px = r.left() + xFrac * r.width();
    const double py = r.bottom() - yFrac * r.height();   // y up
    return { px, py };
}

// ---------------------------------------------------------------------------
// Hit-test
// ---------------------------------------------------------------------------

int ProfilePlotWidget::nodeIndexAt(const QPoint &widgetPos) const
{
    if (m_path.nodes.isEmpty()) return -1;
    // The manhole glyph is the tube rect from rim → invert, width
    // 2 * kShaftHalfWidthPx (see paintNodes).  A click counts as a node hit
    // only if it lands inside that rect (with a few pixels of slack).
    constexpr double kShaftHalfWidthPx = 3.5;
    constexpr double kTolPx            = 4.0;
    int    bestIdx  = -1;
    double bestDx   = std::numeric_limits<double>::infinity();
    for (int i = 0; i < m_path.nodes.size(); ++i) {
        const auto  &n     = m_path.nodes[i];
        // Virtual junctions included: their dashed rectangle occupies the
        // same footprint as a manhole tube (see paintNodes), so a click in
        // that band picks the break rather than the conduit through it.
        const double chain = virtualX(i);
        const QPointF rim  = dataToPixel(chain, ProfileBuilder::groundElev(n));
        const QPointF inv  = dataToPixel(chain, n.invertElev);
        const QRectF tube(QPointF(rim.x() - kShaftHalfWidthPx - kTolPx,
                                  rim.y() - kTolPx),
                          QPointF(rim.x() + kShaftHalfWidthPx + kTolPx,
                                  inv.y() + kTolPx));
        if (!tube.contains(widgetPos)) continue;
        const double dx = std::abs(rim.x() - widgetPos.x());
        if (dx < bestDx) { bestDx = dx; bestIdx = i; }
    }
    return bestIdx;
}

void ProfilePlotWidget::setSelectedElementNames(const QStringList &names)
{
    QSet<QString> next;
    for (const QString &n : names) next.insert(n);
    if (next == m_selectedNames) return;
    m_selectedNames = next;
    update();
}

int ProfilePlotWidget::linkIndexAt(const QPoint &widgetPos) const
{
    if (m_path.links.isEmpty()) return -1;
    constexpr double kTolPx       = 3.0;
    constexpr double kEndInsetPx  = 4.0;   // node tubes win near boundaries
    for (int i = 0; i < m_path.links.size(); ++i) {
        const auto  &l    = m_path.links[i];
        const double xU   = virtualX(i);
        const double xD   = virtualX(i + 1);
        // Effective invert elevations at each end.  Mirrors the rendering
        // path: orifices use a flat sill (linkBottomOf) at both ends, and
        // pumps sit at the inlet node's invert at both ends, so each
        // glyph's hit-rect matches what the user sees.
        double zUiv, zDiv;
        if (l.kind == ProfileBuilder::LinkKind::Orifice) {
            const int inletIdx = l.reversed ? (i + 1) : i;
            const double sill  = m_path.nodes[inletIdx].invertElev + l.offset1;
            zUiv = sill;
            zDiv = sill;
        } else if (l.kind == ProfileBuilder::LinkKind::Pump) {
            const int inletIdx = l.reversed ? (i + 1) : i;
            const double inletInv = m_path.nodes[inletIdx].invertElev;
            zUiv = inletInv;
            zDiv = inletInv;
        } else {
            zUiv = m_path.nodes[i    ].invertElev + l.offset1;
            zDiv = m_path.nodes[i + 1].invertElev + l.offset2;
        }
        const QPointF pUiv = dataToPixel(xU, zUiv);
        const QPointF pDiv = dataToPixel(xD, zDiv);

        if (linkKindRendersAsConduit(l.kind)) {
            const QPointF pUcr = dataToPixel(xU, zUiv + l.maxDepth);
            const QPointF pDcr = dataToPixel(xD, zDiv + l.maxDepth);
            const double xL = pUiv.x() + kEndInsetPx;
            const double xR = pDiv.x() - kEndInsetPx;
            if (xL >= xR) continue;
            if (widgetPos.x() < xL || widgetPos.x() > xR) continue;
            // Linearly interp invert / crown Y at the click's X and require
            // the click to fall inside the (crown ↔ invert) band.
            const double span = pDiv.x() - pUiv.x();
            const double t    = (span > 0.0) ? (widgetPos.x() - pUiv.x()) / span
                                             : 0.0;
            const double yInv = pUiv.y() + t * (pDiv.y() - pUiv.y());
            const double yCr  = pUcr.y() + t * (pDcr.y() - pUcr.y());
            const double yTop = std::min(yInv, yCr) - kTolPx;
            const double yBot = std::max(yInv, yCr) + kTolPx;
            if (widgetPos.y() >= yTop && widgetPos.y() <= yBot) return i;
        } else if (l.kind == ProfileBuilder::LinkKind::Weir) {
            // Weirs render as a tall rectangle from the inlet node's
            // invert up to the crest (see paintConduits weir branch),
            // not a 16-pixel centred glyph.  Hit-test against the actual
            // rendered rect or clicks on the block miss entirely.
            constexpr double kShaftHalfWidthPx = 3.5;
            const int    inletIdx  = l.reversed ? (i + 1) : i;
            const double zInletInv = m_path.nodes[inletIdx].invertElev;
            const double zSill     = zInletInv + l.crestHeight;
            const double xUpPx = dataToPixel(xU, 0.0).x();
            const double xDnPx = dataToPixel(xD, 0.0).x();
            const double rawSpanPx = xDnPx - xUpPx;
            const double trim = (rawSpanPx > 4.0 * kShaftHalfWidthPx)
                                    ? kShaftHalfWidthPx
                                    : 0.0;
            const double leftPx  = xUpPx + trim;
            const double rightPx = xDnPx - trim;
            const double topPy   = dataToPixel(0.0, zSill).y();
            const double botPy   = dataToPixel(0.0, zInletInv).y();
            const QRectF rect(leftPx  - kTolPx, std::min(topPy, botPy) - kTolPx,
                              (rightPx - leftPx) + 2 * kTolPx,
                              std::abs(botPy - topPy) + 2 * kTolPx);
            if (rect.contains(widgetPos)) return i;
        } else {
            // Pump / Outlet glyph: rounded rectangle centred on the
            // midpoint (see paintConduits).  Size matches the renderer
            // so a click on any pixel of the glyph hits.
            const QPointF mid((pUiv.x() + pDiv.x()) / 2.0,
                              (pUiv.y() + pDiv.y()) / 2.0);
            const double gapPx  = std::max(8.0, std::abs(pDiv.x() - pUiv.x()));
            const double glyphW = std::max(12.0, gapPx * 0.85);
            const double glyphH = 16.0;
            const QRectF rect(mid.x() - glyphW / 2.0 - kTolPx,
                              mid.y() - glyphH / 2.0 - kTolPx,
                              glyphW + 2 * kTolPx,
                              glyphH + 2 * kTolPx);
            if (rect.contains(widgetPos)) return i;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Event handlers
// ---------------------------------------------------------------------------

void ProfilePlotWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    paintBackgroundAndAxes(p);
    if (m_path.nodes.isEmpty()) return;

    paintSoilFill(p);
    paintConduits(p);

    using K = ProfileBuilder::OutputKind;

    // ── Pass 1: water-style fills inside pipes / manholes (animated).
    // WaterSurface fill sits underneath HGL fill (visible only when no HGL
    // series obscures it).  EGL has no fill — the velocity-head band would
    // imply water above the pipe crown which isn't physically meaningful.
    for (int s = 0; s < m_series.size(); ++s) {
        const auto &b = m_series[s];
        if (!b.visible || !b.derived) continue;
        if (b.kind == K::WaterSurface) paintWaterSurfaceFill(p, s);
    }
    // Live HGL has two independent toggles — line and fill — exposed on
    // ProfilePlotOptions. The line is gated below in Pass 3; the fill
    // (both the per-link in-pipe polygon and the per-node manhole water
    // column) is gated here so toggling Fill off hides both consistently.
    const bool optHglFill = !m_options || m_options->currentHglFill();
    for (int s = 0; s < m_series.size(); ++s) {
        const auto &b = m_series[s];
        if (!b.visible || !b.derived) continue;
        if (b.kind == K::HGL && optHglFill) {
            paintHglFill(p, s);
            paintNodeFill(p, s);
        }
    }

    paintNodes(p);
    paintLabelAxis(p);

    // ── Pass 2: envelope bands + lines, drawn behind current-time lines.
    for (int s = 0; s < m_series.size(); ++s) {
        const auto &b = m_series[s];
        if (!b.visible || !b.derived) continue;
        if (isCurrentTimeKind(b.kind)) continue;
        paintSeriesEnvelope(p, s);
        // Bridge the per-link envelope polygons through each manhole
        // shaft so the band reads as one continuous figure across the
        // whole profile instead of breaking at every node.
        paintNodeEnvelopeFill(p, s);
        // Max HGL gets a short horizontal segment at each node so the
        // trace stays continuous across the manhole tube.
        if (b.kind == K::MaxHGL) paintNodeHglLine(p, s);
    }

    // ── Pass 3: current-time lines on top.  Within this pass paint in
    // kind order EGL → WaterSurface → HGL so the primary HGL line reads
    // above the others.
    for (int s = 0; s < m_series.size(); ++s) {
        const auto &b = m_series[s];
        if (b.visible && b.derived && b.kind == K::EGL)
            paintSeriesCurrentLine(p, s);
    }
    for (int s = 0; s < m_series.size(); ++s) {
        const auto &b = m_series[s];
        if (b.visible && b.derived && b.kind == K::WaterSurface)
            paintSeriesCurrentLine(p, s);
    }
    {
        const bool optHglLine = !m_options || m_options->currentHglLine();
        if (optHglLine) {
            for (int s = 0; s < m_series.size(); ++s) {
                const auto &b = m_series[s];
                if (b.visible && b.derived && b.kind == K::HGL) {
                    paintSeriesCurrentLine(p, s);
                    // Node-level HGL segment across the manhole tube,
                    // butting against the link line at each pipe edge.
                    paintNodeHglLine(p, s);
                }
            }
        }
    }

    paintSelectionHighlights(p);
    paintLegend(p);
    paintTimeLabel(p);
}

void ProfilePlotWidget::resizeEvent(QResizeEvent *)
{
    update();
}

void ProfilePlotWidget::mousePressEvent(QMouseEvent *event)
{
    // Overlay drag — claim the click first so legend/timestamp can be moved
    // regardless of the current interaction mode (Identify/Pan/Zoom).
    if (event->button() == Qt::LeftButton) {
        const QPointF pos = event->position();
        if (m_options) {
            if (!m_timeLabelRect.isNull() && m_timeLabelRect.contains(pos)) {
                m_overlayDrag        = OverlayDrag::TimeLabel;
                m_overlayDragLastPos = event->pos();
                setCursor(Qt::ClosedHandCursor);
                event->accept();
                return;
            }
            if (!m_legendRect.isNull() && m_legendRect.contains(pos)) {
                m_overlayDrag        = OverlayDrag::Legend;
                m_overlayDragLastPos = event->pos();
                setCursor(Qt::ClosedHandCursor);
                event->accept();
                return;
            }
        }
    }
    if (event->button() == Qt::RightButton) {
        const int nodeIdx = nodeIndexAt(event->pos());
        if (nodeIdx >= 0) {
            emit nodeRightClicked(nodeIdx, event->globalPosition().toPoint());
            return;
        }
        const int linkIdx = linkIndexAt(event->pos());
        if (linkIdx >= 0) {
            emit linkRightClicked(linkIdx, event->globalPosition().toPoint());
            return;
        }
        emit backgroundRightClicked(event->globalPosition().toPoint());
        return;
    }
    // Middle-button drag — pan unconditionally (GIS/CAD convention).
    if (event->button() == Qt::MiddleButton) {
        m_panActive    = true;
        m_lastMousePos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (event->button() == Qt::LeftButton) {
        const AxisEdge edge = axisEdgeAt(event->pos());
        if (edge != AxisEdge::None) {
            m_pressedAxisEdge = edge;
            m_lastMousePos = event->pos();
            event->accept();
            return;
        }

        if (m_mode == Mode::Pan) {
            m_panActive    = true;
            m_lastMousePos = event->pos();
            setCursor(Qt::ClosedHandCursor);
            return;
        }
        if (m_mode == Mode::ZoomIn || m_mode == Mode::ZoomOut) {
            m_zoomActive = true;
            m_zoomAnchor = event->pos();
            if (!m_rubberBand)
                m_rubberBand = new QRubberBand(QRubberBand::Rectangle, this);
            m_rubberBand->setGeometry(QRect(m_zoomAnchor, QSize()));
            m_rubberBand->show();
            return;
        }
        // Identify mode — click selects, blank-area click deselects.
        const int nodeIdx = nodeIndexAt(event->pos());
        if (nodeIdx >= 0) { emit nodeClicked(nodeIdx); return; }
        const int linkIdx = linkIndexAt(event->pos());
        if (linkIdx >= 0) { emit linkClicked(linkIdx); return; }
        emit backgroundClicked();
        return;
    }
    QWidget::mousePressEvent(event);
}

void ProfilePlotWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_pressedAxisEdge != AxisEdge::None) {
        event->accept();
        return;
    }

    if (m_overlayDrag != OverlayDrag::None && m_options) {
        const QPoint dPx = event->pos() - m_overlayDragLastPos;
        m_overlayDragLastPos = event->pos();
        if (m_overlayDrag == OverlayDrag::TimeLabel) {
            m_options->setTimeLabelOffset(m_options->timeLabelOffset()
                                          + QPointF(dPx));
        } else if (m_overlayDrag == OverlayDrag::Legend) {
            m_options->setLegendOffset(m_options->legendOffset()
                                       + QPointF(dPx));
        }
        return;  // options->changed() triggers a repaint already
    }
    if (m_panActive) {
        const QRectF r = plotRect();
        const double dxPx = event->pos().x() - m_lastMousePos.x();
        const double dyPx = event->pos().y() - m_lastMousePos.y();
        const double dxData = -dxPx / r.width()  * (m_dataXMax - m_dataXMin);
        const double dyData =  dyPx / r.height() * (m_dataYMax - m_dataYMin);
        m_fitMode = false;
        m_dataXMin += dxData; m_dataXMax += dxData;
        m_dataYMin += dyData; m_dataYMax += dyData;
        m_lastMousePos = event->pos();
        update();
        emitXRangeIfChanged();
        return;
    }
    if (m_zoomActive && m_rubberBand) {
        m_rubberBand->setGeometry(
            QRect(m_zoomAnchor, event->pos()).normalized());
        return;
    }
    // Hover cursor over draggable overlays (only when no other mode is
    // actively driving the cursor).
    if (m_mode == Mode::Identify) {
        const QPointF pos = event->position();
        const bool overOverlay =
            (!m_timeLabelRect.isNull() && m_timeLabelRect.contains(pos)) ||
            (!m_legendRect.isNull()    && m_legendRect.contains(pos));
        setCursor(overOverlay ? Qt::OpenHandCursor : Qt::ArrowCursor);
    }
    QWidget::mouseMoveEvent(event);
}

void ProfilePlotWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_overlayDrag != OverlayDrag::None
        && event->button() == Qt::LeftButton) {
        m_overlayDrag = OverlayDrag::None;
        setCursor(m_mode == Mode::Pan      ? Qt::OpenHandCursor :
                  m_mode == Mode::ZoomIn   ? Qt::CrossCursor    :
                  m_mode == Mode::ZoomOut  ? Qt::CrossCursor    :
                                             Qt::ArrowCursor);
        return;
    }
    if (m_pressedAxisEdge != AxisEdge::None
        && event->button() == Qt::LeftButton) {
        const AxisEdge edge = m_pressedAxisEdge;
        m_pressedAxisEdge = AxisEdge::None;
        const bool click = std::abs(event->pos().x() - m_lastMousePos.x()) < 5
            && std::abs(event->pos().y() - m_lastMousePos.y()) < 5;
        if (click && axisEdgeAt(event->pos()) == edge)
            editAxisEdge(edge);
        event->accept();
        return;
    }
    if (m_panActive &&
        (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton)) {
        m_panActive = false;
        setCursor(m_mode == Mode::Pan      ? Qt::OpenHandCursor :
                  m_mode == Mode::ZoomIn   ? Qt::CrossCursor    :
                  m_mode == Mode::ZoomOut  ? Qt::CrossCursor    :
                                             Qt::ArrowCursor);
        return;
    }
    if (m_zoomActive && event->button() == Qt::LeftButton) {
        m_zoomActive = false;
        const QRect band = m_rubberBand
                               ? m_rubberBand->geometry()
                               : QRect(m_zoomAnchor, event->pos()).normalized();
        if (m_rubberBand) m_rubberBand->hide();
        const QRectF plot = plotRect();
        const QRect  bandClipped = band.intersected(plot.toRect());

        auto pixelToData = [&](const QPoint &px) {
            const double xFrac = (px.x() - plot.left())  / plot.width();
            const double yFrac = (plot.bottom() - px.y()) / plot.height();
            return QPointF(m_dataXMin + xFrac * (m_dataXMax - m_dataXMin),
                           m_dataYMin + yFrac * (m_dataYMax - m_dataYMin));
        };

        const bool isClick = bandClipped.width() < 4 && bandClipped.height() < 4;
        m_fitMode = false;

        if (isClick) {
            // Single-click in zoom mode: zoom around the click point.
            const QPointF anchor = pixelToData(event->pos());
            const double factor = (m_mode == Mode::ZoomIn) ? 0.5 : 2.0;
            const double halfX = (m_dataXMax - m_dataXMin) / 2.0 * factor;
            const double halfY = (m_dataYMax - m_dataYMin) / 2.0 * factor;
            m_dataXMin = anchor.x() - halfX; m_dataXMax = anchor.x() + halfX;
            m_dataYMin = anchor.y() - halfY; m_dataYMax = anchor.y() + halfY;
        } else {
            const QPointF a = pixelToData(bandClipped.topLeft());
            const QPointF b = pixelToData(bandClipped.bottomRight());
            const double xMin = std::min(a.x(), b.x());
            const double xMax = std::max(a.x(), b.x());
            const double yMin = std::min(a.y(), b.y());
            const double yMax = std::max(a.y(), b.y());
            if (m_mode == Mode::ZoomIn) {
                m_dataXMin = xMin; m_dataXMax = xMax;
                m_dataYMin = yMin; m_dataYMax = yMax;
            } else {
                // Zoom out: make the current view fit *into* the rubberband
                // proportions — i.e. enlarge the view rect so the existing
                // data area now occupies just the band's size.
                const double sx = (m_dataXMax - m_dataXMin) / std::max(1e-9, xMax - xMin);
                const double sy = (m_dataYMax - m_dataYMin) / std::max(1e-9, yMax - yMin);
                const double cx = (xMin + xMax) / 2.0;
                const double cy = (yMin + yMax) / 2.0;
                const double halfX = (m_dataXMax - m_dataXMin) / 2.0 * sx;
                const double halfY = (m_dataYMax - m_dataYMin) / 2.0 * sy;
                m_dataXMin = cx - halfX; m_dataXMax = cx + halfX;
                m_dataYMin = cy - halfY; m_dataYMax = cy + halfY;
            }
        }
        update();
        emitXRangeIfChanged();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void ProfilePlotWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_mode == Mode::Identify) {
        const int nodeIdx = nodeIndexAt(event->pos());
        if (nodeIdx >= 0) { emit nodeDoubleClicked(nodeIdx); return; }
        const int linkIdx = linkIndexAt(event->pos());
        if (linkIdx >= 0) { emit linkDoubleClicked(linkIdx); return; }
    }
    QWidget::mouseDoubleClickEvent(event);
}

void ProfilePlotWidget::wheelEvent(QWheelEvent *event)
{
    // Standard zoom-around-cursor: positive scroll zooms in.  Anchors the
    // data point under the cursor so it stays put across the zoom.
    const double steps = event->angleDelta().y() / 120.0;
    if (steps == 0.0) return;
    const double factor = std::pow(0.85, steps);  // <1 zooms in
    m_fitMode = false;

    const QRectF r = plotRect();
    const QPointF widgetPos = event->position();
    const double xFrac = (widgetPos.x() - r.left())  / r.width();
    const double yFrac = (r.bottom() - widgetPos.y()) / r.height();
    const double dataX = m_dataXMin + xFrac * (m_dataXMax - m_dataXMin);
    const double dataY = m_dataYMin + yFrac * (m_dataYMax - m_dataYMin);

    const double halfXNew = (m_dataXMax - m_dataXMin) / 2.0 * factor;
    const double halfYNew = (m_dataYMax - m_dataYMin) / 2.0 * factor;
    const double newCx = dataX + (0.5 - xFrac) * halfXNew * 2.0;
    const double newCy = dataY + (0.5 - yFrac) * halfYNew * 2.0;
    m_dataXMin = newCx - halfXNew; m_dataXMax = newCx + halfXNew;
    m_dataYMin = newCy - halfYNew; m_dataYMax = newCy + halfYNew;

    event->accept();
    update();
    emitXRangeIfChanged();
}

// ---------------------------------------------------------------------------
// Paint helpers
// ---------------------------------------------------------------------------

void ProfilePlotWidget::paintBackgroundAndAxes(QPainter &p) const
{
    const QRectF r = plotRect();

    // Plot-area background + frame.
    p.fillRect(r, Qt::white);

    // Light horizontal grid lines (5 divisions).
    QPen gridPen(plotTheme().plotGrid);
    gridPen.setWidthF(1.0);
    p.setPen(gridPen);
    for (int i = 1; i < 5; ++i) {
        const double y = r.top() + r.height() * (i / 5.0);
        p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
    }

    // Frame.
    QPen axisPen(plotTheme().plotAxis);
    axisPen.setWidthF(1.2);
    p.setPen(axisPen);
    p.drawRect(r);

    // Y tick labels (5 divisions).
    // Axis number format comes from the plot options (which inherit the
    // global Preferences default); fall back to the legacy precision when
    // no options object is attached.
    using openswmmvis::plot::NumberFormat;
    using openswmmvis::plot::NumberFormatMode;
    const NumberFormat yFmt = m_options ? m_options->yFormat()
                                        : NumberFormat{NumberFormatMode::Decimals, 1};
    const NumberFormat xFmt = m_options ? m_options->xFormat()
                                        : NumberFormat{NumberFormatMode::Decimals, 0};
    QFontMetricsF fm(p.font());
    p.setPen(plotTheme().plotAxis);
    for (int i = 0; i <= 5; ++i) {
        const double frac = i / 5.0;
        const double y = r.bottom() - r.height() * frac;
        const double val = m_dataYMin + frac * (m_dataYMax - m_dataYMin);
        const QString s = yFmt.format(val);
        p.drawText(QRectF(0, y - 8, kMarginLeft - 4, 16),
                   Qt::AlignRight | Qt::AlignVCenter, s);
        p.drawLine(QPointF(r.left() - 3, y), QPointF(r.left(), y));
    }

    // X tick labels (6 divisions).  Tick positions live in *virtual* x
    // (so they're evenly spaced on the plot), but labels show the real
    // chainage at each position so the distance axis stays accurate when
    // zero-length links open a visual gap.
    for (int i = 0; i <= 6; ++i) {
        const double frac = i / 6.0;
        const double x = r.left() + r.width() * frac;
        const double vx  = m_dataXMin + frac * (m_dataXMax - m_dataXMin);
        const double val = virtualToRealChainage(vx);
        const QString s = xFmt.format(val);
        p.drawText(QRectF(x - 30, r.bottom() + 2, 60, 16),
                   Qt::AlignHCenter | Qt::AlignTop, s);
        p.drawLine(QPointF(x, r.bottom()), QPointF(x, r.bottom() + 3));
    }

    // Axis labels.
    p.drawText(QRectF(r.left(), r.bottom() + 18,
                      r.width(), 14),
               Qt::AlignHCenter | Qt::AlignVCenter, m_xLabel);

    p.save();
    p.translate(14, r.center().y());
    p.rotate(-90);
    p.drawText(QRectF(-80, -8, 160, 16),
               Qt::AlignHCenter | Qt::AlignVCenter, m_yLabel);
    p.restore();
}

void ProfilePlotWidget::paintSoilFill(QPainter &p) const
{
    // Two soil regions:
    //   - Ground fill — bounded by the *rim* line on top and the *crown*
    //     line on the bottom.  Above the rim is sky (empty).  Manhole
    //     shafts (rim → invert at each node) are knocked out so the
    //     chamber above the pipe reads as open space.
    //   - Bedding fill — bounded above by a structure-following polyline
    //     (pipe inverts between nodes, dipping through each node's own
    //     invert at the node's chainage) and below by the plot rect
    //     bottom.  Solid fill, no cutouts.

    const QRectF r = plotRect();
    constexpr double kShaftHalfWidthPx = 3.5;

    // Conduit crowns / inverts incorporate the link's offset above each
    // adjacent node invert.  For weir / pump / orifice ("excavated"
    // structures) the channel is open to the surface — model them as the
    // surface dropping down to the inlet node invert and staying flat
    // across to the outlet, so the soil polygon pinches to zero there
    // and the structural block reads as exposed.  Outlets retain the
    // conduit-style rim because they're typically buried discharge points.
    auto isConduitLink = [&](int linkIdx) {
        if (linkIdx < 0 || linkIdx >= m_path.links.size()) return false;
        return m_path.links[linkIdx].kind == ProfileBuilder::LinkKind::Conduit;
    };
    auto isExcavatedLink = [&](int linkIdx) {
        if (linkIdx < 0 || linkIdx >= m_path.links.size()) return false;
        return linkKindIsExcavated(m_path.links[linkIdx].kind);
    };
    auto inletInvertOf = [&](int linkIdx) -> double {
        const auto &l = m_path.links[linkIdx];
        const int inletIdx = l.reversed ? (linkIdx + 1) : linkIdx;
        return m_path.nodes[inletIdx].invertElev;
    };
    // Floor of an excavated (non-conduit) link — the link's actual bottom
    // elevation = inlet node invert + offset1.  For a weir/pump/outlet
    // (offset1 = 0) this is just the inlet's invert.  For an orifice with
    // a sill (offset1 > 0) this lifts to the sill elevation, so the soil
    // polygon and rim follow the sill across the structure.  Transitions
    // at the upstream / downstream nodes step *through* the node's own
    // invert (handled by the rim / crown emit loops below).
    auto linkBottomOf = [&](int linkIdx) -> double {
        if (linkIdx < 0 || linkIdx >= m_path.links.size()) return std::nan("");
        return inletInvertOf(linkIdx) + m_path.links[linkIdx].offset1;
    };
    auto crownUpstream = [&](int linkIdx) -> double {
        if (linkIdx < 0 || linkIdx >= m_path.links.size()) return std::nan("");
        const auto &l = m_path.links[linkIdx];
        if (isExcavatedLink(linkIdx))
            return linkBottomOf(linkIdx);
        // Orifices share the conduit crown convention but with a flat
        // sill: crown at upstream end = sill + maxDepth, same value at
        // downstream end (so the orifice tube has a horizontal crown).
        if (l.kind == ProfileBuilder::LinkKind::Orifice)
            return linkBottomOf(linkIdx) + l.maxDepth;
        if (!isConduitLink(linkIdx))
            return ProfileBuilder::groundElev(m_path.nodes[linkIdx]);
        return m_path.nodes[linkIdx].invertElev + l.offset1 + l.maxDepth;
    };
    auto crownDownstream = [&](int linkIdx) -> double {
        if (linkIdx < 0 || linkIdx >= m_path.links.size()) return std::nan("");
        const auto &l = m_path.links[linkIdx];
        if (isExcavatedLink(linkIdx))
            return linkBottomOf(linkIdx);
        if (l.kind == ProfileBuilder::LinkKind::Orifice)
            return linkBottomOf(linkIdx) + l.maxDepth;
        if (!isConduitLink(linkIdx))
            return ProfileBuilder::groundElev(m_path.nodes[linkIdx + 1]);
        return m_path.nodes[linkIdx + 1].invertElev + l.offset2 + l.maxDepth;
    };
    // Link-invert elevations at the path-upstream and path-downstream ends
    // of link `linkIdx`.
    //
    // Conduits have independent offset1 / offset2, so the pipe invert
    // slopes between (node[i].invert + offset1) and (node[i+1].invert +
    // offset2).
    //
    // Non-conduits (orifice / weir / outlet / pump) only carry a single
    // "inlet offset" in SWMM.  The engine duplicates it into offset2
    // (offset2 = offset1) — applying offset2 to the downstream node's
    // invert would produce a sloped sill whenever the two node inverts
    // differ, which is wrong.  Treat the sill as flat at linkBottomOf
    // (= inletInvert + offset1) at both ends — same elevation the rim
    // emit loop already uses for excavated links.
    auto invertUpstream = [&](int linkIdx) -> double {
        if (linkIdx < 0 || linkIdx >= m_path.links.size()) return std::nan("");
        if (!isConduitLink(linkIdx))
            return linkBottomOf(linkIdx);
        return m_path.nodes[linkIdx].invertElev + m_path.links[linkIdx].offset1;
    };
    auto invertDownstream = [&](int linkIdx) -> double {
        if (linkIdx < 0 || linkIdx >= m_path.links.size()) return std::nan("");
        if (!isConduitLink(linkIdx))
            return linkBottomOf(linkIdx);
        return m_path.nodes[linkIdx + 1].invertElev + m_path.links[linkIdx].offset2;
    };
    auto chainAt = [&](int i) {
        return virtualX(i);
    };

    // Terrain samples carry real-chainage x.  Convert each sample to a
    // virtual x by interpolating into the link whose real-chainage span
    // contains it; otherwise the terrain ground line and the node row drift
    // apart whenever a zero-length link sits in between.
    auto terrainSampleToVirtualX = [&](double realX) -> double {
        if (m_path.chainage.size() < 2 || m_virtualChainage.size() < 2)
            return realX;
        if (realX <= m_path.chainage.first()) return m_virtualChainage.first();
        if (realX >= m_path.chainage.last())  return m_virtualChainage.last();
        for (int i = 0; i + 1 < m_path.chainage.size(); ++i) {
            const double ra = m_path.chainage[i];
            const double rb = m_path.chainage[i + 1];
            if (realX >= ra && realX <= rb) {
                const double span = rb - ra;
                const double t = (span > 0.0) ? (realX - ra) / span : 0.0;
                const double va = m_virtualChainage[i];
                const double vb = m_virtualChainage[i + 1];
                return va + t * (vb - va);
            }
        }
        return realX;
    };

    // ---- Top edge: terrain samples if the user opted in and the path
    // has DEM coverage, otherwise the rim line clamped to not dip below
    // adjacent crowns.  For nodes with no max depth (typical outfalls)
    // the rim sits below the conduit crown — clamp so the polygon
    // pinches to zero rather than reaching into the pipe.
    QVector<QPointF> rimPx;
    if (m_toggles.useTerrainGround && !m_path.terrainSamples.isEmpty()) {
        rimPx.reserve(m_path.terrainSamples.size());
        for (const QPointF &s : m_path.terrainSamples)
            rimPx.push_back(dataToPixel(terrainSampleToVirtualX(s.x()), s.y()));
    } else {
        rimPx.reserve(m_path.nodes.size() * 3);
        for (int i = 0; i < m_path.nodes.size(); ++i) {
            const bool incomingExcavated =
                (i > 0 && isExcavatedLink(i - 1));
            const bool outgoingExcavated =
                (i + 1 < m_path.nodes.size() && isExcavatedLink(i));

            // ── Incoming excavated: arrive at xN at the previous link's
            //    floor, then drop through this node's own invert.  The
            //    drop captures the "drop down to the outlet (= downstream)
            //    elevation" semantic at the excavated link's far end.
            if (incomingExcavated) {
                rimPx.push_back(dataToPixel(chainAt(i),
                                            linkBottomOf(i - 1)));
                rimPx.push_back(dataToPixel(chainAt(i),
                                            m_path.nodes[i].invertElev));
            }
            // ── Node rim point — skipped when the node is sandwiched
            //    between two excavated links (no exposed manhole there).
            //    Emit a tube-edge U-notch (rim → invert → invert → rim,
            //    inset by kShaftHalfWidthPx on each side) so the soil
            //    polygon's top edge dips out of the node tube interior.
            //    This is belt-and-braces alongside the shaft subtraction
            //    below — the subtraction handles the chamber-internal
            //    cut while the U-notch prevents any sliver of soil from
            //    bleeding across the tube at narrow zooms.
            if (!(incomingExcavated && outgoingExcavated)) {
                double topElev = ProfileBuilder::groundElev(m_path.nodes[i]);
                const double cIn  = crownDownstream(i - 1);
                const double cOut = crownUpstream(i);
                if (!std::isnan(cIn)  && cIn  > topElev) topElev = cIn;
                if (!std::isnan(cOut) && cOut > topElev) topElev = cOut;
                const QPointF rimC = dataToPixel(chainAt(i), topElev);
                if (m_path.nodes[i].kind
                        == ProfileBuilder::NodeKind::VirtualJunction) {
                    // No manhole to knock out — a virtual junction is a break
                    // point inside the pipe. One rim point, so the soil runs
                    // unbroken over it (through its ground elevation when the
                    // model supplies one, else the crown, as before).
                    rimPx.push_back(rimC);
                } else {
                    const QPointF invC = dataToPixel(chainAt(i),
                                                      m_path.nodes[i].invertElev);
                    rimPx.push_back(QPointF(rimC.x() - kShaftHalfWidthPx, rimC.y()));
                    rimPx.push_back(QPointF(rimC.x() - kShaftHalfWidthPx, invC.y()));
                    rimPx.push_back(QPointF(rimC.x() + kShaftHalfWidthPx, invC.y()));
                    rimPx.push_back(QPointF(rimC.x() + kShaftHalfWidthPx, rimC.y()));
                }
            }
            // ── Outgoing excavated: drop from rim (just emitted, unless
            //    sandwiched) through this node's invert, then *up* to the
            //    outgoing link's floor (= inletInvert + offset1).  When
            //    offset1 == 0 (pump / weir / outlet) the floor equals the
            //    inlet's invert; for orifices with a sill the rise to the
            //    sill is visible.
            if (outgoingExcavated) {
                rimPx.push_back(dataToPixel(chainAt(i),
                                            m_path.nodes[i].invertElev));
                rimPx.push_back(dataToPixel(chainAt(i),
                                            linkBottomOf(i)));
            }
        }
    }

    // ---- Bottom edge: crown line (sawtooth at each node) ----------------
    // At every node we emit (crown_in, crown_out).  When either adjacent
    // link is excavated, an additional point at this node's own invert is
    // inserted between them so the polygon edge dips through the chamber
    // bottom — matching the "drop to outlet elevation" semantic on the
    // downstream side of every excavated link.  At terminal nodes
    // (outfalls or chain endpoints) only one side has an adjacent crown;
    // we mirror that crown to the missing side so the ground line stays
    // at crown level rather than jumping up to the rim.
    QVector<QPointF> crownPx;
    for (int i = 0; i < m_path.nodes.size(); ++i) {
        const bool incomingExc =
            (i > 0) && isExcavatedLink(i - 1);
        const bool outgoingExc =
            (i + 1 < m_path.nodes.size()) && isExcavatedLink(i);

        const double cIn    = crownDownstream(i - 1);
        const double cOut   = crownUpstream(i);
        const double rim    = ProfileBuilder::groundElev(m_path.nodes[i]);
        double zIn = cIn, zOut = cOut;
        if (std::isnan(zIn) && std::isnan(zOut)) { zIn = rim; zOut = rim; }
        else if (std::isnan(zIn))                 { zIn = zOut; }
        else if (std::isnan(zOut))                { zOut = zIn; }
        crownPx.push_back(dataToPixel(chainAt(i), zIn));
        if (incomingExc || outgoingExc) {
            crownPx.push_back(dataToPixel(chainAt(i),
                                          m_path.nodes[i].invertElev));
        }
        crownPx.push_back(dataToPixel(chainAt(i), zOut));
    }

    // ---- Ground polygon: rim line forward, then crown line reverse -----
    QPainterPath ground;
    if (!rimPx.isEmpty()) {
        ground.moveTo(rimPx.first());
        for (int i = 1; i < rimPx.size(); ++i) ground.lineTo(rimPx[i]);
        for (int i = crownPx.size() - 1; i >= 0; --i) ground.lineTo(crownPx[i]);
        ground.closeSubpath();
    }

    // ---- Manhole knockouts ---------------------------------------------
    QPainterPath shafts;
    for (int i = 0; i < m_path.nodes.size(); ++i) {
        const auto &n = m_path.nodes[i];
        // A virtual junction has no shaft to knock out (see paintNodes).
        if (n.kind == ProfileBuilder::NodeKind::VirtualJunction) continue;
        const QPointF top = dataToPixel(chainAt(i), ProfileBuilder::groundElev(n));
        const QPointF bot = dataToPixel(chainAt(i), n.invertElev);
        QPainterPath shaft;
        shaft.addRect(QRectF(QPointF(top.x() - kShaftHalfWidthPx, top.y()),
                             QPointF(bot.x() + kShaftHalfWidthPx, bot.y())));
        shafts.addPath(shaft);
    }
    QPainterPath cutGround = ground.subtracted(shafts);

    // ---- Bedding polygon: structure-following top edge, plot bottom back -
    // The top edge follows the pipe inverts between nodes AND dips through
    // each node's own invert at the node's chainage, so the polygon's upper
    // boundary traces the actual buried structure (pipe inverts + node-
    // invert sumps) without any gaps or cutouts.
    // At each node we emit three points at the same x:
    //   (xN, zIn)  — incoming pipe invert (or node invert at terminals)
    //   (xN, nInv) — the node's own invert
    //   (xN, zOut) — outgoing pipe invert (or node invert at terminals)
    // When zIn == nInv or zOut == nInv (terminals, or zero-offset pipes)
    // the extra point is harmlessly collinear.
    QVector<QPointF> invertPx;
    for (int i = 0; i < m_path.nodes.size(); ++i) {
        const double iIn  = invertDownstream(i - 1);
        const double iOut = invertUpstream(i);
        const double nInv = m_path.nodes[i].invertElev;
        const double zIn  = std::isnan(iIn)  ? nInv : iIn;
        const double zOut = std::isnan(iOut) ? nInv : iOut;
        // Tube-edge U-notch so the bedding polygon's top edge drops to
        // the node invert across the manhole tube width instead of
        // continuing along the link invert into the tube interior. For
        // links with offset > 0 (link invert above node invert) this
        // is the difference between "bedding bleeding into the
        // chamber" and "bedding cleanly stopping at the tube edges."
        const QPointF inC  = dataToPixel(chainAt(i), zIn);
        const QPointF nC   = dataToPixel(chainAt(i), nInv);
        const QPointF outC = dataToPixel(chainAt(i), zOut);
        invertPx.push_back(QPointF(nC.x() - kShaftHalfWidthPx, inC.y()));
        invertPx.push_back(QPointF(nC.x() - kShaftHalfWidthPx, nC.y()));
        invertPx.push_back(QPointF(nC.x() + kShaftHalfWidthPx, nC.y()));
        invertPx.push_back(QPointF(nC.x() + kShaftHalfWidthPx, outC.y()));
    }

    QPainterPath bedding;
    if (!invertPx.isEmpty()) {
        // Bedding floor sits at a fixed elevation (m_beddingFloorElev: 5%
        // of the profile range below the lowest invert) so the block has
        // bounded thickness regardless of zoom.  Fall back to the plot
        // rect bottom only when the floor elevation was not computed.
        const double floorPx = m_haveBeddingFloor
            ? dataToPixel(0.0, m_beddingFloorElev).y()
            : r.bottom();
        bedding.moveTo(invertPx.first());
        for (int i = 1; i < invertPx.size(); ++i) bedding.lineTo(invertPx[i]);
        bedding.lineTo(QPointF(invertPx.last().x(),  floorPx));
        bedding.lineTo(QPointF(invertPx.first().x(), floorPx));
        bedding.closeSubpath();
    }

    p.save();
    p.setClipRect(r);
    p.setPen(Qt::NoPen);
    p.setBrush(themeSoilFill());
    p.drawPath(cutGround);
    p.setBrush(themeBeddingFill());
    p.drawPath(bedding);
    p.restore();
}

void ProfilePlotWidget::paintConduits(QPainter &p) const
{
    p.save();
    p.setClipRect(plotRect());

    for (int i = 0; i < m_path.links.size(); ++i) {
        const auto &l = m_path.links[i];
        const double xU = virtualX(i);
        const double xD = virtualX(i + 1);
        // Effective invert elevations at each end.  Orifices render with
        // a flat sill, so both ends share linkBottomOf (= inletInvert +
        // offset1) regardless of node-invert differences.  Pumps have no
        // physical invert profile in SWMM — the glyph sits at the inlet
        // node's invert at both ends so the symbol reads as a fixed
        // elevation (and matches the inlet's manhole tube).  Conduits
        // use the sloped offset1/offset2 per end.  Weirs / Outlets keep
        // the simple per-node invert mapping (their glyphs/blocks
        // resolve their own elevation in their dedicated branches).
        double zUinv, zDinv;
        if (l.kind == ProfileBuilder::LinkKind::Orifice) {
            const int inletIdx = l.reversed ? (i + 1) : i;
            const double sill  = m_path.nodes[inletIdx].invertElev + l.offset1;
            zUinv = sill;
            zDinv = sill;
        } else if (l.kind == ProfileBuilder::LinkKind::Pump) {
            const int inletIdx = l.reversed ? (i + 1) : i;
            const double inletInv = m_path.nodes[inletIdx].invertElev;
            zUinv = inletInv;
            zDinv = inletInv;
        } else {
            zUinv = m_path.nodes[i    ].invertElev + l.offset1;
            zDinv = m_path.nodes[i + 1].invertElev + l.offset2;
        }
        const bool   selected = m_selectedNames.contains(l.name);
        // Per-link-kind pen carries the user-configured width / dash / color
        // for this kind (conduit, orifice, weir, pump, outlet).  When the
        // link is selected, override the color with the bright-orange
        // highlight and bump the width.
        QPen   outlinePen = themeLinkOutlinePen(l.kind);
        if (selected) {
            outlinePen.setColor(QColor(0xFF, 0x66, 0x00));
            outlinePen.setWidthF(outlinePen.widthF() * 1.8);
        }
        const double penWidth = outlinePen.widthF();

        if (linkKindRendersAsConduit(l.kind)) {
            const double zUcr = zUinv + l.maxDepth;
            const double zDcr = zDinv + l.maxDepth;

            // Trim the conduit's pixel extent inward by the manhole
            // half-width at each end so the link's vertical end-caps
            // coincide with the tube's vertical edges (rather than its
            // centre).  Y-positions are linearly interpolated along the
            // original invert / crown line so the slope still reads.
            constexpr double kShaftHalfWidthPx = 3.5;
            const QPointF pUinv = dataToPixel(xU, zUinv);
            const QPointF pDinv = dataToPixel(xD, zDinv);
            const QPointF pUcr  = dataToPixel(xU, zUcr);
            const QPointF pDcr  = dataToPixel(xD, zDcr);

            // A virtual junction is a computational break point inside one
            // continuous pipe, not a manhole, so there is no tube edge to
            // butt against: that end keeps its full extent (no trim, no end
            // cap below).  Consecutive conduits then meet at the same
            // chainage, each carrying its own slope — the invert and crown
            // read as one continuous polyline with a kink at the break.
            const bool vjUp = isVirtualNode(i);
            const bool vjDn = isVirtualNode(i + 1);

            const double spanPx = pDinv.x() - pUinv.x();
            // Skip the trim when the link is too short to accommodate
            // both tube widths — the conduit then renders centre-to-
            // centre as a degenerate fallback.
            const double shiftPx = (spanPx > 4.0 * kShaftHalfWidthPx)
                                       ? kShaftHalfWidthPx
                                       : 0.0;
            const double tBase   = (spanPx > 0.0) ? shiftPx / spanPx : 0.0;
            const double tUp     = vjUp ? 0.0 : tBase;
            const double tDn     = vjDn ? 0.0 : tBase;
            auto shifted = [](const QPointF &up, const QPointF &dn,
                              double tFrac, bool isUpstream) -> QPointF {
                const QPointF dir = dn - up;
                return isUpstream
                           ? up + dir * tFrac
                           : dn - dir * tFrac;
            };
            const QPointF upInv = shifted(pUinv, pDinv, tUp, true);
            const QPointF dnInv = shifted(pUinv, pDinv, tDn, false);
            const QPointF upCr  = shifted(pUcr,  pDcr,  tUp, true);
            const QPointF dnCr  = shifted(pUcr,  pDcr,  tDn, false);

            QPainterPath body;
            body.moveTo(upCr);
            body.lineTo(dnCr);
            body.lineTo(dnInv);
            body.lineTo(upInv);
            body.closeSubpath();
            QColor bodyFill = themeLinkFill(l.kind);
            bodyFill.setAlphaF(0.35);
            p.setBrush(bodyFill);
            p.setPen(Qt::NoPen);
            p.drawPath(body);

            p.setPen(outlinePen);
            p.setBrush(Qt::NoBrush);
            p.drawLine(upInv, dnInv);    // invert
            p.drawLine(upCr,  dnCr);     // crown
            QPen capPen = outlinePen;
            capPen.setWidthF(penWidth * 0.7);
            p.setPen(capPen);
            // No cap at a virtual junction — capping there would draw the
            // very break the pipe is supposed to run through.
            if (!vjUp) p.drawLine(upInv, upCr);  // upstream cap at tube edge
            if (!vjDn) p.drawLine(dnInv, dnCr);  // downstream cap at tube edge
        } else if (l.kind == ProfileBuilder::LinkKind::Weir) {
            // Weirs are point structures with a SINGLE crest elevation
            // set by the inlet node: crest = inletInvert + crestHeight.
            // Render the body as a solid rectangle that spans the visual
            // gap from inlet invert up to the crest.
            const int    inletIdx  = l.reversed ? (i + 1) : i;
            const double zInletInv = m_path.nodes[inletIdx].invertElev;
            const double zFloor    = zInletInv;
            const double zSill     = zInletInv + l.crestHeight;

            // Trim the block's horizontal extent inward by the manhole
            // half-width at each end so the block butts against the
            // *outer edge* of each connecting node's tube (same
            // convention as paintConduits for pipes).
            constexpr double kShaftHalfWidthPx = 3.5;
            const double xUpPx = dataToPixel(xU, 0.0).x();
            const double xDnPx = dataToPixel(xD, 0.0).x();
            const double rawSpanPx = xDnPx - xUpPx;
            const double trim = (rawSpanPx > 4.0 * kShaftHalfWidthPx)
                                    ? kShaftHalfWidthPx
                                    : 0.0;
            const double leftPx  = xUpPx + trim;
            const double rightPx = xDnPx - trim;
            const double topPy   = dataToPixel(0.0, zSill).y();
            const double botPy   = dataToPixel(0.0, zFloor).y();

            QPainterPath body;
            body.addRect(QRectF(QPointF(leftPx,  topPy),
                                QPointF(rightPx, botPy)));
            p.setBrush(themeLinkFill(l.kind));
            p.setPen(outlinePen);
            p.drawPath(body);

            // Single-letter label centred just above the crest.
            const double midPx = (leftPx + rightPx) / 2.0;
            p.setPen(plotTheme().plotNodeLabel);
            p.drawText(QRectF(midPx - 8, topPy - 18, 16, 14),
                       Qt::AlignCenter, QString(QChar('W')));
        } else {
            // Pump / Outlet: rounded-rectangle glyph in the virtual-gap
            // region.  Useful as a hit target — these elements have no
            // elevation geometry to draw.
            const QPointF u = dataToPixel(xU, zUinv);
            const QPointF d = dataToPixel(xD, zDinv);
            const QPointF mid((u.x() + d.x()) / 2.0, (u.y() + d.y()) / 2.0);
            const double gapPx = std::max(8.0, std::abs(d.x() - u.x()));
            const double glyphW = std::max(12.0, gapPx * 0.85);
            const double glyphH = 16.0;
            QRectF glyphRect(mid.x() - glyphW / 2.0,
                             mid.y() - glyphH / 2.0,
                             glyphW, glyphH);
            QPainterPath body;
            body.addRoundedRect(glyphRect, 3.5, 3.5);
            p.setBrush(themeLinkFill(l.kind));
            p.setPen(outlinePen);
            p.drawPath(body);

            const QChar c =
                (l.kind == ProfileBuilder::LinkKind::Pump)   ? QChar('P') :
                (l.kind == ProfileBuilder::LinkKind::Outlet) ? QChar('U') :
                                                               QChar('?');
            p.setPen(plotTheme().plotNodeLabel);
            p.drawText(glyphRect, Qt::AlignCenter, QString(c));

            // Connect endpoints with a thin dashed line so the path still reads
            // as continuous.
            QPen dashed = makeLinePen(plotTheme().plotConduit, 1.0, /*dashed=*/true);
            dashed.setColor(withAlphaF(plotTheme().plotConduit, 0.5));
            p.setPen(dashed);
            p.drawLine(u, QPointF(glyphRect.left(),  mid.y()));
            p.drawLine(QPointF(glyphRect.right(), mid.y()), d);
        }
    }
    p.restore();
}

void ProfilePlotWidget::paintNodes(QPainter &p) const
{
    p.save();
    p.setClipRect(plotRect());

    // Narrow tube so the wider path / animated HGL lines remain readable
    // through the manhole interior.  Matches the soil-fill knockout width.
    constexpr double kShaftHalfWidthPx = 3.5;
    for (int i = 0; i < m_path.nodes.size(); ++i) {
        // Suppress the manhole tube + rim glyph when this node sits between
        // two excavated links — there's no real manhole there to draw, and
        // the stepwise ground line in paintSoilFill never rises to the rim
        // for these nodes, so a tube would float disconnected from the soil.
        const bool incomingExc =
            (i > 0) && linkKindIsExcavated(m_path.links[i - 1].kind);
        const bool outgoingExc =
            (i + 1 < m_path.nodes.size())
            && linkKindIsExcavated(m_path.links[i].kind);
        if (incomingExc && outgoingExc) continue;

        // Same reasoning for a virtual junction: it is a computational break
        // point inside one continuous pipe, not a structure, so there is no
        // manhole to draw. Its rim still shapes the ground line above.
        // Mark the break with a dashed rectangle on the same footprint a
        // manhole tube would occupy (invert → rim, tube width), so it reads
        // as a node and hit-tests like one.  The pipe is drawn through it
        // unbroken, so the rectangle's lower part overlaps a sliver of the
        // conduits either side — that overlap is what shows the split.
        if (m_path.nodes[i].kind == ProfileBuilder::NodeKind::VirtualJunction) {
            const auto &vn     = m_path.nodes[i];
            const double vchain = virtualX(i);
            const QPointF vrim  = dataToPixel(vchain,
                                              ProfileBuilder::groundElev(vn));
            const QPointF vinv  = dataToPixel(vchain, vn.invertElev);
            QPen vjPen = themeVirtualJunctionPen();
            if (m_selectedNames.contains(vn.name)) {
                vjPen.setColor(QColor(0xFF, 0x66, 0x00));   // bright orange
                vjPen.setWidthF(vjPen.widthF() + 1.5);
            }
            p.setBrush(Qt::NoBrush);
            p.setPen(vjPen);
            p.drawRect(QRectF(QPointF(vrim.x() - kShaftHalfWidthPx, vrim.y()),
                              QPointF(vrim.x() + kShaftHalfWidthPx, vinv.y())));
            continue;
        }

        const auto &n = m_path.nodes[i];
        const double chain = virtualX(i);
        const QPointF rim   = dataToPixel(chain, ProfileBuilder::groundElev(n));
        const QPointF inv   = dataToPixel(chain, n.invertElev);

        const bool   selected = m_selectedNames.contains(n.name);
        const QColor fill     = themeNodeFill(n.kind);
        const QColor outline  = selected ? QColor(0xFF, 0x66, 0x00)   // bright orange
                                          : themeNodeOutline(n.kind);
        QPen pen(outline);
        pen.setWidthF(selected ? kNodeBarWidth + 1.5 : kNodeBarWidth);
        pen.setCapStyle(Qt::FlatCap);
        pen.setJoinStyle(Qt::MiterJoin);

        // Manhole tube — outline-only rectangle from invert to rim.  The
        // interior stays transparent so the HGL / EGL lines and any soil
        // knockout behind it remain visible through the tube.
        const QRectF tube(QPointF(rim.x() - kShaftHalfWidthPx, rim.y()),
                          QPointF(rim.x() + kShaftHalfWidthPx, inv.y()));
        p.setBrush(Qt::NoBrush);
        p.setPen(pen);
        p.drawRect(tube);

        // Flooding test — outfalls cannot flood (open boundary), so they
        // always render with the normal outfall glyph.  Any other node
        // whose current HGL meets the surcharge threshold (in any visible
        // HGL series) renders as a red sector instead of its kind-specific
        // rim glyph.
        bool flooding = false;
        if (m_currentPeriod >= 0
            && n.kind != ProfileBuilder::NodeKind::Outfall) {
            const double surchargeElev =
                n.invertElev + n.maxDepth + n.surchargeDepth;
            for (const auto &s : m_series) {
                if (!s.visible || !s.derived) continue;
                if (s.kind != ProfileBuilder::OutputKind::HGL) continue;
                // SourceDerived is period-major: arr[period][node].
                const auto &arr = s.derived->hglByPeriod;
                if (m_currentPeriod < 0 || m_currentPeriod >= arr.size()) continue;
                const auto &row = arr[m_currentPeriod];
                if (i >= row.size()) continue;
                const double hgl = row[i];
                if (isFinite(hgl) && hgl >= surchargeElev) {
                    flooding = true;
                    break;
                }
            }
        }

        if (flooding) {
            // Red wedge pointing straight up out of the manhole — reads as
            // a spray of water escaping the rim.  Radius, sweep angle and
            // fill colour are user-configurable via ProfilePlotOptions;
            // outline is auto-derived as a darker tint of the fill so a
            // single colour setting keeps the glyph coherent.
            const qreal  radiusPx = m_options ? m_options->floodRadiusPx() : 15.0;
            const int    sweepDeg = m_options ? m_options->floodSweepDeg() : 60;
            const QColor fillCol  = m_options ? m_options->floodColor()
                                              : QColor(0xE5, 0x21, 0x21);
            const int    startDeg = 90 - sweepDeg / 2;        // centred up
            const QRectF floodBox(rim.x() - radiusPx,
                                  rim.y() - radiusPx,
                                  radiusPx * 2,
                                  radiusPx * 2);
            p.save();
            p.setBrush(fillCol);
            p.setPen(QPen(fillCol.darker(160), 1.2));
            p.drawPie(floodBox, startDeg * 16, sweepDeg * 16);
            p.restore();
        } else {
            // Original rim glyph rendering — these were the shapes in
            // place at the start of this conversation, before the
            // centroid-alignment and flooding changes.
            constexpr double kHalf = 6.0;
            QPainterPath sym;
            switch (n.kind) {
            case ProfileBuilder::NodeKind::Junction:
                sym.addRect(QRectF(rim.x() - kHalf, rim.y() - 2.0,
                                   kHalf * 2, 4.0));
                break;
            case ProfileBuilder::NodeKind::Outfall:
                sym.moveTo(rim + QPointF(-kHalf, -kHalf));
                sym.lineTo(rim + QPointF( kHalf, 0));
                sym.lineTo(rim + QPointF(-kHalf,  kHalf));
                sym.closeSubpath();
                break;
            case ProfileBuilder::NodeKind::Storage:
                sym.moveTo(rim + QPointF(-kHalf - 2,  kHalf / 2));
                sym.lineTo(rim + QPointF( kHalf + 2,  kHalf / 2));
                sym.lineTo(rim + QPointF( kHalf - 2, -kHalf));
                sym.lineTo(rim + QPointF(-kHalf + 2, -kHalf));
                sym.closeSubpath();
                break;
            case ProfileBuilder::NodeKind::Divider:
                sym.moveTo(rim + QPointF(0, -kHalf));
                sym.lineTo(rim + QPointF( kHalf, 0));
                sym.lineTo(rim + QPointF(0,  kHalf));
                sym.lineTo(rim + QPointF(-kHalf, 0));
                sym.closeSubpath();
                break;
            }
            p.setBrush(fill);
            p.setPen(pen);
            p.drawPath(sym);
        }

        // Inline node label above the rim — opt-in, since label-axis row
        // already gives users the same info without crowding the plot.
        if (m_toggles.inlineNodeLabels) {
            p.setPen(plotTheme().plotNodeLabel);
            p.drawText(QRectF(rim.x() - 40, rim.y() - 18, 80, 14),
                       Qt::AlignCenter, n.name);
        }
    }
    p.restore();
}

void ProfilePlotWidget::paintSelectionHighlights(QPainter &p) const
{
    if (m_selectedNames.isEmpty()) return;

    p.save();
    p.setClipRect(plotRect());
    p.setBrush(Qt::NoBrush);

    const QColor highlight(0xFF, 0x66, 0x00);
    constexpr double kShaftHalfWidthPx = 3.5;

    // Selected links first, then nodes, so endpoint halos win at shared
    // boundaries.  This pass intentionally runs after all result series.
    for (int i = 0; i < m_path.links.size(); ++i) {
        const auto &l = m_path.links[i];
        if (!m_selectedNames.contains(l.name)) continue;

        const double xU = virtualX(i);
        const double xD = virtualX(i + 1);
        double zUinv = 0.0;
        double zDinv = 0.0;
        if (l.kind == ProfileBuilder::LinkKind::Orifice) {
            const int inletIdx = l.reversed ? (i + 1) : i;
            const double sill = m_path.nodes[inletIdx].invertElev + l.offset1;
            zUinv = sill;
            zDinv = sill;
        } else if (l.kind == ProfileBuilder::LinkKind::Pump) {
            const int inletIdx = l.reversed ? (i + 1) : i;
            const double inletInv = m_path.nodes[inletIdx].invertElev;
            zUinv = inletInv;
            zDinv = inletInv;
        } else {
            zUinv = m_path.nodes[i].invertElev + l.offset1;
            zDinv = m_path.nodes[i + 1].invertElev + l.offset2;
        }

        QPen pen(highlight);
        pen.setWidthF(std::max(3.0, themeLinkOutlinePen(l.kind).widthF() * 2.0));
        pen.setJoinStyle(Qt::RoundJoin);
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);

        if (linkKindRendersAsConduit(l.kind)) {
            const double zUcr = zUinv + l.maxDepth;
            const double zDcr = zDinv + l.maxDepth;
            const QPointF pUinv = dataToPixel(xU, zUinv);
            const QPointF pDinv = dataToPixel(xD, zDinv);
            const QPointF pUcr  = dataToPixel(xU, zUcr);
            const QPointF pDcr  = dataToPixel(xD, zDcr);

            // Same per-end rule as paintConduits: no trim and no closing
            // edge where the conduit meets a virtual junction, so the halo
            // runs through the break exactly as the pipe it outlines does.
            const bool vjUp = isVirtualNode(i);
            const bool vjDn = isVirtualNode(i + 1);

            const double spanPx = pDinv.x() - pUinv.x();
            const double shiftPx = (spanPx > 4.0 * kShaftHalfWidthPx)
                                       ? kShaftHalfWidthPx
                                       : 0.0;
            const double tBase = (spanPx > 0.0) ? shiftPx / spanPx : 0.0;
            const double tUp   = vjUp ? 0.0 : tBase;
            const double tDn   = vjDn ? 0.0 : tBase;
            auto shifted = [](const QPointF &up, const QPointF &dn,
                              double tFrac, bool isUpstream) -> QPointF {
                const QPointF dir = dn - up;
                return isUpstream ? up + dir * tFrac : dn - dir * tFrac;
            };
            const QPointF upInv = shifted(pUinv, pDinv, tUp, true);
            const QPointF dnInv = shifted(pUinv, pDinv, tDn, false);
            const QPointF upCr  = shifted(pUcr,  pDcr,  tUp, true);
            const QPointF dnCr  = shifted(pUcr,  pDcr,  tDn, false);

            QPainterPath body;
            if (vjUp && vjDn) {          // open at both ends: two rails
                body.moveTo(upCr);  body.lineTo(dnCr);
                body.moveTo(upInv); body.lineTo(dnInv);
            } else if (vjUp) {           // open upstream
                body.moveTo(upCr);  body.lineTo(dnCr);
                body.lineTo(dnInv); body.lineTo(upInv);
            } else if (vjDn) {           // open downstream
                body.moveTo(dnCr);  body.lineTo(upCr);
                body.lineTo(upInv); body.lineTo(dnInv);
            } else {
                body.moveTo(upCr);  body.lineTo(dnCr);
                body.lineTo(dnInv); body.lineTo(upInv);
                body.closeSubpath();
            }
            p.drawPath(body);
        } else if (l.kind == ProfileBuilder::LinkKind::Weir) {
            const int inletIdx = l.reversed ? (i + 1) : i;
            const double zInletInv = m_path.nodes[inletIdx].invertElev;
            const double zSill = zInletInv + l.crestHeight;
            const double xUpPx = dataToPixel(xU, 0.0).x();
            const double xDnPx = dataToPixel(xD, 0.0).x();
            const double rawSpanPx = xDnPx - xUpPx;
            const double trim = (rawSpanPx > 4.0 * kShaftHalfWidthPx)
                                    ? kShaftHalfWidthPx
                                    : 0.0;
            p.drawRect(QRectF(QPointF(xUpPx + trim, dataToPixel(0.0, zSill).y()),
                              QPointF(xDnPx - trim, dataToPixel(0.0, zInletInv).y())));
        } else {
            const QPointF u = dataToPixel(xU, zUinv);
            const QPointF d = dataToPixel(xD, zDinv);
            const QPointF mid((u.x() + d.x()) / 2.0, (u.y() + d.y()) / 2.0);
            const double gapPx = std::max(8.0, std::abs(d.x() - u.x()));
            const double glyphW = std::max(12.0, gapPx * 0.85);
            const double glyphH = 16.0;
            p.drawRoundedRect(QRectF(mid.x() - glyphW / 2.0,
                                     mid.y() - glyphH / 2.0,
                                     glyphW, glyphH),
                              3.5, 3.5);
        }
    }

    QPen nodePen(highlight);
    nodePen.setWidthF(kNodeBarWidth + 2.5);
    nodePen.setCapStyle(Qt::FlatCap);
    nodePen.setJoinStyle(Qt::MiterJoin);
    p.setPen(nodePen);

    for (int i = 0; i < m_path.nodes.size(); ++i) {
        const auto &n = m_path.nodes[i];
        if (!m_selectedNames.contains(n.name)) continue;
        // A virtual junction is marked by a dashed rectangle rather than a
        // manhole tube, so its halo is dashed on the same footprint — a
        // solid one would contradict the unbroken pipe running through it.
        // There is no rim glyph below to outline either.
        if (n.kind == ProfileBuilder::NodeKind::VirtualJunction) {
            const double vchain = virtualX(i);
            const QPointF vrim  = dataToPixel(vchain,
                                              ProfileBuilder::groundElev(n));
            const QPointF vinv  = dataToPixel(vchain, n.invertElev);
            QPen vjHalo = themeVirtualJunctionPen();
            vjHalo.setColor(highlight);
            vjHalo.setWidthF(vjHalo.widthF() + 2.5);
            p.save();
            p.setPen(vjHalo);
            p.drawRect(QRectF(QPointF(vrim.x() - kShaftHalfWidthPx, vrim.y()),
                              QPointF(vrim.x() + kShaftHalfWidthPx, vinv.y())));
            p.restore();
            continue;
        }

        const bool incomingExc =
            (i > 0) && linkKindIsExcavated(m_path.links[i - 1].kind);
        const bool outgoingExc =
            (i + 1 < m_path.nodes.size())
            && linkKindIsExcavated(m_path.links[i].kind);
        if (incomingExc && outgoingExc) continue;

        const double chain = virtualX(i);
        const QPointF rim = dataToPixel(chain, ProfileBuilder::groundElev(n));
        const QPointF inv = dataToPixel(chain, n.invertElev);

        p.drawRect(QRectF(QPointF(rim.x() - kShaftHalfWidthPx, rim.y()),
                          QPointF(rim.x() + kShaftHalfWidthPx, inv.y())));

        constexpr double kHalf = 6.0;
        QPainterPath sym;
        switch (n.kind) {
        case ProfileBuilder::NodeKind::Junction:
            sym.addRect(QRectF(rim.x() - kHalf, rim.y() - 2.0,
                               kHalf * 2.0, 4.0));
            break;
        case ProfileBuilder::NodeKind::Outfall:
            sym.moveTo(rim + QPointF(-kHalf, -kHalf));
            sym.lineTo(rim + QPointF( kHalf, 0.0));
            sym.lineTo(rim + QPointF(-kHalf,  kHalf));
            sym.closeSubpath();
            break;
        case ProfileBuilder::NodeKind::Storage:
            sym.moveTo(rim + QPointF(-kHalf - 2.0,  kHalf / 2.0));
            sym.lineTo(rim + QPointF( kHalf + 2.0,  kHalf / 2.0));
            sym.lineTo(rim + QPointF( kHalf - 2.0, -kHalf));
            sym.lineTo(rim + QPointF(-kHalf + 2.0, -kHalf));
            sym.closeSubpath();
            break;
        case ProfileBuilder::NodeKind::Divider:
            sym.moveTo(rim + QPointF(0.0, -kHalf));
            sym.lineTo(rim + QPointF( kHalf, 0.0));
            sym.lineTo(rim + QPointF(0.0,  kHalf));
            sym.lineTo(rim + QPointF(-kHalf, 0.0));
            sym.closeSubpath();
            break;
        }
        p.drawPath(sym);
    }

    p.restore();
}

void ProfilePlotWidget::paintSeriesEnvelope(QPainter &p, int seriesIdx) const
{
    if (seriesIdx < 0 || seriesIdx >= m_series.size()) return;
    const auto &s = m_series[seriesIdx];
    if (!s.derived) return;
    const auto &arr = envelopeArray(*s.derived, s.kind);
    if (arr.size() != m_path.nodes.size()) return;
    const int N = m_path.nodes.size();

    auto chainAt = [&](int i) {
        return virtualX(i);
    };

    // Per-node curve builder for the envelope LINE.  Unlike the per-link
    // fill polygon (which clamps to [invert, crown] inside each pipe to
    // stay physical), the envelope line plots the raw envelope value at
    // each node.  Clamping the line up to the link invert / sill at each
    // node-link end would inflate the line whenever the max-over-time
    // sits between the node invert and the connecting pipe invert — at
    // high zoom that artificial lift reads as the line "shooting up".
    // SWMM already guarantees max HGL >= node invert, so no additional
    // floor is needed here.
    auto buildClampedCurve = [&](const QVector<double> &arr) {
        QVector<QPointF> curve;
        for (int i = 0; i < N; ++i) {
            if (!isFinite(arr[i])) continue;
            curve.push_back(dataToPixel(chainAt(i), arr[i]));
        }
        return curve;
    };

    QVector<QPointF> envCurve = buildClampedCurve(arr);
    if (envCurve.isEmpty()) return;

    // ---- Fill: per-link polygons -------------------------------------
    // Use the same waterfall + crown-clamp helper the per-period
    // paintHglFill uses, so the envelope band aligns exactly with the
    // Max HGL line (which is built via hglPolylineForLink below).  The
    // pixel-edge inset matches the line's inset so the two graphics
    // share the same manhole-tube endpoints.
    using K = ProfileBuilder::LinkKind;
    QPainterPath fill;
    for (int i = 0; i < m_path.links.size(); ++i) {
        const auto &l = m_path.links[i];
        const double vU = (i     < arr.size()) ? arr[i]     : std::nan("");
        const double vD = (i + 1 < arr.size()) ? arr[i + 1] : std::nan("");
        const double xU = chainAt(i);
        const double xD = chainAt(i + 1);

        QVector<QPointF> top;
        double inletInv, outletInv;
        if (!buildHglFillTop(l, m_path.nodes[i], m_path.nodes[i + 1],
                             xU, xD, vU, vD, top, inletInv, outletInv))
            continue;

        qreal pxU = 0.0, pxD = 0.0;
        hglEdgePixels(i, pxU, pxD);

        auto toPx = [&](const QPointF &dp) {
            QPointF px = dataToPixel(dp.x(), dp.y());
            if (dp.x() == xU)      px.setX(pxU);
            else if (dp.x() == xD) px.setX(pxD);
            return px;
        };

        QPainterPath poly;
        poly.moveTo(toPx(top.front()));
        for (int k = 1; k < top.size(); ++k) poly.lineTo(toPx(top[k]));
        QPointF bdr = dataToPixel(xD, outletInv); bdr.setX(pxD);
        QPointF bul = dataToPixel(xU, inletInv);  bul.setX(pxU);
        poly.lineTo(bdr);
        poly.lineTo(bul);
        poly.closeSubpath();
        fill.addPath(poly);
    }

    p.save();
    p.setClipRect(plotRect());

    // Series carries its own resolved brush/pen.  Caller-controlled
    // visibility: setting `brush.style() == Qt::NoBrush` hides the band,
    // setting `pen.style() == Qt::NoPen` hides the outline.  This is how
    // the dialog supports "band only", "line only", or both.
    if (s.brush.style() != Qt::NoBrush) {
        p.setPen(Qt::NoPen);
        p.setBrush(s.brush);
        p.drawPath(fill);
    }
    if (s.pen.style() != Qt::NoPen) {
        QPen linePen = s.pen;
        if (!linePen.color().isValid()) linePen.setColor(s.color);
        p.setBrush(Qt::NoBrush);
        p.setPen(linePen);
        QPainterPath outline;
        if (s.kind == ProfileBuilder::OutputKind::MaxHGL) {
            // Max HGL outline follows the same bidirectional waterfall
            // rule the live HGL uses (see hglPolylineForLink). One sub-
            // path per renderable link, with pipe-edge inset so the
            // line stops at the tube edge.
            for (int i = 0; i < m_path.links.size(); ++i) {
                const auto &l = m_path.links[i];
                if (l.kind == K::Pump || l.kind == K::Outlet) continue;
                const double vU = (i     < arr.size()) ? arr[i]     : std::nan("");
                const double vD = (i + 1 < arr.size()) ? arr[i + 1] : std::nan("");
                if (!isFinite(vU) || !isFinite(vD)) continue;
                const double xU = chainAt(i);
                const double xD = chainAt(i + 1);
                double inletInv, outletInv;
                if (!hglInletOutletInv(l, m_path.nodes[i], m_path.nodes[i + 1],
                                       inletInv, outletInv))
                    continue;
                const QVector<QPointF> pts = hglPolylineForLink(
                    xU, xD, vU, vD, inletInv, outletInv);

                qreal pxU = 0.0, pxD = 0.0;
                hglEdgePixels(i, pxU, pxD);
                auto toPx = [&](const QPointF &dp) {
                    QPointF px = dataToPixel(dp.x(), dp.y());
                    px.setX(dp.x() == xU ? pxU : pxD);
                    return px;
                };
                outline.moveTo(toPx(pts.front()));
                for (int k = 1; k < pts.size(); ++k) outline.lineTo(toPx(pts[k]));
            }
        } else {
            outline.moveTo(envCurve.first());
            for (int i = 1; i < envCurve.size(); ++i) outline.lineTo(envCurve[i]);
        }
        p.drawPath(outline);
    }
    p.restore();
}

void ProfilePlotWidget::paintHglFill(QPainter &p, int seriesIdx) const
{
    if (seriesIdx < 0 || seriesIdx >= m_series.size()) return;
    const auto &s = m_series[seriesIdx];
    if (!s.derived) return;
    // SourceDerived is period-major; hoist the current period's row so
    // the lambda below indexes into one contiguous QVector instead of
    // pointer-chasing across one inner vector per node.
    const auto &series = s.derived->hglByPeriod;
    if (m_currentPeriod < 0 || m_currentPeriod >= series.size()) return;
    const auto &periodRow = series[m_currentPeriod];
    if (periodRow.size() != m_path.nodes.size()) return;

    auto headAt = [&](int n) -> double {
        if (n < 0 || n >= periodRow.size()) return std::nan("");
        return periodRow[n];
    };
    auto chainAt = [&](int i) {
        return virtualX(i);
    };

    // Per-conduit water fill — polygon between the link-clamped HGL line
    // and the link invert.  Same clamp convention as paintSeriesCurrentLine
    // so the fill hugs the line and pinches to zero on dry ends.
    p.save();
    p.setClipRect(plotRect());
    p.setPen(Qt::NoPen);
    // HGL fill brush: prefer the series's brush; if NoBrush (line-only
    // styling), fall back to the global theme so the fill still reads
    // — users who want no fill should hide the HGL series entirely.
    QBrush fillBrush = s.brush;
    if (fillBrush.style() == Qt::NoBrush) {
        fillBrush = m_options ? m_options->hglFillBrush()
                              : QBrush(withAlphaF(s.color, 0.43));
    }
    p.setBrush(fillBrush);

    using K = ProfileBuilder::LinkKind;
    for (int i = 0; i < m_path.links.size(); ++i) {
        const auto &l = m_path.links[i];
        // Pumps & outlets have no physical water profile to fill — pumps
        // lift discharge without a continuous water surface, outlets are
        // terminal discharges.  Skip them; the rest (conduit, orifice,
        // weir) get a per-link fill polygon under the bidirectional
        // waterfall rule (see hglPolylineForLink in the anon namespace).
        if (l.kind == K::Pump || l.kind == K::Outlet) continue;
        const double xU = chainAt(i);
        const double xD = chainAt(i + 1);
        const double vU = headAt(i);
        const double vD = headAt(i + 1);
        if (!isFinite(vU) || !isFinite(vD)) continue;

        QVector<QPointF> top;
        double inletInv, outletInv;
        if (!buildHglFillTop(l, m_path.nodes[i], m_path.nodes[i + 1],
                             xU, xD, vU, vD, top, inletInv, outletInv))
            continue;

        // Inset the endpoints in pixel space so the polygon stops at
        // the manhole tube edge instead of bleeding into the node glyph
        // (the per-node nodal HGL is drawn separately by paintNodeFill).
        // Ends at a virtual junction take no inset — there is no tube
        // there, so consecutive fills abut and read as one body of water.
        qreal pxU = 0.0, pxD = 0.0;
        hglEdgePixels(i, pxU, pxD);

        auto toPx = [&](const QPointF &dp) {
            QPointF px = dataToPixel(dp.x(), dp.y());
            // Endpoints get snapped to the inset pixel-x so the line/
            // fill meets the tube edge; the optional crown-crossing
            // midpoint sits at an interior x and uses dataToPixel as-is.
            if (dp.x() == xU)      px.setX(pxU);
            else if (dp.x() == xD) px.setX(pxD);
            return px;
        };

        QPainterPath poly;
        poly.moveTo(toPx(top.front()));
        for (int k = 1; k < top.size(); ++k) poly.lineTo(toPx(top[k]));
        // Close the polygon along the link invert. The bottom-downstream
        // corner is at (pxD, outletInv); bottom-upstream at (pxU, inletInv).
        // Both are duplicate-free even in the waterfall cases — Qt
        // collapses any zero-length segment harmlessly.
        QPointF bdr = dataToPixel(xD, outletInv); bdr.setX(pxD);
        QPointF bul = dataToPixel(xU, inletInv);  bul.setX(pxU);
        poly.lineTo(bdr);
        poly.lineTo(bul);
        poly.closeSubpath();
        p.drawPath(poly);
    }
    p.restore();
}

void ProfilePlotWidget::paintLegend(QPainter &p) const
{
    m_legendRect = QRectF();
    if (m_series.isEmpty()) return;
    if (m_options && !m_options->legendVisible()) return;

    const QRectF r = plotRect();
    const QFont legendFont = m_options ? m_options->legendFont() : p.font();
    const QFontMetricsF fm(legendFont);
    p.save();
    p.setFont(legendFont);
    // Wide swatch so dashed pen patterns cycle at least 3× — otherwise
    // users see one half-dash and line styles read identically.
    const qreal swatchW = 56;
    const qreal rowH    = fm.height() + 2;
    const qreal padX    = 8;
    const qreal padY    = 6;

    // One row per visible series — fill and line brushes are merged into
    // a single swatch so the legend reads what's actually drawn on the
    // plot: fill alone, line alone, or line over fill.
    struct Row {
        QString text;
        bool    hasFill = false;
        bool    hasLine = false;
        QBrush  fillBrush;
        QPen    linePen;
    };
    QVector<Row> rows;
    for (const auto &s : m_series) {
        if (!s.visible || !s.derived) continue;
        Row r;
        r.text     = s.label;
        r.hasFill  = (s.brush.style() != Qt::NoBrush);
        r.hasLine  = (s.pen.style()   != Qt::NoPen);
        if (!r.hasFill && !r.hasLine) continue;
        r.fillBrush = s.brush;
        QPen pen = s.pen;
        if (!pen.color().isValid()) pen.setColor(s.color);
        r.linePen   = pen;
        rows.push_back(r);
    }
    if (rows.isEmpty()) return;

    // Compute the box width = widest row's text width + swatch + padding.
    qreal maxTextW = 0;
    for (const Row &row : rows) maxTextW = std::max(maxTextW,
                                                     fm.horizontalAdvance(row.text));
    const qreal boxW = swatchW + 6 + maxTextW + padX * 2;
    const qreal boxH = rows.size() * rowH + padY * 2;

    // Anchor in one of the four corners of the plot rect, then add the
    // user-drag offset.  The offset is stored on ProfilePlotOptions so it
    // survives repaints and (via QSettings/property serialisation) future
    // session changes.
    using LP = ProfilePlotOptions::LegendPosition;
    LP pos = m_options ? m_options->legendPosition() : LP::TopRight;
    qreal bx = r.right()  - boxW - 10;
    qreal by = r.top()    + 10;
    if (pos == LP::TopLeft     || pos == LP::BottomLeft)  bx = r.left()   + 10;
    if (pos == LP::BottomLeft  || pos == LP::BottomRight) by = r.bottom() - boxH - 10;
    if (m_options) {
        const QPointF off = m_options->legendOffset();
        bx += off.x();
        by += off.y();
    }
    const QRectF box(bx, by, boxW, boxH);
    m_legendRect = box;

    // Backing card.  Opacity comes from options when present.
    const double opacity = m_options ? m_options->legendOpacity() : 0.86;
    const int    alpha   = std::clamp(int(opacity * 255 + 0.5), 0, 255);
    p.setPen(QPen(QColor(0x80, 0x80, 0x80, std::min(255, alpha + 30)), 1.0));
    p.setBrush(QColor(0xFF, 0xFF, 0xFF, alpha));
    p.drawRoundedRect(box, 4, 4);

    qreal y = box.top() + padY;
    for (const Row &row : rows) {
        const qreal cy = y + rowH / 2.0;
        const QPointF a(box.left() + padX,            cy);
        const QPointF b(box.left() + padX + swatchW,  cy);
        // Swatch is a small filled rectangle (when fill is active) with
        // the line drawn through its vertical centre (when line is active).
        // Reflects exactly the brushes/pens the renderer will use, so any
        // brush-style change propagates immediately on the next repaint.
        const QRectF swatchRect(a.x(), cy - (rowH - 4) / 2.0,
                                swatchW, rowH - 4);
        if (row.hasFill) {
            p.setPen(Qt::NoPen);
            p.setBrush(row.fillBrush);
            p.drawRect(swatchRect);
        }
        if (row.hasLine) {
            p.setBrush(Qt::NoBrush);
            p.setPen(row.linePen);
            p.drawLine(a, b);
        }
        // Thin border to delimit the swatch even when only fill is shown.
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(0x80, 0x80, 0x80, 120), 0.6));
        p.drawRect(swatchRect);
        // Label.
        p.setPen(QColor(0x20, 0x20, 0x20));
        p.drawText(QPointF(b.x() + 6, cy + fm.ascent() / 2.0 - 1), row.text);
        y += rowH;
    }
    p.restore();
}

void ProfilePlotWidget::paintNodeFill(QPainter &p, int seriesIdx) const
{
    if (seriesIdx < 0 || seriesIdx >= m_series.size()) return;
    const auto &s = m_series[seriesIdx];
    if (!s.derived) return;
    // SourceDerived is period-major; index one period's row and walk it.
    const auto &series = s.derived->hglByPeriod;
    if (m_currentPeriod < 0 || m_currentPeriod >= series.size()) return;
    const auto &periodRow = series[m_currentPeriod];
    if (periodRow.size() != m_path.nodes.size()) return;

    // Manhole interior water fill — invert (bottom) up to the node's
    // current HGL (clamped to the rim).  Sized to match the tube glyph
    // in `paintNodes` so the fill snaps cleanly inside the outline.
    constexpr double kShaftHalfWidthPx = 3.5;
    p.save();
    p.setClipRect(plotRect());
    p.setPen(Qt::NoPen);
    QBrush fillBrush = s.brush;
    if (fillBrush.style() == Qt::NoBrush) {
        fillBrush = m_options ? m_options->hglFillBrush()
                              : QBrush(withAlphaF(s.color, 0.55));
    }
    p.setBrush(fillBrush);

    for (int i = 0; i < m_path.nodes.size(); ++i) {
        const double hgl = periodRow[i];
        if (!isFinite(hgl)) continue;
        // A virtual junction has no shaft to fill, and its rim sits well
        // above the pipe crown — a column here would push water up into
        // the soil.  The adjacent per-link fills already abut at its
        // chainage (hglEdgePixels takes no inset there).
        if (isVirtualNode(i)) continue;
        const auto &n = m_path.nodes[i];
        const double rim = ProfileBuilder::groundElev(n);
        const double cap = std::clamp(hgl, n.invertElev, rim);
        if (cap <= n.invertElev) continue;     // nothing to fill
        const double chain = virtualX(i);
        const QPointF top  = dataToPixel(chain, cap);
        const QPointF bot  = dataToPixel(chain, n.invertElev);
        p.drawRect(QRectF(QPointF(top.x() - kShaftHalfWidthPx, top.y()),
                          QPointF(bot.x() + kShaftHalfWidthPx, bot.y())));
    }
    p.restore();
}

void ProfilePlotWidget::paintNodeEnvelopeFill(QPainter &p, int seriesIdx) const
{
    if (seriesIdx < 0 || seriesIdx >= m_series.size()) return;
    const auto &s = m_series[seriesIdx];
    if (!s.derived) return;
    if (s.brush.style() == Qt::NoBrush) return;  // band hidden via toggle
    const auto &arr = envelopeArray(*s.derived, s.kind);
    if (arr.size() != m_path.nodes.size()) return;

    // Counterpart to paintNodeFill (per-period live HGL), but driven by the
    // static envelope array (maxHgl / maxEgl / etc.) so the Max-HGL band
    // covers the manhole shafts between consecutive link polygons.  Without
    // this pass each per-link envelope polygon is inset by kHglPipeEdgeInsetPx
    // at the tube edge and the band reads as a series of disconnected
    // segments interrupted at every node.
    constexpr double kShaftHalfWidthPx = 3.5;
    p.save();
    p.setClipRect(plotRect());
    p.setPen(Qt::NoPen);
    p.setBrush(s.brush);

    for (int i = 0; i < m_path.nodes.size(); ++i) {
        const double v = arr[i];
        if (!isFinite(v)) continue;
        // No shaft at a virtual junction — see paintNodeFill.
        if (isVirtualNode(i)) continue;
        const auto &n = m_path.nodes[i];
        const double rim = ProfileBuilder::groundElev(n);
        const double cap = std::clamp(v, n.invertElev, rim);
        if (cap <= n.invertElev) continue;     // nothing to fill
        const double chain = virtualX(i);
        const QPointF top  = dataToPixel(chain, cap);
        const QPointF bot  = dataToPixel(chain, n.invertElev);
        p.drawRect(QRectF(QPointF(top.x() - kShaftHalfWidthPx, top.y()),
                          QPointF(bot.x() + kShaftHalfWidthPx, bot.y())));
    }
    p.restore();
}

void ProfilePlotWidget::paintNodeHglLine(QPainter &p, int seriesIdx) const
{
    if (seriesIdx < 0 || seriesIdx >= m_series.size()) return;
    const auto &s = m_series[seriesIdx];
    if (!s.derived) return;

    // Per-node short horizontal HGL segment spanning the manhole tube
    // width.  Designed to butt up against the link HGL line at each
    // pipe edge (both inset by kHglPipeEdgeInsetPx), giving a
    // continuous trace across nodes + links.  Supports Current HGL
    // (per-period) and Max HGL (envelope) — kind determines the source.
    // hglByPeriod is period-major; bind to the current period's row once.
    using OK = ProfileBuilder::OutputKind;
    const QVector<double> *periodRowHgl = nullptr;
    {
        const auto &series = s.derived->hglByPeriod;
        if (m_currentPeriod >= 0 && m_currentPeriod < series.size())
            periodRowHgl = &series[m_currentPeriod];
    }
    auto valueAt = [&](int n) -> double {
        if (s.kind == OK::HGL) {
            if (!periodRowHgl) return std::nan("");
            if (n < 0 || n >= periodRowHgl->size()) return std::nan("");
            return (*periodRowHgl)[n];
        }
        if (s.kind == OK::MaxHGL) {
            const auto &arr = s.derived->maxHgl;
            if (n < 0 || n >= arr.size()) return std::nan("");
            return arr[n];
        }
        return std::nan("");
    };

    QPen pen = s.pen;
    if (!pen.color().isValid()) pen.setColor(s.color);
    if (pen.style() == Qt::NoPen) return;   // line hidden via toggle

    p.save();
    p.setClipRect(plotRect());
    p.setBrush(Qt::NoBrush);
    p.setPen(pen);

    for (int i = 0; i < m_path.nodes.size(); ++i) {
        const double v = valueAt(i);
        if (!isFinite(v)) continue;
        // A virtual junction spans no tube, so there is nothing to bridge:
        // the two link traces already meet at its chainage.  Drawing the
        // horizontal stub here would flatten a sloping water surface.
        if (isVirtualNode(i)) continue;
        const double xn = virtualX(i);
        const QPointF c = dataToPixel(xn, v);
        const QPointF left (c.x() - kHglPipeEdgeInsetPx, c.y());
        const QPointF right(c.x() + kHglPipeEdgeInsetPx, c.y());
        p.drawLine(left, right);
    }
    p.restore();
}

void ProfilePlotWidget::paintWaterSurfaceFill(QPainter &p, int seriesIdx) const
{
    if (seriesIdx < 0 || seriesIdx >= m_series.size()) return;
    const auto &s = m_series[seriesIdx];
    if (!s.derived) return;
    // SourceDerived is period-major; bind to the current period's row.
    const auto &arr = s.derived->waterSurfaceByPeriod;
    if (m_currentPeriod < 0 || m_currentPeriod >= arr.size()) return;
    const auto &periodRow = arr[m_currentPeriod];
    if (periodRow.size() != m_path.nodes.size()) return;

    auto sampleAt = [&](int n) -> double {
        if (n < 0 || n >= periodRow.size()) return std::nan("");
        return periodRow[n];
    };
    auto chainAt = [&](int i) {
        return virtualX(i);
    };

    // Free-surface water fill — between the per-link water-surface line
    // (invert + depth, capped at rim by the engine) and the link invert.
    // Distinct from HGL fill when pressurized: HGL keeps climbing above
    // rim, water surface clamps at rim.  No fill is drawn when the series
    // carries no brush — leave the in-pipe area to whichever HGL series
    // already painted it.
    if (s.brush.style() == Qt::NoBrush) return;

    p.save();
    p.setClipRect(plotRect());
    p.setPen(Qt::NoPen);
    p.setBrush(s.brush);

    for (int i = 0; i < m_path.links.size(); ++i) {
        const auto &l = m_path.links[i];
        if (l.kind != ProfileBuilder::LinkKind::Conduit) continue;
        const double xU = chainAt(i);
        const double xD = chainAt(i + 1);
        const double vU = sampleAt(i);
        const double vD = sampleAt(i + 1);
        if (!isFinite(vU) || !isFinite(vD)) continue;
        const double invUp = m_path.nodes[i    ].invertElev + l.offset1;
        const double invDn = m_path.nodes[i + 1].invertElev + l.offset2;
        // Free surface caps at the conduit crown — the engine already does
        // this in nodeDepth (bounded by maxDepth), but clamp here too for
        // safety in case a series carries a depth from a different source.
        const double crownUp = invUp + l.maxDepth;
        const double crownDn = invDn + l.maxDepth;
        const double topUp = std::clamp(vU, invUp, crownUp);
        const double topDn = std::clamp(vD, invDn, crownDn);
        QPainterPath poly;
        poly.moveTo(dataToPixel(xU, topUp));
        poly.lineTo(dataToPixel(xD, topDn));
        poly.lineTo(dataToPixel(xD, invDn));
        poly.lineTo(dataToPixel(xU, invUp));
        poly.closeSubpath();
        p.drawPath(poly);
    }
    p.restore();
}

void ProfilePlotWidget::paintLabelAxis(QPainter &p) const
{
    if (!m_toggles.showNodeLabels && !m_toggles.showLinkLabels) return;

    const QRectF r = plotRect();
    const QFontMetricsF fm(p.font());

    // One label, anchored at the bottom of its row (i.e. adjacent to the
    // plot's top edge), reading bottom-to-top for vertical / diagonal so
    // the head of the text lands away from the plot.
    auto drawLabel = [&](const QString &text, double chainage,
                         double rowTopY, double rowBottomY,
                         const QColor &color) {
        const double pixelX = dataToPixel(chainage, m_dataYMin).x();
        p.setPen(color);
        p.save();
        const QPointF anchor(pixelX, rowBottomY - 1);
        switch (m_toggles.labelOrientation) {
        case LayerToggles::Vertical:
            p.translate(anchor);
            p.rotate(-90);
            p.drawText(QRectF(0, -fm.height() / 2.0,
                              rowBottomY - rowTopY, fm.height()),
                       Qt::AlignLeft | Qt::AlignVCenter, text);
            break;
        case LayerToggles::Diagonal: {
            const double angle = std::clamp(m_toggles.labelAngleDeg, 1, 89);
            p.translate(anchor);
            p.rotate(-angle);
            const double widthBudget =
                (rowBottomY - rowTopY) / std::sin(angle * M_PI / 180.0);
            p.drawText(QRectF(0, -fm.height() / 2.0, widthBudget, fm.height()),
                       Qt::AlignLeft | Qt::AlignVCenter, text);
            break;
        }
        case LayerToggles::Horizontal: {
            const double w = fm.horizontalAdvance(text) + 4;
            p.drawText(QRectF(pixelX - w / 2.0, rowTopY,
                              w, rowBottomY - rowTopY),
                       Qt::AlignHCenter | Qt::AlignBottom, text);
            break;
        }
        }
        p.restore();
    };

    int longestNode = 0, longestLink = 0;
    for (const auto &n : m_path.nodes) longestNode = std::max(longestNode, static_cast<int>(n.name.size()));
    for (const auto &l : m_path.links) longestLink = std::max(longestLink, static_cast<int>(l.name.size()));

    // Stack order (top → bottom of widget): link row, then node row, then
    // plot.  Walking *upward* from r.top() so the row immediately above
    // the plot edge is the node row (closest to plot top edge).
    double cursor = r.top() - kLabelRowGap;
    if (m_toggles.showNodeLabels) {
        const int h = labelRowHeightPx(m_toggles.labelOrientation, longestNode,
                                       m_toggles.labelAngleDeg, fm);
        const double rowBot = cursor;
        const double rowTop = cursor - h;
        QPen leader(QColor(0xAA, 0xAA, 0xAA));
        leader.setWidthF(0.8);
        p.setPen(leader);
        for (int i = 0; i < m_path.nodes.size(); ++i) {
            const double chain = virtualX(i);
            const double x     = dataToPixel(chain, m_dataYMin).x();
            p.drawLine(QPointF(x, rowBot + 1), QPointF(x, r.top()));
        }
        const QColor labelColor(0x10, 0x10, 0x10);
        for (int i = 0; i < m_path.nodes.size(); ++i) {
            const double chain = virtualX(i);
            drawLabel(m_path.nodes[i].name, chain, rowTop, rowBot, labelColor);
        }
        cursor = rowTop - kLabelRowGap;
    }
    if (m_toggles.showLinkLabels) {
        const int h = labelRowHeightPx(m_toggles.labelOrientation, longestLink,
                                       m_toggles.labelAngleDeg, fm);
        const double rowBot = cursor;
        const double rowTop = cursor - h;
        const QColor labelColor(0x33, 0x33, 0x33);
        QPen leader(QColor(0xCC, 0xCC, 0xCC));
        leader.setWidthF(0.6);
        p.setPen(leader);
        for (int i = 0; i < m_path.links.size(); ++i) {
            const double mid = virtualXAlongLink(i, 0.5);
            const double x   = dataToPixel(mid, m_dataYMin).x();
            p.drawLine(QPointF(x, rowBot + 1), QPointF(x, r.top()));
        }
        for (int i = 0; i < m_path.links.size(); ++i) {
            const double mid = virtualXAlongLink(i, 0.5);
            drawLabel(m_path.links[i].name, mid, rowTop, rowBot, labelColor);
        }
    }
}

void ProfilePlotWidget::paintSeriesCurrentLine(QPainter &p, int seriesIdx) const
{
    if (seriesIdx < 0 || seriesIdx >= m_series.size()) return;
    const auto &s = m_series[seriesIdx];
    if (!s.derived) return;
    if (!isCurrentTimeKind(s.kind)) return;
    // currentTimeArray returns the period-major SourceDerived row vector;
    // index the current period once and read N contiguous floats from it.
    const auto &series = currentTimeArray(*s.derived, s.kind);
    if (m_currentPeriod < 0 || m_currentPeriod >= series.size()) return;
    const auto &periodRow = series[m_currentPeriod];
    if (periodRow.size() != m_path.nodes.size()) return;

    auto valueAt = [&](int n) -> double {
        if (n < 0 || n >= periodRow.size()) return std::nan("");
        return periodRow[n];
    };
    auto chainAt = [&](int i) {
        return virtualX(i);
    };

    p.save();
    p.setClipRect(plotRect());
    // The series's pen is the source of truth — the dialog merges layer
    // color, per-output dash defaults, and any user override before the
    // series reaches the widget.  Fall back to source color when the pen
    // has no valid color set.
    QPen pen = s.pen;
    if (!pen.color().isValid()) pen.setColor(s.color);
    if (pen.style() == Qt::NoPen) pen.setStyle(Qt::SolidLine);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    // Walk *per-link* so each conduit gets its own physically-meaningful
    // endpoint values.  HGL uses the bidirectional-waterfall rule (see
    // hglPolylineForLink in the anon namespace); EGL and WaterSurface
    // keep the legacy per-kind clamping below.
    using K = ProfileBuilder::LinkKind;
    const bool isHgl = (s.kind == ProfileBuilder::OutputKind::HGL);
    for (int i = 0; i < m_path.links.size(); ++i) {
        const auto &l   = m_path.links[i];
        const double xU = chainAt(i);
        const double xD = chainAt(i + 1);
        const double vU = valueAt(i);
        const double vD = valueAt(i + 1);
        if (!isFinite(vU) || !isFinite(vD)) continue;

        if (isHgl) {
            // Pumps and outlets render no HGL line — the head jump at
            // each end is conveyed by paintNodeFill's nodal HGL bar.
            if (l.kind == K::Pump || l.kind == K::Outlet) continue;
            double inletInv, outletInv;
            if (!hglInletOutletInv(l, m_path.nodes[i], m_path.nodes[i + 1],
                                   inletInv, outletInv))
                continue;
            const QVector<QPointF> pts = hglPolylineForLink(
                xU, xD, vU, vD, inletInv, outletInv);

            qreal pxU = 0.0, pxD = 0.0;
            hglEdgePixels(i, pxU, pxD);
            auto toPx = [&](const QPointF &dp) {
                QPointF px = dataToPixel(dp.x(), dp.y());
                px.setX(dp.x() == xU ? pxU : pxD);
                return px;
            };
            for (int k = 1; k < pts.size(); ++k)
                p.drawLine(toPx(pts[k - 1]), toPx(pts[k]));
            continue;
        }

        if (l.kind == ProfileBuilder::LinkKind::Conduit) {
            const double invUp = m_path.nodes[i    ].invertElev + l.offset1;
            const double invDn = m_path.nodes[i + 1].invertElev + l.offset2;
            // EGL = HGL + v²/(2g): when the link is dry the velocity head
            // collapses too, so the same invert-floor clamp applies.
            const double clampUp = std::max(vU, invUp);
            const double clampDn = std::max(vD, invDn);
            p.drawLine(dataToPixel(xU, clampUp), dataToPixel(xD, clampDn));
        } else if (l.kind == ProfileBuilder::LinkKind::Weir
                || l.kind == ProfileBuilder::LinkKind::Orifice) {
            // Crest elevation:
            //   Weir   → inletInvert + crest_height (dedicated SWMM
            //            field; offset1 is always 0 for weirs).
            //   Orifice → inletInvert + offset1 (orifices DO store the
            //            sill offset in offset1 — see PostParseResolver).
            const int    inletIdx  = l.reversed ? (i + 1) : i;
            const double zInletInv = m_path.nodes[inletIdx].invertElev;
            const double crest =
                (l.kind == ProfileBuilder::LinkKind::Weir)
                    ? zInletInv + l.crestHeight
                    : zInletInv + (l.reversed ? l.offset2 : l.offset1);

            const bool upAbove = (vU >= crest);
            const bool dnAbove = (vD >= crest);
            // Surface shape (cartoon-style):
            //   Both above crest (submerged) — single sloped line; both
            //     ends are above the crest so the line itself never dips
            //     below the weir.
            //   One above, one below (free spill) — horizontal "thin
            //     water surface" at the higher HGL across the weir top,
            //     then a vertical fall on the low-side face down to that
            //     node's HGL.
            //   Both below crest — disconnected; draw nothing across
            //     (each side's pool is shown by paintHglFill).
            if (upAbove && dnAbove) {
                p.drawLine(dataToPixel(xU, vU), dataToPixel(xD, vD));
            } else if (upAbove) {
                p.drawLine(dataToPixel(xU, vU), dataToPixel(xD, vU));
                p.drawLine(dataToPixel(xD, vU), dataToPixel(xD, vD));
            } else if (dnAbove) {
                p.drawLine(dataToPixel(xU, vU), dataToPixel(xU, vD));
                p.drawLine(dataToPixel(xU, vD), dataToPixel(xD, vD));
            }
        } else {
            // Pump / Outlet — no sill semantics, draw the raw node-to-
            // node connection so the user can still read the head jump.
            p.drawLine(dataToPixel(xU, vU), dataToPixel(xD, vD));
        }
    }
    p.restore();
}

void ProfilePlotWidget::paintTimeLabel(QPainter &p) const
{
    m_timeLabelRect = QRectF();
    if (!m_currentDateTime.isValid()) return;
    if (m_options && !m_options->showTimeLabel()) return;

    const QString fmt = m_options ? m_options->timeLabelFormat()
                                  : QStringLiteral("dd-MMM-yyyy HH:mm:ss");
    const QString text = m_currentDateTime.toString(fmt);
    if (text.isEmpty()) return;

    const QFont    font  = m_options ? m_options->timeLabelFont() : p.font();
    const QColor   color = m_options ? m_options->timeLabelColor()
                                     : QColor(0x10, 0x10, 0x10);
    const QFontMetricsF fm(font);

    constexpr qreal kPad  = 6.0;
    constexpr qreal kEdge = 8.0;
    const qreal textW = fm.horizontalAdvance(text);
    const qreal textH = fm.height();
    const qreal boxW  = textW + 2 * kPad;
    const qreal boxH  = textH + 2 * kPad;

    using TP = ProfilePlotOptions::TimeLabelPosition;
    TP pos = m_options ? m_options->timeLabelPosition() : TP::TimeTopLeft;
    const QRectF r = plotRect();
    qreal bx = r.right() - boxW - kEdge;
    qreal by = r.top()   + kEdge;
    if (pos == TP::TimeTopLeft     || pos == TP::TimeBottomLeft)  bx = r.left()   + kEdge;
    if (pos == TP::TimeBottomLeft  || pos == TP::TimeBottomRight) by = r.bottom() - boxH - kEdge;
    if (m_options) {
        const QPointF off = m_options->timeLabelOffset();
        bx += off.x();
        by += off.y();
    }

    const QRectF box(bx, by, boxW, boxH);
    m_timeLabelRect = box;

    p.save();
    p.setFont(font);
    p.setPen(QPen(QColor(0x80, 0x80, 0x80, 130), 0.8));
    p.setBrush(QColor(0xFF, 0xFF, 0xFF, 200));
    p.drawRoundedRect(box, 3, 3);
    p.setPen(color);
    p.drawText(box, Qt::AlignCenter, text);
    p.restore();
}
