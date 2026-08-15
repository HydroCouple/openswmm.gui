/*!
 * \file   symbolstyleadapter.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Q_PROPERTY view over a Rule's SingleSymbolRenderer (Slice B.6c).
 *
 *         SymbolStyleAdapter is what makes the Single Symbol case
 *         editable through the Rule Model. The existing
 *         `FeatureStyleEditor` / `SwmmElementSymbolEditor` editors bind
 *         to QObjects via QPropertyModel, but Rule's renderer hands out
 *         a plain `SymbolStyle` struct — not a QObject. This adapter
 *         bridges the gap: it owns a non-owning pointer to a Rule and
 *         exposes the most-common SymbolStyle fields as Q_PROPERTYs.
 *
 *         The adapter watches the Rule's `rendererReplaced` signal so
 *         external swaps (e.g. SymbologyTab class change) refresh the
 *         Q_PROPERTY view automatically.
 *
 *         Property keys mirror the canonical SymbolLayer prop keys from
 *         MarkerSymbolLayerSpec (Z.4) + LineSymbolLayerSpec (Z.5). Edits
 *         are written back to the first SymbolLayer's props map; multi-
 *         layer symbols (composite stacks) still work but only the
 *         bottom layer is exposed to the editor in this slice.
 *
 *         Slice B.6c shipped the read/write seam (this class). Slice SS.1
 *         adds the three archetype-aware adapters
 *         (\ref PointSymbolStyleAdapter / \ref LineSymbolStyleAdapter /
 *         \ref PolygonSymbolStyleAdapter) so the QPropertyModel-driven
 *         editor only surfaces properties that apply to the underlying
 *         geometry. The factory \ref SymbolStyleAdapter::createFor picks
 *         the right archetype subclass by inspecting the Rule's first
 *         SymbolLayer kind. See
 *         docs/RENDERING_SINGLE_SYMBOL_TRANSFER_PLAN.md.
 */

#ifndef OPENSWMM_RENDER_SYMBOLSTYLEADAPTER_H
#define OPENSWMM_RENDER_SYMBOLSTYLEADAPTER_H

#include "render/markershape.h"

#include <QColor>
#include <QFont>
#include <QObject>
#include <QtGlobal>

namespace OpenSWMM::Render
{

class Rule;
class IFeatureRenderer;

class SymbolStyleAdapter : public QObject
{
    Q_OBJECT

    /*! \brief Overall symbol opacity in [0, 1]. */
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity NOTIFY changed)

    /*! \brief Fill colour of the first SymbolLayer's "fillColor" prop
     *         (markers, polygons). Invalid when the layer doesn't carry
     *         a fillColor; writes are no-ops in that case. */
    Q_PROPERTY(QColor fillColor READ fillColor WRITE setFillColor NOTIFY changed)

    /*! \brief Stroke colour ("color" prop on lines, "outlineColor" on
     *         markers / fills). The setter writes both keys to stay
     *         archetype-agnostic. */
    Q_PROPERTY(QColor strokeColor READ strokeColor WRITE setStrokeColor NOTIFY changed)

    /*! \brief Stroke / outline width in pixels. Reads "width" first
     *         (lines), falls back to "outlineWidth" (markers). Writes
     *         both. */
    Q_PROPERTY(qreal strokeWidth READ strokeWidth WRITE setStrokeWidth NOTIFY changed)

    /*! \brief Marker size in pixels. No-op for non-marker symbol layers. */
    Q_PROPERTY(qreal markerSize READ markerSize WRITE setMarkerSize NOTIFY changed)

    /*! \brief Marker shape (Z.4 canonical enum). Typed as MarkerShape so
     *         QPropertyModel surfaces it through a Q_ENUM-aware editor
     *         (the registered MarkerShapeEditor renders the shapes as
     *         icons in a combo). Stored in the SymbolLayer's prop bag as
     *         the underlying int. */
    Q_PROPERTY(OpenSWMM::Render::MarkerShape markerShape
               READ markerShape WRITE setMarkerShape NOTIFY changed)

public:
    /*! \param rule  Non-null. Outlives this adapter. The adapter
     *               watches the Rule's rendererReplaced signal so
     *               external swaps refresh the Q_PROPERTY view. */
    explicit SymbolStyleAdapter(Rule *rule, QObject *parent = nullptr);
    ~SymbolStyleAdapter() override;

    /*! \brief Slice SS.1 factory. Returns a freshly-allocated
     *         archetype-aware adapter (Point / Line / Polygon subclass)
     *         picked from \p rule's first SymbolLayer kind. Falls back
     *         to the generic SymbolStyleAdapter when the rule's symbol
     *         has no layers or the kind doesn't map to one of the three
     *         archetypes (e.g. raster / hillshade / contour passes).
     *
     *         The returned QObject* is owned by \p parent (if non-null)
     *         or by the caller. Concrete type is one of:
     *           - PointSymbolStyleAdapter
     *           - LineSymbolStyleAdapter
     *           - PolygonSymbolStyleAdapter
     *           - SymbolStyleAdapter (generic fallback)
     *         The QPropertyModel-driven editor only sees the properties
     *         declared on the concrete metaobject — "incompatible
     *         properties not visible at all" per
     *         RENDERING_SINGLE_SYMBOL_TRANSFER_PLAN.md §3. */
    [[nodiscard]] static QObject *createFor(Rule *rule, QObject *parent = nullptr);

