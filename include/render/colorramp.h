/**
 * @file      colorramp.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     RasterColorRamp — gradient mapping from a normalised value in [0,1]
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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
enum class RampInterp : int { Rgb = 0, HsvShort = 1, HsvLong = 2 };

/*!
 * \struct RasterColorRamp
 * \brief Gradient mapping from a normalised value in [0,1] to a QColor.
 */
struct RasterColorRamp {
    double minValue = 0.0;
    double maxValue = 1.0;
    QGradientStops stops;  /*!< Sorted list of (position [0..1], QColor) pairs. */
    bool clampMin = false; /*!< Legacy serialized flag; underflow is always transparent. */
    bool clampMax = false; /*!< Legacy serialized flag; overflow always uses the maximum color. */
    RampInterp interp = RampInterp::Rgb; /*!< Colour-space used between adjacent stops. */

    /**
     * @brief Interpolate ramp stops at a normalized position.
     * @param[in] normalisedPos Finite dimensionless ramp coordinate, clamped to [0,1].
     * @return Transparent for no stops; the sole/end stop color outside the covered
     *         stop interval; otherwise an interpolated color in the configured space.
     * @pre Stops are ordered by finite normalized position and contain valid QColors.
     * @details RGB interpolation includes alpha. HSV interpolation follows the selected
     *          hue arc, falling back to RGB when an endpoint is achromatic. Neither
     *          numeric data bounds nor the legacy clamp flags are consulted here.
     */
    [[nodiscard]] QColor colorAt(double normalisedPos) const;

    /**
     * @brief Map a raw scientific value through this ramp's numeric range.
     * @param[in] value Raw attribute/result value in the same unit as minValue and maxValue.
     * @return Transparent for non-finite input or a value below minValue; the midpoint
     *         color for a fuzzy-zero range; otherwise colorAt((value-minValue)/range).
     *         Overflow saturates to the upper stop through colorAt()'s clamping.
     * @pre The stored range is finite and ordinarily increasing; no range validation occurs.
     * @note clampMin/clampMax are retained for persistence compatibility but do not change
     *       this underflow/overflow behavior. There is no unit conversion or state mutation.
     */
    [[nodiscard]] QColor colorForValue(double value) const;

    /**
     * @brief Serialize bounds, legacy flags, interpolation mode, and gradient stops.
     * @return JSON containing every field and each stop's numeric position plus an ARGB
     *         hexadecimal color string. Stop order is preserved; input values are not validated.
     */
    [[nodiscard]] QJsonObject toJson() const;

    /**
     * @brief Restore a ramp and order its stops by their persisted positions.
     * @param[in] j Project/custom-ramp JSON; unknown keys are ignored and input is unchanged.
     * @return A new ramp, defaulting to range [0,1], false clamp flags, and RGB interpolation.
     *         Missing stops produce an empty transparent ramp. Invalid colors become transparent;
     *         missing/wrong-typed positions become zero. Unknown interpolation IDs use RGB.
     * @note Positions are sorted but not clamped or deduplicated. Numeric bounds are loaded
     *       without validation, and no built-in palette is substituted for absent stops.
     */
    static RasterColorRamp fromJson(const QJsonObject &j);

    // ── Built-in catalogue ───────────────────────────────────────────────
    //
    // Stops for all built-ins are authored assuming RGB interpolation (the
    // published palettes were tuned that way). The `interp` field defaults
    // to Rgb to honour that.

