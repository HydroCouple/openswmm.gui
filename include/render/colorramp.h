/*!
 * \file   colorramp.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  RasterColorRamp — gradient mapping from a normalised value in [0,1]
 *         to a display QColor.
 *
 *         Slice BB-α (2026-05-24): the struct's previous home was
 *         layers/gisrasterlayer.h; it has been relocated here so any code
 *         that needs a ramp (raster rendering, GraduatedRenderer, the
 *         color-ramp combobox / editor) can include it without pulling in
 *         GDAL transitively. The struct stays at global scope to keep the
 *         ~20 existing call sites (Q_DECLARE_METATYPE, GISRasterLayer,
 *         SWMMResultsLayer, ColorRampEditorDialog, …) compiling unchanged.
 *
 *         Extensions over the previous form:
 *           - `interp` field selects RGB vs HSV-short-arc vs HSV-long-arc
 *             interpolation between adjacent stops. Defaults to RGB to
 *             preserve existing built-in visuals; the editor sets HSV-short
 *             as the default for user-authored ramps per user request.
 *           - 12 built-in named ramps reachable via `builtin(name)` —
 *             Viridis / Plasma / Magma / Inferno / Cividis / Turbo /
 *             RdBu / RdYlGn / Spectral / BrBG / legacy-SWMM-5interval /
 *             legacy-SWMM-pollutant. Plus the pre-existing `grayscale`
 *             and `viridis` static factories.
 *           - `toJson` / `fromJson` round-trip for `.oswp` persistence
 *             and for the PreferencesManager custom-ramp library.
 *
 *         Cross-slice: GUI_IMPLEMENTATION_PLAN.md §L.BB-α.
 */

#ifndef OPENSWMM_RENDER_COLORRAMP_H
#define OPENSWMM_RENDER_COLORRAMP_H

#include <QColor>
#include <QGradientStops>
#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QStringList>

/*!
 * \enum RampInterp
 * \brief Colour-space used when interpolating between two adjacent ramp stops.
 *
 *  - `Rgb` — linear interpolation in RGBA-F. Standard for published palettes
 *    (Viridis, Plasma, etc.) whose RGB tuples were authored assuming RGB
 *    interpolation between them. Default for built-in ramps.
 *  - `HsvShort` — interpolate in HSV space, taking the shorter arc on the
 *    hue wheel (e.g. red → blue goes via magenta, not via green). Saturation
 *    and value interpolate linearly. Default for user-authored custom ramps
 *    per the Slice BB-α user request — produces smoother transitions when
 *    the stops are chosen by hand rather than from a perceptually-tuned LUT.
 *  - `HsvLong` — interpolate via the longer arc on the hue wheel. Useful
 *    for rainbow-style ramps where the user wants to traverse the whole
 *    wheel between two stops.
 */
enum class RampInterp : int
{
    Rgb       = 0,
    HsvShort  = 1,
    HsvLong   = 2
};

/*!
 * \struct RasterColorRamp
 * \brief Gradient mapping from a normalised value in [0,1] to a QColor.
 */
struct RasterColorRamp
{
    double          minValue = 0.0;
    double          maxValue = 1.0;
    QGradientStops  stops;        /*!< Sorted list of (position [0..1], QColor) pairs. */
    bool            clampMin = false; /*!< When true, values below minValue are transparent. */
    bool            clampMax = false; /*!< When true, values above maxValue are transparent. */
    RampInterp      interp   = RampInterp::Rgb; /*!< Colour-space used between adjacent stops. */

    /*! Interpolated colour for a normalised position in [0,1]. */
    [[nodiscard]] QColor colorAt(double normalisedPos) const;

    /*! Colour for a raw data value (normalises to [0,1] then calls colorAt). */
    [[nodiscard]] QColor colorForValue(double value) const;

    /*! JSON round-trip — used by .oswp persistence and the custom-ramp library. */
    [[nodiscard]] QJsonObject toJson() const;
    static RasterColorRamp    fromJson(const QJsonObject &j);

    // ── Built-in catalogue ───────────────────────────────────────────────
    //
    // Stops for all built-ins are authored assuming RGB interpolation (the
    // published palettes were tuned that way). The `interp` field defaults
    // to Rgb to honour that.

