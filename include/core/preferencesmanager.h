/*!
 * \file   preferencesmanager.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice V — Settings & Preferences infrastructure.
 *
 * Typed, signal-driven accessor for user preferences. Two-level scope:
 *   - App scope: persisted to QSettings, per-user / per-installation.
 *   - Project scope: overrides attached to the active project (planned
 *     for Phase 12's .oswp serializer; in-memory only today).
 *
 * Live binding: every setter emits `preferenceChanged(group, key)` so
 * the Select tool, the batched renderer's LOD, the project window's
 * default tool, etc. refresh without restart.
 */

#ifndef PREFERENCESMANAGER_H
#define PREFERENCESMANAGER_H

#include <QColor>
#include <QObject>
#include <QSettings>
#include <QString>

class PreferencesManager : public QObject
{
    Q_OBJECT

public:
    /*! Process-wide singleton. Constructed on first access; reads
     *  current values from QSettings at construction so app-scope
     *  keys are immediately available to early callers. */
    static PreferencesManager *instance();

    // ── Selection (MapToolSelect) ────────────────────────────────────────
    /*! Pixel-tolerance floor for click-picking. Effective tolerance at
     *  pick time is `max(clickTolerancePx, markerFloor + 4 px halo)`,
     *  so clicks inside the visible glyph always hit regardless of
     *  this value. Default 16. */
    [[nodiscard]] int  clickTolerancePx() const;
    void setClickTolerancePx(int pixels);

    /*! Drag threshold in pixels before a click turns into a rubber-band
     *  rectangle select. Default 8. */
    [[nodiscard]] int  dragThresholdPx() const;
    void setDragThresholdPx(int pixels);

    /*! When the user clicks in empty space (no hit-target), clear the
     *  current selection. Default true. */
    [[nodiscard]] bool clearSelectionOnMiss() const;
    void setClearSelectionOnMiss(bool on);

    /*! Highlight color for selected features of a given vector class.
     *  \p className is one of "link", "node", "subcatchment", "gage"
     *  (case-insensitive). Unknown classes return Qt::yellow.
     *
     *  Stored under SWMMVis/Preferences/Selection/Color/<Class>. */
    [[nodiscard]] QColor selectionColor(const QString &className) const;
    void setSelectionColor(const QString &className, const QColor &color);

    // ── Canvas / Default tool ────────────────────────────────────────────
    /*! Default tool active when a project opens. One of
     *  "Select" / "Pan" / "Zoom". Default "Select". */
    [[nodiscard]] QString defaultTool() const;
    void setDefaultTool(const QString &tool);

    /*! Default CRS authority (e.g. "EPSG") + code (e.g. 4326) used
     *  when the loaded .inp has no CRS. Defaults to EPSG:4326 (WGS 84). */
    [[nodiscard]] QString defaultCrsAuthority() const;
    [[nodiscard]] int     defaultCrsCode()      const;
    void setDefaultCrsAuthority(const QString &authority);
    void setDefaultCrsCode(int code);

    // ── Rendering / Map LOD ──────────────────────────────────────────────
    /*! Minimum view-transform `m11()` required for label drawing in
     *  the batched renderer. Below this the label pass is skipped
     *  entirely. Default 0.5. */
    [[nodiscard]] qreal labelLodM11Min() const;
    void setLabelLodM11Min(qreal m11);

    /*! Base rendering color for a link subtype.
     *  
     *  \\p linkType is one of: "conduit", "pump", "orifice", "weir", "outlet"
     *  (case-insensitive). Unknown keys fall back to the conduit default.
     *
     *  Stored under SWMMVis/Preferences/Rendering/LinkColor/<Type>. */
    [[nodiscard]] QColor linkColor(const QString &linkType) const;
    void setLinkColor(const QString &linkType, const QColor &color);

    // ── Simulation ───────────────────────────────────────────────────────
    /*! Progress-tick interval (ms) for live UI updates while a
     *  simulation runs. 1 Hz by default — short enough to feel live,
     *  long enough to not starve the event loop on small models. */
    [[nodiscard]] int progressTickMs() const;
    void setProgressTickMs(int ms);

