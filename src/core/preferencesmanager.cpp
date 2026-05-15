/*!
 * \file   preferencesmanager.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "core/preferencesmanager.h"

#include <QCoreApplication>

namespace {

// All keys live under SWMMVis/Preferences/<group>/<key> so a future
// schema migration can wipe the entire Preferences subtree without
// collateral on unrelated QSettings keys.
constexpr const char *kGroupRoot = "SWMMVis/Preferences";

// ── Compiled-in defaults ──────────────────────────────────────────────────
// Centralised so the constructor seeds sensible values before QSettings
// provides any, and the Preferences dialog's "Reset to defaults" button
// can snap back to these.

constexpr int     kDefaultClickTolerancePx      = 16;
// 15 px drag threshold — trackpad micro-jitter on a click routinely
// exceeds 8 px, which flipped the Select tool into rubber-band mode
// and ended up selecting both a node and its connecting link when
// the jitter landed near both. 15 gives a comfortable dead zone
// without making intentional rubber-band selects feel sluggish.
constexpr int     kDefaultDragThresholdPx       = 15;
constexpr bool    kDefaultClearSelectionOnMiss  = true;

constexpr const char *kDefaultTool              = "Select";
constexpr const char *kDefaultCrsAuthority      = "EPSG";
constexpr int     kDefaultCrsCode               = 4326;

constexpr qreal   kDefaultLabelLodM11Min        = 0.5;

const QColor kDefaultConduitColor(50, 50, 200);
const QColor kDefaultPumpColor(Qt::red);
const QColor kDefaultOrificeColor(200, 150, 0);
const QColor kDefaultWeirColor(0, 180, 100);
const QColor kDefaultOutletColor(120, 90, 40);

constexpr int     kDefaultProgressTickMs        = 1000;

// ── MeasureTool defaults ──────────────────────────────────────────────────
const QColor kDefaultMeasureLineColor(Qt::red);
const QColor kDefaultMeasureFillColor(100, 149, 237);  // cornflower blue
constexpr const char *kDefaultMeasureLabelFontFamily = "sans-serif";
constexpr int         kDefaultMeasureLabelFontSize   = 8;
constexpr int         kDefaultMeasureLabelDecimals   = 2;
constexpr int         kDefaultMeasureFillOpacity     = 30;

// ── Element naming prefix defaults ───────────────────────────────────────
struct PrefixDefault { const char *kind; const char *prefix; };
constexpr PrefixDefault kPrefixDefaults[] = {
    { "junction",     "J"   },
    { "outfall",      "O"   },
    { "storage",      "S"   },
    { "divider",      "D"   },
    { "conduit",      "C"   },
    { "pump",         "Pu"  },
    { "orifice",      "Or"  },
    { "weir",         "W"   },
    { "outlet",       "Ou"  },
    { "raingage",     "RG"  },
    { "subcatchment", "Sub" },
};

// ── ScaleBar defaults ─────────────────────────────────────────────────────
constexpr int     kDefaultScaleBarPenWidth     = 2;
constexpr int     kDefaultScaleBarPenStyle     = Qt::SolidLine;
constexpr const char *kDefaultScaleBarFontFamily = "sans-serif";
constexpr int     kDefaultScaleBarFontSize     = 8;
constexpr int     kDefaultScaleBarUnits        = 0;    // ScaleBarSettings::Auto
constexpr int     kDefaultScaleBarPosition     = 0;    // ScaleBarSettings::BottomLeft
constexpr int     kDefaultScaleBarMaxBarLength = 100;
constexpr int     kDefaultScaleBarLabelDecimals   = -1;
constexpr bool    kDefaultScaleBarCompactNotation = false;

} // anonymous

PreferencesManager *PreferencesManager::instance()
{
    // Heap-allocated with QCoreApplication as parent so shutdown order
    // is deterministic. The global QSettings under the hood is
    // thread-safe for read; writes are serialised to the INI file Qt
    // manages for us.
    static PreferencesManager *s_instance = nullptr;
    if (!s_instance) {
        s_instance = new PreferencesManager(QCoreApplication::instance());
    }
    return s_instance;
}

PreferencesManager::PreferencesManager(QObject *parent)
    : QObject(parent)
{
}

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------

int PreferencesManager::clickTolerancePx() const
{
    return m_settings.value(QStringLiteral("%1/Selection/ClickTolerancePx")
                                .arg(kGroupRoot),
                            kDefaultClickTolerancePx).toInt();
}

void PreferencesManager::setClickTolerancePx(int pixels)
{
    if (pixels < 1 || pixels > 200) return;
    if (pixels == clickTolerancePx()) return;
    m_settings.setValue(QStringLiteral("%1/Selection/ClickTolerancePx")
                            .arg(kGroupRoot), pixels);
    emit preferenceChanged(QStringLiteral("Selection"),
                           QStringLiteral("ClickTolerancePx"));
}

int PreferencesManager::dragThresholdPx() const
{
    return m_settings.value(QStringLiteral("%1/Selection/DragThresholdPx")
                                .arg(kGroupRoot),
                            kDefaultDragThresholdPx).toInt();
}

void PreferencesManager::setDragThresholdPx(int pixels)
{
    if (pixels < 1 || pixels > 200) return;
    if (pixels == dragThresholdPx()) return;
    m_settings.setValue(QStringLiteral("%1/Selection/DragThresholdPx")
                            .arg(kGroupRoot), pixels);
    emit preferenceChanged(QStringLiteral("Selection"),
                           QStringLiteral("DragThresholdPx"));
}

bool PreferencesManager::clearSelectionOnMiss() const
{
    return m_settings.value(QStringLiteral("%1/Selection/ClearOnMiss")
                                .arg(kGroupRoot),
                            kDefaultClearSelectionOnMiss).toBool();
}

void PreferencesManager::setClearSelectionOnMiss(bool on)
{
    if (on == clearSelectionOnMiss()) return;
    m_settings.setValue(QStringLiteral("%1/Selection/ClearOnMiss")
                            .arg(kGroupRoot), on);
    emit preferenceChanged(QStringLiteral("Selection"),
                           QStringLiteral("ClearOnMiss"));
}

namespace {
// Normalises whatever the caller hands in to one of the four canonical
// class keys used inside QSettings. Anything unrecognised falls
// through to "Default" so the caller gets a sane color rather than an
// empty value.
QString canonicalSelectionClass(const QString &className)
{
    const QString k = className.trimmed().toLower();
    if (k == QLatin1String("link") || k == QLatin1String("links")
        || k == QLatin1String("conduit") || k == QLatin1String("conduits"))
        return QStringLiteral("Link");
    if (k == QLatin1String("node") || k == QLatin1String("nodes")
        || k == QLatin1String("junction") || k == QLatin1String("junctions"))
        return QStringLiteral("Node");
    if (k == QLatin1String("subcatchment") || k == QLatin1String("subcatchments")
        || k == QLatin1String("catchment")  || k == QLatin1String("catchments")
        || k == QLatin1String("polygon"))
        return QStringLiteral("Subcatchment");
    if (k == QLatin1String("gage") || k == QLatin1String("gages")
        || k == QLatin1String("raingage") || k == QLatin1String("raingages"))
        return QStringLiteral("Gage");
    return QStringLiteral("Default");
}
} // anonymous

QColor PreferencesManager::selectionColor(const QString &className) const
{
    const QString k = canonicalSelectionClass(className);
    const QString key = QStringLiteral("%1/Selection/Color/%2")
                            .arg(QString::fromLatin1(kGroupRoot), k);
    const QVariant v = m_settings.value(key);
    if (v.isValid()) {
        const QColor c = v.value<QColor>();
        if (c.isValid()) return c;
    }
    // Yellow across the board is the conventional "selected" cue. The
    // dialog lets users pick distinct per-class colors when they want
    // to differentiate selections of mixed feature types.
    return QColor(255, 255, 0);
}

void PreferencesManager::setSelectionColor(const QString &className,
                                            const QColor &color)
{
    if (!color.isValid()) return;
    const QString k = canonicalSelectionClass(className);
    if (k == QLatin1String("Default")) return;
    if (selectionColor(className) == color) return;
    const QString key = QStringLiteral("%1/Selection/Color/%2")
                            .arg(QString::fromLatin1(kGroupRoot), k);
    m_settings.setValue(key, color);
    emit preferenceChanged(QStringLiteral("Selection"),
                           QStringLiteral("Color/%1").arg(k));
}

// ---------------------------------------------------------------------------
// Canvas
// ---------------------------------------------------------------------------

QString PreferencesManager::defaultTool() const
{
    return m_settings.value(QStringLiteral("%1/Canvas/DefaultTool")
                                .arg(kGroupRoot),
                            QString::fromLatin1(kDefaultTool)).toString();
}

void PreferencesManager::setDefaultTool(const QString &tool)
{
    if (tool.isEmpty()) return;
    if (tool == defaultTool()) return;
    m_settings.setValue(QStringLiteral("%1/Canvas/DefaultTool")
                            .arg(kGroupRoot), tool);
    emit preferenceChanged(QStringLiteral("Canvas"),
                           QStringLiteral("DefaultTool"));
}

QString PreferencesManager::defaultCrsAuthority() const
{
    return m_settings.value(QStringLiteral("%1/Canvas/DefaultCrsAuthority")
                                .arg(kGroupRoot),
                            QString::fromLatin1(kDefaultCrsAuthority)).toString();
}

int PreferencesManager::defaultCrsCode() const
{
    return m_settings.value(QStringLiteral("%1/Canvas/DefaultCrsCode")
                                .arg(kGroupRoot),
                            kDefaultCrsCode).toInt();
}

void PreferencesManager::setDefaultCrsAuthority(const QString &authority)
{
    if (authority.isEmpty()) return;
    if (authority == defaultCrsAuthority()) return;
    m_settings.setValue(QStringLiteral("%1/Canvas/DefaultCrsAuthority")
                            .arg(kGroupRoot), authority);
    emit preferenceChanged(QStringLiteral("Canvas"),
                           QStringLiteral("DefaultCrsAuthority"));
}

void PreferencesManager::setDefaultCrsCode(int code)
{
    if (code <= 0) return;
    if (code == defaultCrsCode()) return;
    m_settings.setValue(QStringLiteral("%1/Canvas/DefaultCrsCode")
                            .arg(kGroupRoot), code);
    emit preferenceChanged(QStringLiteral("Canvas"),
                           QStringLiteral("DefaultCrsCode"));
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

qreal PreferencesManager::labelLodM11Min() const
{
    return m_settings.value(QStringLiteral("%1/Rendering/LabelLodM11Min")
                                .arg(kGroupRoot),
                            kDefaultLabelLodM11Min).toReal();
}

void PreferencesManager::setLabelLodM11Min(qreal m11)
{
    if (m11 <= 0.0) return;
    if (qFuzzyCompare(m11, labelLodM11Min())) return;
    m_settings.setValue(QStringLiteral("%1/Rendering/LabelLodM11Min")
                            .arg(kGroupRoot), m11);
    emit preferenceChanged(QStringLiteral("Rendering"),
                           QStringLiteral("LabelLodM11Min"));
}

namespace {
QString canonicalLinkType(const QString &linkType)
{
    const QString k = linkType.trimmed().toLower();
    if (k == QLatin1String("pump") || k == QLatin1String("pumps"))
        return QStringLiteral("Pump");
    if (k == QLatin1String("orifice") || k == QLatin1String("orifices"))
        return QStringLiteral("Orifice");
    if (k == QLatin1String("weir") || k == QLatin1String("weirs"))
        return QStringLiteral("Weir");
    if (k == QLatin1String("outlet") || k == QLatin1String("outlets"))
        return QStringLiteral("Outlet");
    return QStringLiteral("Conduit");
}

QColor defaultLinkColorForKey(const QString &canonicalKey)
{
    if (canonicalKey == QLatin1String("Pump"))
        return kDefaultPumpColor;
    if (canonicalKey == QLatin1String("Orifice"))
        return kDefaultOrificeColor;
    if (canonicalKey == QLatin1String("Weir"))
        return kDefaultWeirColor;
    if (canonicalKey == QLatin1String("Outlet"))
        return kDefaultOutletColor;
    return kDefaultConduitColor;
}
} // anonymous

QColor PreferencesManager::linkColor(const QString &linkType) const
{
    const QString k = canonicalLinkType(linkType);
    const QString key = QStringLiteral("%1/Rendering/LinkColor/%2")
                            .arg(QString::fromLatin1(kGroupRoot), k);
    const QVariant v = m_settings.value(key);
    if (v.isValid()) {
        const QColor c = v.value<QColor>();
        if (c.isValid()) return c;
    }
    return defaultLinkColorForKey(k);
}

void PreferencesManager::setLinkColor(const QString &linkType, const QColor &color)
{
    if (!color.isValid()) return;
    const QString k = canonicalLinkType(linkType);
    if (linkColor(k) == color) return;
    const QString key = QStringLiteral("%1/Rendering/LinkColor/%2")
                            .arg(QString::fromLatin1(kGroupRoot), k);
    m_settings.setValue(key, color);
    emit preferenceChanged(QStringLiteral("Rendering"),
                           QStringLiteral("LinkColor/%1").arg(k));
}

// ---------------------------------------------------------------------------
// Simulation
// ---------------------------------------------------------------------------

int PreferencesManager::progressTickMs() const
{
    return m_settings.value(QStringLiteral("%1/Simulation/ProgressTickMs")
                                .arg(kGroupRoot),
                            kDefaultProgressTickMs).toInt();
}

void PreferencesManager::setProgressTickMs(int ms)
{
    if (ms < 50 || ms > 10000) return;
    if (ms == progressTickMs()) return;
    m_settings.setValue(QStringLiteral("%1/Simulation/ProgressTickMs")
                            .arg(kGroupRoot), ms);
    emit preferenceChanged(QStringLiteral("Simulation"),
                           QStringLiteral("ProgressTickMs"));
}

// ---------------------------------------------------------------------------
// Map Decorations / Scale Bar
// ---------------------------------------------------------------------------

QColor PreferencesManager::scaleBarPenColor() const
{
    const QVariant v = m_settings.value(
        QStringLiteral("%1/Decorations/ScaleBar/PenColor").arg(kGroupRoot));
    if (v.isValid()) {
        const QColor c = v.value<QColor>();
        if (c.isValid()) return c;
    }
    return Qt::black;
}

void PreferencesManager::setScaleBarPenColor(const QColor &color)
{
    if (!color.isValid() || color == scaleBarPenColor()) return;
    m_settings.setValue(QStringLiteral("%1/Decorations/ScaleBar/PenColor").arg(kGroupRoot), color);
    emit preferenceChanged(QStringLiteral("Decorations"), QStringLiteral("ScaleBar/PenColor"));
}

int PreferencesManager::scaleBarPenWidth() const
{
    return m_settings.value(QStringLiteral("%1/Decorations/ScaleBar/PenWidth").arg(kGroupRoot),
                            kDefaultScaleBarPenWidth).toInt();
}

void PreferencesManager::setScaleBarPenWidth(int width)
{
    if (width < 1 || width > 20) return;
    if (width == scaleBarPenWidth()) return;
    m_settings.setValue(QStringLiteral("%1/Decorations/ScaleBar/PenWidth").arg(kGroupRoot), width);
    emit preferenceChanged(QStringLiteral("Decorations"), QStringLiteral("ScaleBar/PenWidth"));
}

int PreferencesManager::scaleBarPenStyle() const
{
    return m_settings.value(QStringLiteral("%1/Decorations/ScaleBar/PenStyle").arg(kGroupRoot),
                            kDefaultScaleBarPenStyle).toInt();
}

void PreferencesManager::setScaleBarPenStyle(int style)
{
    if (style == scaleBarPenStyle()) return;
    m_settings.setValue(QStringLiteral("%1/Decorations/ScaleBar/PenStyle").arg(kGroupRoot), style);
    emit preferenceChanged(QStringLiteral("Decorations"), QStringLiteral("ScaleBar/PenStyle"));
}

QString PreferencesManager::scaleBarFontFamily() const
{
    return m_settings.value(QStringLiteral("%1/Decorations/ScaleBar/FontFamily").arg(kGroupRoot),
                            QString::fromLatin1(kDefaultScaleBarFontFamily)).toString();
}

void PreferencesManager::setScaleBarFontFamily(const QString &family)
{
    if (family.isEmpty() || family == scaleBarFontFamily()) return;
    m_settings.setValue(QStringLiteral("%1/Decorations/ScaleBar/FontFamily").arg(kGroupRoot), family);
    emit preferenceChanged(QStringLiteral("Decorations"), QStringLiteral("ScaleBar/FontFamily"));
}

int PreferencesManager::scaleBarFontSize() const
{
    return m_settings.value(QStringLiteral("%1/Decorations/ScaleBar/FontSize").arg(kGroupRoot),
                            kDefaultScaleBarFontSize).toInt();
}

void PreferencesManager::setScaleBarFontSize(int size)
{
    if (size < 4 || size > 72) return;
    if (size == scaleBarFontSize()) return;
    m_settings.setValue(QStringLiteral("%1/Decorations/ScaleBar/FontSize").arg(kGroupRoot), size);
    emit preferenceChanged(QStringLiteral("Decorations"), QStringLiteral("ScaleBar/FontSize"));
}

int PreferencesManager::scaleBarUnits() const
{
    return m_settings.value(QStringLiteral("%1/Decorations/ScaleBar/Units").arg(kGroupRoot),
                            kDefaultScaleBarUnits).toInt();
}

void PreferencesManager::setScaleBarUnits(int units)
{
    if (units == scaleBarUnits()) return;
    m_settings.setValue(QStringLiteral("%1/Decorations/ScaleBar/Units").arg(kGroupRoot), units);
    emit preferenceChanged(QStringLiteral("Decorations"), QStringLiteral("ScaleBar/Units"));
}

int PreferencesManager::scaleBarPosition() const
{
    return m_settings.value(QStringLiteral("%1/Decorations/ScaleBar/Position").arg(kGroupRoot),
                            kDefaultScaleBarPosition).toInt();
}

void PreferencesManager::setScaleBarPosition(int position)
{
    if (position == scaleBarPosition()) return;
    m_settings.setValue(QStringLiteral("%1/Decorations/ScaleBar/Position").arg(kGroupRoot), position);
    emit preferenceChanged(QStringLiteral("Decorations"), QStringLiteral("ScaleBar/Position"));
}

int PreferencesManager::scaleBarMaxBarLength() const
{
    return m_settings.value(QStringLiteral("%1/Decorations/ScaleBar/MaxBarLength").arg(kGroupRoot),
                            kDefaultScaleBarMaxBarLength).toInt();
}

void PreferencesManager::setScaleBarMaxBarLength(int length)
{
    if (length < 20 || length > 500) return;
    if (length == scaleBarMaxBarLength()) return;
    m_settings.setValue(QStringLiteral("%1/Decorations/ScaleBar/MaxBarLength").arg(kGroupRoot), length);
    emit preferenceChanged(QStringLiteral("Decorations"), QStringLiteral("ScaleBar/MaxBarLength"));
}

int PreferencesManager::scaleBarLabelDecimals() const
{
    return m_settings.value(QStringLiteral("%1/Decorations/ScaleBar/LabelDecimals").arg(kGroupRoot),
                            kDefaultScaleBarLabelDecimals).toInt();
}

void PreferencesManager::setScaleBarLabelDecimals(int decimals)
{
    if (decimals == scaleBarLabelDecimals()) return;
    m_settings.setValue(QStringLiteral("%1/Decorations/ScaleBar/LabelDecimals").arg(kGroupRoot), decimals);
    emit preferenceChanged(QStringLiteral("Decorations"), QStringLiteral("ScaleBar/LabelDecimals"));
}

bool PreferencesManager::scaleBarCompactNotation() const
{
    return m_settings.value(QStringLiteral("%1/Decorations/ScaleBar/CompactNotation").arg(kGroupRoot),
                            kDefaultScaleBarCompactNotation).toBool();
}

void PreferencesManager::setScaleBarCompactNotation(bool compact)
{
    if (compact == scaleBarCompactNotation()) return;
    m_settings.setValue(QStringLiteral("%1/Decorations/ScaleBar/CompactNotation").arg(kGroupRoot), compact);
    emit preferenceChanged(QStringLiteral("Decorations"), QStringLiteral("ScaleBar/CompactNotation"));
}

// ── Map Decorations / Measure Tool ───────────────────────────────────────

QColor PreferencesManager::measureLineColor() const
{
    return m_settings.value(QStringLiteral("%1/Decorations/MeasureTool/LineColor").arg(kGroupRoot),
                            kDefaultMeasureLineColor).value<QColor>();
}
void PreferencesManager::setMeasureLineColor(const QColor &color)
{
    if (!color.isValid() || color == measureLineColor()) return;
    m_settings.setValue(QStringLiteral("%1/Decorations/MeasureTool/LineColor").arg(kGroupRoot), color);
    emit preferenceChanged(QStringLiteral("Decorations"), QStringLiteral("MeasureTool/LineColor"));
}

QString PreferencesManager::measureLabelFontFamily() const
{
    return m_settings.value(QStringLiteral("%1/Decorations/MeasureTool/LabelFontFamily").arg(kGroupRoot),
                            QString::fromLatin1(kDefaultMeasureLabelFontFamily)).toString();
}
void PreferencesManager::setMeasureLabelFontFamily(const QString &family)
{
    if (family.isEmpty() || family == measureLabelFontFamily()) return;
    m_settings.setValue(QStringLiteral("%1/Decorations/MeasureTool/LabelFontFamily").arg(kGroupRoot), family);
    emit preferenceChanged(QStringLiteral("Decorations"), QStringLiteral("MeasureTool/LabelFontFamily"));
}

int PreferencesManager::measureLabelFontSize() const
{
    return m_settings.value(QStringLiteral("%1/Decorations/MeasureTool/LabelFontSize").arg(kGroupRoot),
                            kDefaultMeasureLabelFontSize).toInt();
}
void PreferencesManager::setMeasureLabelFontSize(int size)
{
    if (size < 4 || size > 72 || size == measureLabelFontSize()) return;
    m_settings.setValue(QStringLiteral("%1/Decorations/MeasureTool/LabelFontSize").arg(kGroupRoot), size);
    emit preferenceChanged(QStringLiteral("Decorations"), QStringLiteral("MeasureTool/LabelFontSize"));
}

int PreferencesManager::measureLabelDecimals() const
{
    return m_settings.value(QStringLiteral("%1/Decorations/MeasureTool/LabelDecimals").arg(kGroupRoot),
                            kDefaultMeasureLabelDecimals).toInt();
}
void PreferencesManager::setMeasureLabelDecimals(int decimals)
{
    if (decimals < 0 || decimals > 6 || decimals == measureLabelDecimals()) return;
    m_settings.setValue(QStringLiteral("%1/Decorations/MeasureTool/LabelDecimals").arg(kGroupRoot), decimals);
    emit preferenceChanged(QStringLiteral("Decorations"), QStringLiteral("MeasureTool/LabelDecimals"));
}

QColor PreferencesManager::measureFillColor() const
{
    return m_settings.value(QStringLiteral("%1/Decorations/MeasureTool/FillColor").arg(kGroupRoot),
                            kDefaultMeasureFillColor).value<QColor>();
}
void PreferencesManager::setMeasureFillColor(const QColor &color)
{
    if (!color.isValid() || color == measureFillColor()) return;
    m_settings.setValue(QStringLiteral("%1/Decorations/MeasureTool/FillColor").arg(kGroupRoot), color);
    emit preferenceChanged(QStringLiteral("Decorations"), QStringLiteral("MeasureTool/FillColor"));
}

int PreferencesManager::measureFillOpacity() const
{
    return m_settings.value(QStringLiteral("%1/Decorations/MeasureTool/FillOpacity").arg(kGroupRoot),
                            kDefaultMeasureFillOpacity).toInt();
}
void PreferencesManager::setMeasureFillOpacity(int opacity)
{
    if (opacity < 0 || opacity > 100 || opacity == measureFillOpacity()) return;
    m_settings.setValue(QStringLiteral("%1/Decorations/MeasureTool/FillOpacity").arg(kGroupRoot), opacity);
    emit preferenceChanged(QStringLiteral("Decorations"), QStringLiteral("MeasureTool/FillOpacity"));
}

// ---------------------------------------------------------------------------
// Element naming prefixes
// ---------------------------------------------------------------------------

QString PreferencesManager::elementNamePrefix(const QString &kind) const
{
    const QString key =
        QStringLiteral("%1/Naming/Prefix/%2").arg(QLatin1String(kGroupRoot), kind.toLower());

    // Find compiled-in default for this kind.
    const char *dflt = kind.toLatin1().constData(); // safe fallback = kind itself
    for (const auto &pd : kPrefixDefaults)
    {
        if (kind.compare(QLatin1String(pd.kind), Qt::CaseInsensitive) == 0)
        {
            dflt = pd.prefix;
            break;
        }
    }

    return m_settings.value(key, QLatin1String(dflt)).toString();
}

void PreferencesManager::setElementNamePrefix(const QString &kind, const QString &prefix)
{
    if (prefix.isEmpty() || prefix == elementNamePrefix(kind))
        return;

    const QString key =
        QStringLiteral("%1/Naming/Prefix/%2").arg(QLatin1String(kGroupRoot), kind.toLower());
    m_settings.setValue(key, prefix);
    emit preferenceChanged(QStringLiteral("Naming"), QStringLiteral("Prefix/") + kind.toLower());
}
