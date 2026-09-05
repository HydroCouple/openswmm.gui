/*!
 * \file   inletdrawingview.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/widgets/inletdrawingview.h"

#include "core/unitsystem.h"
#include "inlet/inletprovider.h"

#include <QCoreApplication>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QMouseEvent>
#include <QPainterPath>
#include <QPair>
#include <QPalette>
#include <QPolygonF>
#include <QShowEvent>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace openswmmvis::ui {

using openswmmvis::inlet::GrateType;
using openswmmvis::inlet::InletCurveKind;
using openswmmvis::inlet::InletDesignData;
using openswmmvis::inlet::InletProvider;
using openswmmvis::inlet::InletType;
using openswmmvis::inlet::ThroatType;

// ─────────────────────────────────────────────────────────────────────────────
// Theme
// ─────────────────────────────────────────────────────────────────────────────

InletTheme InletTheme::fromPalette(const QPalette &pal)
{
    InletTheme t;
    t.background = pal.base().color();
    t.outline    = pal.windowText().color();
    t.pavement   = pal.mid().color();
    t.accent     = pal.highlight().color();
    t.dimension  = pal.mid().color();
    t.text       = pal.windowText().color();
    return t;
}

// ─────────────────────────────────────────────────────────────────────────────
// Scene-building primitives
// ─────────────────────────────────────────────────────────────────────────────

namespace {

constexpr double kTargetSpanPx = 260.0;  ///< Longest model dimension in scene px.
constexpr double kMinScale     = 6.0;
constexpr double kMaxScale     = 4000.0;
constexpr double kMinExtentPx  = 18.0;   ///< Minimum on-screen extent of any dimension.
constexpr double kPanelGap     = 96.0;
constexpr double kDimOffset    = 30.0;
constexpr double kArrow        = 6.0;
constexpr double kLabelPt      = 9.0;

QString trs(const char *text)
{
    return QCoreApplication::translate("InletSceneBuilder", text);
}

struct Ctx
{
    QGraphicsScene *scene = nullptr;
    InletTheme      theme;
    QString         lengthLabel   = QStringLiteral("ft");
    QString         velocityLabel = QStringLiteral("ft/s");
    double          s = 1.0;          ///< Scene px per model length unit.
};

double px(const Ctx &c, double v, double minPx = kMinExtentPx)
{
    return std::max(v * c.s, minPx);
}

QPointF unitVec(const QPointF &v)
{
    const double len = std::hypot(v.x(), v.y());
    return (len > 1e-9) ? QPointF(v.x() / len, v.y() / len) : QPointF(1.0, 0.0);
}

QGraphicsSimpleTextItem *addText(Ctx &c, const QPointF &pos, const QString &s,
                                  const QColor &colour, bool centred = false)
{
    QGraphicsSimpleTextItem *t = c.scene->addSimpleText(s);
    QFont f = t->font();
    f.setPointSizeF(kLabelPt);
    t->setFont(f);
    t->setBrush(colour);
    QPointF p = pos;
    if (centred) {
        const QRectF br = t->boundingRect();
        p -= QPointF(br.width() / 2.0, br.height() / 2.0);
    }
    t->setPos(p);
    return t;
}

/*! \brief A label that participates in the callout count (tagged with
 *  `InletSceneBuilder::CalloutRole`). */
QGraphicsSimpleTextItem *addNote(Ctx &c, const QPointF &pos, const QString &label,
                                  bool centred = false)
{
    QGraphicsSimpleTextItem *t = addText(c, pos, label, c.theme.text, centred);
    t->setData(InletSceneBuilder::CalloutRole, label);
    return t;
}

void addArrowHead(Ctx &c, const QPointF &tip, const QPointF &dir)
{
    const QPointF d = unitVec(dir);
    const QPointF n(-d.y(), d.x());
    QPolygonF poly;
    poly << tip
         << tip - d * kArrow + n * (kArrow * 0.35)
         << tip - d * kArrow - n * (kArrow * 0.35);
    c.scene->addPolygon(poly, QPen(Qt::NoPen), QBrush(c.theme.dimension));
}

