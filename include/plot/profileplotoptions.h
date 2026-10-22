/**
 * @file      profileplotoptions.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Q_OBJECT facade for every user-configurable display option on
 *         the profile plot.  Both the per-dialog Layer Panel (quick
 *         visibility toggles) and the QPropertyModel-backed Display
 *         Options dialog (theming / legend / labels) edit the same
 *         instance, and the plot widget reads from it directly — so any
 *         setter call propagates to every view through the single
 *         `changed()` signal.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef PROFILE_PLOT_OPTIONS_H
#define PROFILE_PLOT_OPTIONS_H

#include "plot/numberformat.h"

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QObject>
#include <QPen>
#include <QPointF>

class ProfilePlotOptions : public QObject
{
    Q_OBJECT

public:
    enum LabelOrientation { Vertical = 0, Horizontal = 1, Diagonal = 2 };
    Q_ENUM(LabelOrientation)

    /*! \brief Axis label number mode; values mirror
     *  openswmmvis::plot::NumberFormatMode (0=Decimals, 1=SignificantFigures,
     *  2=Scientific, 3=Engineering, 4=Thousands). */
    enum LabelFormatMode {
        Decimals = 0,
        SignificantFigures = 1,
        Scientific = 2,
        Engineering = 3,
        Thousands = 4
    };
    Q_ENUM(LabelFormatMode)

    /*! Combined axis number format offered to the user as ONE dropdown.
     *  Mirrors openswmmvis::plot::AxisNumberFormatPreset value-for-value —
     *  QPropertyModel resolves a property's enumerator list through the
     *  declaring class's meta-object, so the list has to be declared here,
     *  and it labels each row with the enumerator's own name. The mapping to
     *  a real mode + digit count lives in numberformat.h. */
    enum AxisNumberFormat {
        Integer = 0,
        Decimals1 = 1,
        Decimals2 = 2,
        Decimals3 = 3,
        Decimals4 = 4,
        Decimals6 = 5,
        SigFigs3 = 6,
        SigFigs4 = 7,
        SigFigs6 = 8,
        Scientific2 = 9,
        Scientific3 = 10,
        Scientific4 = 11,
        Engineering2 = 12,
        Engineering3 = 13,
        ThousandsInteger = 14,
        Thousands1 = 15,
        Thousands2 = 16
    };
    Q_ENUM(AxisNumberFormat)

    enum LegendPosition { TopRight = 0, TopLeft = 1, BottomLeft = 2, BottomRight = 3 };
    Q_ENUM(LegendPosition)

    enum TimeLabelPosition {
        TimeTopRight = 0,
        TimeTopLeft = 1,
        TimeBottomLeft = 2,
        TimeBottomRight = 3
    };
    Q_ENUM(TimeLabelPosition)

    /*! \brief Ground-line source. `Auto` resolves to `Mesh2D` when the
     *  project has a 2D mesh layer, else `NodeRims`. */
    enum GroundSource { Auto = 0, NodeRims = 1, Mesh2D = 2, TerrainDEM = 3 };
    Q_ENUM(GroundSource)

    // ── Layer visibility ────────────────────────────────────────────────
    // Live HGL is split into two independent toggles: the stroked line
    // and the under-line fill polygon. Either can be on without the
    // other; the legend still shows one HGL entry whenever at least
    // one is enabled.
    Q_PROPERTY(bool currentHglLine READ currentHglLine WRITE setCurrentHglLine NOTIFY changed)
    Q_PROPERTY(bool currentHglFill READ currentHglFill WRITE setCurrentHglFill NOTIFY changed)
    Q_PROPERTY(bool currentEgl READ currentEgl WRITE setCurrentEgl NOTIFY changed)
    Q_PROPERTY(bool maxHglBand READ maxHglBand WRITE setMaxHglBand NOTIFY changed)
    Q_PROPERTY(bool maxHglLine READ maxHglLine WRITE setMaxHglLine NOTIFY changed)
    // EGL has no fill — only a line toggle.
    Q_PROPERTY(bool maxEglLine READ maxEglLine WRITE setMaxEglLine NOTIFY changed)

    // ── Labels ──────────────────────────────────────────────────────────
    Q_PROPERTY(bool showNodeLabels READ showNodeLabels WRITE setShowNodeLabels NOTIFY changed)
    Q_PROPERTY(bool showLinkLabels READ showLinkLabels WRITE setShowLinkLabels NOTIFY changed)
    Q_PROPERTY(bool inlineNodeLabels READ inlineNodeLabels WRITE setInlineNodeLabels NOTIFY changed)
    Q_PROPERTY(ProfilePlotOptions::LabelOrientation labelOrientation READ labelOrientation WRITE
                   setLabelOrientation NOTIFY changed)
    Q_PROPERTY(int labelAngleDeg READ labelAngleDeg WRITE setLabelAngleDeg NOTIFY changed)

    // ── Axis number format ──────────────────────────────────────────────
    // X axis = distance/chainage; Y axis = elevation/value. Seeded from the
    // global Preferences default; edits here override per-plot.
    Q_PROPERTY(ProfilePlotOptions::AxisNumberFormat xAxisNumberFormat READ xAxisNumberFormat WRITE
                   setXAxisNumberFormat NOTIFY changed)
    Q_PROPERTY(QString xLabelFormat READ xLabelFormat WRITE setXLabelFormat NOTIFY changed)
    Q_PROPERTY(ProfilePlotOptions::AxisNumberFormat yAxisNumberFormat READ yAxisNumberFormat WRITE
                   setYAxisNumberFormat NOTIFY changed)
    Q_PROPERTY(QString yLabelFormat READ yLabelFormat WRITE setYLabelFormat NOTIFY changed)

    // ── Ground line source ──────────────────────────────────────────────
    // Where the soil's top edge comes from. Auto (default) = the 2D mesh
    // vertex elevations interpolated at the path stations whenever the
    // project has a mesh layer, else the node rims (invert + max depth).
    Q_PROPERTY(ProfilePlotOptions::GroundSource groundSource READ groundSource WRITE setGroundSource
                   NOTIFY changed)

    // ── 2D inundation overlay ───────────────────────────────────────────
    // Water surface of the active 2D results layer sampled along the path
    // (mesh bed + interpolated depth), drawn as a band above the ground with
    // a WSE line, animated with the profile cursor. Nothing draws when the
    // project has no active 2D results layer.
    Q_PROPERTY(bool show2DInundation READ show2DInundation WRITE setShow2DInundation NOTIFY changed)
    Q_PROPERTY(QPen inundation2DLinePen READ inundation2DLinePen WRITE setInundation2DLinePen NOTIFY
                   changed)
    Q_PROPERTY(QBrush inundation2DFillBrush READ inundation2DFillBrush WRITE
                   setInundation2DFillBrush NOTIFY changed)

    // ── Node connectivity ───────────────────────────────────────────────
    // What ELSE meets each node on the path: truncated stubs of the links
    // the profile does not follow, and a small plan rose giving every
    // connected link's map bearing and flow direction.
    Q_PROPERTY(bool showBranchStubs READ showBranchStubs WRITE setShowBranchStubs NOTIFY changed)
    Q_PROPERTY(bool showNodeRoses READ showNodeRoses WRITE setShowNodeRoses NOTIFY changed)

    // ── Flooding indicator (animated wedge above the rim) ───────────────
    Q_PROPERTY(
        bool showFloodingGlyphs READ showFloodingGlyphs WRITE setShowFloodingGlyphs NOTIFY changed)
    Q_PROPERTY(double floodRadiusPx READ floodRadiusPx WRITE setFloodRadiusPx NOTIFY changed)
    Q_PROPERTY(int floodSweepDeg READ floodSweepDeg WRITE setFloodSweepDeg NOTIFY changed)
    Q_PROPERTY(QColor floodColor READ floodColor WRITE setFloodColor NOTIFY changed)

    // ── Theming: node-type colours ──────────────────────────────────────
    Q_PROPERTY(QColor junctionFill READ junctionFill WRITE setJunctionFill NOTIFY changed)
    Q_PROPERTY(QColor junctionOutline READ junctionOutline WRITE setJunctionOutline NOTIFY changed)
    Q_PROPERTY(QColor outfallFill READ outfallFill WRITE setOutfallFill NOTIFY changed)
    Q_PROPERTY(QColor outfallOutline READ outfallOutline WRITE setOutfallOutline NOTIFY changed)
    Q_PROPERTY(QColor storageFill READ storageFill WRITE setStorageFill NOTIFY changed)
    Q_PROPERTY(QColor storageOutline READ storageOutline WRITE setStorageOutline NOTIFY changed)
    Q_PROPERTY(QColor dividerFill READ dividerFill WRITE setDividerFill NOTIFY changed)
    Q_PROPERTY(QColor dividerOutline READ dividerOutline WRITE setDividerOutline NOTIFY changed)
    // A virtual junction is a break point inside a pipe, not a structure: it
    // draws as a dashed outline rectangle over the conduit running through
    // it, with no fill and no rim glyph.  One pen therefore carries its whole
    // appearance — colour, width, style and dash pattern — instead of the
    // fill/outline colour pair the physical node kinds use.
    Q_PROPERTY(QPen virtualJunctionOutlinePen READ virtualJunctionOutlinePen WRITE
                   setVirtualJunctionOutlinePen NOTIFY changed)

    // ── Theming: link-type colours ──────────────────────────────────────
    Q_PROPERTY(QColor conduitFill READ conduitFill WRITE setConduitFill NOTIFY changed)
    Q_PROPERTY(QColor conduitOutline READ conduitOutline WRITE setConduitOutline NOTIFY changed)
    Q_PROPERTY(QColor pumpFill READ pumpFill WRITE setPumpFill NOTIFY changed)
    Q_PROPERTY(QColor pumpOutline READ pumpOutline WRITE setPumpOutline NOTIFY changed)
    Q_PROPERTY(QColor weirFill READ weirFill WRITE setWeirFill NOTIFY changed)
    Q_PROPERTY(QColor weirOutline READ weirOutline WRITE setWeirOutline NOTIFY changed)
    Q_PROPERTY(QColor orificeFill READ orificeFill WRITE setOrificeFill NOTIFY changed)
    Q_PROPERTY(QColor orificeOutline READ orificeOutline WRITE setOrificeOutline NOTIFY changed)
    Q_PROPERTY(QColor outletFill READ outletFill WRITE setOutletFill NOTIFY changed)
    Q_PROPERTY(QColor outletOutline READ outletOutline WRITE setOutletOutline NOTIFY changed)

    // ── Theming: soil + lines ───────────────────────────────────────────
    Q_PROPERTY(QColor soilFill READ soilFill WRITE setSoilFill NOTIFY changed)
    Q_PROPERTY(QColor beddingFill READ beddingFill WRITE setBeddingFill NOTIFY changed)
    // Line pens carry colour, width, style (solid / dashed / dotted) and
    // a custom dash pattern — all editable via the property tree's QPen
    // editor (qpenpropertyitem) which exposes those fields as expandable
    // children.  Fill brushes stay QBrush since gradients/textures matter
    // for fills.
    Q_PROPERTY(QPen hglLinePen READ hglLinePen WRITE setHglLinePen NOTIFY changed)
    Q_PROPERTY(QBrush hglFillBrush READ hglFillBrush WRITE setHglFillBrush NOTIFY changed)
    Q_PROPERTY(QPen eglLinePen READ eglLinePen WRITE setEglLinePen NOTIFY changed)
    // EGL has no fill brush: the velocity-head band above the HGL is not a
    // physically meaningful filled region. EGL renders as a line only.
    Q_PROPERTY(QPen maxHglLinePen READ maxHglLinePen WRITE setMaxHglLinePen NOTIFY changed)
    Q_PROPERTY(QBrush maxHglFillBrush READ maxHglFillBrush WRITE setMaxHglFillBrush NOTIFY changed)
    Q_PROPERTY(QPen maxEglLinePen READ maxEglLinePen WRITE setMaxEglLinePen NOTIFY changed)
    Q_PROPERTY(
        QPen conduitOutlinePen READ conduitOutlinePen WRITE setConduitOutlinePen NOTIFY changed)
    Q_PROPERTY(
        QPen orificeOutlinePen READ orificeOutlinePen WRITE setOrificeOutlinePen NOTIFY changed)
    Q_PROPERTY(QPen weirOutlinePen READ weirOutlinePen WRITE setWeirOutlinePen NOTIFY changed)
    Q_PROPERTY(QPen pumpOutlinePen READ pumpOutlinePen WRITE setPumpOutlinePen NOTIFY changed)
    Q_PROPERTY(QPen outletOutlinePen READ outletOutlinePen WRITE setOutletOutlinePen NOTIFY changed)
    Q_PROPERTY(
        QBrush streetInvertBrush READ streetInvertBrush WRITE setStreetInvertBrush NOTIFY changed)

    // ── Legend ──────────────────────────────────────────────────────────
    Q_PROPERTY(bool legendVisible READ legendVisible WRITE setLegendVisible NOTIFY changed)
    Q_PROPERTY(ProfilePlotOptions::LegendPosition legendPosition READ legendPosition WRITE
                   setLegendPosition NOTIFY changed)
    Q_PROPERTY(QFont legendFont READ legendFont WRITE setLegendFont NOTIFY changed)
    Q_PROPERTY(double legendOpacity READ legendOpacity WRITE setLegendOpacity NOTIFY changed)
    Q_PROPERTY(QPointF legendOffset READ legendOffset WRITE setLegendOffset NOTIFY changed)

    // ── Timestamp overlay ───────────────────────────────────────────────
    Q_PROPERTY(bool showTimeLabel READ showTimeLabel WRITE setShowTimeLabel NOTIFY changed)
    Q_PROPERTY(ProfilePlotOptions::TimeLabelPosition timeLabelPosition READ timeLabelPosition WRITE
                   setTimeLabelPosition NOTIFY changed)
    Q_PROPERTY(QColor timeLabelColor READ timeLabelColor WRITE setTimeLabelColor NOTIFY changed)
    Q_PROPERTY(QFont timeLabelFont READ timeLabelFont WRITE setTimeLabelFont NOTIFY changed)
    Q_PROPERTY(QString timeLabelFormat READ timeLabelFormat WRITE setTimeLabelFormat NOTIFY changed)
    Q_PROPERTY(QPointF timeLabelOffset READ timeLabelOffset WRITE setTimeLabelOffset NOTIFY changed)