    [[nodiscard]] qreal       opacity() const;
    [[nodiscard]] QColor      fillColor() const;
    [[nodiscard]] QColor      strokeColor() const;
    [[nodiscard]] qreal       strokeWidth() const;
    [[nodiscard]] qreal       markerSize() const;
    [[nodiscard]] MarkerShape markerShape() const;

    void setOpacity(qreal v);
    void setFillColor(const QColor &c);
    void setStrokeColor(const QColor &c);
    void setStrokeWidth(qreal v);
    void setMarkerSize(qreal v);
    void setMarkerShape(MarkerShape v);

    [[nodiscard]] Rule *rule() const { return m_rule; }

signals:
    /*! Single coalesced signal — any property write fires it. The
     *  QPropertyModel walks the metaobject on each `changed` so per-
     *  property NOTIFY granularity isn't useful here. */
    void changed();

private slots:
    void onRendererReplaced();

private:
    /*! Read a typed value from the first SymbolLayer's props map. */
    template <typename T>
    [[nodiscard]] T readProp(const QString &key, const T &fallback) const;

    /*! Write a value to the first SymbolLayer's props map. Returns
     *  true when the value actually changed; emits `changed` then. */
    template <typename T>
    bool writeProp(const QString &key, const T &value);

    Rule *m_rule = nullptr;
};

// ===========================================================================
// Slice SS.1 — archetype-aware adapters
// ===========================================================================
//
// Each subclass declares ONLY the Q_PROPERTYs that apply to its archetype.
// QPropertyModel walks the concrete metaobject, so the editor sees exactly
// the matrix from RENDERING_SINGLE_SYMBOL_TRANSFER_PLAN.md §3 — no marker
// knobs on lines, no arrow knobs on points.
//
// All three subclasses share the same plumbing: hold a Rule*, watch
// rendererReplaced, read/write canonical prop keys on the Rule's
// SingleSymbolRenderer's first SymbolLayer. Common helpers live in the
// .cpp (free functions in an anonymous namespace) — these classes are
// siblings (not derived from SymbolStyleAdapter) precisely so the base
// metaobject doesn't leak its archetype-agnostic Q_PROPERTYs.

/*!
 * \class PointSymbolStyleAdapter
 * \brief Single-Symbol panel surface for point (marker) archetypes —
 *        Junctions / Outfalls / Storage / Dividers / RainGages on the
 *        SWMM model layer, and the equivalent point categories on the
 *        results layer.
 */
class PointSymbolStyleAdapter : public QObject
{
    Q_OBJECT

    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity NOTIFY changed)
    Q_PROPERTY(OpenSWMM::Render::MarkerShape markerShape
               READ markerShape WRITE setMarkerShape NOTIFY changed)
    Q_PROPERTY(qreal  markerSize   READ markerSize   WRITE setMarkerSize   NOTIFY changed)
    Q_PROPERTY(QColor fillColor    READ fillColor    WRITE setFillColor    NOTIFY changed)
    Q_PROPERTY(QColor outlineColor READ outlineColor WRITE setOutlineColor NOTIFY changed)
    Q_PROPERTY(qreal  outlineWidth READ outlineWidth WRITE setOutlineWidth NOTIFY changed)
    Q_PROPERTY(bool   showLabel    READ showLabel    WRITE setShowLabel    NOTIFY changed)
    Q_PROPERTY(QFont  labelFont    READ labelFont    WRITE setLabelFont    NOTIFY changed)
    Q_PROPERTY(QColor labelColor   READ labelColor   WRITE setLabelColor   NOTIFY changed)

    Q_CLASSINFO("group:opacity",      "Symbology")
    Q_CLASSINFO("group:markerShape",  "Symbology")
    Q_CLASSINFO("group:markerSize",   "Symbology")
    Q_CLASSINFO("group:fillColor",    "Fill")
    Q_CLASSINFO("group:outlineColor", "Outline")
    Q_CLASSINFO("group:outlineWidth", "Outline")
    Q_CLASSINFO("group:showLabel",    "Labels")
    Q_CLASSINFO("group:labelFont",    "Labels")
    Q_CLASSINFO("group:labelColor",   "Labels")