/*! \brief Extension lines + dimension line with arrowheads + a callout label.
 *  \p offset is signed: the dimension line sits that far along the left-hand
 *  normal of a→b (positive) or the right-hand normal (negative). */
void addDimension(Ctx &c, const QPointF &a, const QPointF &b,
                   double offset, const QString &label)
{
    const QPointF d = unitVec(b - a);
    const QPointF n(-d.y(), d.x());
    const double  sgn = (offset >= 0.0) ? 1.0 : -1.0;
    const QPointF a2 = a + n * offset;
    const QPointF b2 = b + n * offset;

    const QPen ext(c.theme.dimension, 0.8, Qt::SolidLine);
    c.scene->addLine(QLineF(a, a2 + n * (5.0 * sgn)), ext);
    c.scene->addLine(QLineF(b, b2 + n * (5.0 * sgn)), ext);
    c.scene->addLine(QLineF(a2, b2), QPen(c.theme.dimension, 1.0));
    addArrowHead(c, a2, a2 - b2);
    addArrowHead(c, b2, b2 - a2);

    addNote(c, (a2 + b2) / 2.0 + n * (11.0 * sgn), label, /*centred*/ true);
}

QString lenLabel(const Ctx &c, const QString &symbol, double value)
{
    return QStringLiteral("%1 = %2 %3")
        .arg(symbol)
        .arg(value, 0, 'f', 2)
        .arg(c.lengthLabel);
}

/*! \brief Hatched body: pavement / subgrade fill under an outline. */
void addHatchedPolygon(Ctx &c, const QPolygonF &poly)
{
    c.scene->addPolygon(poly, QPen(c.theme.outline, 1.6),
                        QBrush(c.theme.pavement, Qt::BDiagPattern));
}

void addHatchedRect(Ctx &c, const QRectF &r)
{
    c.scene->addRect(r, QPen(c.theme.outline, 1.6),
                     QBrush(c.theme.pavement, Qt::BDiagPattern));
}

/*! \brief Section title above a panel. */
double addPanelTitle(Ctx &c, double y, const QString &title)
{
    QGraphicsSimpleTextItem *t = addText(c, QPointF(0.0, y), title, c.theme.text);
    QFont f = t->font();
    f.setBold(true);
    t->setFont(f);
    return y + t->boundingRect().height() + 10.0;
}

// ── Grate bar patterns (HEC-22) ──────────────────────────────────────────────

