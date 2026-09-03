/*!
 * \file   profileplotoptions.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Q_OBJECT facade for every user-configurable display option on
 *         the profile plot.  Both the per-dialog Layer Panel (quick
 *         visibility toggles) and the QPropertyModel-backed Display
 *         Options dialog (theming / legend / labels) edit the same
 *         instance, and the plot widget reads from it directly — so any
 *         setter call propagates to every view through the single
 *         `changed()` signal.
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
    enum LabelFormatMode { Decimals = 0, SignificantFigures = 1,
                           Scientific = 2, Engineering = 3, Thousands = 4 };
    Q_ENUM(LabelFormatMode)

    /*! Combined axis number format offered to the user as ONE dropdown.
     *  Mirrors openswmmvis::plot::AxisNumberFormatPreset value-for-value —
     *  QPropertyModel resolves a property's enumerator list through the
     *  declaring class's meta-object, so the list has to be declared here,
     *  and it labels each row with the enumerator's own name. The mapping to
     *  a real mode + digit count lives in numberformat.h. */
    enum AxisNumberFormat {
        Integer   = 0,
        Decimals1 = 1,
        Decimals2 = 2,
        Decimals3 = 3,
        Decimals4 = 4,
        Decimals6 = 5,
        SigFigs3  = 6,
        SigFigs4  = 7,
        SigFigs6  = 8,
        Scientific2      = 9,
        Scientific3      = 10,
        Scientific4      = 11,
        Engineering2     = 12,
        Engineering3     = 13,
        ThousandsInteger = 14,
        Thousands1       = 15,
        Thousands2       = 16
    };
    Q_ENUM(AxisNumberFormat)

    enum LegendPosition   { TopRight = 0, TopLeft = 1,
                            BottomLeft = 2, BottomRight = 3 };
    Q_ENUM(LegendPosition)

    enum TimeLabelPosition { TimeTopRight = 0, TimeTopLeft = 1,
                             TimeBottomLeft = 2, TimeBottomRight = 3 };
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
    Q_PROPERTY(bool currentHglLine   READ currentHglLine   WRITE setCurrentHglLine   NOTIFY changed)
    Q_PROPERTY(bool currentHglFill   READ currentHglFill   WRITE setCurrentHglFill   NOTIFY changed)
    Q_PROPERTY(bool currentEgl       READ currentEgl       WRITE setCurrentEgl       NOTIFY changed)
    Q_PROPERTY(bool maxHglBand       READ maxHglBand       WRITE setMaxHglBand       NOTIFY changed)
    Q_PROPERTY(bool maxHglLine       READ maxHglLine       WRITE setMaxHglLine       NOTIFY changed)
    // EGL has no fill — only a line toggle.
    Q_PROPERTY(bool maxEglLine       READ maxEglLine       WRITE setMaxEglLine       NOTIFY changed)

    // ── Labels ──────────────────────────────────────────────────────────
    Q_PROPERTY(bool showNodeLabels   READ showNodeLabels   WRITE setShowNodeLabels   NOTIFY changed)
    Q_PROPERTY(bool showLinkLabels   READ showLinkLabels   WRITE setShowLinkLabels   NOTIFY changed)
    Q_PROPERTY(bool inlineNodeLabels READ inlineNodeLabels WRITE setInlineNodeLabels NOTIFY changed)
    Q_PROPERTY(ProfilePlotOptions::LabelOrientation labelOrientation
               READ labelOrientation WRITE setLabelOrientation NOTIFY changed)
    Q_PROPERTY(int labelAngleDeg     READ labelAngleDeg    WRITE setLabelAngleDeg    NOTIFY changed)

    // ── Axis number format ──────────────────────────────────────────────
    // X axis = distance/chainage; Y axis = elevation/value. Seeded from the
    // global Preferences default; edits here override per-plot.
    Q_PROPERTY(ProfilePlotOptions::AxisNumberFormat xAxisNumberFormat
               READ xAxisNumberFormat WRITE setXAxisNumberFormat NOTIFY changed)
    Q_PROPERTY(QString xLabelFormat
               READ xLabelFormat     WRITE setXLabelFormat     NOTIFY changed)
    Q_PROPERTY(ProfilePlotOptions::AxisNumberFormat yAxisNumberFormat
               READ yAxisNumberFormat WRITE setYAxisNumberFormat NOTIFY changed)
    Q_PROPERTY(QString yLabelFormat
               READ yLabelFormat     WRITE setYLabelFormat     NOTIFY changed)

    // ── Ground line source ──────────────────────────────────────────────
    // Where the soil's top edge comes from. Auto (default) = the 2D mesh
    // vertex elevations interpolated at the path stations whenever the
    // project has a mesh layer, else the node rims (invert + max depth).
    Q_PROPERTY(ProfilePlotOptions::GroundSource groundSource
               READ groundSource WRITE setGroundSource NOTIFY changed)

    // ── 2D inundation overlay ───────────────────────────────────────────
    // Water surface of the active 2D results layer sampled along the path
    // (mesh bed + interpolated depth), drawn as a band above the ground with
    // a WSE line, animated with the profile cursor. Nothing draws when the
    // project has no active 2D results layer.
    Q_PROPERTY(bool   show2DInundation   READ show2DInundation   WRITE setShow2DInundation   NOTIFY changed)
    Q_PROPERTY(QPen   inundation2DLinePen   READ inundation2DLinePen   WRITE setInundation2DLinePen   NOTIFY changed)
    Q_PROPERTY(QBrush inundation2DFillBrush READ inundation2DFillBrush WRITE setInundation2DFillBrush NOTIFY changed)

    // ── Node connectivity ───────────────────────────────────────────────
    // What ELSE meets each node on the path: truncated stubs of the links
    // the profile does not follow, and a small plan rose giving every
    // connected link's map bearing and flow direction.
    Q_PROPERTY(bool showBranchStubs  READ showBranchStubs  WRITE setShowBranchStubs  NOTIFY changed)
    Q_PROPERTY(bool showNodeRoses    READ showNodeRoses    WRITE setShowNodeRoses    NOTIFY changed)

    // ── Flooding indicator (animated wedge above the rim) ───────────────
    Q_PROPERTY(double floodRadiusPx  READ floodRadiusPx   WRITE setFloodRadiusPx   NOTIFY changed)
    Q_PROPERTY(int    floodSweepDeg  READ floodSweepDeg   WRITE setFloodSweepDeg   NOTIFY changed)
    Q_PROPERTY(QColor floodColor     READ floodColor      WRITE setFloodColor      NOTIFY changed)

    // ── Theming: node-type colours ──────────────────────────────────────
    Q_PROPERTY(QColor junctionFill    READ junctionFill    WRITE setJunctionFill    NOTIFY changed)
    Q_PROPERTY(QColor junctionOutline READ junctionOutline WRITE setJunctionOutline NOTIFY changed)
    Q_PROPERTY(QColor outfallFill     READ outfallFill     WRITE setOutfallFill     NOTIFY changed)
    Q_PROPERTY(QColor outfallOutline  READ outfallOutline  WRITE setOutfallOutline  NOTIFY changed)
    Q_PROPERTY(QColor storageFill     READ storageFill     WRITE setStorageFill     NOTIFY changed)
    Q_PROPERTY(QColor storageOutline  READ storageOutline  WRITE setStorageOutline  NOTIFY changed)
    Q_PROPERTY(QColor dividerFill     READ dividerFill     WRITE setDividerFill     NOTIFY changed)
    Q_PROPERTY(QColor dividerOutline  READ dividerOutline  WRITE setDividerOutline  NOTIFY changed)
    // A virtual junction is a break point inside a pipe, not a structure: it
    // draws as a dashed outline rectangle over the conduit running through
    // it, with no fill and no rim glyph.  One pen therefore carries its whole
    // appearance — colour, width, style and dash pattern — instead of the
    // fill/outline colour pair the physical node kinds use.
    Q_PROPERTY(QPen   virtualJunctionOutlinePen READ virtualJunctionOutlinePen
               WRITE setVirtualJunctionOutlinePen NOTIFY changed)

    // ── Theming: link-type colours ──────────────────────────────────────
    Q_PROPERTY(QColor conduitFill     READ conduitFill     WRITE setConduitFill     NOTIFY changed)
    Q_PROPERTY(QColor conduitOutline  READ conduitOutline  WRITE setConduitOutline  NOTIFY changed)
    Q_PROPERTY(QColor pumpFill        READ pumpFill        WRITE setPumpFill        NOTIFY changed)
    Q_PROPERTY(QColor pumpOutline     READ pumpOutline     WRITE setPumpOutline     NOTIFY changed)
    Q_PROPERTY(QColor weirFill        READ weirFill        WRITE setWeirFill        NOTIFY changed)
    Q_PROPERTY(QColor weirOutline     READ weirOutline     WRITE setWeirOutline     NOTIFY changed)
    Q_PROPERTY(QColor orificeFill     READ orificeFill     WRITE setOrificeFill     NOTIFY changed)
    Q_PROPERTY(QColor orificeOutline  READ orificeOutline  WRITE setOrificeOutline  NOTIFY changed)
    Q_PROPERTY(QColor outletFill      READ outletFill      WRITE setOutletFill      NOTIFY changed)
    Q_PROPERTY(QColor outletOutline   READ outletOutline   WRITE setOutletOutline   NOTIFY changed)

    // ── Theming: soil + lines ───────────────────────────────────────────
    Q_PROPERTY(QColor soilFill        READ soilFill        WRITE setSoilFill        NOTIFY changed)
    Q_PROPERTY(QColor beddingFill     READ beddingFill     WRITE setBeddingFill     NOTIFY changed)
    // Line pens carry colour, width, style (solid / dashed / dotted) and
    // a custom dash pattern — all editable via the property tree's QPen
    // editor (qpenpropertyitem) which exposes those fields as expandable
    // children.  Fill brushes stay QBrush since gradients/textures matter
    // for fills.
    Q_PROPERTY(QPen   hglLinePen        READ hglLinePen        WRITE setHglLinePen        NOTIFY changed)
    Q_PROPERTY(QBrush hglFillBrush      READ hglFillBrush      WRITE setHglFillBrush      NOTIFY changed)
    Q_PROPERTY(QPen   eglLinePen        READ eglLinePen        WRITE setEglLinePen        NOTIFY changed)
    // EGL has no fill brush: the velocity-head band above the HGL is not a
    // physically meaningful filled region. EGL renders as a line only.
    Q_PROPERTY(QPen   maxHglLinePen     READ maxHglLinePen     WRITE setMaxHglLinePen     NOTIFY changed)
    Q_PROPERTY(QBrush maxHglFillBrush   READ maxHglFillBrush   WRITE setMaxHglFillBrush   NOTIFY changed)
    Q_PROPERTY(QPen   maxEglLinePen     READ maxEglLinePen     WRITE setMaxEglLinePen     NOTIFY changed)
    Q_PROPERTY(QPen   conduitOutlinePen READ conduitOutlinePen WRITE setConduitOutlinePen NOTIFY changed)
    Q_PROPERTY(QPen   orificeOutlinePen READ orificeOutlinePen WRITE setOrificeOutlinePen NOTIFY changed)
    Q_PROPERTY(QPen   weirOutlinePen    READ weirOutlinePen    WRITE setWeirOutlinePen    NOTIFY changed)
    Q_PROPERTY(QPen   pumpOutlinePen    READ pumpOutlinePen    WRITE setPumpOutlinePen    NOTIFY changed)
    Q_PROPERTY(QPen   outletOutlinePen  READ outletOutlinePen  WRITE setOutletOutlinePen  NOTIFY changed)

    // ── Legend ──────────────────────────────────────────────────────────
    Q_PROPERTY(bool legendVisible    READ legendVisible    WRITE setLegendVisible    NOTIFY changed)
    Q_PROPERTY(ProfilePlotOptions::LegendPosition legendPosition
               READ legendPosition WRITE setLegendPosition NOTIFY changed)
    Q_PROPERTY(QFont legendFont      READ legendFont       WRITE setLegendFont       NOTIFY changed)
    Q_PROPERTY(double legendOpacity  READ legendOpacity    WRITE setLegendOpacity    NOTIFY changed)
    Q_PROPERTY(QPointF legendOffset  READ legendOffset     WRITE setLegendOffset     NOTIFY changed)

    // ── Timestamp overlay ───────────────────────────────────────────────
    Q_PROPERTY(bool showTimeLabel    READ showTimeLabel    WRITE setShowTimeLabel    NOTIFY changed)
    Q_PROPERTY(ProfilePlotOptions::TimeLabelPosition timeLabelPosition
               READ timeLabelPosition WRITE setTimeLabelPosition NOTIFY changed)
    Q_PROPERTY(QColor timeLabelColor READ timeLabelColor   WRITE setTimeLabelColor   NOTIFY changed)
    Q_PROPERTY(QFont  timeLabelFont  READ timeLabelFont    WRITE setTimeLabelFont    NOTIFY changed)
    Q_PROPERTY(QString timeLabelFormat READ timeLabelFormat WRITE setTimeLabelFormat NOTIFY changed)
    Q_PROPERTY(QPointF timeLabelOffset READ timeLabelOffset WRITE setTimeLabelOffset NOTIFY changed)