public:
    explicit PointSymbolStyleAdapter(Rule *rule, QObject *parent = nullptr);
    ~PointSymbolStyleAdapter() override;

    [[nodiscard]] qreal       opacity()      const;
    [[nodiscard]] MarkerShape markerShape()  const;
    [[nodiscard]] qreal       markerSize()   const;
    [[nodiscard]] QColor      fillColor()    const;
    [[nodiscard]] QColor      outlineColor() const;
    [[nodiscard]] qreal       outlineWidth() const;
    [[nodiscard]] bool        showLabel()    const;
    [[nodiscard]] QFont       labelFont()    const;
    [[nodiscard]] QColor      labelColor()   const;

    void setOpacity(qreal v);
    void setMarkerShape(MarkerShape v);
    void setMarkerSize(qreal v);
    void setFillColor(const QColor &c);
    void setOutlineColor(const QColor &c);
    void setOutlineWidth(qreal v);
    void setShowLabel(bool v);
    void setLabelFont(const QFont &f);
    void setLabelColor(const QColor &c);

    [[nodiscard]] Rule *rule() const { return m_rule; }

signals:
    void changed();

private slots:
    void onRendererReplaced();

private:
    Rule *m_rule = nullptr;
};

/*!
 * \class LineSymbolStyleAdapter
 * \brief Single-Symbol panel surface for line archetypes — Conduits /
 *        Pumps / Orifices / Weirs / Outlets on the SWMM model layer
 *        and the equivalent link categories on the results layer.
 */
class LineSymbolStyleAdapter : public QObject
{
    Q_OBJECT

    Q_PROPERTY(qreal        opacity      READ opacity      WRITE setOpacity      NOTIFY changed)
    Q_PROPERTY(QColor       lineColor    READ lineColor    WRITE setLineColor    NOTIFY changed)
    Q_PROPERTY(qreal        lineWidth    READ lineWidth    WRITE setLineWidth    NOTIFY changed)
    Q_PROPERTY(Qt::PenStyle dashPattern  READ dashPattern  WRITE setDashPattern  NOTIFY changed)
    Q_PROPERTY(qreal        offsetPx     READ offsetPx     WRITE setOffsetPx     NOTIFY changed)
    Q_PROPERTY(bool         showLabel    READ showLabel    WRITE setShowLabel    NOTIFY changed)
    Q_PROPERTY(QFont        labelFont    READ labelFont    WRITE setLabelFont    NOTIFY changed)
    Q_PROPERTY(QColor       labelColor   READ labelColor   WRITE setLabelColor   NOTIFY changed)
    Q_PROPERTY(bool         showArrows   READ showArrows   WRITE setShowArrows   NOTIFY changed)
    Q_PROPERTY(qreal        arrowSize    READ arrowSize    WRITE setArrowSize    NOTIFY changed)
    Q_PROPERTY(QColor       arrowColor   READ arrowColor   WRITE setArrowColor   NOTIFY changed)
    Q_PROPERTY(bool         arrowOnlyWhenFlowPos READ arrowOnlyWhenFlowPos
               WRITE setArrowOnlyWhenFlowPos NOTIFY changed)

    Q_CLASSINFO("group:opacity",              "Symbology")
    Q_CLASSINFO("group:lineColor",            "Line")
    Q_CLASSINFO("group:lineWidth",            "Line")
    Q_CLASSINFO("group:dashPattern",          "Line")
    Q_CLASSINFO("group:offsetPx",             "Line")
    Q_CLASSINFO("group:showLabel",            "Labels")
    Q_CLASSINFO("group:labelFont",            "Labels")
    Q_CLASSINFO("group:labelColor",           "Labels")
    Q_CLASSINFO("group:showArrows",           "Flow arrows")
    Q_CLASSINFO("group:arrowSize",            "Flow arrows")
    Q_CLASSINFO("group:arrowColor",           "Flow arrows")
    Q_CLASSINFO("group:arrowOnlyWhenFlowPos", "Flow arrows")

public:
    explicit LineSymbolStyleAdapter(Rule *rule, QObject *parent = nullptr);
    ~LineSymbolStyleAdapter() override;

    [[nodiscard]] qreal        opacity()              const;
    [[nodiscard]] QColor       lineColor()            const;
    [[nodiscard]] qreal        lineWidth()            const;
    [[nodiscard]] Qt::PenStyle dashPattern()          const;
    [[nodiscard]] qreal        offsetPx()             const;
    [[nodiscard]] bool         showLabel()            const;
    [[nodiscard]] QFont        labelFont()            const;
    [[nodiscard]] QColor       labelColor()           const;
    [[nodiscard]] bool         showArrows()           const;
    [[nodiscard]] qreal        arrowSize()            const;
    [[nodiscard]] QColor       arrowColor()           const;
    [[nodiscard]] bool         arrowOnlyWhenFlowPos() const;