void drawGrateBars(Ctx &c, const QRectF &r, GrateType g)
{
    const QPen bar(c.theme.accent, 1.2);

    // Bar-family note (Inlets plan §2.3). Descriptive, not a dimension, so it
    // is deliberately NOT tagged as a callout.
    addText(c, QPointF(r.right() + 14.0, r.top() - 18.0),
            openswmmvis::inlet::grateTypeLabel(g), c.theme.text);

    switch (g) {
    case GrateType::PBar50:
    case GrateType::PBar30:
    case GrateType::PBar50x100: {
        // Bars parallel to the flow, spaced per the bar family.
        const int nBars = (g == GrateType::PBar30) ? 7 : 5;
        for (int i = 1; i < nBars; ++i) {
            const double y = r.top() + r.height() * i / double(nBars);
            c.scene->addLine(QLineF(r.left(), y, r.right(), y), bar);
        }
        if (g == GrateType::PBar50x100) {
            for (int i = 1; i < 4; ++i) {
                const double x = r.left() + r.width() * i / 4.0;
                c.scene->addLine(QLineF(x, r.top(), x, r.bottom()), bar);
            }
        }
        break;
    }
    case GrateType::CurvedVane: {
        const int n = 6;
        const double bow = r.width() / (2.0 * n);
        for (int i = 0; i < n; ++i) {
            const double x = r.left() + r.width() * (i + 0.5) / n;
            QPainterPath p(QPointF(x - bow * 0.6, r.top()));
            p.quadTo(QPointF(x + bow, r.center().y()),
                     QPointF(x - bow * 0.6, r.bottom()));
            c.scene->addPath(p, bar);
        }
        break;
    }
    case GrateType::TiltBar45:
    case GrateType::TiltBar30:
    case GrateType::Reticuline:
    case GrateType::Generic: {
        // Patterns that would spill outside the frame are drawn as children
        // of a clipping frame item instead of being clipped by hand.
        QGraphicsRectItem *clip =
            c.scene->addRect(r, QPen(Qt::NoPen), QBrush(Qt::NoBrush));
        clip->setFlag(QGraphicsItem::ItemClipsChildrenToShape, true);

        if (g == GrateType::Generic) {
            for (double y = r.top() + 4.0; y < r.bottom(); y += 7.0)
                for (double x = r.left() + 4.0; x < r.right(); x += 7.0) {
                    auto *dot = new QGraphicsEllipseItem(
                        QRectF(x - 1.0, y - 1.0, 2.0, 2.0), clip);
                    dot->setPen(QPen(Qt::NoPen));
                    dot->setBrush(c.theme.accent);
                }
        } else {
            const double deg = (g == GrateType::TiltBar30) ? 30.0 : 45.0;
            const double tangent = std::tan(qDegreesToRadians(deg));
            const double dx = r.height() / std::max(tangent, 1e-6);
            for (double x = r.left() - std::fabs(dx);
                 x < r.right() + std::fabs(dx); x += 9.0)
            {
                auto *l = new QGraphicsLineItem(
                    QLineF(x, r.bottom(), x + dx, r.top()), clip);
                l->setPen(bar);
                if (g == GrateType::Reticuline) {
                    auto *m = new QGraphicsLineItem(
                        QLineF(x, r.top(), x + dx, r.bottom()), clip);
                    m->setPen(bar);
                }
            }
        }
        break;
    }
    }
}

/*! \brief GENERIC grates carry two extra callouts (open fraction, splash-over
 *  velocity) — the two parameters that only apply to that family. */
void addGenericGrateNotes(Ctx &c, const InletDesignData &d, const QPointF &at)
{
    if (d.grateType != GrateType::Generic) return;
    addNote(c, at,
            trs("Open area = %1 %").arg(d.openArea * 100.0, 0, 'f', 0));
    addNote(c, at + QPointF(0.0, 15.0),
            trs("V₀ = %1 %2").arg(d.splashVeloc, 0, 'f', 2).arg(c.velocityLabel));
}

/*! \brief Curb + pavement + subgrade body of a gutter section. Returns the
 *  gutter invert (foot of the curb face). */
QPointF drawGutterSection(Ctx &c, double x0, double y0,
                           double widthPx, double curbHeightPx)
{
    const double rise  = widthPx * 0.08;
    const double depth = 30.0;
    const QPointF invert(x0, y0);

    QPolygonF body;
    body << QPointF(x0 - 16.0, y0 - curbHeightPx)   // back of curb, top
         << QPointF(x0,        y0 - curbHeightPx)   // curb face, top
         << invert                                   // gutter invert
         << QPointF(x0 + widthPx, y0 - rise)         // pavement crown side
         << QPointF(x0 + widthPx, y0 + depth)        // subgrade
         << QPointF(x0 - 16.0,    y0 + depth);
    addHatchedPolygon(c, body);
    return invert;
}

/*! \brief Trapezoidal channel section for the DROP types. Returns the
 *  invert-left / invert-right pair of the channel bottom. */
QPair<QPointF, QPointF> drawChannelSection(Ctx &c, double x0, double y0,
                                             double bottomPx, double depthPx)
{
    const double flare = depthPx * 0.6;
    const QPointF bl(x0, y0);
    const QPointF br(x0 + bottomPx, y0);

    QPolygonF body;
    body << QPointF(x0 - flare - 14.0, y0 - depthPx - 12.0)
         << QPointF(x0 - flare,        y0 - depthPx)
         << bl
         << br
         << QPointF(x0 + bottomPx + flare,        y0 - depthPx)
         << QPointF(x0 + bottomPx + flare + 14.0, y0 - depthPx - 12.0)
         << QPointF(x0 + bottomPx + flare + 14.0, y0 + 26.0)
         << QPointF(x0 - flare - 14.0,            y0 + 26.0);
    addHatchedPolygon(c, body);
    return { bl, br };
}

