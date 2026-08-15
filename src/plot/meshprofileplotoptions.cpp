/*!
 * \file   meshprofileplotoptions.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */
#include "plot/meshprofileplotoptions.h"

#include "core/preferencesmanager.h"

#include <QFontDatabase>
#include <QHash>

#include <algorithm>

#define SET_PRIM(field, value)                                              \
    do { if (field != (value)) { field = (value); emit changed(); } } while (0)

#define SET_OBJ(field, value)                                               \
    do { if (!(field == (value))) { field = (value); emit changed(); } } while (0)

MeshProfilePlotOptions::MeshProfilePlotOptions(QObject *parent)
    : QObject(parent),
      m_legendFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont)),
      m_timeLabelFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont))
{
    // Inherit the global default axis precision; per-plot edits override it.
    auto *prefs = PreferencesManager::instance();
    m_xLabelMode      = static_cast<LabelFormatMode>(prefs->plotXAxisFormatMode());
    m_xLabelPrecision = prefs->plotXAxisPrecision();
    m_yLabelMode      = static_cast<LabelFormatMode>(prefs->plotYAxisFormatMode());
    m_yLabelPrecision = prefs->plotYAxisPrecision();
}

QString MeshProfilePlotOptions::displayLabelFor(const QString &propertyName) const
{
    static const QHash<QString, QString> kLabels = {
        { QStringLiteral("showDepthFill"),       QObject::tr("Show depth fill") },
        { QStringLiteral("showWseLine"),         QObject::tr("Show water-surface line") },
        { QStringLiteral("showMaxEnvelopeFill"), QObject::tr("Show max-depth band") },
        { QStringLiteral("showMaxEnvelopeLine"), QObject::tr("Show max-depth line") },
        { QStringLiteral("showCellBoundaries"),  QObject::tr("Show cell boundaries") },
        { QStringLiteral("cellBoundaryColor"),   QObject::tr("Cell boundary color") },
        { QStringLiteral("soilFill"),            QObject::tr("Soil fill") },
        { QStringLiteral("groundLinePen"),       QObject::tr("Ground line pen") },
        { QStringLiteral("depthFillBrush"),      QObject::tr("Depth fill brush") },
        { QStringLiteral("wseLinePen"),          QObject::tr("Water-surface line pen") },
        { QStringLiteral("maxEnvelopePen"),      QObject::tr("Max-depth line pen") },
        { QStringLiteral("maxEnvelopeBrush"),    QObject::tr("Max-depth band brush") },
        { QStringLiteral("xLabelFormatMode"),    QObject::tr("X Axis — Number format") },
        { QStringLiteral("xLabelFormat"),        QObject::tr("X Axis — Custom format") },
        { QStringLiteral("yLabelFormatMode"),    QObject::tr("Y Axis — Number format") },
        { QStringLiteral("yLabelFormat"),        QObject::tr("Y Axis — Custom format") },
        { QStringLiteral("legendVisible"),       QObject::tr("Show legend") },
        { QStringLiteral("legendPosition"),      QObject::tr("Legend position") },
        { QStringLiteral("legendFont"),          QObject::tr("Legend font") },
        { QStringLiteral("legendOpacity"),       QObject::tr("Legend opacity") },
        { QStringLiteral("legendOffset"),        QObject::tr("Legend offset (px)") },
        { QStringLiteral("showTimeLabel"),       QObject::tr("Show timestamp") },
        { QStringLiteral("timeLabelPosition"),   QObject::tr("Timestamp position") },
        { QStringLiteral("timeLabelColor"),      QObject::tr("Timestamp color") },
        { QStringLiteral("timeLabelFont"),       QObject::tr("Timestamp font") },
        { QStringLiteral("timeLabelFormat"),     QObject::tr("Timestamp format") },
        { QStringLiteral("timeLabelOffset"),     QObject::tr("Timestamp offset (px)") },
    };
    return kLabels.value(propertyName, propertyName);
}

void MeshProfilePlotOptions::setShowDepthFill(bool v)       { SET_PRIM(m_showDepthFill, v); }
void MeshProfilePlotOptions::setShowWseLine(bool v)         { SET_PRIM(m_showWseLine, v); }
void MeshProfilePlotOptions::setShowMaxEnvelopeFill(bool v) { SET_PRIM(m_showMaxEnvelopeFill, v); }
void MeshProfilePlotOptions::setShowMaxEnvelopeLine(bool v) { SET_PRIM(m_showMaxEnvelopeLine, v); }
void MeshProfilePlotOptions::setShowCellBoundaries(bool v)  { SET_PRIM(m_showCellBoundaries, v); }
void MeshProfilePlotOptions::setCellBoundaryColor(const QColor &c) { SET_OBJ(m_cellBoundaryColor, c); }