    void setOpacity(qreal v);
    void setLineColor(const QColor &c);
    void setLineWidth(qreal v);
    void setDashPattern(Qt::PenStyle s);
    void setOffsetPx(qreal v);
    void setShowLabel(bool v);
    void setLabelFont(const QFont &f);
    void setLabelColor(const QColor &c);
    void setShowArrows(bool v);
    void setArrowSize(qreal v);
    void setArrowColor(const QColor &c);
    void setArrowOnlyWhenFlowPos(bool v);

    [[nodiscard]] Rule *rule() const { return m_rule; }

signals:
    void changed();

private slots:
    void onRendererReplaced();

private:
    Rule *m_rule = nullptr;
};

// ===========================================================================
// Slice AN.2 — raster / mesh / glyph archetype adapters (2D results layer)
// ===========================================================================
//
// Same pattern as the Point / Line / Polygon trio above: each subclass
// declares only its 2D-style Q_PROPERTYs so QPropertyModel surfaces
// exactly the editable knobs for that sublayer. Factory dispatch lives
// in createFor (.cpp) and reads SymbolLayer.kind + the "mode" prop for
// the two Contour archetypes (bands vs lines).
//
// Each adapter's properties + groups mirror the matching legacy style
// class (MeshFillStyle / DepthColorRampStyle / ContourBandStyle /
// IsolineStyle / MeshEdgeStyle / MeshNodeStyle / VelocityVectorStyle).
// See docs/RENDERING_2D_RESULTS_STYLING_PLAN.md §3.

/*!
 * \class RasterColorRampSymbolStyleAdapter
 * \brief Depth color-ramp surface — `DepthColorRampStyle`.
 */
class RasterColorRampSymbolStyleAdapter : public QObject
{
    Q_OBJECT

    Q_PROPERTY(qreal   opacity        READ opacity        WRITE setOpacity        NOTIFY changed)
    Q_PROPERTY(QString attribute      READ attribute      WRITE setAttribute      NOTIFY changed)
    Q_PROPERTY(qreal   minValue       READ minValue       WRITE setMinValue       NOTIFY changed)
    Q_PROPERTY(qreal   maxValue       READ maxValue       WRITE setMaxValue       NOTIFY changed)
    Q_PROPERTY(QColor  lowColor       READ lowColor       WRITE setLowColor       NOTIFY changed)
    Q_PROPERTY(QColor  highColor      READ highColor      WRITE setHighColor      NOTIFY changed)
    Q_PROPERTY(QColor  belowMinColor  READ belowMinColor  WRITE setBelowMinColor  NOTIFY changed)
    Q_PROPERTY(QColor  aboveMaxColor  READ aboveMaxColor  WRITE setAboveMaxColor  NOTIFY changed)
    Q_PROPERTY(bool    useLogScale    READ useLogScale    WRITE setUseLogScale    NOTIFY changed)

    Q_CLASSINFO("group:opacity",       "Symbology")
    Q_CLASSINFO("group:attribute",     "Classification")
    Q_CLASSINFO("group:minValue",      "Range")
    Q_CLASSINFO("group:maxValue",      "Range")
    Q_CLASSINFO("group:useLogScale",   "Range")
    Q_CLASSINFO("group:lowColor",      "Ramp")
    Q_CLASSINFO("group:highColor",     "Ramp")
    Q_CLASSINFO("group:belowMinColor", "Out of range")
    Q_CLASSINFO("group:aboveMaxColor", "Out of range")

public:
    explicit RasterColorRampSymbolStyleAdapter(Rule *rule, QObject *parent = nullptr);
    ~RasterColorRampSymbolStyleAdapter() override;

    [[nodiscard]] qreal   opacity()       const;
    [[nodiscard]] QString attribute()     const;
    [[nodiscard]] qreal   minValue()      const;
    [[nodiscard]] qreal   maxValue()      const;
    [[nodiscard]] QColor  lowColor()      const;
    [[nodiscard]] QColor  highColor()     const;
    [[nodiscard]] QColor  belowMinColor() const;
    [[nodiscard]] QColor  aboveMaxColor() const;
    [[nodiscard]] bool    useLogScale()   const;

    void setOpacity(qreal v);
    void setAttribute(const QString &v);
    void setMinValue(qreal v);
    void setMaxValue(qreal v);
    void setLowColor(const QColor &c);
    void setHighColor(const QColor &c);
    void setBelowMinColor(const QColor &c);
    void setAboveMaxColor(const QColor &c);
    void setUseLogScale(bool v);

    [[nodiscard]] Rule *rule() const { return m_rule; }

signals:
    void changed();

private slots:
    void onRendererReplaced();

private:
    Rule *m_rule = nullptr;
};

/*!
 * \class HillshadeSymbolStyleAdapter
 * \brief Mesh-fill / hillshade surface — `MeshFillStyle`.
 */
class HillshadeSymbolStyleAdapter : public QObject
{
    Q_OBJECT