// ── Per-type drawings ────────────────────────────────────────────────────────

/*! \brief GRATE and DROP GRATE: plan with the bar pattern, section in the
 *  gutter (or in a trapezoidal channel for the drop variant). */
void buildGrate(Ctx &c, const InletDesignData &d, bool drop)
{
    const double L = px(c, d.grateLength);
    const double W = px(c, d.grateWidth);

    double y = addPanelTitle(c, 0.0, trs("Plan"));

    // Pavement (or channel invert) around the grate.
    addHatchedRect(c, QRectF(-56.0, y - 30.0, L + 112.0, W + 60.0));
    if (!drop) {
        // Curb line along the top edge of the pavement band.
        c.scene->addLine(QLineF(-56.0, y - 30.0, L + 56.0, y - 30.0),
                         QPen(c.theme.outline, 2.4));
    } else {
        // Channel walls converge on the grate in plan.
        c.scene->addLine(QLineF(-56.0, y - 16.0, L + 56.0, y - 16.0),
                         QPen(c.theme.outline, 1.4));
        c.scene->addLine(QLineF(-56.0, y + W + 16.0, L + 56.0, y + W + 16.0),
                         QPen(c.theme.outline, 1.4));
    }

    const QRectF frame(0.0, y, L, W);
    c.scene->addRect(frame, QPen(c.theme.outline, 1.8), QBrush(c.theme.background));
    drawGrateBars(c, frame, d.grateType);

    addDimension(c, frame.bottomLeft(), frame.bottomRight(), kDimOffset,
                 lenLabel(c, QStringLiteral("L"), d.grateLength));
    addDimension(c, frame.bottomLeft(), frame.topLeft(), -kDimOffset,
                 lenLabel(c, QStringLiteral("W"), d.grateWidth));
    addGenericGrateNotes(c, d, QPointF(L + 74.0, y));

    // ── Section ─────────────────────────────────────────────────────────────
    y = frame.bottom() + kPanelGap;
    y = addPanelTitle(c, y, drop ? trs("Section — channel") : trs("Section — gutter"));

    const double sectionY = y + 70.0;
    if (drop) {
        const auto invert = drawChannelSection(c, 0.0, sectionY, W, 62.0);
        c.scene->addRect(QRectF(invert.first.x(), sectionY - 5.0, W, 5.0),
                         QPen(c.theme.outline, 1.4), QBrush(c.theme.accent));
    } else {
        const QPointF invert =
            drawGutterSection(c, 0.0, sectionY, std::max(W * 2.0, 150.0),
                              px(c, 0.5, 26.0));
        c.scene->addRect(QRectF(invert.x(), sectionY - 5.0, W, 5.0),
                         QPen(c.theme.outline, 1.4), QBrush(c.theme.accent));
    }
}

/*! \brief CURB OPENING and DROP CURB: elevation of the curb face with the
 *  opening, plus a section showing the throat orientation. */