public:
    explicit ProfilePlotOptions(QObject *parent = nullptr);

    /*!
     * \brief Human-readable label QPropertyModel uses in the row header
     *        instead of the raw Q_PROPERTY name.  Falls back to the
     *        property name when no mapping is registered.
     */
    Q_INVOKABLE QString displayLabelFor(const QString &propertyName) const;

    // ── Getters ─────────────────────────────────────────────────────────
    bool     currentHglLine()   const { return m_currentHglLine; }
    bool     currentHglFill()   const { return m_currentHglFill; }
    bool     currentEgl()       const { return m_currentEgl; }
    bool     maxHglBand()       const { return m_maxHglBand; }
    bool     maxHglLine()       const { return m_maxHglLine; }
    bool     maxEglLine()       const { return m_maxEglLine; }
    bool     showNodeLabels()   const { return m_showNodeLabels; }
    bool     showLinkLabels()   const { return m_showLinkLabels; }
    bool     inlineNodeLabels() const { return m_inlineNodeLabels; }
    LabelOrientation labelOrientation() const { return m_labelOrientation; }
    int      labelAngleDeg()    const { return m_labelAngleDeg; }
    /*! Combined format of each axis, derived from the mode + digit count the
     *  rest of the code still works in. */
    AxisNumberFormat xAxisNumberFormat() const;
    AxisNumberFormat yAxisNumberFormat() const;

    LabelFormatMode xLabelFormatMode() const { return m_xLabelMode; }
    int           xLabelPrecision()  const { return m_xLabelPrecision; }
    QString       xLabelFormat()     const { return m_xLabelFormatStr; }
    LabelFormatMode yLabelFormatMode() const { return m_yLabelMode; }
    int           yLabelPrecision()  const { return m_yLabelPrecision; }
    QString       yLabelFormat()     const { return m_yLabelFormatStr; }
    /*! \brief Current X/Y axis label format as the shared value type. */
    openswmmvis::plot::NumberFormat xFormat() const
    { return { static_cast<openswmmvis::plot::NumberFormatMode>(m_xLabelMode), m_xLabelPrecision, m_xLabelFormatStr }; }
    openswmmvis::plot::NumberFormat yFormat() const
    { return { static_cast<openswmmvis::plot::NumberFormatMode>(m_yLabelMode), m_yLabelPrecision, m_yLabelFormatStr }; }
    GroundSource groundSource() const { return m_groundSource; }
    bool     show2DInundation() const { return m_show2DInundation; }
    QPen     inundation2DLinePen()   const { return m_inundation2DLinePen; }
    QBrush   inundation2DFillBrush() const { return m_inundation2DFillBrush; }
    bool     showBranchStubs()  const { return m_showBranchStubs; }
    bool     showNodeRoses()    const { return m_showNodeRoses; }

    double   floodRadiusPx()    const { return m_floodRadiusPx; }
    int      floodSweepDeg()    const { return m_floodSweepDeg; }
    QColor   floodColor()       const { return m_floodColor; }

    QColor junctionFill()    const { return m_junctionFill; }
    QColor junctionOutline() const { return m_junctionOutline; }
    QColor outfallFill()     const { return m_outfallFill; }
    QColor outfallOutline()  const { return m_outfallOutline; }
    QColor storageFill()     const { return m_storageFill; }
    QColor storageOutline()  const { return m_storageOutline; }
    QColor dividerFill()     const { return m_dividerFill; }
    QColor dividerOutline()  const { return m_dividerOutline; }
    QPen   virtualJunctionOutlinePen() const { return m_virtualJunctionOutlinePen; }
    QColor conduitFill()     const { return m_conduitFill; }
    QColor conduitOutline()  const { return m_conduitOutline; }
    QColor pumpFill()        const { return m_pumpFill; }
    QColor pumpOutline()     const { return m_pumpOutline; }
    QColor weirFill()        const { return m_weirFill; }
    QColor weirOutline()     const { return m_weirOutline; }
    QColor orificeFill()     const { return m_orificeFill; }
    QColor orificeOutline()  const { return m_orificeOutline; }
    QColor outletFill()      const { return m_outletFill; }
    QColor outletOutline()   const { return m_outletOutline; }
    QColor soilFill()        const { return m_soilFill; }
    QColor beddingFill()     const { return m_beddingFill; }
    QPen   hglLinePen()       const { return m_hglLinePen; }
    QBrush hglFillBrush()     const { return m_hglFillBrush; }
    QPen   eglLinePen()       const { return m_eglLinePen; }
    QPen   maxHglLinePen()    const { return m_maxHglLinePen; }
    QBrush maxHglFillBrush()  const { return m_maxHglFillBrush; }
    QPen   maxEglLinePen()    const { return m_maxEglLinePen; }
    QPen   conduitOutlinePen() const { return m_conduitOutlinePen; }
    QPen   orificeOutlinePen() const { return m_orificeOutlinePen; }
    QPen   weirOutlinePen()    const { return m_weirOutlinePen; }
    QPen   pumpOutlinePen()    const { return m_pumpOutlinePen; }
    QPen   outletOutlinePen()  const { return m_outletOutlinePen; }
    bool   legendVisible()   const { return m_legendVisible; }
    LegendPosition legendPosition() const { return m_legendPosition; }
    QFont  legendFont()      const { return m_legendFont; }
    double legendOpacity()   const { return m_legendOpacity; }
    QPointF legendOffset()   const { return m_legendOffset; }

    bool             showTimeLabel()     const { return m_showTimeLabel; }
    TimeLabelPosition timeLabelPosition() const { return m_timeLabelPosition; }
    QColor           timeLabelColor()    const { return m_timeLabelColor; }
    QFont            timeLabelFont()     const { return m_timeLabelFont; }
    QString          timeLabelFormat()   const { return m_timeLabelFormat; }
    QPointF          timeLabelOffset()   const { return m_timeLabelOffset; }