    Q_PROPERTY(qreal  opacity            READ opacity            WRITE setOpacity            NOTIFY changed)
    Q_PROPERTY(QColor fillColor          READ fillColor          WRITE setFillColor          NOTIFY changed)
    Q_PROPERTY(qreal  hillshadeStrength  READ hillshadeStrength  WRITE setHillshadeStrength  NOTIFY changed)
    Q_PROPERTY(bool   useElevationRamp   READ useElevationRamp   WRITE setUseElevationRamp   NOTIFY changed)

    Q_CLASSINFO("group:opacity",           "Symbology")
    Q_CLASSINFO("group:fillColor",         "Fill")
    Q_CLASSINFO("group:useElevationRamp",  "Fill")
    Q_CLASSINFO("group:hillshadeStrength", "Shading")

public:
    explicit HillshadeSymbolStyleAdapter(Rule *rule, QObject *parent = nullptr);
    ~HillshadeSymbolStyleAdapter() override;

    [[nodiscard]] qreal  opacity()           const;
    [[nodiscard]] QColor fillColor()         const;
    [[nodiscard]] qreal  hillshadeStrength() const;
    [[nodiscard]] bool   useElevationRamp()  const;

    void setOpacity(qreal v);
    void setFillColor(const QColor &c);
    void setHillshadeStrength(qreal v);
    void setUseElevationRamp(bool v);

    [[nodiscard]] Rule *rule() const { return m_rule; }

signals:
    void changed();

private slots:
    void onRendererReplaced();

private:
    Rule *m_rule = nullptr;
};

/*!
 * \class ContourBandSymbolStyleAdapter
 * \brief Filled-isoband surface — `ContourBandStyle`.
 */
class ContourBandSymbolStyleAdapter : public QObject
{
    Q_OBJECT

    Q_PROPERTY(qreal   opacity        READ opacity        WRITE setOpacity        NOTIFY changed)
    Q_PROPERTY(QString attribute      READ attribute      WRITE setAttribute      NOTIFY changed)
    Q_PROPERTY(int     bandCount      READ bandCount      WRITE setBandCount      NOTIFY changed)
    Q_PROPERTY(QColor  lowColor       READ lowColor       WRITE setLowColor       NOTIFY changed)
    Q_PROPERTY(QColor  highColor      READ highColor      WRITE setHighColor      NOTIFY changed)
    Q_PROPERTY(QColor  belowMinColor  READ belowMinColor  WRITE setBelowMinColor  NOTIFY changed)
    Q_PROPERTY(QColor  aboveMaxColor  READ aboveMaxColor  WRITE setAboveMaxColor  NOTIFY changed)
    Q_PROPERTY(bool    smoothBands    READ smoothBands    WRITE setSmoothBands    NOTIFY changed)

    Q_CLASSINFO("group:opacity",       "Symbology")
    Q_CLASSINFO("group:attribute",     "Classification")
    Q_CLASSINFO("group:bandCount",     "Classification")
    Q_CLASSINFO("group:lowColor",      "Ramp")
    Q_CLASSINFO("group:highColor",     "Ramp")
    Q_CLASSINFO("group:belowMinColor", "Out of range")
    Q_CLASSINFO("group:aboveMaxColor", "Out of range")
    Q_CLASSINFO("group:smoothBands",   "Rendering")

public:
    explicit ContourBandSymbolStyleAdapter(Rule *rule, QObject *parent = nullptr);
    ~ContourBandSymbolStyleAdapter() override;

    [[nodiscard]] qreal   opacity()       const;
    [[nodiscard]] QString attribute()     const;
    [[nodiscard]] int     bandCount()     const;
    [[nodiscard]] QColor  lowColor()      const;
    [[nodiscard]] QColor  highColor()     const;
    [[nodiscard]] QColor  belowMinColor() const;
    [[nodiscard]] QColor  aboveMaxColor() const;
    [[nodiscard]] bool    smoothBands()   const;

    void setOpacity(qreal v);
    void setAttribute(const QString &v);
    void setBandCount(int v);
    void setLowColor(const QColor &c);
    void setHighColor(const QColor &c);
    void setBelowMinColor(const QColor &c);
    void setAboveMaxColor(const QColor &c);
    void setSmoothBands(bool v);

    [[nodiscard]] Rule *rule() const { return m_rule; }

signals:
    void changed();

private slots:
    void onRendererReplaced();

private:
    Rule *m_rule = nullptr;
};

/*!
 * \class IsolineSymbolStyleAdapter
 * \brief Contour-line surface — `IsolineStyle`.
 */
class IsolineSymbolStyleAdapter : public QObject
{
    Q_OBJECT

    Q_PROPERTY(qreal        opacity        READ opacity        WRITE setOpacity        NOTIFY changed)
    Q_PROPERTY(QString      attribute      READ attribute      WRITE setAttribute      NOTIFY changed)
    Q_PROPERTY(int          isoValueCount  READ isoValueCount  WRITE setIsoValueCount  NOTIFY changed)
    Q_PROPERTY(QColor       color          READ color          WRITE setColor          NOTIFY changed)
    Q_PROPERTY(qreal        lineWidthPx    READ lineWidthPx    WRITE setLineWidthPx    NOTIFY changed)
    Q_PROPERTY(Qt::PenStyle dashPattern    READ dashPattern    WRITE setDashPattern    NOTIFY changed)
    Q_PROPERTY(bool         labels         READ labels         WRITE setLabels         NOTIFY changed)