void buildCurb(Ctx &c, const InletDesignData &d, bool drop)
{
    const double L = px(c, d.curbLength);
    const double H = px(c, d.curbHeight);
    const double faceHeight = std::max(H * 2.2, 54.0);

    double y = addPanelTitle(c, 0.0, drop ? trs("Plan — opening")
                                          : trs("Elevation — curb face"));

    // Curb face band (or, for a drop curb, the surrounding channel bank).
    addHatchedRect(c, QRectF(-48.0, y, L + 96.0, faceHeight));
    c.scene->addLine(QLineF(-48.0, y, L + 48.0, y), QPen(c.theme.outline, 2.4));

    const QRectF opening(0.0, y + faceHeight - H, L, H);
    c.scene->addRect(opening, QPen(c.theme.outline, 1.8), QBrush(c.theme.background));

    if (drop) {
        // A drop-curb opening runs around all four sides of the shaft.
        c.scene->addRect(opening.adjusted(-8.0, -8.0, 8.0, 8.0),
                         QPen(c.theme.accent, 1.2, Qt::DashLine), QBrush(Qt::NoBrush));
    }

    addDimension(c, opening.bottomLeft(), opening.bottomRight(), kDimOffset,
                 drop ? trs("L = %1 %2 (×4 sides)")
                            .arg(d.curbLength, 0, 'f', 2).arg(c.lengthLabel)
                      : lenLabel(c, QStringLiteral("L"), d.curbLength));
    addDimension(c, opening.bottomLeft(), opening.topLeft(), -kDimOffset,
                 lenLabel(c, QStringLiteral("h"), d.curbHeight));

    // ── Section ─────────────────────────────────────────────────────────────
    y = opening.bottom() + kPanelGap;
    y = addPanelTitle(c, y, drop ? trs("Section — shaft") : trs("Section — throat"));

    const double sectionY = y + 80.0;
    if (drop) {
        const auto invert = drawChannelSection(c, 0.0, sectionY, L, 62.0);
        c.scene->addRect(QRectF(invert.first.x(), sectionY - H, L, H),
                         QPen(c.theme.outline, 1.4), QBrush(c.theme.background));
    } else {
        const QPointF invert =
            drawGutterSection(c, 0.0, sectionY, 150.0, H + 24.0);

        // Inlet box behind the curb.
        const QRectF box(invert.x() - 62.0, sectionY - H - 6.0, 62.0, H + 46.0);
        c.scene->addRect(box, QPen(c.theme.outline, 1.4), QBrush(c.theme.background));

        // Throat: horizontal (0°), inclined (45°) or vertical (90°).
        double angleDeg = 90.0;
        QString throatWord = trs("VERTICAL");
        if (d.throat == ThroatType::Inclined)        { angleDeg = 45.0; throatWord = trs("INCLINED"); }
        else if (d.throat == ThroatType::Horizontal) { angleDeg =  0.0; throatWord = trs("HORIZONTAL"); }

        const double rad = qDegreesToRadians(angleDeg);
        const QPointF mouth(invert.x(), sectionY - H);
        const QPointF back(mouth.x() - std::cos(rad) * 46.0 - 8.0,
                           mouth.y() + std::sin(rad) * 46.0);
        c.scene->addLine(QLineF(mouth, back), QPen(c.theme.accent, 2.0));
        c.scene->addLine(QLineF(QPointF(mouth.x(), sectionY),
                                 QPointF(back.x(), back.y() + H * 0.4)),
                         QPen(c.theme.accent, 2.0));

        addNote(c, QPointF(box.left(), box.bottom() + 12.0),
                trs("Throat: %1 (%2°)").arg(throatWord).arg(angleDeg, 0, 'f', 0));
    }
}

/*! \brief COMBINATION: grate plan with the curb opening alongside; the part
 *  of the curb opening upstream of the grate is the shaded "sweeper". */