void MeshProfilePlotOptions::setSoilFill(const QBrush &b)        { SET_OBJ(m_soilFill, b); }
void MeshProfilePlotOptions::setGroundLinePen(const QPen &p)     { SET_OBJ(m_groundLinePen, p); }
void MeshProfilePlotOptions::setDepthFillBrush(const QBrush &b)  { SET_OBJ(m_depthFillBrush, b); }
void MeshProfilePlotOptions::setWseLinePen(const QPen &p)        { SET_OBJ(m_wseLinePen, p); }
void MeshProfilePlotOptions::setMaxEnvelopePen(const QPen &p)    { SET_OBJ(m_maxEnvelopePen, p); }
void MeshProfilePlotOptions::setMaxEnvelopeBrush(const QBrush &b){ SET_OBJ(m_maxEnvelopeBrush, b); }

MeshProfilePlotOptions::AxisNumberFormat MeshProfilePlotOptions::xAxisNumberFormat() const
{
    return static_cast<AxisNumberFormat>(openswmmvis::plot::presetForNumberFormat(
        static_cast<openswmmvis::plot::NumberFormatMode>(m_xLabelMode), m_xLabelPrecision));
}

MeshProfilePlotOptions::AxisNumberFormat MeshProfilePlotOptions::yAxisNumberFormat() const
{
    return static_cast<AxisNumberFormat>(openswmmvis::plot::presetForNumberFormat(
        static_cast<openswmmvis::plot::NumberFormatMode>(m_yLabelMode), m_yLabelPrecision));
}

void MeshProfilePlotOptions::setXAxisNumberFormat(AxisNumberFormat f)
{
    // One user choice drives both stored fields; mode + count stay the
    // internal representation every label formatter already reads.
    const auto nf = openswmmvis::plot::numberFormatForPreset(static_cast<int>(f));
    setXLabelFormatMode(static_cast<LabelFormatMode>(nf.mode));
    setXLabelPrecision(nf.count);
}

void MeshProfilePlotOptions::setYAxisNumberFormat(AxisNumberFormat f)
{
    const auto nf = openswmmvis::plot::numberFormatForPreset(static_cast<int>(f));
    setYLabelFormatMode(static_cast<LabelFormatMode>(nf.mode));
    setYLabelPrecision(nf.count);
}

void MeshProfilePlotOptions::setXLabelFormatMode(LabelFormatMode m) { SET_PRIM(m_xLabelMode, m); }
void MeshProfilePlotOptions::setXLabelPrecision(int count) {
    const int c = std::clamp(count, 0, 10);
    SET_PRIM(m_xLabelPrecision, c);
}
void MeshProfilePlotOptions::setXLabelFormat(const QString &spec) { SET_OBJ(m_xLabelFormatStr, spec); }
void MeshProfilePlotOptions::setYLabelFormatMode(LabelFormatMode m) { SET_PRIM(m_yLabelMode, m); }
void MeshProfilePlotOptions::setYLabelPrecision(int count) {
    const int c = std::clamp(count, 0, 10);
    SET_PRIM(m_yLabelPrecision, c);
}
void MeshProfilePlotOptions::setYLabelFormat(const QString &spec) { SET_OBJ(m_yLabelFormatStr, spec); }

void MeshProfilePlotOptions::setLegendVisible(bool v)           { SET_PRIM(m_legendVisible, v); }
void MeshProfilePlotOptions::setLegendPosition(LegendPosition p){ SET_PRIM(m_legendPosition, p); }
void MeshProfilePlotOptions::setLegendFont(const QFont &f)      { SET_OBJ(m_legendFont, f); }
void MeshProfilePlotOptions::setLegendOpacity(double a) {
    a = std::clamp(a, 0.0, 1.0);
    SET_PRIM(m_legendOpacity, a);
}
void MeshProfilePlotOptions::setLegendOffset(const QPointF &p)  { SET_OBJ(m_legendOffset, p); }

void MeshProfilePlotOptions::setShowTimeLabel(bool v)                  { SET_PRIM(m_showTimeLabel, v); }
void MeshProfilePlotOptions::setTimeLabelPosition(TimeLabelPosition p) { SET_PRIM(m_timeLabelPosition, p); }
void MeshProfilePlotOptions::setTimeLabelColor(const QColor &c)        { SET_OBJ(m_timeLabelColor, c); }
void MeshProfilePlotOptions::setTimeLabelFont(const QFont &f)          { SET_OBJ(m_timeLabelFont, f); }
void MeshProfilePlotOptions::setTimeLabelFormat(const QString &fmt)    { SET_OBJ(m_timeLabelFormat, fmt); }
void MeshProfilePlotOptions::setTimeLabelOffset(const QPointF &p)      { SET_OBJ(m_timeLabelOffset, p); }