    Q_CLASSINFO("group:opacity",       "Symbology")
    Q_CLASSINFO("group:attribute",     "Classification")
    Q_CLASSINFO("group:isoValueCount", "Classification")
    Q_CLASSINFO("group:color",         "Symbology")
    Q_CLASSINFO("group:lineWidthPx",   "Symbology")
    Q_CLASSINFO("group:dashPattern",   "Symbology")
    Q_CLASSINFO("group:labels",        "Labels")

public:
    explicit IsolineSymbolStyleAdapter(Rule *rule, QObject *parent = nullptr);
    ~IsolineSymbolStyleAdapter() override;

    [[nodiscard]] qreal        opacity()       const;
    [[nodiscard]] QString      attribute()     const;
    [[nodiscard]] int          isoValueCount() const;
    [[nodiscard]] QColor       color()         const;
    [[nodiscard]] qreal        lineWidthPx()   const;
    [[nodiscard]] Qt::PenStyle dashPattern()   const;
    [[nodiscard]] bool         labels()        const;

    void setOpacity(qreal v);
    void setAttribute(const QString &v);
    void setIsoValueCount(int v);
    void setColor(const QColor &c);
    void setLineWidthPx(qreal v);
    void setDashPattern(Qt::PenStyle s);
    void setLabels(bool v);

    [[nodiscard]] Rule *rule() const { return m_rule; }

signals:
    void changed();

private slots:
    void onRendererReplaced();

private:
    Rule *m_rule = nullptr;
};

/*!
 * \class MeshEdgeSymbolStyleAdapter
 * \brief Mesh-wireframe surface — `MeshEdgeStyle`.
 */
class MeshEdgeSymbolStyleAdapter : public QObject
{
    Q_OBJECT

    Q_PROPERTY(qreal        opacity              READ opacity              WRITE setOpacity              NOTIFY changed)
    Q_PROPERTY(QColor       color                READ color                WRITE setColor                NOTIFY changed)
    Q_PROPERTY(qreal        lineWidthPx          READ lineWidthPx          WRITE setLineWidthPx          NOTIFY changed)
    Q_PROPERTY(Qt::PenStyle dashPattern          READ dashPattern          WRITE setDashPattern          NOTIFY changed)
    Q_PROPERTY(bool         useSlopeDrivenWidth  READ useSlopeDrivenWidth  WRITE setUseSlopeDrivenWidth  NOTIFY changed)
    Q_PROPERTY(qreal        slopeBreak           READ slopeBreak           WRITE setSlopeBreak           NOTIFY changed)
    Q_PROPERTY(qreal        wideWidthPx          READ wideWidthPx          WRITE setWideWidthPx          NOTIFY changed)
    Q_PROPERTY(QColor       wideColor            READ wideColor            WRITE setWideColor            NOTIFY changed)

    Q_CLASSINFO("group:opacity",             "Symbology")
    Q_CLASSINFO("group:color",               "Symbology")
    Q_CLASSINFO("group:lineWidthPx",         "Symbology")
    Q_CLASSINFO("group:dashPattern",         "Symbology")
    Q_CLASSINFO("group:useSlopeDrivenWidth", "Slope emphasis")
    Q_CLASSINFO("group:slopeBreak",          "Slope emphasis")
    Q_CLASSINFO("group:wideWidthPx",         "Slope emphasis")
    Q_CLASSINFO("group:wideColor",           "Slope emphasis")

public:
    explicit MeshEdgeSymbolStyleAdapter(Rule *rule, QObject *parent = nullptr);
    ~MeshEdgeSymbolStyleAdapter() override;

    [[nodiscard]] qreal        opacity()             const;
    [[nodiscard]] QColor       color()               const;
    [[nodiscard]] qreal        lineWidthPx()         const;
    [[nodiscard]] Qt::PenStyle dashPattern()         const;
    [[nodiscard]] bool         useSlopeDrivenWidth() const;
    [[nodiscard]] qreal        slopeBreak()          const;
    [[nodiscard]] qreal        wideWidthPx()         const;
    [[nodiscard]] QColor       wideColor()           const;

    void setOpacity(qreal v);
    void setColor(const QColor &c);
    void setLineWidthPx(qreal v);
    void setDashPattern(Qt::PenStyle s);
    void setUseSlopeDrivenWidth(bool v);
    void setSlopeBreak(qreal v);
    void setWideWidthPx(qreal v);
    void setWideColor(const QColor &c);