public slots:
    void setCurrentHglLine(bool v);
    void setCurrentHglFill(bool v);
    void setCurrentEgl(bool v);
    void setMaxHglBand(bool v);
    void setMaxHglLine(bool v);
    void setMaxEglLine(bool v);
    void setShowNodeLabels  (bool v);
    void setShowLinkLabels  (bool v);
    void setInlineNodeLabels(bool v);
    void setLabelOrientation(LabelOrientation o);
    void setLabelAngleDeg   (int deg);
    void setXAxisNumberFormat(AxisNumberFormat f);
    void setYAxisNumberFormat(AxisNumberFormat f);
    void setXLabelFormatMode(LabelFormatMode m);
    void setXLabelPrecision (int count);
    void setXLabelFormat    (const QString &spec);
    void setYLabelFormatMode(LabelFormatMode m);
    void setYLabelPrecision (int count);
    void setYLabelFormat    (const QString &spec);
    void setGroundSource(GroundSource s);
    void setShow2DInundation(bool v);
    void setInundation2DLinePen  (const QPen   &p);
    void setInundation2DFillBrush(const QBrush &b);
    void setShowBranchStubs (bool v);
    void setShowNodeRoses   (bool v);
    void setFloodRadiusPx (double r);
    void setFloodSweepDeg (int deg);
    void setFloodColor    (const QColor &c);
    void setJunctionFill   (const QColor &c);
    void setJunctionOutline(const QColor &c);
    void setOutfallFill    (const QColor &c);
    void setOutfallOutline (const QColor &c);
    void setStorageFill    (const QColor &c);
    void setStorageOutline (const QColor &c);
    void setDividerFill    (const QColor &c);
    void setDividerOutline (const QColor &c);
    void setVirtualJunctionOutlinePen(const QPen &p);
    void setConduitFill    (const QColor &c);
    void setConduitOutline (const QColor &c);
    void setPumpFill       (const QColor &c);
    void setPumpOutline    (const QColor &c);
    void setWeirFill       (const QColor &c);
    void setWeirOutline    (const QColor &c);
    void setOrificeFill    (const QColor &c);
    void setOrificeOutline (const QColor &c);
    void setOutletFill     (const QColor &c);
    void setOutletOutline  (const QColor &c);
    void setSoilFill       (const QColor &c);
    void setBeddingFill    (const QColor &c);
    void setHglLinePen     (const QPen &p);
    void setHglFillBrush   (const QBrush &b);
    void setEglLinePen     (const QPen &p);
    void setMaxHglLinePen  (const QPen &p);
    void setMaxHglFillBrush(const QBrush &b);
    void setMaxEglLinePen  (const QPen &p);
    void setConduitOutlinePen(const QPen &p);
    void setOrificeOutlinePen(const QPen &p);
    void setWeirOutlinePen   (const QPen &p);
    void setPumpOutlinePen   (const QPen &p);
    void setOutletOutlinePen (const QPen &p);
    void setLegendVisible  (bool v);
    void setLegendPosition (LegendPosition p);
    void setLegendFont     (const QFont &f);
    void setLegendOpacity  (double a);
    void setLegendOffset   (const QPointF &p);
    void setShowTimeLabel    (bool v);
    void setTimeLabelPosition(TimeLabelPosition p);
    void setTimeLabelColor   (const QColor &c);
    void setTimeLabelFont    (const QFont &f);
    void setTimeLabelFormat  (const QString &fmt);
    void setTimeLabelOffset  (const QPointF &p);