public:
    /**
     * @brief Create per-plot display state seeded from application axis-format preferences.
     * @param[in] parent Optional QObject owner; options do not own a chart or any result source. Legend/timestamp fonts use the system general font.
     * @note Construction emits no changed() notification. Typed setters edit only this cache, emit changed() for effective changes and do not write application preferences; connected views apply rendering/resampling as needed. Use on the GUI thread.
     */
    explicit ProfilePlotOptions(QObject *parent = nullptr);

    /*!
     * \brief Human-readable label QPropertyModel uses in the row header
     *        instead of the raw Q_PROPERTY name.  Falls back to the
     *        property name when no mapping is registered.
     * @param[in] propertyName Exact meta-property identifier.
     * @return Translated cached label or the original identifier when unmapped; the static mapping is created on first call.
     */
    Q_INVOKABLE QString displayLabelFor(const QString &propertyName) const;

    // ── Getters ─────────────────────────────────────────────────────────
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Whether to stroke the current hydraulic-grade curve independently of its fill. Returns a value copy; no data-unit conversion occurs.
     */
    bool currentHglLine() const { return m_currentHglLine; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Whether to fill beneath the current hydraulic-grade curve independently of its line. Returns a value copy; no data-unit conversion occurs.
     */
    bool currentHglFill() const { return m_currentHglFill; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Whether to draw the current energy-grade line; no EGL fill is implied. Returns a value copy; no data-unit conversion occurs.
     */
    bool currentEgl() const { return m_currentEgl; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Whether to draw the hydraulic-grade maximum/envelope band. Returns a value copy; no data-unit conversion occurs.
     */
    bool maxHglBand() const { return m_maxHglBand; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Whether to stroke the maximum hydraulic-grade curve. Returns a value copy; no data-unit conversion occurs.
     */
    bool maxHglLine() const { return m_maxHglLine; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Whether to stroke the maximum energy-grade curve. Returns a value copy; no data-unit conversion occurs.
     */
    bool maxEglLine() const { return m_maxEglLine; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Whether to draw node names on the profile's label axis. Returns a value copy; no data-unit conversion occurs.
     */
    bool showNodeLabels() const { return m_showNodeLabels; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Whether to draw link names on the profile's label axis. Returns a value copy; no data-unit conversion occurs.
     */
    bool showLinkLabels() const { return m_showLinkLabels; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Whether to place node IDs above the structure rims. Returns a value copy; no data-unit conversion occurs.
     */
    bool inlineNodeLabels() const { return m_inlineNodeLabels; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Node/link label orientation policy; raw enum edits are not validated. Returns a value copy; no data-unit conversion occurs.
     */
    LabelOrientation labelOrientation() const { return m_labelOrientation; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Diagonal label angle in degrees; typed edits clamp to [1,89]. Returns a value copy; no data-unit conversion occurs.
     */
    int labelAngleDeg() const { return m_labelAngleDeg; }
    /*! Combined format of each axis, derived from the mode + digit count the
     *  rest of the code still works in.
     * @brief Approximate the chainage/distance numeric policy as a preset.
     * @return Closest preset for cached mode/count; custom text is ignored and unknown modes fall back to Decimals2. The returned integer can exceed a limited local enum vocabulary when the shared mode contains additional families.
     */
    AxisNumberFormat xAxisNumberFormat() const;
    /**
     * @brief Approximate the elevation/value numeric policy as a preset.
     * @return Closest preset for cached mode/count; custom text is ignored and unknown modes fall back to Decimals2. The returned integer can exceed a limited local enum vocabulary when the shared mode contains additional families.
     */
    AxisNumberFormat yAxisNumberFormat() const;

    /**
     * @brief Inspect chainage/distance numeric label policy.
     * @return Raw cached NumberFormatMode mirror; no enum validation is applied by the typed setter.
     */
    LabelFormatMode xLabelFormatMode() const { return m_xLabelMode; }
    /**
     * @brief Inspect chainage/distance numeric label policy.
     * @return Cached digit count, with typed precision edits clamped to [0,10]; SignificantFigures rendering uses at least one.
     */
    int xLabelPrecision() const { return m_xLabelPrecision; }
    /**
     * @brief Inspect chainage/distance numeric label policy.
     * @return Optional printf override copied verbatim; invalid syntax remains stored but is ignored by NumberFormat.
     */
    QString xLabelFormat() const { return m_xLabelFormatStr; }
    /**
     * @brief Inspect elevation/value numeric label policy.
     * @return Raw cached NumberFormatMode mirror; no enum validation is applied by the typed setter.
     */
    LabelFormatMode yLabelFormatMode() const { return m_yLabelMode; }
    /**
     * @brief Inspect elevation/value numeric label policy.
     * @return Cached digit count, with typed precision edits clamped to [0,10]; SignificantFigures rendering uses at least one.
     */
    int yLabelPrecision() const { return m_yLabelPrecision; }
    /**
     * @brief Inspect elevation/value numeric label policy.
     * @return Optional printf override copied verbatim; invalid syntax remains stored but is ignored by NumberFormat.
     */
    QString yLabelFormat() const { return m_yLabelFormatStr; }
    /*! \brief Current X/Y axis label format as the shared value type.
     * @brief Copy the chainage/distance numeric formatting policy.
     * @return Independent raw mode/count/custom snapshot without validation or conversion.
     */
    openswmmvis::plot::NumberFormat xFormat() const
    {
        return {static_cast<openswmmvis::plot::NumberFormatMode>(m_xLabelMode), m_xLabelPrecision,
                m_xLabelFormatStr};
    }
    /**
     * @brief Copy the elevation/value numeric formatting policy.
     * @return Independent raw mode/count/custom snapshot without validation or conversion.
     */
    openswmmvis::plot::NumberFormat yFormat() const
    {
        return {static_cast<openswmmvis::plot::NumberFormatMode>(m_yLabelMode), m_yLabelPrecision,
                m_yLabelFormatStr};
    }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Ground sampling policy Auto/NodeRims/Mesh2D/TerrainDEM; the host resolves availability and resampling. Typed edits do not load terrain or validate enum membership. Returns a value copy; no data-unit conversion occurs.
     */
    GroundSource groundSource() const { return m_groundSource; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Whether to draw available active 2D water-surface overlay samples; it does not load a source. Returns a value copy; no data-unit conversion occurs.
     */
    bool show2DInundation() const { return m_show2DInundation; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return 2D water-surface outline pen, including logical-pixel width, dash/cap/join and colour. Returns a value copy; no data-unit conversion occurs.
     */
    QPen inundation2DLinePen() const { return m_inundation2DLinePen; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return 2D inundation fill brush with its alpha/pattern retained. Returns a value copy; no data-unit conversion occurs.
     */
    QBrush inundation2DFillBrush() const { return m_inundation2DFillBrush; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Whether off-path incident links are rendered as truncated connections at structures. Returns a value copy; no data-unit conversion occurs.
     */
    bool showBranchStubs() const { return m_showBranchStubs; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Whether to draw plan-direction roses from the recorded incident-link bearings. Returns a value copy; no data-unit conversion occurs.
     */
    bool showNodeRoses() const { return m_showNodeRoses; }

    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Whether to draw the animated surcharge/flooding wedges above node rims. Returns a value copy; no data-unit conversion occurs.
     */
    bool showFloodingGlyphs() const { return m_showFloodingGlyphs; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Flooding wedge radius in logical pixels; typed edits clamp to [4,60], expecting finite input. Returns a value copy; no data-unit conversion occurs.
     */
    double floodRadiusPx() const { return m_floodRadiusPx; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Flooding wedge angular sweep in degrees; typed edits clamp to [5,180]. Returns a value copy; no data-unit conversion occurs.
     */
    int floodSweepDeg() const { return m_floodSweepDeg; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Flooding wedge fill colour including alpha; the painter derives its outline. No typed validity check occurs. Returns a value copy; no data-unit conversion occurs.
     */
    QColor floodColor() const { return m_floodColor; }

    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Junction glyph/interior fill colour including alpha; no typed colour-validity check occurs. Returns a value copy; no data-unit conversion occurs.
     */
    QColor junctionFill() const { return m_junctionFill; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Junction outline colour, kept independently from complete outline-pen properties where those exist; no automatic field synchronization occurs. Returns a value copy; no data-unit conversion occurs.
     */
    QColor junctionOutline() const { return m_junctionOutline; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Outfall glyph/interior fill colour including alpha; no typed colour-validity check occurs. Returns a value copy; no data-unit conversion occurs.
     */
    QColor outfallFill() const { return m_outfallFill; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Outfall outline colour, kept independently from complete outline-pen properties where those exist; no automatic field synchronization occurs. Returns a value copy; no data-unit conversion occurs.
     */
    QColor outfallOutline() const { return m_outfallOutline; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Storage glyph/interior fill colour including alpha; no typed colour-validity check occurs. Returns a value copy; no data-unit conversion occurs.
     */
    QColor storageFill() const { return m_storageFill; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Storage outline colour, kept independently from complete outline-pen properties where those exist; no automatic field synchronization occurs. Returns a value copy; no data-unit conversion occurs.
     */
    QColor storageOutline() const { return m_storageOutline; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Divider glyph/interior fill colour including alpha; no typed colour-validity check occurs. Returns a value copy; no data-unit conversion occurs.
     */
    QColor dividerFill() const { return m_dividerFill; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Divider outline colour, kept independently from complete outline-pen properties where those exist; no automatic field synchronization occurs. Returns a value copy; no data-unit conversion occurs.
     */
    QColor dividerOutline() const { return m_dividerOutline; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Computational-break marker pen, with logical-pixel width and custom dash/cap/join retained. Returns a value copy; no data-unit conversion occurs.
     */
    QPen virtualJunctionOutlinePen() const { return m_virtualJunctionOutlinePen; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Conduit glyph/interior fill colour including alpha; no typed colour-validity check occurs. Returns a value copy; no data-unit conversion occurs.
     */
    QColor conduitFill() const { return m_conduitFill; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Conduit outline colour, kept independently from complete outline-pen properties where those exist; no automatic field synchronization occurs. Returns a value copy; no data-unit conversion occurs.
     */
    QColor conduitOutline() const { return m_conduitOutline; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Pump glyph/interior fill colour including alpha; no typed colour-validity check occurs. Returns a value copy; no data-unit conversion occurs.
     */
    QColor pumpFill() const { return m_pumpFill; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Pump outline colour, kept independently from complete outline-pen properties where those exist; no automatic field synchronization occurs. Returns a value copy; no data-unit conversion occurs.
     */
    QColor pumpOutline() const { return m_pumpOutline; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Weir glyph/interior fill colour including alpha; no typed colour-validity check occurs. Returns a value copy; no data-unit conversion occurs.
     */
    QColor weirFill() const { return m_weirFill; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Weir outline colour, kept independently from complete outline-pen properties where those exist; no automatic field synchronization occurs. Returns a value copy; no data-unit conversion occurs.
     */
    QColor weirOutline() const { return m_weirOutline; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Orifice glyph/interior fill colour including alpha; no typed colour-validity check occurs. Returns a value copy; no data-unit conversion occurs.
     */
    QColor orificeFill() const { return m_orificeFill; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Orifice outline colour, kept independently from complete outline-pen properties where those exist; no automatic field synchronization occurs. Returns a value copy; no data-unit conversion occurs.
     */
    QColor orificeOutline() const { return m_orificeOutline; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Outlet glyph/interior fill colour including alpha; no typed colour-validity check occurs. Returns a value copy; no data-unit conversion occurs.
     */
    QColor outletFill() const { return m_outletFill; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Outlet outline colour, kept independently from complete outline-pen properties where those exist; no automatic field synchronization occurs. Returns a value copy; no data-unit conversion occurs.
     */
    QColor outletOutline() const { return m_outletOutline; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Soil polygon colour including alpha. Returns a value copy; no data-unit conversion occurs.
     */
    QColor soilFill() const { return m_soilFill; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Conduit bedding polygon colour including alpha. Returns a value copy; no data-unit conversion occurs.
     */
    QColor beddingFill() const { return m_beddingFill; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Current hydraulic-grade stroke pen, with logical-pixel width and complete dash/cap/join properties. Returns a value copy; no data-unit conversion occurs.
     */
    QPen hglLinePen() const { return m_hglLinePen; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Current hydraulic-grade fill brush including alpha/pattern. Returns a value copy; no data-unit conversion occurs.
     */
    QBrush hglFillBrush() const { return m_hglFillBrush; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Current energy-grade stroke pen, independent of the HGL fill. Returns a value copy; no data-unit conversion occurs.
     */
    QPen eglLinePen() const { return m_eglLinePen; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Maximum hydraulic-grade outline pen in logical pixels. Returns a value copy; no data-unit conversion occurs.
     */
    QPen maxHglLinePen() const { return m_maxHglLinePen; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Maximum hydraulic-grade band brush including alpha/pattern. Returns a value copy; no data-unit conversion occurs.
     */
    QBrush maxHglFillBrush() const { return m_maxHglFillBrush; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Maximum energy-grade stroke pen in logical pixels. Returns a value copy; no data-unit conversion occurs.
     */
    QPen maxEglLinePen() const { return m_maxEglLinePen; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Conduit complete outline pen in logical pixels, independent of the separate outline-colour cache. Returns a value copy; no data-unit conversion occurs.
     */
    QPen conduitOutlinePen() const { return m_conduitOutlinePen; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Orifice complete outline pen in logical pixels, independent of the separate outline-colour cache. Returns a value copy; no data-unit conversion occurs.
     */
    QPen orificeOutlinePen() const { return m_orificeOutlinePen; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Weir complete outline pen in logical pixels, independent of the separate outline-colour cache. Returns a value copy; no data-unit conversion occurs.
     */
    QPen weirOutlinePen() const { return m_weirOutlinePen; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Pump complete outline pen in logical pixels, independent of the separate outline-colour cache. Returns a value copy; no data-unit conversion occurs.
     */
    QPen pumpOutlinePen() const { return m_pumpOutlinePen; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Outlet complete outline pen in logical pixels, independent of the separate outline-colour cache. Returns a value copy; no data-unit conversion occurs.
     */
    QPen outletOutlinePen() const { return m_outletOutlinePen; }
    /*! Pavement band drawn under a STREET conduit's gutter line, so a street
     *  reads as a road rather than as any other open channel.
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Pavement band brush beneath STREET conduit gutter geometry, including pattern/alpha. Returns a value copy; no data-unit conversion occurs.
     */
    QBrush streetInvertBrush() const { return m_streetInvertBrush; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Whether the in-plot legend is requested; does not change source/series visibility. Returns a value copy; no data-unit conversion occurs.
     */
    bool legendVisible() const { return m_legendVisible; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Legend corner anchor; raw enum values are stored without validation by the typed setter. Returns a value copy; no data-unit conversion occurs.
     */
    LegendPosition legendPosition() const { return m_legendPosition; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Legend text font with Qt point/pixel-size semantics. Returns a value copy; no data-unit conversion occurs.
     */
    QFont legendFont() const { return m_legendFont; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Legend background opacity, with typed edits clamped to [0,1]; finite input is expected. Returns a value copy; no data-unit conversion occurs.
     */
    double legendOpacity() const { return m_legendOpacity; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Logical-pixel XY displacement from the chosen corner anchor, with positive X right and positive Y down. Returns a value copy; no data-unit conversion occurs.
     */
    QPointF legendOffset() const { return m_legendOffset; }

    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Whether the timestamp overlay is requested; does not affect the animation cursor. Returns a value copy; no data-unit conversion occurs.
     */
    bool showTimeLabel() const { return m_showTimeLabel; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Timestamp corner anchor, stored as a raw enum by typed edits. Returns a value copy; no data-unit conversion occurs.
     */
    TimeLabelPosition timeLabelPosition() const { return m_timeLabelPosition; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Timestamp foreground colour including alpha; typed edits do not reject invalid colours. Returns a value copy; no data-unit conversion occurs.
     */
    QColor timeLabelColor() const { return m_timeLabelColor; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Timestamp font with Qt point/pixel-size semantics. Returns a value copy; no data-unit conversion occurs.
     */
    QFont timeLabelFont() const { return m_timeLabelFont; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return QDateTime format pattern copied verbatim, independent of numeric axis formatting. Returns a value copy; no data-unit conversion occurs.
     */
    QString timeLabelFormat() const { return m_timeLabelFormat; }
    /**
     * @brief Read per-plot display state without inspecting live geometry/results.
     * @return Logical-pixel XY timestamp displacement from its anchor, positive right/down. Returns a value copy; no data-unit conversion occurs.
     */
    QPointF timeLabelOffset() const { return m_timeLabelOffset; }

public slots:
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] v Whether to stroke the current hydraulic-grade curve independently of its fill.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setCurrentHglLine(bool v);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] v Whether to fill beneath the current hydraulic-grade curve independently of its line.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setCurrentHglFill(bool v);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] v Whether to draw the current energy-grade line; no EGL fill is implied.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setCurrentEgl(bool v);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] v Whether to draw the hydraulic-grade maximum/envelope band.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setMaxHglBand(bool v);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] v Whether to stroke the maximum hydraulic-grade curve.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setMaxHglLine(bool v);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] v Whether to stroke the maximum energy-grade curve.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setMaxEglLine(bool v);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] v Whether to draw node names on the profile's label axis.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setShowNodeLabels(bool v);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] v Whether to draw link names on the profile's label axis.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setShowLinkLabels(bool v);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] v Whether to place node IDs above the structure rims.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setInlineNodeLabels(bool v);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] o Node/link label orientation policy; raw enum edits are not validated.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setLabelOrientation(LabelOrientation o);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] deg Diagonal label angle in degrees; typed edits clamp to [1,89].
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setLabelAngleDeg(int deg);
    /**
     * @brief Apply a combined chainage/distance preset through mode/precision setters.
     * @param[in] f Shared preset index; unsupported indices fall back to two decimals. Existing custom printf text is retained and can still override it.
     * @note Mode and count are updated sequentially and can each emit changed(); this is not an atomic one-notification edit.
     */
    void setXAxisNumberFormat(AxisNumberFormat f);
    /**
     * @brief Apply a combined elevation/value preset through mode/precision setters.
     * @param[in] f Shared preset index; unsupported indices fall back to two decimals. Existing custom printf text is retained and can still override it.
     * @note Mode and count are updated sequentially and can each emit changed(); this is not an atomic one-notification edit.
     */
    void setYAxisNumberFormat(AxisNumberFormat f);
    /**
     * @brief Store the chainage/distance numeric-format family.
     * @param[in] m Raw mode enum, including shared NumberFormatMode values beyond a limited local mirror; no enum validation occurs.
     * @note Equality is a no-op; otherwise caches and emits changed(). Does not clear a custom printf override.
     */
    void setXLabelFormatMode(LabelFormatMode m);
    /**
     * @brief Store the chainage/distance label digit count.
     * @param[in] count Requested precision clamped to [0,10]; SignificantFigures rendering subsequently floors zero to one.
     * @note Equality after clamping is a no-op; otherwise stores and emits changed(), without clearing custom text.
     */
    void setXLabelPrecision(int count);
    /**
     * @brief Store an optional chainage/distance printf override.
     * @param[in] spec Text copied verbatim; empty/invalid strings fall back to mode/count during NumberFormat rendering.
     * @note Equality is a no-op; otherwise emits changed(). Invalid syntax is retained rather than rejected.
     */
    void setXLabelFormat(const QString &spec);
    /**
     * @brief Store the elevation/value numeric-format family.
     * @param[in] m Raw mode enum, including shared NumberFormatMode values beyond a limited local mirror; no enum validation occurs.
     * @note Equality is a no-op; otherwise caches and emits changed(). Does not clear a custom printf override.
     */
    void setYLabelFormatMode(LabelFormatMode m);
    /**
     * @brief Store the elevation/value label digit count.
     * @param[in] count Requested precision clamped to [0,10]; SignificantFigures rendering subsequently floors zero to one.
     * @note Equality after clamping is a no-op; otherwise stores and emits changed(), without clearing custom text.
     */
    void setYLabelPrecision(int count);
    /**
     * @brief Store an optional elevation/value printf override.
     * @param[in] spec Text copied verbatim; empty/invalid strings fall back to mode/count during NumberFormat rendering.
     * @note Equality is a no-op; otherwise emits changed(). Invalid syntax is retained rather than rejected.
     */
    void setYLabelFormat(const QString &spec);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] s Ground sampling policy Auto/NodeRims/Mesh2D/TerrainDEM; the host resolves availability and resampling. Typed edits do not load terrain or validate enum membership.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setGroundSource(GroundSource s);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] v Whether to draw available active 2D water-surface overlay samples; it does not load a source.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setShow2DInundation(bool v);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] p 2D water-surface outline pen, including logical-pixel width, dash/cap/join and colour.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setInundation2DLinePen(const QPen &p);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] b 2D inundation fill brush with its alpha/pattern retained.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setInundation2DFillBrush(const QBrush &b);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] v Whether off-path incident links are rendered as truncated connections at structures.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setShowBranchStubs(bool v);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] v Whether to draw plan-direction roses from the recorded incident-link bearings.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setShowNodeRoses(bool v);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] v Whether to draw the animated surcharge/flooding wedges above node rims.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setShowFloodingGlyphs(bool v);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] r Flooding wedge radius in logical pixels; typed edits clamp to [4,60], expecting finite input.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setFloodRadiusPx(double r);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] deg Flooding wedge angular sweep in degrees; typed edits clamp to [5,180].
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setFloodSweepDeg(int deg);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] c Flooding wedge fill colour including alpha; the painter derives its outline. No typed validity check occurs.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setFloodColor(const QColor &c);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] c Junction glyph/interior fill colour including alpha; no typed colour-validity check occurs.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setJunctionFill(const QColor &c);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] c Junction outline colour, kept independently from complete outline-pen properties where those exist; no automatic field synchronization occurs.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setJunctionOutline(const QColor &c);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] c Outfall glyph/interior fill colour including alpha; no typed colour-validity check occurs.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setOutfallFill(const QColor &c);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] c Outfall outline colour, kept independently from complete outline-pen properties where those exist; no automatic field synchronization occurs.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setOutfallOutline(const QColor &c);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] c Storage glyph/interior fill colour including alpha; no typed colour-validity check occurs.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setStorageFill(const QColor &c);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] c Storage outline colour, kept independently from complete outline-pen properties where those exist; no automatic field synchronization occurs.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setStorageOutline(const QColor &c);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] c Divider glyph/interior fill colour including alpha; no typed colour-validity check occurs.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setDividerFill(const QColor &c);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] c Divider outline colour, kept independently from complete outline-pen properties where those exist; no automatic field synchronization occurs.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setDividerOutline(const QColor &c);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] p Computational-break marker pen, with logical-pixel width and custom dash/cap/join retained.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setVirtualJunctionOutlinePen(const QPen &p);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] c Conduit glyph/interior fill colour including alpha; no typed colour-validity check occurs.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setConduitFill(const QColor &c);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] c Conduit outline colour, kept independently from complete outline-pen properties where those exist; no automatic field synchronization occurs.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setConduitOutline(const QColor &c);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] c Pump glyph/interior fill colour including alpha; no typed colour-validity check occurs.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setPumpFill(const QColor &c);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] c Pump outline colour, kept independently from complete outline-pen properties where those exist; no automatic field synchronization occurs.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setPumpOutline(const QColor &c);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] c Weir glyph/interior fill colour including alpha; no typed colour-validity check occurs.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setWeirFill(const QColor &c);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] c Weir outline colour, kept independently from complete outline-pen properties where those exist; no automatic field synchronization occurs.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setWeirOutline(const QColor &c);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] c Orifice glyph/interior fill colour including alpha; no typed colour-validity check occurs.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setOrificeFill(const QColor &c);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] c Orifice outline colour, kept independently from complete outline-pen properties where those exist; no automatic field synchronization occurs.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setOrificeOutline(const QColor &c);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] c Outlet glyph/interior fill colour including alpha; no typed colour-validity check occurs.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setOutletFill(const QColor &c);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] c Outlet outline colour, kept independently from complete outline-pen properties where those exist; no automatic field synchronization occurs.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setOutletOutline(const QColor &c);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] c Soil polygon colour including alpha.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setSoilFill(const QColor &c);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] c Conduit bedding polygon colour including alpha.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setBeddingFill(const QColor &c);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] p Current hydraulic-grade stroke pen, with logical-pixel width and complete dash/cap/join properties.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setHglLinePen(const QPen &p);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] b Current hydraulic-grade fill brush including alpha/pattern.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setHglFillBrush(const QBrush &b);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] p Current energy-grade stroke pen, independent of the HGL fill.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setEglLinePen(const QPen &p);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] p Maximum hydraulic-grade outline pen in logical pixels.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setMaxHglLinePen(const QPen &p);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] b Maximum hydraulic-grade band brush including alpha/pattern.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setMaxHglFillBrush(const QBrush &b);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] p Maximum energy-grade stroke pen in logical pixels.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setMaxEglLinePen(const QPen &p);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] p Conduit complete outline pen in logical pixels, independent of the separate outline-colour cache.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setConduitOutlinePen(const QPen &p);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] p Orifice complete outline pen in logical pixels, independent of the separate outline-colour cache.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setOrificeOutlinePen(const QPen &p);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] p Weir complete outline pen in logical pixels, independent of the separate outline-colour cache.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setWeirOutlinePen(const QPen &p);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] p Pump complete outline pen in logical pixels, independent of the separate outline-colour cache.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setPumpOutlinePen(const QPen &p);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] p Outlet complete outline pen in logical pixels, independent of the separate outline-colour cache.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setOutletOutlinePen(const QPen &p);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] b Pavement band brush beneath STREET conduit gutter geometry, including pattern/alpha.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setStreetInvertBrush(const QBrush &b);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] v Whether the in-plot legend is requested; does not change source/series visibility.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setLegendVisible(bool v);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] p Legend corner anchor; raw enum values are stored without validation by the typed setter.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setLegendPosition(LegendPosition p);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] f Legend text font with Qt point/pixel-size semantics.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setLegendFont(const QFont &f);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] a Legend background opacity, with typed edits clamped to [0,1]; finite input is expected.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setLegendOpacity(double a);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] p Logical-pixel XY displacement from the chosen corner anchor, with positive X right and positive Y down.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setLegendOffset(const QPointF &p);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] v Whether the timestamp overlay is requested; does not affect the animation cursor.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setShowTimeLabel(bool v);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] p Timestamp corner anchor, stored as a raw enum by typed edits.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setTimeLabelPosition(TimeLabelPosition p);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] c Timestamp foreground colour including alpha; typed edits do not reject invalid colours.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setTimeLabelColor(const QColor &c);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] f Timestamp font with Qt point/pixel-size semantics.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setTimeLabelFont(const QFont &f);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] fmt QDateTime format pattern copied verbatim, independent of numeric axis formatting.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setTimeLabelFormat(const QString &fmt);
    /**
     * @brief Update this plot's rendering preference.
     * @param[in] p Logical-pixel XY timestamp displacement from its anchor, positive right/down.
     * @note Equality after any stated clamp is a no-op; otherwise caches the value and emits changed() once. Pens/brushes/colours are copied without additional validity/range normalization. No source data, chart ownership or global preferences are changed.
     */
    void setTimeLabelOffset(const QPointF &p);