void buildCombo(Ctx &c, const InletDesignData &d)
{
    const double Lg = px(c, d.grateLength);
    const double W  = px(c, d.grateWidth);
    const double Lc = px(c, d.curbLength);
    const double H  = px(c, d.curbHeight);
    const double sweeper = std::max(Lc - Lg, 0.0);

    double y = addPanelTitle(c, 0.0, trs("Plan"));

    const double bandTop = y;
    const double curbBand = std::max(H, 16.0);
    addHatchedRect(c, QRectF(-56.0, bandTop - 30.0,
                             std::max(Lc, Lg) + 112.0, W + curbBand + 60.0));
    c.scene->addLine(QLineF(-56.0, bandTop - 30.0,
                             std::max(Lc, Lg) + 56.0, bandTop - 30.0),
                     QPen(c.theme.outline, 2.4));

    // Curb opening strip, drawn from the downstream end (x = Lc) backwards so
    // the sweeper section is the stretch upstream of the grate.
    const QRectF curbStrip(0.0, bandTop, Lc, curbBand);
    c.scene->addRect(curbStrip, QPen(c.theme.outline, 1.6), QBrush(c.theme.background));
    if (sweeper > 0.0) {
        QColor shade = c.theme.accent;
        shade.setAlpha(70);
        c.scene->addRect(QRectF(0.0, bandTop, sweeper, curbBand),
                         QPen(Qt::NoPen), QBrush(shade));
    }

    // Grate sits at the downstream end of the curb opening.
    const QRectF frame(sweeper, bandTop + curbBand, Lg, W);
    c.scene->addRect(frame, QPen(c.theme.outline, 1.8), QBrush(c.theme.background));
    drawGrateBars(c, frame, d.grateType);

    addDimension(c, frame.bottomLeft(), frame.bottomRight(), kDimOffset,
                 lenLabel(c, QStringLiteral("L grate"), d.grateLength));
    addDimension(c, frame.bottomLeft(), frame.topLeft(), -kDimOffset,
                 lenLabel(c, QStringLiteral("W"), d.grateWidth));
    addDimension(c, curbStrip.topLeft(), curbStrip.topRight(), -kDimOffset,
                 lenLabel(c, QStringLiteral("L curb"), d.curbLength));
    addNote(c, QPointF(curbStrip.right() + 16.0, curbStrip.top()),
            trs("Sweeper L curb − L grate = %1 %2")
                .arg(std::max(d.curbLength - d.grateLength, 0.0), 0, 'f', 2)
                .arg(c.lengthLabel));
    addGenericGrateNotes(c, d, QPointF(std::max(Lc, Lg) + 74.0, frame.top()));

    // ── Section ─────────────────────────────────────────────────────────────
    y = frame.bottom() + kPanelGap;
    y = addPanelTitle(c, y, trs("Section — gutter"));

    const double sectionY = y + 80.0;
    const QPointF invert =
        drawGutterSection(c, 0.0, sectionY, std::max(W * 2.0, 150.0), H + 24.0);
    c.scene->addRect(QRectF(invert.x(), sectionY - 5.0, W, 5.0),
                     QPen(c.theme.outline, 1.4), QBrush(c.theme.accent));

    const QRectF box(invert.x() - 62.0, sectionY - H - 6.0, 62.0, H + 46.0);
    c.scene->addRect(box, QPen(c.theme.outline, 1.4), QBrush(c.theme.background));
    addDimension(c, QPointF(box.left() - 14.0, sectionY),
                 QPointF(box.left() - 14.0, sectionY - H), -kDimOffset * 0.6,
                 lenLabel(c, QStringLiteral("h"), d.curbHeight));
}

/*! \brief SLOTTED DRAIN: plan of the slot in the pavement plus the section
 *  through it. */
void buildSlotted(Ctx &c, const InletDesignData &d)
{
    const double L = px(c, d.slotLength);
    const double w = px(c, d.slotWidth, 8.0);

    double y = addPanelTitle(c, 0.0, trs("Plan"));

    addHatchedRect(c, QRectF(-56.0, y - 34.0, L + 112.0, w + 68.0));
    c.scene->addLine(QLineF(-56.0, y - 34.0, L + 56.0, y - 34.0),
                     QPen(c.theme.outline, 2.4));

    const QRectF slot(0.0, y, L, w);
    c.scene->addRect(slot, QPen(c.theme.outline, 1.6), QBrush(c.theme.accent));

    addDimension(c, slot.bottomLeft(), slot.bottomRight(), kDimOffset,
                 lenLabel(c, QStringLiteral("L"), d.slotLength));
    addDimension(c, slot.bottomLeft(), slot.topLeft(), -kDimOffset,
                 lenLabel(c, QStringLiteral("w"), d.slotWidth));

    // ── Section ─────────────────────────────────────────────────────────────
    y = slot.bottom() + kPanelGap;
    y = addPanelTitle(c, y, trs("Section — pavement"));

    const double sectionY = y + 70.0;
    const QPointF invert =
        drawGutterSection(c, 0.0, sectionY, 150.0, px(c, 0.5, 26.0));
    // The slot itself: a narrow notch through the pavement into the pipe.
    c.scene->addRect(QRectF(invert.x() + 26.0, sectionY - 3.0, w, 30.0),
                     QPen(c.theme.outline, 1.2), QBrush(c.theme.background));
}