    /**
     * @brief Create the black-to-white grayscale palette over caller-supplied data bounds.
     * @param[in] min Lower raw-data bound in the displayed attribute's unit; stored verbatim.
     * @param[in] max Upper bound in the same unit; stored verbatim without validation.
     * @return An owned ramp with the named normalized stop catalogue and RGB interpolation.
     *         The method performs no physical-unit conversion or range correction.
     */
    [[nodiscard]] static RasterColorRamp grayscale(double min = 0.0, double max = 255.0);
    /**
     * @brief Create the viridis palette over caller-supplied data bounds.
     * @param[in] min Lower raw-data bound in the displayed attribute's unit; stored verbatim.
     * @param[in] max Upper bound in the same unit; stored verbatim without validation.
     * @return An owned ramp with the named normalized stop catalogue and RGB interpolation.
     *         The method performs no physical-unit conversion or range correction.
     */
    [[nodiscard]] static RasterColorRamp viridis(double min = 0.0, double max = 1.0);
    /**
     * @brief Create the plasma palette over caller-supplied data bounds.
     * @param[in] min Lower raw-data bound in the displayed attribute's unit; stored verbatim.
     * @param[in] max Upper bound in the same unit; stored verbatim without validation.
     * @return An owned ramp with the named normalized stop catalogue and RGB interpolation.
     *         The method performs no physical-unit conversion or range correction.
     */
    [[nodiscard]] static RasterColorRamp plasma(double min = 0.0, double max = 1.0);
    /**
     * @brief Create the magma palette over caller-supplied data bounds.
     * @param[in] min Lower raw-data bound in the displayed attribute's unit; stored verbatim.
     * @param[in] max Upper bound in the same unit; stored verbatim without validation.
     * @return An owned ramp with the named normalized stop catalogue and RGB interpolation.
     *         The method performs no physical-unit conversion or range correction.
     */
    [[nodiscard]] static RasterColorRamp magma(double min = 0.0, double max = 1.0);
    /**
     * @brief Create the inferno palette over caller-supplied data bounds.
     * @param[in] min Lower raw-data bound in the displayed attribute's unit; stored verbatim.
     * @param[in] max Upper bound in the same unit; stored verbatim without validation.
     * @return An owned ramp with the named normalized stop catalogue and RGB interpolation.
     *         The method performs no physical-unit conversion or range correction.
     */
    [[nodiscard]] static RasterColorRamp inferno(double min = 0.0, double max = 1.0);
    /**
     * @brief Create the cividis palette over caller-supplied data bounds.
     * @param[in] min Lower raw-data bound in the displayed attribute's unit; stored verbatim.
     * @param[in] max Upper bound in the same unit; stored verbatim without validation.
     * @return An owned ramp with the named normalized stop catalogue and RGB interpolation.
     *         The method performs no physical-unit conversion or range correction.
     */
    [[nodiscard]] static RasterColorRamp cividis(double min = 0.0, double max = 1.0);
    /**
     * @brief Create the turbo palette over caller-supplied data bounds.
     * @param[in] min Lower raw-data bound in the displayed attribute's unit; stored verbatim.
     * @param[in] max Upper bound in the same unit; stored verbatim without validation.
     * @return An owned ramp with the named normalized stop catalogue and RGB interpolation.
     *         The method performs no physical-unit conversion or range correction.
     */
    [[nodiscard]] static RasterColorRamp turbo(double min = 0.0, double max = 1.0);
    /**
     * @brief Create the rd Bu palette over caller-supplied data bounds.
     * @param[in] min Lower raw-data bound in the displayed attribute's unit; stored verbatim.
     * @param[in] max Upper bound in the same unit; stored verbatim without validation.
     * @return An owned ramp with the named normalized stop catalogue and RGB interpolation.
     *         The method performs no physical-unit conversion or range correction.
     */
    [[nodiscard]] static RasterColorRamp rdBu(double min = 0.0, double max = 1.0);
    /**
     * @brief Create the rd Yl Gn palette over caller-supplied data bounds.
     * @param[in] min Lower raw-data bound in the displayed attribute's unit; stored verbatim.
     * @param[in] max Upper bound in the same unit; stored verbatim without validation.
     * @return An owned ramp with the named normalized stop catalogue and RGB interpolation.
     *         The method performs no physical-unit conversion or range correction.
     */
    [[nodiscard]] static RasterColorRamp rdYlGn(double min = 0.0, double max = 1.0);
    /**
     * @brief Create the spectral palette over caller-supplied data bounds.
     * @param[in] min Lower raw-data bound in the displayed attribute's unit; stored verbatim.
     * @param[in] max Upper bound in the same unit; stored verbatim without validation.
     * @return An owned ramp with the named normalized stop catalogue and RGB interpolation.
     *         The method performs no physical-unit conversion or range correction.
     */
    [[nodiscard]] static RasterColorRamp spectral(double min = 0.0, double max = 1.0);
    /**
     * @brief Create the br BG palette over caller-supplied data bounds.
     * @param[in] min Lower raw-data bound in the displayed attribute's unit; stored verbatim.
     * @param[in] max Upper bound in the same unit; stored verbatim without validation.
     * @return An owned ramp with the named normalized stop catalogue and RGB interpolation.
     *         The method performs no physical-unit conversion or range correction.
     */
    [[nodiscard]] static RasterColorRamp brBG(double min = 0.0, double max = 1.0);
    /**
     * @brief Create the legacy SWMM5Interval palette over caller-supplied data bounds.
     * @param[in] min Lower raw-data bound in the displayed attribute's unit; stored verbatim.
     * @param[in] max Upper bound in the same unit; stored verbatim without validation.
     * @return An owned ramp with the named normalized stop catalogue and RGB interpolation.
     *         The method performs no physical-unit conversion or range correction.
     */
    [[nodiscard]] static RasterColorRamp legacySWMM5Interval(double min = 0.0, double max = 1.0);
    /**
     * @brief Create the legacy SWMMPollutant palette over caller-supplied data bounds.
     * @param[in] min Lower raw-data bound in the displayed attribute's unit; stored verbatim.
     * @param[in] max Upper bound in the same unit; stored verbatim without validation.
     * @return An owned ramp with the named normalized stop catalogue and RGB interpolation.
     *         The method performs no physical-unit conversion or range correction.
     */
    [[nodiscard]] static RasterColorRamp legacySWMMPollutant(double min = 0.0, double max = 1.0);
    /**
     * @brief Create the terrain palette over caller-supplied data bounds.
     * @param[in] min Lower raw-data bound in the displayed attribute's unit; stored verbatim.
     * @param[in] max Upper bound in the same unit; stored verbatim without validation.
     * @return An owned ramp with the named normalized stop catalogue and RGB interpolation.
     *         The method performs no physical-unit conversion or range correction.
     *
     * The historic 5-stop 2D mesh terrain elevation palette (deep blue →
     * green → ochre → off-white). Registering it as a named builtin lets the
     * mesh-fill default agree between the editor's ramp combo and the
     * renderer (which used a hard-coded copy of these stops).
     */
    [[nodiscard]] static RasterColorRamp terrain(double min = 0.0, double max = 1.0);