    [[nodiscard]] Rule *rule() const { return m_rule; }

signals:
    void changed();

private slots:
    void onRendererReplaced();

private:
    Rule *m_rule = nullptr;
};

/*!
 * \class MeshNodeSymbolStyleAdapter
 * \brief Mesh-vertex marker surface — `MeshNodeStyle`.
 */
class MeshNodeSymbolStyleAdapter : public QObject
{
    Q_OBJECT

    Q_PROPERTY(qreal       opacity         READ opacity         WRITE setOpacity         NOTIFY changed)
    Q_PROPERTY(QColor      color           READ color           WRITE setColor           NOTIFY changed)
    Q_PROPERTY(qreal       markerSizePx    READ markerSizePx    WRITE setMarkerSizePx    NOTIFY changed)
    Q_PROPERTY(OpenSWMM::Render::MarkerShape shape
               READ shape         WRITE setShape         NOTIFY changed)
    Q_PROPERTY(QColor      outlineColor    READ outlineColor    WRITE setOutlineColor    NOTIFY changed)
    Q_PROPERTY(qreal       outlineWidthPx  READ outlineWidthPx  WRITE setOutlineWidthPx  NOTIFY changed)
    Q_PROPERTY(bool        highlightTagged READ highlightTagged WRITE setHighlightTagged NOTIFY changed)
    Q_PROPERTY(QColor      taggedColor     READ taggedColor     WRITE setTaggedColor     NOTIFY changed)
    Q_PROPERTY(qreal       taggedSizePx    READ taggedSizePx    WRITE setTaggedSizePx    NOTIFY changed)

    Q_CLASSINFO("group:opacity",         "Symbology")
    Q_CLASSINFO("group:color",           "Symbology")
    Q_CLASSINFO("group:markerSizePx",    "Symbology")
    Q_CLASSINFO("group:shape",           "Symbology")
    Q_CLASSINFO("group:outlineColor",    "Outline")
    Q_CLASSINFO("group:outlineWidthPx",  "Outline")
    Q_CLASSINFO("group:highlightTagged", "Tagged vertices")
    Q_CLASSINFO("group:taggedColor",     "Tagged vertices")
    Q_CLASSINFO("group:taggedSizePx",    "Tagged vertices")

public:
    explicit MeshNodeSymbolStyleAdapter(Rule *rule, QObject *parent = nullptr);
    ~MeshNodeSymbolStyleAdapter() override;

    [[nodiscard]] qreal       opacity()         const;
    [[nodiscard]] QColor      color()           const;
    [[nodiscard]] qreal       markerSizePx()    const;
    [[nodiscard]] MarkerShape shape()           const;
    [[nodiscard]] QColor      outlineColor()    const;
    [[nodiscard]] qreal       outlineWidthPx()  const;
    [[nodiscard]] bool        highlightTagged() const;
    [[nodiscard]] QColor      taggedColor()     const;
    [[nodiscard]] qreal       taggedSizePx()    const;

    void setOpacity(qreal v);
    void setColor(const QColor &c);
    void setMarkerSizePx(qreal v);
    void setShape(MarkerShape s);
    void setOutlineColor(const QColor &c);
    void setOutlineWidthPx(qreal v);
    void setHighlightTagged(bool v);
    void setTaggedColor(const QColor &c);
    void setTaggedSizePx(qreal v);

    [[nodiscard]] Rule *rule() const { return m_rule; }

signals:
    void changed();

private slots:
    void onRendererReplaced();

private:
    Rule *m_rule = nullptr;
};

/*!
 * \class VelocityVectorSymbolStyleAdapter
 * \brief 2D velocity-arrow surface — `VelocityVectorStyle`.
 */
class VelocityVectorSymbolStyleAdapter : public QObject
{
    Q_OBJECT

    Q_PROPERTY(qreal  opacity                  READ opacity                  WRITE setOpacity                  NOTIFY changed)
    Q_PROPERTY(qreal  glyphLengthScalePxPerMps READ glyphLengthScalePxPerMps WRITE setGlyphLengthScalePxPerMps NOTIFY changed)
    Q_PROPERTY(qreal  glyphLengthMinPx         READ glyphLengthMinPx         WRITE setGlyphLengthMinPx         NOTIFY changed)
    Q_PROPERTY(qreal  glyphLengthMaxPx         READ glyphLengthMaxPx         WRITE setGlyphLengthMaxPx         NOTIFY changed)
    Q_PROPERTY(qreal  glyphSpacingPx           READ glyphSpacingPx           WRITE setGlyphSpacingPx           NOTIFY changed)
    Q_PROPERTY(qreal  headSizePx               READ headSizePx               WRITE setHeadSizePx               NOTIFY changed)
    Q_PROPERTY(QColor color                    READ color                    WRITE setColor                    NOTIFY changed)
    Q_PROPERTY(qreal  dryDepthCutoff           READ dryDepthCutoff           WRITE setDryDepthCutoff           NOTIFY changed)