    [[nodiscard]] static RasterColorRamp grayscale(double min = 0.0, double max = 255.0);
    [[nodiscard]] static RasterColorRamp viridis(double min = 0.0, double max = 1.0);
    [[nodiscard]] static RasterColorRamp plasma(double min = 0.0, double max = 1.0);
    [[nodiscard]] static RasterColorRamp magma(double min = 0.0, double max = 1.0);
    [[nodiscard]] static RasterColorRamp inferno(double min = 0.0, double max = 1.0);
    [[nodiscard]] static RasterColorRamp cividis(double min = 0.0, double max = 1.0);
    [[nodiscard]] static RasterColorRamp turbo(double min = 0.0, double max = 1.0);
    [[nodiscard]] static RasterColorRamp rdBu(double min = 0.0, double max = 1.0);
    [[nodiscard]] static RasterColorRamp rdYlGn(double min = 0.0, double max = 1.0);
    [[nodiscard]] static RasterColorRamp spectral(double min = 0.0, double max = 1.0);
    [[nodiscard]] static RasterColorRamp brBG(double min = 0.0, double max = 1.0);
    [[nodiscard]] static RasterColorRamp legacySWMM5Interval(double min = 0.0, double max = 1.0);
    [[nodiscard]] static RasterColorRamp legacySWMMPollutant(double min = 0.0, double max = 1.0);
    /*! The historic 5-stop 2D mesh terrain elevation palette (deep blue →
     *  green → ochre → off-white). Registering it as a named builtin lets the
     *  mesh-fill default agree between the editor's ramp combo and the
     *  renderer (which used a hard-coded copy of these stops). */
    [[nodiscard]] static RasterColorRamp terrain(double min = 0.0, double max = 1.0);

    // Slice BB-β (2026-05-25) — Plotly's continuous palettes from
    // plotly.colors.sequential / .diverging. Stops authored per Plotly's
    // reference; all RGB-interp by default.
    [[nodiscard]] static RasterColorRamp plotly3(double min = 0.0, double max = 1.0);
    [[nodiscard]] static RasterColorRamp iceFire(double min = 0.0, double max = 1.0);
    [[nodiscard]] static RasterColorRamp blackbody(double min = 0.0, double max = 1.0);
    [[nodiscard]] static RasterColorRamp electric(double min = 0.0, double max = 1.0);
    [[nodiscard]] static RasterColorRamp hot(double min = 0.0, double max = 1.0);
    [[nodiscard]] static RasterColorRamp jet(double min = 0.0, double max = 1.0);
    [[nodiscard]] static RasterColorRamp picnic(double min = 0.0, double max = 1.0);
    [[nodiscard]] static RasterColorRamp portland(double min = 0.0, double max = 1.0);
    [[nodiscard]] static RasterColorRamp rainbow(double min = 0.0, double max = 1.0);
    [[nodiscard]] static RasterColorRamp bluered(double min = 0.0, double max = 1.0);

    /*! 2026-09-01 — sequential water-depth ramp for the 2D inundation /
     *  smooth-depth fill: very light blue at ~40% opacity for barely-wet
     *  cells (the shoreline fades into the terrain beneath) deepening to a
     *  fully opaque navy at maximum depth (user direction: deeper water =
     *  dark blue, shallower = lighter shade). The only builtin carrying an
     *  alpha gradient in its stops — interpolation and JSON round-trip
     *  alpha already, so it rides the existing machinery unchanged. */
    [[nodiscard]] static RasterColorRamp waterDepth(double min = 0.0, double max = 1.0);

    /*! Look up a built-in ramp by short name (case-insensitive). Returns
     *  `grayscale()` when the name is unknown so callers never get an
     *  invalid ramp. Recognised names: "grayscale", "viridis", "plasma",
     *  "magma", "inferno", "cividis", "turbo", "rdbu", "rdylgn",
     *  "spectral", "brbg", "legacy-swmm-5interval", "legacy-swmm-pollutant", "terrain",
     *  plus Slice BB-β Plotly entries: "plotly3", "icefire", "blackbody",
     *  "electric", "hot", "jet", "picnic", "portland", "rainbow", "bluered",
     *  and the 2026-09-01 depth-fill default "water-depth". */
    [[nodiscard]] static RasterColorRamp builtin(const QString &name);

    /*! Names of all built-in ramps, in catalogue order. Used by the
     *  ColorRampComboBox to populate its dropdown. */
    [[nodiscard]] static QStringList builtinNames();
};

Q_DECLARE_METATYPE(RasterColorRamp)

#endif // OPENSWMM_RENDER_COLORRAMP_H
