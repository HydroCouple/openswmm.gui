/*!
 * \file   colorramp.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "render/colorramp.h"

#include <QJsonArray>
#include <QJsonValue>

#include <algorithm>
#include <array>
#include <cmath>

namespace
{

QColor interpRgb(const QColor &c0, const QColor &c1, double f)
{
    return QColor::fromRgbF(
        c0.redF()   + f * (c1.redF()   - c0.redF()),
        c0.greenF() + f * (c1.greenF() - c0.greenF()),
        c0.blueF()  + f * (c1.blueF()  - c0.blueF()),
        c0.alphaF() + f * (c1.alphaF() - c0.alphaF()));
}

// Achromatic colours (black / white / pure gray) have hue == -1 in Qt.
// When one endpoint is achromatic, hue interpolation is undefined; we
// fall back to RGB interpolation in that case so the result is sensible.
// Qt 6's QColor::getHsvF expects `float *`; cast carefully on read.
bool isAchromatic(const QColor &c)
{
    float h = 0.0f, s = 0.0f, v = 0.0f, a = 0.0f;
    c.getHsvF(&h, &s, &v, &a);
    return h < 0.0f || s <= 1e-6f;
}

QColor interpHsv(const QColor &c0, const QColor &c1, double f, bool longArc)
{
    if (isAchromatic(c0) || isAchromatic(c1))
        return interpRgb(c0, c1, f);

    float h0 = 0.0f, s0 = 0.0f, v0 = 0.0f, a0 = 0.0f;
    float h1 = 0.0f, s1 = 0.0f, v1 = 0.0f, a1 = 0.0f;
    c0.getHsvF(&h0, &s0, &v0, &a0);
    c1.getHsvF(&h1, &s1, &v1, &a1);

    // Walk the hue wheel on the chosen arc. Hues are in [0,1).
    double dh = h1 - h0;
    if (longArc)
    {
        if (dh > 0.0 && dh < 0.5) dh -= 1.0;
        else if (dh < 0.0 && dh > -0.5) dh += 1.0;
    }
    else
    {
        if (dh > 0.5)  dh -= 1.0;
        else if (dh < -0.5) dh += 1.0;
    }
    double h = h0 + f * dh;
    // Wrap to [0,1).
    h = h - std::floor(h);
    const double s = s0 + f * (s1 - s0);
    const double v = v0 + f * (v1 - v0);
    const double a = a0 + f * (a1 - a0);
    return QColor::fromHsvF(static_cast<float>(h),
                            static_cast<float>(std::clamp(s, 0.0, 1.0)),
                            static_cast<float>(std::clamp(v, 0.0, 1.0)),
                            static_cast<float>(std::clamp(a, 0.0, 1.0)));
}

} // namespace

// ─── colorAt / colorForValue ────────────────────────────────────────────────

QColor RasterColorRamp::colorAt(double t) const
{
    t = std::clamp(t, 0.0, 1.0);

    if (stops.isEmpty())
        return Qt::transparent;

    if (stops.size() == 1 || t <= stops.first().first)
        return stops.first().second;

    if (t >= stops.last().first)
        return stops.last().second;

    for (int i = 1; i < stops.size(); ++i)
    {
        if (t <= stops[i].first)
        {
            const double t0 = stops[i - 1].first;
            const double t1 = stops[i].first;
            const double f  = (t - t0) / (t1 - t0);

            const QColor &c0 = stops[i - 1].second;
            const QColor &c1 = stops[i].second;

            switch (interp)
            {
            case RampInterp::Rgb:      return interpRgb(c0, c1, f);
            case RampInterp::HsvShort: return interpHsv(c0, c1, f, false);
            case RampInterp::HsvLong:  return interpHsv(c0, c1, f, true);
            }
            return interpRgb(c0, c1, f);
        }
    }

    return stops.last().second;
}

QColor RasterColorRamp::colorForValue(double value) const
{
    if (clampMin && value < minValue)
        return Qt::transparent;
    if (clampMax && value > maxValue)
        return Qt::transparent;

    const double range = maxValue - minValue;
    if (qFuzzyIsNull(range))
        return colorAt(0.5);

    return colorAt((value - minValue) / range);
}

// ─── JSON round-trip ────────────────────────────────────────────────────────

QJsonObject RasterColorRamp::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("minValue"), minValue);
    obj.insert(QStringLiteral("maxValue"), maxValue);
    obj.insert(QStringLiteral("clampMin"), clampMin);
    obj.insert(QStringLiteral("clampMax"), clampMax);
    obj.insert(QStringLiteral("interp"), static_cast<int>(interp));

    QJsonArray jstops;
    for (const QGradientStop &s : stops)
    {
        QJsonObject jstop;
        jstop.insert(QStringLiteral("pos"), s.first);
        jstop.insert(QStringLiteral("color"), s.second.name(QColor::HexArgb));
        jstops.append(jstop);
    }
    obj.insert(QStringLiteral("stops"), jstops);
    return obj;
}

RasterColorRamp RasterColorRamp::fromJson(const QJsonObject &j)
{
    RasterColorRamp r;
    r.minValue = j.value(QStringLiteral("minValue")).toDouble(0.0);
    r.maxValue = j.value(QStringLiteral("maxValue")).toDouble(1.0);
    r.clampMin = j.value(QStringLiteral("clampMin")).toBool(false);
    r.clampMax = j.value(QStringLiteral("clampMax")).toBool(false);

    const int interpInt = j.value(QStringLiteral("interp")).toInt(0);
    switch (interpInt)
    {
    case 1: r.interp = RampInterp::HsvShort; break;
    case 2: r.interp = RampInterp::HsvLong; break;
    default: r.interp = RampInterp::Rgb; break;
    }

    QGradientStops stops;
    const QJsonArray jstops = j.value(QStringLiteral("stops")).toArray();
    stops.reserve(jstops.size());
    for (const QJsonValue &v : jstops)
    {
        const QJsonObject o = v.toObject();
        const double pos = o.value(QStringLiteral("pos")).toDouble(0.0);
        const QColor col(o.value(QStringLiteral("color")).toString());
        stops.append({pos, col.isValid() ? col : QColor(Qt::transparent)});
    }
    std::sort(stops.begin(), stops.end(),
              [](const QGradientStop &a, const QGradientStop &b) { return a.first < b.first; });
    r.stops = stops;
    return r;
}

// ─── Built-in catalogue ─────────────────────────────────────────────────────
//
// All sequential built-ins use 5–9 RGB stops sampled from the standard
// matplotlib / ColorBrewer / SWMM5 palettes. Custom user-authored ramps
// default to HsvShort; the built-ins keep Rgb (their stops were authored
// for RGB blending).

RasterColorRamp RasterColorRamp::grayscale(double min, double max)
{
    RasterColorRamp r;
    r.minValue = min;
    r.maxValue = max;
    r.stops    = { {0.0, Qt::black}, {1.0, Qt::white} };
    return r;
}

RasterColorRamp RasterColorRamp::viridis(double min, double max)
{
    RasterColorRamp r;
    r.minValue = min;
    r.maxValue = max;
    r.stops    = {
        {0.000, QColor( 68,   1,  84)},
        {0.125, QColor( 72,  40, 120)},
        {0.250, QColor( 62,  83, 137)},
        {0.375, QColor( 49, 120, 137)},
        {0.500, QColor( 53, 153, 122)},
        {0.625, QColor( 90, 186,  91)},
        {0.750, QColor(163, 214,  63)},
        {0.875, QColor(227, 238,  58)},
        {1.000, QColor(253, 231,  37)},
    };
    return r;
}

RasterColorRamp RasterColorRamp::plasma(double min, double max)
{
    RasterColorRamp r;
    r.minValue = min;
    r.maxValue = max;
    r.stops    = {
        {0.00, QColor( 13,   8, 135)},
        {0.25, QColor( 84,   2, 163)},
        {0.50, QColor(184,  50, 137)},
        {0.75, QColor(244, 136,  73)},
        {1.00, QColor(240, 249,  33)},
    };
    return r;
}

RasterColorRamp RasterColorRamp::magma(double min, double max)
{
    RasterColorRamp r;
    r.minValue = min;
    r.maxValue = max;
    r.stops    = {
        {0.00, QColor(  0,   0,   4)},
        {0.25, QColor( 80,  18, 123)},
        {0.50, QColor(183,  55, 121)},
        {0.75, QColor(251, 135,  97)},
        {1.00, QColor(252, 253, 191)},
    };
    return r;
}

RasterColorRamp RasterColorRamp::inferno(double min, double max)
{
    RasterColorRamp r;
    r.minValue = min;
    r.maxValue = max;
    r.stops    = {
        {0.00, QColor(  0,   0,   4)},
        {0.25, QColor( 87,  16, 110)},
        {0.50, QColor(187,  55,  84)},
        {0.75, QColor(249, 142,   9)},
        {1.00, QColor(252, 255, 164)},
    };
    return r;
}

RasterColorRamp RasterColorRamp::cividis(double min, double max)
{
    RasterColorRamp r;
    r.minValue = min;
    r.maxValue = max;
    r.stops    = {
        {0.00, QColor(  0,  32,  77)},
        {0.25, QColor( 51,  74, 104)},
        {0.50, QColor(124, 123, 120)},
        {0.75, QColor(192, 175, 110)},
        {1.00, QColor(255, 234,  70)},
    };
    return r;
}

RasterColorRamp RasterColorRamp::turbo(double min, double max)
{
    RasterColorRamp r;
    r.minValue = min;
    r.maxValue = max;
    r.stops    = {
        {0.00, QColor( 48,  18,  59)},
        {0.25, QColor( 70, 134, 251)},
        {0.50, QColor( 53, 245, 110)},
        {0.75, QColor(252, 199,  31)},
        {1.00, QColor(122,   4,   3)},
    };
    return r;
}

RasterColorRamp RasterColorRamp::rdBu(double min, double max)
{
    RasterColorRamp r;
    r.minValue = min;
    r.maxValue = max;
    // ColorBrewer RdBu (red → white → blue diverging).
    r.stops    = {
        {0.00, QColor(103,   0,  31)},
        {0.25, QColor(214,  96,  77)},
        {0.50, QColor(247, 247, 247)},
        {0.75, QColor( 67, 147, 195)},
        {1.00, QColor(  5,  48,  97)},
    };
    return r;
}

RasterColorRamp RasterColorRamp::rdYlGn(double min, double max)
{
    RasterColorRamp r;
    r.minValue = min;
    r.maxValue = max;
    r.stops    = {
        {0.00, QColor(165,   0,  38)},
        {0.25, QColor(252, 141,  89)},
        {0.50, QColor(255, 255, 191)},
        {0.75, QColor(145, 207,  96)},
        {1.00, QColor(  0, 104,  55)},
    };
    return r;
}

RasterColorRamp RasterColorRamp::spectral(double min, double max)
{
    RasterColorRamp r;
    r.minValue = min;
    r.maxValue = max;
    r.stops    = {
        {0.000, QColor(158,   1,  66)},
        {0.166, QColor(244, 109,  67)},
        {0.333, QColor(253, 224, 139)},
        {0.500, QColor(255, 255, 191)},
        {0.666, QColor(171, 221, 164)},
        {0.833, QColor( 50, 136, 189)},
        {1.000, QColor( 94,  79, 162)},
    };
    return r;
}

RasterColorRamp RasterColorRamp::brBG(double min, double max)
{
    RasterColorRamp r;
    r.minValue = min;
    r.maxValue = max;
    // ColorBrewer BrBG (brown → white → blue-green diverging).
    r.stops    = {
        {0.00, QColor( 84,  48,   5)},
        {0.25, QColor(216, 179, 101)},
        {0.50, QColor(245, 245, 245)},
        {0.75, QColor( 90, 180, 172)},
        {1.00, QColor(  0,  60,  48)},
    };
    return r;
}

RasterColorRamp RasterColorRamp::legacySWMM5Interval(double min, double max)
{
    RasterColorRamp r;
    r.minValue = min;
    r.maxValue = max;
    // Legacy SWMM-5 GUI uses 5 evenly-spaced interval colours.
    // Cool→warm progression matches the historic palette.
    r.stops    = {
        {0.00, QColor(  0,   0, 255)}, // blue
        {0.25, QColor(  0, 255, 255)}, // cyan
        {0.50, QColor(  0, 255,   0)}, // green
        {0.75, QColor(255, 255,   0)}, // yellow
        {1.00, QColor(255,   0,   0)}, // red
    };
    return r;
}

RasterColorRamp RasterColorRamp::legacySWMMPollutant(double min, double max)
{
    RasterColorRamp r;
    r.minValue = min;
    r.maxValue = max;
    // Legacy SWMM-5 pollutant palette — light→dark single-hue brown.
    r.stops    = {
        {0.00, QColor(255, 245, 235)},
        {0.25, QColor(253, 208, 162)},
        {0.50, QColor(253, 141,  60)},
        {0.75, QColor(217,  72,   1)},
        {1.00, QColor(127,  39,   4)},
    };
    return r;
}

RasterColorRamp RasterColorRamp::terrain(double min, double max)
{
    RasterColorRamp r;
    r.minValue = min;
    r.maxValue = max;
    r.interp   = RampInterp::Rgb;
    // The historic 2D mesh elevation palette (was hard-coded as
    // legacyElevationRamp() in SWMM2DMeshQSGRenderer). Deep water-blue at the
    // low bed, through marsh green and ochre, to off-white at the high bed.
    // These bytes are identical to that copy so the default terrain fill is
    // unchanged now that the renderer resolves the ramp by name.
    r.stops    = {
        {0.00, QColor(0x1a, 0x3d, 0x6b)},
        {0.20, QColor(0x2e, 0x8b, 0x57)},
        {0.50, QColor(0xc8, 0xd9, 0x4e)},
        {0.75, QColor(0xc8, 0xa0, 0x00)},
        {1.00, QColor(0xf0, 0xf0, 0xe8)},
    };
    return r;
}

// ─── Slice BB-β (2026-05-25) — Plotly continuous palettes ───────────────────
//
// Stops sourced from plotly.colors.sequential / .diverging. Each palette
// uses 7–11 stops at evenly-spaced positions. The Plotly catalogue is a
// reasonable target for users coming from dash / plotly workflows.

RasterColorRamp RasterColorRamp::plotly3(double min, double max)
{
    RasterColorRamp r;
    r.minValue = min;
    r.maxValue = max;
    r.stops    = {
        {0.000, QColor( 13,   8, 135)},
        {0.143, QColor( 90,   3, 165)},
        {0.286, QColor(157,  23, 158)},
        {0.429, QColor(212,  53, 136)},
        {0.571, QColor(248,  98, 105)},
        {0.714, QColor(252, 153,  67)},
        {0.857, QColor(243, 214,  44)},
        {1.000, QColor(240, 249,  33)},
    };
    return r;
}

RasterColorRamp RasterColorRamp::iceFire(double min, double max)
{
    RasterColorRamp r;
    r.minValue = min;
    r.maxValue = max;
    r.stops    = {
        {0.000, QColor(  0,   0,   4)},
        {0.111, QColor( 33,  37, 122)},
        {0.222, QColor( 47,  79, 159)},
        {0.333, QColor( 71, 138, 174)},
        {0.444, QColor(116, 196, 158)},
        {0.555, QColor(241, 245, 213)},
        {0.666, QColor(247, 197,  91)},
        {0.777, QColor(231, 110,  50)},
        {0.888, QColor(160,  34,  21)},
        {1.000, QColor( 40,   0,   3)},
    };
    return r;
}

RasterColorRamp RasterColorRamp::blackbody(double min, double max)
{
    RasterColorRamp r;
    r.minValue = min;
    r.maxValue = max;
    r.stops    = {
        {0.00, QColor(  0,   0,   0)},
        {0.25, QColor(230,  20,   0)},
        {0.50, QColor(230, 140,  20)},
        {0.75, QColor(255, 230,  80)},
        {1.00, QColor(255, 255, 255)},
    };
    return r;
}

RasterColorRamp RasterColorRamp::electric(double min, double max)
{
    RasterColorRamp r;
    r.minValue = min;
    r.maxValue = max;
    r.stops    = {
        {0.00, QColor(  0,   0,   0)},
        {0.25, QColor( 30,  40, 110)},
        {0.50, QColor(120,  80, 175)},
        {0.75, QColor(230, 175,  35)},
        {1.00, QColor(255, 250, 220)},
    };
    return r;
}

RasterColorRamp RasterColorRamp::hot(double min, double max)
{
    RasterColorRamp r;
    r.minValue = min;
    r.maxValue = max;
    r.stops    = {
        {0.00, QColor(  0,   0,   0)},
        {0.33, QColor(230,   0,   0)},
        {0.66, QColor(255, 210,   0)},
        {1.00, QColor(255, 255, 255)},
    };
    return r;
}

RasterColorRamp RasterColorRamp::jet(double min, double max)
{
    RasterColorRamp r;
    r.minValue = min;
    r.maxValue = max;
    // Classic Matlab Jet — saturated rainbow. Notoriously perceptually
    // non-uniform; included for parity with legacy workflows.
    r.stops    = {
        {0.000, QColor(  0,   0, 131)},
        {0.125, QColor(  0,  60, 170)},
        {0.375, QColor(  5, 255, 255)},
        {0.625, QColor(255, 255,   0)},
        {0.875, QColor(250,   0,   0)},
        {1.000, QColor(128,   0,   0)},
    };
    return r;
}

RasterColorRamp RasterColorRamp::picnic(double min, double max)
{
    RasterColorRamp r;
    r.minValue = min;
    r.maxValue = max;
    // Diverging — blue → white → magenta.
    r.stops    = {
        {0.00, QColor(  0,   0, 255)},
        {0.10, QColor( 51, 153, 255)},
        {0.20, QColor(102, 204, 255)},
        {0.30, QColor(178, 229, 255)},
        {0.40, QColor(204, 229, 255)},
        {0.50, QColor(255, 255, 255)},
        {0.60, QColor(255, 204, 255)},
        {0.70, QColor(255, 153, 255)},
        {0.80, QColor(255, 102, 204)},
        {0.90, QColor(255,  51, 153)},
        {1.00, QColor(255,   0, 102)},
    };
    return r;
}

RasterColorRamp RasterColorRamp::portland(double min, double max)
{
    RasterColorRamp r;
    r.minValue = min;
    r.maxValue = max;
    r.stops    = {
        {0.00, QColor( 12,  51, 131)},
        {0.25, QColor( 10, 136, 186)},
        {0.50, QColor(242, 211,  56)},
        {0.75, QColor(242, 143,  56)},
        {1.00, QColor(217,  30,  30)},
    };
    return r;
}

RasterColorRamp RasterColorRamp::rainbow(double min, double max)
{
    RasterColorRamp r;
    r.minValue = min;
    r.maxValue = max;
    r.stops    = {
        {0.00, QColor(150,   0,  90)},
        {0.13, QColor(  0,   0, 200)},
        {0.25, QColor(  0,  25, 255)},
        {0.38, QColor(  0, 152, 255)},
        {0.50, QColor( 44, 255, 150)},
        {0.63, QColor(151, 255,   0)},
        {0.75, QColor(255, 234,   0)},
        {0.88, QColor(255, 111,   0)},
        {1.00, QColor(255,   0,   0)},
    };
    return r;
}

RasterColorRamp RasterColorRamp::bluered(double min, double max)
{
    RasterColorRamp r;
    r.minValue = min;
    r.maxValue = max;
    // Plotly's Bluered diverging — straight blue → red, no neutral mid.
    r.stops    = {
        {0.00, QColor(  0,   0, 255)},
        {1.00, QColor(255,   0,   0)},
    };
    return r;
}

RasterColorRamp RasterColorRamp::waterDepth(double min, double max)
{
    RasterColorRamp r;
    r.minValue = min;
    r.maxValue = max;
    r.interp   = RampInterp::Rgb;
    // ColorBrewer "Blues" hues with an alpha gradient, low → high depth:
    // barely-wet cells are a translucent near-white blue (the shoreline
    // fades into the terrain beneath), deepening to a fully opaque navy at
    // maximum depth (user direction 2026-09-01: deeper water = dark blue,
    // shallower = lighter shade).
    r.stops    = {
        {0.00, QColor(0xf7, 0xfb, 0xff, 100)},  // near-white blue, translucent
        {0.25, QColor(0xc6, 0xdb, 0xef, 160)},  // pale blue
        {0.50, QColor(0x6b, 0xae, 0xd6, 210)},  // mid blue
        {0.75, QColor(0x21, 0x71, 0xb5, 240)},  // strong blue
        {1.00, QColor(0x08, 0x30, 0x6b, 255)},  // deep navy, fully opaque
    };
    return r;
}

namespace
{

struct BuiltinEntry
{
    QString key;                       // canonical lowercase
    QString displayName;               // pretty label for combobox
    RasterColorRamp (*factory)(double, double);
};

const std::array<BuiltinEntry, 25> &builtinTable()
{
    static const std::array<BuiltinEntry, 25> table = {{
        {QStringLiteral("grayscale"),             QStringLiteral("Grayscale"),             &RasterColorRamp::grayscale},
        {QStringLiteral("viridis"),               QStringLiteral("Viridis"),               &RasterColorRamp::viridis},
        {QStringLiteral("plasma"),                QStringLiteral("Plasma"),                &RasterColorRamp::plasma},
        {QStringLiteral("magma"),                 QStringLiteral("Magma"),                 &RasterColorRamp::magma},
        {QStringLiteral("inferno"),               QStringLiteral("Inferno"),               &RasterColorRamp::inferno},
        {QStringLiteral("cividis"),               QStringLiteral("Cividis"),               &RasterColorRamp::cividis},
        {QStringLiteral("turbo"),                 QStringLiteral("Turbo"),                 &RasterColorRamp::turbo},
        {QStringLiteral("rdbu"),                  QStringLiteral("RdBu"),                  &RasterColorRamp::rdBu},
        {QStringLiteral("rdylgn"),                QStringLiteral("RdYlGn"),                &RasterColorRamp::rdYlGn},
        {QStringLiteral("spectral"),              QStringLiteral("Spectral"),              &RasterColorRamp::spectral},
        {QStringLiteral("brbg"),                  QStringLiteral("BrBG"),                  &RasterColorRamp::brBG},
        {QStringLiteral("legacy-swmm-5interval"), QStringLiteral("Legacy SWMM (5-interval)"), &RasterColorRamp::legacySWMM5Interval},
        {QStringLiteral("legacy-swmm-pollutant"), QStringLiteral("Legacy SWMM (pollutant)"),  &RasterColorRamp::legacySWMMPollutant},
        {QStringLiteral("terrain"),               QStringLiteral("Terrain"),               &RasterColorRamp::terrain},
        // Slice BB-β — Plotly continuous palettes
        {QStringLiteral("plotly3"),               QStringLiteral("Plotly3"),               &RasterColorRamp::plotly3},
        {QStringLiteral("icefire"),               QStringLiteral("IceFire"),               &RasterColorRamp::iceFire},
        {QStringLiteral("blackbody"),             QStringLiteral("Blackbody"),             &RasterColorRamp::blackbody},
        {QStringLiteral("electric"),              QStringLiteral("Electric"),              &RasterColorRamp::electric},
        {QStringLiteral("hot"),                   QStringLiteral("Hot"),                   &RasterColorRamp::hot},
        {QStringLiteral("jet"),                   QStringLiteral("Jet"),                   &RasterColorRamp::jet},
        {QStringLiteral("picnic"),                QStringLiteral("Picnic"),                &RasterColorRamp::picnic},
        {QStringLiteral("portland"),              QStringLiteral("Portland"),              &RasterColorRamp::portland},
        {QStringLiteral("rainbow"),               QStringLiteral("Rainbow"),               &RasterColorRamp::rainbow},
        {QStringLiteral("bluered"),               QStringLiteral("Bluered"),               &RasterColorRamp::bluered},
        // 2026-09-01 — depth-fill default: translucent near-white blue →
        // opaque deep navy (the one builtin with an alpha gradient).
        {QStringLiteral("water-depth"),           QStringLiteral("Water Depth"),           &RasterColorRamp::waterDepth},
    }};
    return table;
}

} // namespace

RasterColorRamp RasterColorRamp::builtin(const QString &name)
{
    const QString key = name.trimmed().toLower();
    for (const BuiltinEntry &e : builtinTable())
    {
        // Match the internal key or the combobox display name — style bags
        // store whichever the UI handed them.
        if (e.key == key
            || e.displayName.compare(name.trimmed(), Qt::CaseInsensitive) == 0)
            return e.factory(0.0, 1.0);
    }
    return grayscale();
}

QStringList RasterColorRamp::builtinNames()
{
    QStringList out;
    out.reserve(static_cast<int>(builtinTable().size()));
    for (const BuiltinEntry &e : builtinTable())
        out.append(e.displayName);
    return out;
}