    // Slice BB-β (2026-05-25) — Plotly's continuous palettes from
    // plotly.colors.sequential / .diverging. Stops authored per Plotly's
    // reference; all RGB-interp by default.
    /**
     * @brief Create the plotly3 palette over caller-supplied data bounds.
     * @param[in] min Lower raw-data bound in the displayed attribute's unit; stored verbatim.
     * @param[in] max Upper bound in the same unit; stored verbatim without validation.
     * @return An owned ramp with the named normalized stop catalogue and RGB interpolation.
     *         The method performs no physical-unit conversion or range correction.
     */
    [[nodiscard]] static RasterColorRamp plotly3(double min = 0.0, double max = 1.0);
    /**
     * @brief Create the ice Fire palette over caller-supplied data bounds.
     * @param[in] min Lower raw-data bound in the displayed attribute's unit; stored verbatim.
     * @param[in] max Upper bound in the same unit; stored verbatim without validation.
     * @return An owned ramp with the named normalized stop catalogue and RGB interpolation.
     *         The method performs no physical-unit conversion or range correction.
     */
    [[nodiscard]] static RasterColorRamp iceFire(double min = 0.0, double max = 1.0);
    /**
     * @brief Create the blackbody palette over caller-supplied data bounds.
     * @param[in] min Lower raw-data bound in the displayed attribute's unit; stored verbatim.
     * @param[in] max Upper bound in the same unit; stored verbatim without validation.
     * @return An owned ramp with the named normalized stop catalogue and RGB interpolation.
     *         The method performs no physical-unit conversion or range correction.
     */
    [[nodiscard]] static RasterColorRamp blackbody(double min = 0.0, double max = 1.0);
    /**
     * @brief Create the electric palette over caller-supplied data bounds.
     * @param[in] min Lower raw-data bound in the displayed attribute's unit; stored verbatim.
     * @param[in] max Upper bound in the same unit; stored verbatim without validation.
     * @return An owned ramp with the named normalized stop catalogue and RGB interpolation.
     *         The method performs no physical-unit conversion or range correction.
     */
    [[nodiscard]] static RasterColorRamp electric(double min = 0.0, double max = 1.0);
    /**
     * @brief Create the hot palette over caller-supplied data bounds.
     * @param[in] min Lower raw-data bound in the displayed attribute's unit; stored verbatim.
     * @param[in] max Upper bound in the same unit; stored verbatim without validation.
     * @return An owned ramp with the named normalized stop catalogue and RGB interpolation.
     *         The method performs no physical-unit conversion or range correction.
     */
    [[nodiscard]] static RasterColorRamp hot(double min = 0.0, double max = 1.0);
    /**
     * @brief Create the jet palette over caller-supplied data bounds.
     * @param[in] min Lower raw-data bound in the displayed attribute's unit; stored verbatim.
     * @param[in] max Upper bound in the same unit; stored verbatim without validation.
     * @return An owned ramp with the named normalized stop catalogue and RGB interpolation.
     *         The method performs no physical-unit conversion or range correction.
     */
    [[nodiscard]] static RasterColorRamp jet(double min = 0.0, double max = 1.0);
    /**
     * @brief Create the picnic palette over caller-supplied data bounds.
     * @param[in] min Lower raw-data bound in the displayed attribute's unit; stored verbatim.
     * @param[in] max Upper bound in the same unit; stored verbatim without validation.
     * @return An owned ramp with the named normalized stop catalogue and RGB interpolation.
     *         The method performs no physical-unit conversion or range correction.
     */
    [[nodiscard]] static RasterColorRamp picnic(double min = 0.0, double max = 1.0);
    /**
     * @brief Create the portland palette over caller-supplied data bounds.
     * @param[in] min Lower raw-data bound in the displayed attribute's unit; stored verbatim.
     * @param[in] max Upper bound in the same unit; stored verbatim without validation.
     * @return An owned ramp with the named normalized stop catalogue and RGB interpolation.
     *         The method performs no physical-unit conversion or range correction.
     */
    [[nodiscard]] static RasterColorRamp portland(double min = 0.0, double max = 1.0);
    /**
     * @brief Create the rainbow palette over caller-supplied data bounds.
     * @param[in] min Lower raw-data bound in the displayed attribute's unit; stored verbatim.
     * @param[in] max Upper bound in the same unit; stored verbatim without validation.
     * @return An owned ramp with the named normalized stop catalogue and RGB interpolation.
     *         The method performs no physical-unit conversion or range correction.
     */
    [[nodiscard]] static RasterColorRamp rainbow(double min = 0.0, double max = 1.0);
    /**
     * @brief Create the bluered palette over caller-supplied data bounds.
     * @param[in] min Lower raw-data bound in the displayed attribute's unit; stored verbatim.
     * @param[in] max Upper bound in the same unit; stored verbatim without validation.
     * @return An owned ramp with the named normalized stop catalogue and RGB interpolation.
     *         The method performs no physical-unit conversion or range correction.
     */
    [[nodiscard]] static RasterColorRamp bluered(double min = 0.0, double max = 1.0);