    Q_CLASSINFO("group:opacity",                  "Symbology")
    Q_CLASSINFO("group:glyphLengthScalePxPerMps", "Glyph")
    Q_CLASSINFO("group:glyphLengthMinPx",         "Glyph")
    Q_CLASSINFO("group:glyphLengthMaxPx",         "Glyph")
    Q_CLASSINFO("group:headSizePx",               "Glyph")
    Q_CLASSINFO("group:color",                    "Glyph")
    Q_CLASSINFO("group:glyphSpacingPx",           "Placement")
    Q_CLASSINFO("group:dryDepthCutoff",           "Filtering")

public:
    explicit VelocityVectorSymbolStyleAdapter(Rule *rule, QObject *parent = nullptr);
    ~VelocityVectorSymbolStyleAdapter() override;

    [[nodiscard]] qreal  opacity()                  const;
    [[nodiscard]] qreal  glyphLengthScalePxPerMps() const;
    [[nodiscard]] qreal  glyphLengthMinPx()         const;
    [[nodiscard]] qreal  glyphLengthMaxPx()         const;
    [[nodiscard]] qreal  glyphSpacingPx()           const;
    [[nodiscard]] qreal  headSizePx()               const;
    [[nodiscard]] QColor color()                    const;
    [[nodiscard]] qreal  dryDepthCutoff()           const;

    void setOpacity(qreal v);
    void setGlyphLengthScalePxPerMps(qreal v);
    void setGlyphLengthMinPx(qreal v);
    void setGlyphLengthMaxPx(qreal v);
    void setGlyphSpacingPx(qreal v);
    void setHeadSizePx(qreal v);
    void setColor(const QColor &c);
    void setDryDepthCutoff(qreal v);

    [[nodiscard]] Rule *rule() const { return m_rule; }

signals:
    void changed();

private slots:
    void onRendererReplaced();

private:
    Rule *m_rule = nullptr;
};

/*!
 * \class PolygonSymbolStyleAdapter
 * \brief Single-Symbol panel surface for polygon archetypes —
 *        Subcatchments on the SWMM model layer and the polygon
 *        categories on the results layer.
 */
class PolygonSymbolStyleAdapter : public QObject
{
    Q_OBJECT

    Q_PROPERTY(qreal  opacity      READ opacity      WRITE setOpacity      NOTIFY changed)
    Q_PROPERTY(QColor fillColor    READ fillColor    WRITE setFillColor    NOTIFY changed)
    Q_PROPERTY(qreal  fillOpacity  READ fillOpacity  WRITE setFillOpacity  NOTIFY changed)
    Q_PROPERTY(QColor outlineColor READ outlineColor WRITE setOutlineColor NOTIFY changed)
    Q_PROPERTY(qreal  outlineWidth READ outlineWidth WRITE setOutlineWidth NOTIFY changed)
    Q_PROPERTY(bool   showLabel    READ showLabel    WRITE setShowLabel    NOTIFY changed)
    Q_PROPERTY(QFont  labelFont    READ labelFont    WRITE setLabelFont    NOTIFY changed)
    Q_PROPERTY(QColor labelColor   READ labelColor   WRITE setLabelColor   NOTIFY changed)

    Q_CLASSINFO("group:opacity",      "Symbology")
    Q_CLASSINFO("group:fillColor",    "Fill")
    Q_CLASSINFO("group:fillOpacity",  "Fill")
    Q_CLASSINFO("group:outlineColor", "Outline")
    Q_CLASSINFO("group:outlineWidth", "Outline")
    Q_CLASSINFO("group:showLabel",    "Labels")
    Q_CLASSINFO("group:labelFont",    "Labels")
    Q_CLASSINFO("group:labelColor",   "Labels")

public:
    explicit PolygonSymbolStyleAdapter(Rule *rule, QObject *parent = nullptr);
    ~PolygonSymbolStyleAdapter() override;

    [[nodiscard]] qreal  opacity()      const;
    [[nodiscard]] QColor fillColor()    const;
    [[nodiscard]] qreal  fillOpacity()  const;
    [[nodiscard]] QColor outlineColor() const;
    [[nodiscard]] qreal  outlineWidth() const;
    [[nodiscard]] bool   showLabel()    const;
    [[nodiscard]] QFont  labelFont()    const;
    [[nodiscard]] QColor labelColor()   const;

    void setOpacity(qreal v);
    void setFillColor(const QColor &c);
    void setFillOpacity(qreal v);
    void setOutlineColor(const QColor &c);
    void setOutlineWidth(qreal v);
    void setShowLabel(bool v);
    void setLabelFont(const QFont &f);
    void setLabelColor(const QColor &c);

    [[nodiscard]] Rule *rule() const { return m_rule; }

signals:
    void changed();

private slots:
    void onRendererReplaced();

private:
    Rule *m_rule = nullptr;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_SYMBOLSTYLEADAPTER_H