/*! \brief CUSTOM: the referenced capture curve plotted with axes labelled
 *  per the curve kind. */
void buildCustom(Ctx &c, const InletDesignData &d, const QVector<QPointF> &pts)
{
    const double y = addPanelTitle(c, 0.0, trs("Capture curve"));

    const QRectF box(0.0, y, 320.0, 220.0);
    c.scene->addRect(box, QPen(c.theme.outline, 1.4), QBrush(c.theme.background));

    // Axes.
    c.scene->addLine(QLineF(box.bottomLeft(), box.bottomRight()),
                     QPen(c.theme.outline, 1.6));
    c.scene->addLine(QLineF(box.bottomLeft(), box.topLeft()),
                     QPen(c.theme.outline, 1.6));

    const bool rating = (d.curveKind == InletCurveKind::Rating);
    const QString xAxis = rating ? trs("Water depth (%1)").arg(c.lengthLabel)
                                 : trs("Approach flow");
    const QString yAxis = trs("Captured flow");
    addText(c, QPointF(box.center().x() - 50.0, box.bottom() + 24.0), xAxis, c.theme.text);
    addText(c, QPointF(box.left(), box.top() - 18.0), yAxis, c.theme.text);

    if (!pts.isEmpty()) {
        double xMax = 0.0, yMax = 0.0;
        for (const QPointF &p : pts) {
            xMax = std::max(xMax, p.x());
            yMax = std::max(yMax, p.y());
        }
        if (xMax <= 0.0) xMax = 1.0;
        if (yMax <= 0.0) yMax = 1.0;

        QPainterPath path;
        for (int i = 0; i < pts.size(); ++i) {
            const QPointF sp(box.left()   + box.width()  * pts.at(i).x() / xMax,
                             box.bottom() - box.height() * pts.at(i).y() / yMax);
            if (i == 0) path.moveTo(sp); else path.lineTo(sp);
        }
        c.scene->addPath(path, QPen(c.theme.accent, 2.0));
    } else {
        addText(c, box.center() - QPointF(60.0, 8.0),
                trs("No curve points"), c.theme.dimension);
    }

    addNote(c, QPointF(box.left(), box.bottom() + 44.0),
            trs("Curve: %1").arg(d.curveId.isEmpty() ? trs("(none)") : d.curveId));
    addNote(c, QPointF(box.left(), box.bottom() + 60.0),
            trs("Kind: %1").arg(rating ? trs("Rating (captured vs depth)")
                                       : trs("Diversion (captured vs approach flow)")));
}