    /**
     * @brief Create the water-depth (translucent blue to opaque navy) palette over caller-supplied data bounds.
     * @param[in] min Lower raw-data bound in the displayed attribute's unit; stored verbatim.
     * @param[in] max Upper bound in the same unit; stored verbatim without validation.
     * @return An owned ramp with the named normalized stop catalogue and RGB interpolation.
     *         The method performs no physical-unit conversion or range correction.
     *
     * 2026-09-01 — sequential water-depth ramp for the 2D inundation /
     * smooth-depth fill: very light blue at ~40% opacity for barely-wet
     * cells (the shoreline fades into the terrain beneath) deepening to a
     * fully opaque navy at maximum depth (user direction: deeper water =
     * dark blue, shallower = lighter shade). The only builtin carrying an
     * alpha gradient in its stops — interpolation and JSON round-trip
     * alpha already, so it rides the existing machinery unchanged.
     */
    [[nodiscard]] static RasterColorRamp waterDepth(double min = 0.0, double max = 1.0);

    /**
     * @brief Construct a built-in ramp from a catalogue key or display name.
     * @details Look up a built-in ramp by short name (case-insensitive). Returns
     *  `grayscale()` when the name is unknown so callers never get an
     *  invalid ramp. Recognised names: "grayscale", "viridis", "plasma",
     *  "magma", "inferno", "cividis", "turbo", "rdbu", "rdylgn",
     *  "spectral", "brbg", "legacy-swmm-5interval", "legacy-swmm-pollutant", "terrain",
     *  plus Slice BB-β Plotly entries: "plotly3", "icefire", "blackbody",
     *  "electric", "hot", "jet", "picnic", "portland", "rainbow", "bluered",
     *  and the 2026-09-01 depth-fill default "water-depth".
     * @param[in] name Internal key or display name; outer whitespace is trimmed and
     *                 matching is case-insensitive.
     * @return A new catalogued ramp over [0,1], or grayscale() with its default [0,255]
     *         range when no name matches. Each returned ramp owns its stop values.
     */
    [[nodiscard]] static RasterColorRamp builtin(const QString &name);

    /**
     * @brief List built-in palette display names in catalogue order.
     * @details Names of all built-in ramps, in catalogue order. Used by the
     *  ColorRampComboBox to populate its dropdown.
     * @return Display labels in catalogue order, not necessarily the internal persistence keys.
     */
    [[nodiscard]] static QStringList builtinNames();
    /**
     * @brief Test whether a text label identifies a supported built-in palette.
     * @param[in] name Internal key or display label; trimmed and compared case-insensitively.
     * @return True on a catalogue match; false for unknown or empty input. Unlike builtin(),
     *         this query does not accept an unknown name by returning a grayscale fallback.
     */
    [[nodiscard]] static bool isBuiltin(const QString &name);
};

Q_DECLARE_METATYPE(RasterColorRamp)

#endif // OPENSWMM_RENDER_COLORRAMP_H