signals:
    /**
     * @brief Notify connected views that this plot's option cache changed.
     * @note Typed effective edits emit once per field; combined presets can emit twice. No external geometry/result/palette edits are monitored by this options object.
     */
    void changed();

private:
    // Visibility defaults match the pre-refactor LayerToggles defaults.
    bool m_currentHglLine = true;
    bool m_currentHglFill = true;
    bool m_currentEgl = true;
    bool m_maxHglBand = false;
    bool m_maxHglLine = false;
    bool m_maxEglLine = false;
    bool m_showNodeLabels = false;
    bool m_showLinkLabels = false;
    bool m_inlineNodeLabels = false;
    LabelOrientation m_labelOrientation = Vertical;
    int m_labelAngleDeg = 45;
    // Axis number format — seeded from the global Preferences default in the
    // constructor (hence no in-class initializer values relied upon here).
    LabelFormatMode m_xLabelMode = Decimals;
    int m_xLabelPrecision = 0;
    QString m_xLabelFormatStr; // optional printf override; empty = use mode+precision
    LabelFormatMode m_yLabelMode = Decimals;
    int m_yLabelPrecision = 2;
    QString m_yLabelFormatStr; // optional printf override; empty = use mode+precision
    GroundSource m_groundSource = Auto;
    // On by default: the overlay is inert without an active 2D results
    // layer, and a coupled model's user expects the surface water shown.
    bool m_show2DInundation = true;
    // Teal, distinct from the HGL blues so the two water surfaces read apart.
    QPen m_inundation2DLinePen = QPen(QColor(0x00, 0x8B, 0x8B), 1.6, Qt::SolidLine);
    QBrush m_inundation2DFillBrush{QColor(0x20, 0xB2, 0xAA, 90), Qt::SolidPattern};
    bool m_showBranchStubs = true;
    bool m_showNodeRoses = true;

    // Flooding-glyph defaults: a 60° wedge with a 15 px radius reads at
    // small zoom levels without crowding adjacent rim glyphs.  Colour is
    // a saturated warning red; outline is auto-derived as a 60 %-darker
    // tint of the fill so a single user setting keeps the glyph coherent.
    bool m_showFloodingGlyphs = true;
    double m_floodRadiusPx = 15.0;
    int m_floodSweepDeg = 60;
    QColor m_floodColor{0xE5, 0x21, 0x21};

    // Theming defaults match the pre-refactor hardcoded helpers in
    // profileplotwidget.cpp.
    QColor m_junctionFill{0xCF, 0xE2, 0xF3};
    QColor m_junctionOutline{0x1C, 0x4E, 0x7A};
    QColor m_outfallFill{0xF4, 0xCC, 0xCC};
    QColor m_outfallOutline{0x8A, 0x21, 0x21};
    QColor m_storageFill{0xD9, 0xEA, 0xD3};
    QColor m_storageOutline{0x2D, 0x6A, 0x2D};
    QColor m_dividerFill{0xFF, 0xE5, 0x99};
    QColor m_dividerOutline{0x9C, 0x6F, 0x14};
    // Dash pattern / cap / join are set in the constructor — QPen needs the
    // calls.  Dark grey like the conduit outline it overlaps, but thinner and
    // dashed so it reads as a marker rather than a structure.
    QPen m_virtualJunctionOutlinePen = QPen(QColor(0x33, 0x33, 0x33), 1.0, Qt::CustomDashLine);
    QColor m_conduitFill{0xEE, 0xEE, 0xEE};
    QColor m_conduitOutline{0x33, 0x33, 0x33};
    QColor m_pumpFill{0xFF, 0xD6, 0xA5};
    QColor m_pumpOutline{0xC2, 0x70, 0x1A};
    QColor m_weirFill{0x5D, 0x40, 0x37};    // dark brown (masonry block)
    QColor m_weirOutline{0x3E, 0x2A, 0x1E}; // very dark brown
    QColor m_orificeFill{0xC9, 0xE4, 0xCA};
    QColor m_orificeOutline{0x2E, 0x6E, 0x39};
    QColor m_outletFill{0xF5, 0xC2, 0xC7};
    QColor m_outletOutline{0xA3, 0x3D, 0x4C};
    QColor m_soilFill{0xC6, 0xA9, 0x7A, 130};
    QColor m_beddingFill{0x9C, 0x82, 0x5A, 110};
    // Pen defaults — solid for HGL (the dominant curve), long-dash for EGL
    // (set in the constructor since QPen needs a dash pattern call), dashed
    // for max-envelope outlines (matches the typical max-envelope
    // convention).  Conduit outline is solid dark grey.
    QPen m_hglLinePen = QPen(QColor(0x1F, 0x6F, 0xB7), 2.2, Qt::SolidLine);
    QBrush m_hglFillBrush{QColor(0x55, 0xA8, 0xE6, 110), Qt::SolidPattern};
    QPen m_eglLinePen = QPen(QColor(0x1F, 0x6F, 0xB7), 1.6, Qt::CustomDashLine);
    QPen m_maxHglLinePen = QPen(QColor(0x1F, 0x6F, 0xB7), 1.54, Qt::DashLine);
    QBrush m_maxHglFillBrush{QColor(0x55, 0xA8, 0xE6, 80), Qt::SolidPattern};
    QPen m_maxEglLinePen = QPen(QColor(0x1F, 0x6F, 0xB7), 1.12, Qt::DashLine);
    QPen m_conduitOutlinePen = QPen(QColor(0x33, 0x33, 0x33), 1.5, Qt::SolidLine);
    QPen m_orificeOutlinePen = QPen(QColor(0x2E, 0x6E, 0x39), 1.5, Qt::SolidLine);
    QPen m_weirOutlinePen = QPen(QColor(0x3E, 0x2A, 0x1E), 1.5, Qt::SolidLine);
    QPen m_pumpOutlinePen = QPen(QColor(0xC2, 0x70, 0x1A), 1.2, Qt::SolidLine);
    QPen m_outletOutlinePen = QPen(QColor(0xA3, 0x3D, 0x4C), 1.2, Qt::SolidLine);
    QBrush m_streetInvertBrush{QColor(0x55, 0x55, 0x55), Qt::BDiagPattern};

    // Legend defaults.
    bool m_legendVisible = true;
    LegendPosition m_legendPosition = TopRight;
    QFont m_legendFont; // initialised from QApplication font
    double m_legendOpacity = 0.86;
    QPointF m_legendOffset{0.0, 0.0};

    // Timestamp overlay defaults.  Anchor in TopLeft by default so the chip
    // doesn't overlap the legend (which anchors top-right).  User can drag
    // either overlay; the offset accumulates relative to the anchor.
    bool m_showTimeLabel = true;
    TimeLabelPosition m_timeLabelPosition = TimeTopLeft;
    QColor m_timeLabelColor{0x10, 0x10, 0x10};
    QFont m_timeLabelFont; // initialised from QApplication font
    QString m_timeLabelFormat = QStringLiteral("dd-MMM-yyyy HH:mm:ss");
    QPointF m_timeLabelOffset{0.0, 0.0};
};

#endif // PROFILE_PLOT_OPTIONS_H
