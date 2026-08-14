/*!
 * \file   profileplotoptions.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "plot/profileplotoptions.h"

#include "core/preferencesmanager.h"

#include <QFontDatabase>
#include <QHash>

#define SET_PRIM(field, value)                                              \
    do { if (field != (value)) { field = (value); emit changed(); } } while (0)

#define SET_OBJ(field, value)                                               \
    do { if (!(field == (value))) { field = (value); emit changed(); } } while (0)

ProfilePlotOptions::ProfilePlotOptions(QObject *parent)
    : QObject(parent),
      m_legendFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont)),
      m_timeLabelFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont))
{
    // EGL default style is a 12/6 long-dash so it stays distinguishable
    // from the solid HGL when both layers are visible without depending
    // on colour.  Pattern set here rather than in the header so the
    // dash sequence stays explicit and easy to tweak.
    m_eglLinePen.setDashPattern({12.0, 6.0});
    m_eglLinePen.setCapStyle(Qt::FlatCap);

    // Inherit the global default axis precision; per-plot edits override it.
    auto *prefs = PreferencesManager::instance();
    m_xLabelMode      = static_cast<LabelFormatMode>(prefs->plotXAxisFormatMode());
    m_xLabelPrecision = prefs->plotXAxisPrecision();
    m_yLabelMode      = static_cast<LabelFormatMode>(prefs->plotYAxisFormatMode());
    m_yLabelPrecision = prefs->plotYAxisPrecision();
}

QString ProfilePlotOptions::displayLabelFor(const QString &propertyName) const
{
    // Maps Q_PROPERTY identifiers to the human-readable labels shown in
    // the Display Options tree (column 0).  Built once and shared.
    static const QHash<QString, QString> kLabels = {
        // Visibility
        { QStringLiteral("currentHglLine"),   QObject::tr("Current HGL line") },
        { QStringLiteral("currentHglFill"),   QObject::tr("Current HGL fill") },
        { QStringLiteral("currentEgl"),       QObject::tr("Current EGL") },
        { QStringLiteral("maxHglBand"),       QObject::tr("Max HGL band") },
        { QStringLiteral("maxHglLine"),       QObject::tr("Max HGL line") },
        { QStringLiteral("maxEglLine"),       QObject::tr("Max EGL line") },
        // Labels
        { QStringLiteral("showNodeLabels"),   QObject::tr("Show node names (top axis)") },
        { QStringLiteral("showLinkLabels"),   QObject::tr("Show link names (top axis)") },
        { QStringLiteral("inlineNodeLabels"), QObject::tr("Inline node IDs (over rim)") },
        { QStringLiteral("labelOrientation"), QObject::tr("Label orientation") },
        { QStringLiteral("labelAngleDeg"),    QObject::tr("Label angle (°)") },
        // Axis number format
        { QStringLiteral("xAxisNumberFormat"), QObject::tr("X Axis — Number format") },
        { QStringLiteral("xLabelFormat"),      QObject::tr("X Axis — Custom format") },
        { QStringLiteral("yAxisNumberFormat"), QObject::tr("Y Axis — Number format") },
        { QStringLiteral("yLabelFormat"),      QObject::tr("Y Axis — Custom format") },
        // Ground
        { QStringLiteral("useTerrainGround"), QObject::tr("Use terrain DEM for ground") },
        // Flooding indicator
        { QStringLiteral("floodRadiusPx"),    QObject::tr("Flooding glyph radius (px)") },
        { QStringLiteral("floodSweepDeg"),    QObject::tr("Flooding glyph sweep angle (°)") },
        { QStringLiteral("floodColor"),       QObject::tr("Flooding glyph colour") },
        // Theme — nodes
        { QStringLiteral("junctionFill"),     QObject::tr("Junction fill") },
        { QStringLiteral("junctionOutline"),  QObject::tr("Junction outline") },
        { QStringLiteral("outfallFill"),      QObject::tr("Outfall fill") },
        { QStringLiteral("outfallOutline"),   QObject::tr("Outfall outline") },
        { QStringLiteral("storageFill"),      QObject::tr("Storage fill") },
        { QStringLiteral("storageOutline"),   QObject::tr("Storage outline") },
        { QStringLiteral("dividerFill"),      QObject::tr("Divider fill") },
        { QStringLiteral("dividerOutline"),   QObject::tr("Divider outline") },
        // Theme — links
        { QStringLiteral("conduitFill"),      QObject::tr("Conduit fill") },
        { QStringLiteral("conduitOutline"),   QObject::tr("Conduit outline") },
        { QStringLiteral("pumpFill"),         QObject::tr("Pump fill") },
        { QStringLiteral("pumpOutline"),      QObject::tr("Pump outline") },
        { QStringLiteral("weirFill"),         QObject::tr("Weir fill") },
        { QStringLiteral("weirOutline"),      QObject::tr("Weir outline") },
        { QStringLiteral("orificeFill"),      QObject::tr("Orifice fill") },
        { QStringLiteral("orificeOutline"),   QObject::tr("Orifice outline") },
        { QStringLiteral("outletFill"),       QObject::tr("Outlet fill") },
        { QStringLiteral("outletOutline"),    QObject::tr("Outlet outline") },
        // Theme — soil + lines
        { QStringLiteral("soilFill"),         QObject::tr("Soil fill") },
        { QStringLiteral("beddingFill"),      QObject::tr("Bedding fill") },
        { QStringLiteral("hglLinePen"),       QObject::tr("HGL line pen") },
        { QStringLiteral("hglFillBrush"),     QObject::tr("HGL fill brush") },
        { QStringLiteral("eglLinePen"),       QObject::tr("EGL line pen") },
        { QStringLiteral("maxHglLinePen"),    QObject::tr("Max HGL line pen") },
        { QStringLiteral("maxHglFillBrush"),  QObject::tr("Max HGL fill brush") },
        { QStringLiteral("maxEglLinePen"),    QObject::tr("Max EGL line pen") },
        { QStringLiteral("conduitOutlinePen"),QObject::tr("Conduit outline pen") },
        { QStringLiteral("orificeOutlinePen"),QObject::tr("Orifice outline pen") },
        { QStringLiteral("weirOutlinePen"),   QObject::tr("Weir outline pen") },
        { QStringLiteral("pumpOutlinePen"),   QObject::tr("Pump outline pen") },
        { QStringLiteral("outletOutlinePen"), QObject::tr("Outlet outline pen") },
        // Legend
        { QStringLiteral("legendVisible"),    QObject::tr("Show legend") },
        { QStringLiteral("legendPosition"),   QObject::tr("Legend position") },
        { QStringLiteral("legendFont"),       QObject::tr("Legend font") },
        { QStringLiteral("legendOpacity"),    QObject::tr("Legend opacity") },
        { QStringLiteral("legendOffset"),     QObject::tr("Legend offset (px)") },
        // Timestamp overlay
        { QStringLiteral("showTimeLabel"),    QObject::tr("Show timestamp") },
        { QStringLiteral("timeLabelPosition"),QObject::tr("Timestamp position") },
        { QStringLiteral("timeLabelColor"),   QObject::tr("Timestamp color") },
        { QStringLiteral("timeLabelFont"),    QObject::tr("Timestamp font") },
        { QStringLiteral("timeLabelFormat"),  QObject::tr("Timestamp format") },
        { QStringLiteral("timeLabelOffset"),  QObject::tr("Timestamp offset (px)") },
    };
    return kLabels.value(propertyName, propertyName);
}

// ── Visibility ──────────────────────────────────────────────────────────
void ProfilePlotOptions::setCurrentHglLine(bool v)     { SET_PRIM(m_currentHglLine, v); }
void ProfilePlotOptions::setCurrentHglFill(bool v)     { SET_PRIM(m_currentHglFill, v); }
void ProfilePlotOptions::setCurrentEgl(bool v)         { SET_PRIM(m_currentEgl, v); }
void ProfilePlotOptions::setMaxHglBand(bool v)         { SET_PRIM(m_maxHglBand, v); }
void ProfilePlotOptions::setMaxHglLine(bool v)         { SET_PRIM(m_maxHglLine, v); }
void ProfilePlotOptions::setMaxEglLine(bool v)         { SET_PRIM(m_maxEglLine, v); }

// ── Labels ──────────────────────────────────────────────────────────────
void ProfilePlotOptions::setShowNodeLabels  (bool v)   { SET_PRIM(m_showNodeLabels, v); }
void ProfilePlotOptions::setShowLinkLabels  (bool v)   { SET_PRIM(m_showLinkLabels, v); }
void ProfilePlotOptions::setInlineNodeLabels(bool v)   { SET_PRIM(m_inlineNodeLabels, v); }
void ProfilePlotOptions::setLabelOrientation(LabelOrientation o) { SET_PRIM(m_labelOrientation, o); }
void ProfilePlotOptions::setLabelAngleDeg   (int deg)  {
    deg = std::clamp(deg, 1, 89);
    SET_PRIM(m_labelAngleDeg, deg);
}
void ProfilePlotOptions::setXLabelFormatMode(LabelFormatMode m) { SET_PRIM(m_xLabelMode, m); }
ProfilePlotOptions::AxisNumberFormat ProfilePlotOptions::xAxisNumberFormat() const
{
    return static_cast<AxisNumberFormat>(openswmmvis::plot::presetForNumberFormat(
        static_cast<openswmmvis::plot::NumberFormatMode>(m_xLabelMode),
        m_xLabelPrecision));
}

ProfilePlotOptions::AxisNumberFormat ProfilePlotOptions::yAxisNumberFormat() const
{
    return static_cast<AxisNumberFormat>(openswmmvis::plot::presetForNumberFormat(
        static_cast<openswmmvis::plot::NumberFormatMode>(m_yLabelMode),
        m_yLabelPrecision));
}

void ProfilePlotOptions::setXAxisNumberFormat(AxisNumberFormat f)
{
    // One user-visible choice drives both stored fields; the mode/count pair
    // stays the internal representation every label formatter already reads.
    const auto nf = openswmmvis::plot::numberFormatForPreset(static_cast<int>(f));
    setXLabelFormatMode(static_cast<LabelFormatMode>(nf.mode));
    setXLabelPrecision(nf.count);
}

void ProfilePlotOptions::setYAxisNumberFormat(AxisNumberFormat f)
{
    const auto nf = openswmmvis::plot::numberFormatForPreset(static_cast<int>(f));
    setYLabelFormatMode(static_cast<LabelFormatMode>(nf.mode));
    setYLabelPrecision(nf.count);
}

void ProfilePlotOptions::setXLabelPrecision (int count) {
    const int c = std::clamp(count, 0, 10);
    SET_PRIM(m_xLabelPrecision, c);
}
void ProfilePlotOptions::setXLabelFormat    (const QString &spec) { SET_OBJ(m_xLabelFormatStr, spec); }
void ProfilePlotOptions::setYLabelFormatMode(LabelFormatMode m) { SET_PRIM(m_yLabelMode, m); }
void ProfilePlotOptions::setYLabelPrecision (int count) {
    const int c = std::clamp(count, 0, 10);
    SET_PRIM(m_yLabelPrecision, c);
}
void ProfilePlotOptions::setYLabelFormat    (const QString &spec) { SET_OBJ(m_yLabelFormatStr, spec); }
void ProfilePlotOptions::setUseTerrainGround(bool v)   { SET_PRIM(m_useTerrainGround, v); }
void ProfilePlotOptions::setFloodRadiusPx (double r) {
    r = std::clamp(r, 4.0, 60.0);
    SET_PRIM(m_floodRadiusPx, r);
}
void ProfilePlotOptions::setFloodSweepDeg (int deg) {
    deg = std::clamp(deg, 5, 180);
    SET_PRIM(m_floodSweepDeg, deg);
}
void ProfilePlotOptions::setFloodColor    (const QColor &c) { SET_OBJ(m_floodColor, c); }

// ── Theme colours (one-liners for each setter) ──────────────────────────
void ProfilePlotOptions::setJunctionFill   (const QColor &c) { SET_OBJ(m_junctionFill, c); }
void ProfilePlotOptions::setJunctionOutline(const QColor &c) { SET_OBJ(m_junctionOutline, c); }
void ProfilePlotOptions::setOutfallFill    (const QColor &c) { SET_OBJ(m_outfallFill, c); }
void ProfilePlotOptions::setOutfallOutline (const QColor &c) { SET_OBJ(m_outfallOutline, c); }
void ProfilePlotOptions::setStorageFill    (const QColor &c) { SET_OBJ(m_storageFill, c); }
void ProfilePlotOptions::setStorageOutline (const QColor &c) { SET_OBJ(m_storageOutline, c); }
void ProfilePlotOptions::setDividerFill    (const QColor &c) { SET_OBJ(m_dividerFill, c); }
void ProfilePlotOptions::setDividerOutline (const QColor &c) { SET_OBJ(m_dividerOutline, c); }
void ProfilePlotOptions::setConduitFill    (const QColor &c) { SET_OBJ(m_conduitFill, c); }
void ProfilePlotOptions::setConduitOutline (const QColor &c) { SET_OBJ(m_conduitOutline, c); }
void ProfilePlotOptions::setPumpFill       (const QColor &c) { SET_OBJ(m_pumpFill, c); }
void ProfilePlotOptions::setPumpOutline    (const QColor &c) { SET_OBJ(m_pumpOutline, c); }
void ProfilePlotOptions::setWeirFill       (const QColor &c) { SET_OBJ(m_weirFill, c); }
void ProfilePlotOptions::setWeirOutline    (const QColor &c) { SET_OBJ(m_weirOutline, c); }
void ProfilePlotOptions::setOrificeFill    (const QColor &c) { SET_OBJ(m_orificeFill, c); }
void ProfilePlotOptions::setOrificeOutline (const QColor &c) { SET_OBJ(m_orificeOutline, c); }
void ProfilePlotOptions::setOutletFill     (const QColor &c) { SET_OBJ(m_outletFill, c); }
void ProfilePlotOptions::setOutletOutline  (const QColor &c) { SET_OBJ(m_outletOutline, c); }
void ProfilePlotOptions::setSoilFill       (const QColor &c) { SET_OBJ(m_soilFill, c); }
void ProfilePlotOptions::setBeddingFill    (const QColor &c) { SET_OBJ(m_beddingFill, c); }
void ProfilePlotOptions::setHglLinePen     (const QPen &p)   { SET_OBJ(m_hglLinePen, p); }
void ProfilePlotOptions::setHglFillBrush   (const QBrush &b) { SET_OBJ(m_hglFillBrush, b); }
void ProfilePlotOptions::setEglLinePen     (const QPen &p)   { SET_OBJ(m_eglLinePen, p); }
void ProfilePlotOptions::setMaxHglLinePen  (const QPen &p)   { SET_OBJ(m_maxHglLinePen, p); }
void ProfilePlotOptions::setMaxHglFillBrush(const QBrush &b) { SET_OBJ(m_maxHglFillBrush, b); }
void ProfilePlotOptions::setMaxEglLinePen  (const QPen &p)   { SET_OBJ(m_maxEglLinePen, p); }
void ProfilePlotOptions::setConduitOutlinePen(const QPen &p) { SET_OBJ(m_conduitOutlinePen, p); }
void ProfilePlotOptions::setOrificeOutlinePen(const QPen &p) { SET_OBJ(m_orificeOutlinePen, p); }
void ProfilePlotOptions::setWeirOutlinePen   (const QPen &p) { SET_OBJ(m_weirOutlinePen,    p); }
void ProfilePlotOptions::setPumpOutlinePen   (const QPen &p) { SET_OBJ(m_pumpOutlinePen,    p); }
void ProfilePlotOptions::setOutletOutlinePen (const QPen &p) { SET_OBJ(m_outletOutlinePen,  p); }

// ── Legend ──────────────────────────────────────────────────────────────
void ProfilePlotOptions::setLegendVisible (bool v)             { SET_PRIM(m_legendVisible, v); }
void ProfilePlotOptions::setLegendPosition(LegendPosition p)   { SET_PRIM(m_legendPosition, p); }
void ProfilePlotOptions::setLegendFont    (const QFont &f)     { SET_OBJ(m_legendFont, f); }
void ProfilePlotOptions::setLegendOpacity (double a) {
    a = std::clamp(a, 0.0, 1.0);
    SET_PRIM(m_legendOpacity, a);
}
void ProfilePlotOptions::setLegendOffset  (const QPointF &p)     { SET_OBJ(m_legendOffset, p); }

// ── Timestamp overlay ───────────────────────────────────────────────────
void ProfilePlotOptions::setShowTimeLabel    (bool v)               { SET_PRIM(m_showTimeLabel, v); }
void ProfilePlotOptions::setTimeLabelPosition(TimeLabelPosition p)  { SET_PRIM(m_timeLabelPosition, p); }
void ProfilePlotOptions::setTimeLabelColor   (const QColor &c)      { SET_OBJ(m_timeLabelColor, c); }
void ProfilePlotOptions::setTimeLabelFont    (const QFont &f)       { SET_OBJ(m_timeLabelFont, f); }
void ProfilePlotOptions::setTimeLabelFormat  (const QString &fmt)   { SET_OBJ(m_timeLabelFormat, fmt); }
void ProfilePlotOptions::setTimeLabelOffset  (const QPointF &p)     { SET_OBJ(m_timeLabelOffset, p); }