double scaleFor(const InletDesignData &d)
{
    const double span = std::max({ d.grateLength, d.grateWidth,
                                    d.curbLength, d.curbHeight,
                                    d.slotLength, d.slotWidth, 0.0 });
    if (span <= 1e-6) return 120.0;
    return qBound(kMinScale, kTargetSpanPx / span, kMaxScale);
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// InletSceneBuilder
// ─────────────────────────────────────────────────────────────────────────────

QGraphicsScene *InletSceneBuilder::build(const InletProvider &design,
                                          const UnitSystem *units,
                                          const InletTheme &theme,
                                          const QVector<QPointF> &customCurve)
{
    auto *scene = new QGraphicsScene;
    scene->setBackgroundBrush(theme.background);

    Ctx c;
    c.scene = scene;
    c.theme = theme;
    if (units) {
        c.lengthLabel   = units->lengthLabel();
        c.velocityLabel = units->velocityLabel();
    }

    const InletDesignData &d = design.design();
    c.s = scaleFor(d);

    switch (d.type) {
    case InletType::Grate:     buildGrate(c, d, /*drop*/ false); break;
    case InletType::DropGrate: buildGrate(c, d, /*drop*/ true);  break;
    case InletType::Curb:      buildCurb(c, d, /*drop*/ false);  break;
    case InletType::DropCurb:  buildCurb(c, d, /*drop*/ true);   break;
    case InletType::Combo:     buildCombo(c, d);                 break;
    case InletType::Slotted:   buildSlotted(c, d);               break;
    case InletType::Custom:    buildCustom(c, d, customCurve);   break;
    }

    scene->setSceneRect(scene->itemsBoundingRect().adjusted(-28.0, -28.0, 28.0, 28.0));
    return scene;
}

// ─────────────────────────────────────────────────────────────────────────────
// InletDrawingView
// ─────────────────────────────────────────────────────────────────────────────

InletDrawingView::InletDrawingView(QWidget *parent)
    : QGraphicsView(parent)
{
    setRenderHint(QPainter::Antialiasing, true);
    setRenderHint(QPainter::TextAntialiasing, true);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setDragMode(QGraphicsView::NoDrag);
    setMinimumSize(260, 220);
    rebuild();
}

InletDrawingView::~InletDrawingView() = default;

void InletDrawingView::setProvider(InletProvider *p)
{
    if (m_provider.data() == p) return;
    if (m_provider) m_provider->disconnect(this);
    m_provider = QPointer<InletProvider>(p);
    if (m_provider)
        connect(m_provider, &InletProvider::paramsChanged,
                this, &InletDrawingView::rebuild);
    rebuild();
}

InletProvider *InletDrawingView::provider() const noexcept
{
    return m_provider.data();
}

void InletDrawingView::setCustomCurve(const QVector<QPointF> &points)
{
    if (points == m_customCurve) return;
    m_customCurve = points;
    rebuild();
}

void InletDrawingView::rebuild()
{
    QGraphicsScene *old = scene();

    QGraphicsScene *built = nullptr;
    if (m_provider) {
        built = InletSceneBuilder::build(*m_provider,
                                          UnitSystem::instance(),
                                          InletTheme::fromPalette(palette()),
                                          m_customCurve);
    } else {
        built = new QGraphicsScene;
        built->setBackgroundBrush(palette().base());
        QGraphicsSimpleTextItem *t = built->addSimpleText(tr("No inlet selected"));
        t->setBrush(palette().mid().color());
        built->setSceneRect(built->itemsBoundingRect().adjusted(-20, -20, 20, 20));
    }

    setScene(built);
    delete old;
    zoomToExtent();
}

void InletDrawingView::zoomToExtent()
{
    if (!scene()) return;
    fitInView(scene()->sceneRect(), Qt::KeepAspectRatio);
    m_fitted = true;
}

void InletDrawingView::zoomIn()
{
    scale(1.25, 1.25);
}

void InletDrawingView::zoomOut()
{
    scale(1.0 / 1.25, 1.0 / 1.25);
}

void InletDrawingView::wheelEvent(QWheelEvent *event)
{
    const int delta = event->angleDelta().y();
    if (delta == 0) { QGraphicsView::wheelEvent(event); return; }
    // AnchorUnderMouse (set in the ctor) keeps the point under the cursor put.
    const double factor = (delta > 0) ? 1.15 : (1.0 / 1.15);
    scale(factor, factor);
    event->accept();
}

void InletDrawingView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton) {
        // Qt's ScrollHandDrag only reacts to the left button, so the middle
        // press is forwarded as a synthetic left press while it is held.
        m_middlePanning = true;
        setDragMode(QGraphicsView::ScrollHandDrag);
        QMouseEvent synthetic(QEvent::MouseButtonPress, event->position(),
                              event->globalPosition(), Qt::LeftButton,
                              Qt::LeftButton, event->modifiers());
        QGraphicsView::mousePressEvent(&synthetic);
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void InletDrawingView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_middlePanning && event->button() == Qt::MiddleButton) {
        QMouseEvent synthetic(QEvent::MouseButtonRelease, event->position(),
                              event->globalPosition(), Qt::LeftButton,
                              Qt::NoButton, event->modifiers());
        QGraphicsView::mouseReleaseEvent(&synthetic);
        setDragMode(QGraphicsView::NoDrag);
        m_middlePanning = false;
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void InletDrawingView::showEvent(QShowEvent *event)
{
    QGraphicsView::showEvent(event);
    if (!m_fitted) zoomToExtent();
}

void InletDrawingView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    zoomToExtent();
}

} // namespace openswmmvis::ui