signals:
    void changed();

private:
    // Visibility defaults match the pre-refactor LayerToggles defaults.
    bool             m_currentHglLine   = true;
    bool             m_currentHglFill   = true;
    bool             m_currentEgl       = true;
    bool             m_maxHglBand       = false;
    bool             m_maxHglLine       = false;
    bool             m_maxEglLine       = false;
    bool             m_showNodeLabels   = false;
    bool             m_showLinkLabels   = false;
    bool             m_inlineNodeLabels = false;
    LabelOrientation m_labelOrientation = Vertical;
    int              m_labelAngleDeg    = 45;
    // Axis number format — seeded from the global Preferences default in the
    // constructor (hence no in-class initializer values relied upon here).
    LabelFormatMode  m_xLabelMode       = Decimals;
    int              m_xLabelPrecision  = 0;
    QString          m_xLabelFormatStr;           // optional printf override; empty = use mode+precision
    LabelFormatMode  m_yLabelMode       = Decimals;
    int              m_yLabelPrecision  = 2;
    QString          m_yLabelFormatStr;           // optional printf override; empty = use mode+precision
    GroundSource     m_groundSource     = Auto;
    // On by default: the overlay is inert without an active 2D results
    // layer, and a coupled model's user expects the surface water shown.
    bool             m_show2DInundation = true;
    // Teal, distinct from the HGL blues so the two water surfaces read apart.
    QPen             m_inundation2DLinePen   = QPen(QColor(0x00, 0x8B, 0x8B), 1.6, Qt::SolidLine);
    QBrush           m_inundation2DFillBrush {QColor(0x20, 0xB2, 0xAA, 90), Qt::SolidPattern};
    bool             m_showBranchStubs  = true;
    bool             m_showNodeRoses    = true;

    // Flooding-glyph defaults: a 60° wedge with a 15 px radius reads at
    // small zoom levels without crowding adjacent rim glyphs.  Colour is
    // a saturated warning red; outline is auto-derived as a 60 %-darker
    // tint of the fill so a single user setting keeps the glyph coherent.
    double           m_floodRadiusPx    = 15.0;
    int              m_floodSweepDeg    = 60;
    QColor           m_floodColor       {0xE5, 0x21, 0x21};

    // Theming defaults match the pre-refactor hardcoded helpers in
    // profileplotwidget.cpp.
    QColor m_junctionFill    {0xCF, 0xE2, 0xF3};
    QColor m_junctionOutline {0x1C, 0x4E, 0x7A};
    QColor m_outfallFill     {0xF4, 0xCC, 0xCC};
    QColor m_outfallOutline  {0x8A, 0x21, 0x21};
    QColor m_storageFill     {0xD9, 0xEA, 0xD3};
    QColor m_storageOutline  {0x2D, 0x6A, 0x2D};
    QColor m_dividerFill     {0xFF, 0xE5, 0x99};
    QColor m_dividerOutline  {0x9C, 0x6F, 0x14};
    // Dash pattern / cap / join are set in the constructor — QPen needs the
    // calls.  Dark grey like the conduit outline it overlaps, but thinner and
    // dashed so it reads as a marker rather than a structure.
    QPen   m_virtualJunctionOutlinePen =
        QPen(QColor(0x33, 0x33, 0x33), 1.0, Qt::CustomDashLine);
    QColor m_conduitFill     {0xEE, 0xEE, 0xEE};
    QColor m_conduitOutline  {0x33, 0x33, 0x33};
    QColor m_pumpFill        {0xFF, 0xD6, 0xA5};
    QColor m_pumpOutline     {0xC2, 0x70, 0x1A};
    QColor m_weirFill        {0x5D, 0x40, 0x37};   // dark brown (masonry block)
    QColor m_weirOutline     {0x3E, 0x2A, 0x1E};   // very dark brown
    QColor m_orificeFill     {0xC9, 0xE4, 0xCA};
    QColor m_orificeOutline  {0x2E, 0x6E, 0x39};
    QColor m_outletFill      {0xF5, 0xC2, 0xC7};
    QColor m_outletOutline   {0xA3, 0x3D, 0x4C};
    QColor m_soilFill        {0xC6, 0xA9, 0x7A, 130};
    QColor m_beddingFill     {0x9C, 0x82, 0x5A, 110};
    // Pen defaults — solid for HGL (the dominant curve), long-dash for EGL
    // (set in the constructor since QPen needs a dash pattern call), dashed
    // for max-envelope outlines (matches the typical max-envelope
    // convention).  Conduit outline is solid dark grey.
    QPen   m_hglLinePen      = QPen(QColor(0x1F, 0x6F, 0xB7), 2.2, Qt::SolidLine);
    QBrush m_hglFillBrush    {QColor(0x55, 0xA8, 0xE6, 110), Qt::SolidPattern};
    QPen   m_eglLinePen      = QPen(QColor(0x1F, 0x6F, 0xB7), 1.6, Qt::CustomDashLine);
    QPen   m_maxHglLinePen   = QPen(QColor(0x1F, 0x6F, 0xB7), 1.54, Qt::DashLine);
    QBrush m_maxHglFillBrush {QColor(0x55, 0xA8, 0xE6, 80),  Qt::SolidPattern};
    QPen   m_maxEglLinePen   = QPen(QColor(0x1F, 0x6F, 0xB7), 1.12, Qt::DashLine);
    QPen   m_conduitOutlinePen = QPen(QColor(0x33, 0x33, 0x33), 1.5, Qt::SolidLine);
    QPen   m_orificeOutlinePen = QPen(QColor(0x2E, 0x6E, 0x39), 1.5, Qt::SolidLine);
    QPen   m_weirOutlinePen    = QPen(QColor(0x3E, 0x2A, 0x1E), 1.5, Qt::SolidLine);
    QPen   m_pumpOutlinePen    = QPen(QColor(0xC2, 0x70, 0x1A), 1.2, Qt::SolidLine);
    QPen   m_outletOutlinePen  = QPen(QColor(0xA3, 0x3D, 0x4C), 1.2, Qt::SolidLine);

    // Legend defaults.
    bool           m_legendVisible  = true;
    LegendPosition m_legendPosition = TopRight;
    QFont          m_legendFont;       // initialised from QApplication font
    double         m_legendOpacity   = 0.86;
    QPointF        m_legendOffset    { 0.0, 0.0 };

    // Timestamp overlay defaults.  Anchor in TopLeft by default so the chip
    // doesn't overlap the legend (which anchors top-right).  User can drag
    // either overlay; the offset accumulates relative to the anchor.
    bool              m_showTimeLabel     = true;
    TimeLabelPosition m_timeLabelPosition = TimeTopLeft;
    QColor            m_timeLabelColor    {0x10, 0x10, 0x10};
    QFont             m_timeLabelFont;   // initialised from QApplication font
    QString           m_timeLabelFormat   = QStringLiteral("dd-MMM-yyyy HH:mm:ss");
    QPointF           m_timeLabelOffset   { 0.0, 0.0 };
};

#endif // PROFILE_PLOT_OPTIONS_H