    // ── Map Decorations / Scale Bar ──────────────────────────────────────
    [[nodiscard]] QColor  scaleBarPenColor()     const;
    void setScaleBarPenColor(const QColor &color);

    [[nodiscard]] int     scaleBarPenWidth()     const;  ///< Default 2; range 1–20
    void setScaleBarPenWidth(int width);

    [[nodiscard]] int     scaleBarPenStyle()     const;  ///< Qt::PenStyle as int; default Qt::SolidLine
    void setScaleBarPenStyle(int style);

    [[nodiscard]] QString scaleBarFontFamily()   const;  ///< Default "sans-serif"
    void setScaleBarFontFamily(const QString &family);

    [[nodiscard]] int     scaleBarFontSize()     const;  ///< Default 8; range 4–72
    void setScaleBarFontSize(int size);

    [[nodiscard]] int     scaleBarUnits()        const;  ///< ScaleBarSettings::Units as int; default 0 (Auto)
    void setScaleBarUnits(int units);

    [[nodiscard]] int     scaleBarPosition()     const;  ///< ScaleBarSettings::Position as int; default 0 (BottomLeft)
    void setScaleBarPosition(int position);

    [[nodiscard]] int     scaleBarMaxBarLength() const;  ///< Default 100; range 20–500
    void setScaleBarMaxBarLength(int length);

    [[nodiscard]] int  scaleBarLabelDecimals()   const;  ///< -1=auto, 0=whole, n=n decimals. Default -1.
    void setScaleBarLabelDecimals(int decimals);

    [[nodiscard]] bool scaleBarCompactNotation() const;  ///< Default false.
    void setScaleBarCompactNotation(bool compact);

    // ── Map Decorations / Measure Tool ───────────────────────────────────
    [[nodiscard]] QColor  measureLineColor()   const;  ///< Segment + vertex dot color. Default Qt::red
    void setMeasureLineColor(const QColor &color);

    [[nodiscard]] QString measureLabelFontFamily() const;  ///< Default "sans-serif"
    void setMeasureLabelFontFamily(const QString &family);

    [[nodiscard]] int     measureLabelFontSize()   const;  ///< Default 8; range 4–72
    void setMeasureLabelFontSize(int size);

    [[nodiscard]] int     measureLabelDecimals()   const;  ///< Decimal places in labels. Default 2; range 0–6
    void setMeasureLabelDecimals(int decimals);

    [[nodiscard]] QColor  measureFillColor()   const;  ///< Area polygon fill base color. Default #6495ED (cornflower blue)
    void setMeasureFillColor(const QColor &color);

    [[nodiscard]] int     measureFillOpacity() const;  ///< Area fill opacity 0–100 %. Default 30
    void setMeasureFillOpacity(int opacity);

    // ── Element naming prefixes ──────────────────────────────────────────
    /*! Name prefix used when auto-generating IDs for newly placed SWMM objects.
     *  \p kind is one of: "junction", "outfall", "storage", "divider",
     *  "conduit", "pump", "orifice", "weir", "outlet", "raingage", "subcatchment".
     *  Unknown kinds return the \p kind string itself as a safe fallback.
     *
     *  Defaults: junction→"J", outfall→"O", storage→"S", divider→"D",
     *            conduit→"C", pump→"Pu", orifice→"Or", weir→"W",
     *            outlet→"Ou", raingage→"RG", subcatchment→"Sub". */
    [[nodiscard]] QString elementNamePrefix(const QString &kind) const;
    void setElementNamePrefix(const QString &kind, const QString &prefix);

signals:
    /*! Emitted after any successful set*. `group` is one of
     *  "Selection" / "Canvas" / "Rendering" / "Simulation" /
     *  "Decorations" / "Output"; `key` identifies the specific setting. */
    void preferenceChanged(const QString &group, const QString &key);

private:
    explicit PreferencesManager(QObject *parent = nullptr);

    QSettings m_settings;
};

#endif // PREFERENCESMANAGER_H
