/*!
 * \file   preferencesmanager.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "core/preferencesmanager.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

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
constexpr bool    kDefaultAutoBuildRasterOverviews = true;

constexpr const char *kDefaultTool              = "Select";
constexpr const char *kDefaultCrsAuthority      = "EPSG";
constexpr int     kDefaultCrsCode               = 4326;

constexpr qreal   kDefaultLabelLodM11Min        = 0.5;

const QColor kDefaultConduitColor(50, 50, 200);
const QColor kDefaultPumpColor(Qt::red);
const QColor kDefaultOrificeColor(200, 150, 0);
const QColor kDefaultWeirColor(0, 180, 100);
const QColor kDefaultOutletColor(120, 90, 40);

// Per-type pen defaults. Conduits stay thin and square-capped (the
// historical look). Non-conduit links default to thicker round-capped
// strokes so pumps/orifices/weirs/outlets read clearly even at small
// scale and at low pixel zooms. Cap/join can be edited individually in
// the Preferences dialog.
struct LinkPenDefault {
    QColor       color;
    qreal        width;
    Qt::PenCapStyle  cap;
    Qt::PenJoinStyle join;
};
// Conduits are the multi-vertex polylines; the CPU painter draws them as
// independent segments (drawLines), so RoundCap is what rounds the shared
// vertices — overlapping round caps fill the bend. FlatCap left wedge gaps
// ("broken corners"). RoundJoin matches the other link kinds and applies if
// a connected-polyline draw path is ever used.
const LinkPenDefault kConduitPenDefault = { kDefaultConduitColor, 1.0, Qt::RoundCap, Qt::RoundJoin };
const LinkPenDefault kPumpPenDefault    = { kDefaultPumpColor,    3.0, Qt::RoundCap, Qt::RoundJoin };
const LinkPenDefault kOrificePenDefault = { kDefaultOrificeColor, 2.5, Qt::RoundCap, Qt::RoundJoin };
const LinkPenDefault kWeirPenDefault    = { kDefaultWeirColor,    2.5, Qt::RoundCap, Qt::RoundJoin };
const LinkPenDefault kOutletPenDefault  = { kDefaultOutletColor,  2.0, Qt::RoundCap, Qt::RoundJoin };

// Per-node-type defaults. Fills and sizes mirror the historical
// SWMMModelLayer constructor seeding (swmmmodellayer.cpp:226-233) so the
// new preferences plumbing reproduces the existing first-open look. The
// outline pen is darkBlue@1px across the board; users can per-type
// override colour, width, style, cap, and join in the Preferences dialog.
struct NodeStyleDefault {
    QColor fill;
    QColor outlineColor;
    qreal  outlineWidth;
    double sizePx;
};
const NodeStyleDefault kJunctionNodeDefault = { QColor(0,   120, 255), Qt::darkBlue, 1.0,  8.0  };
const NodeStyleDefault kOutfallNodeDefault  = { QColor(220, 0,   0  ), Qt::darkBlue, 1.0, 12.5  };
const NodeStyleDefault kStorageNodeDefault  = { QColor(180, 60,  200), Qt::darkBlue, 1.0, 12.0  };
const NodeStyleDefault kDividerNodeDefault  = { QColor(0,   255, 0  ), Qt::darkBlue, 1.0,  8.0  };

constexpr int     kDefaultProgressTickMs        = 1000;
constexpr double  kDefaultAnimationSpeed         = 1.0;
constexpr int     kDefaultProfileMaxPaths       = 100;

constexpr int     kDefaultProfileEndpointHaloRadiusPx = 10;
const QColor      kDefaultProfileStartColor(0x2c, 0xa0, 0x2c);   // green
const QColor      kDefaultProfileEndColor  (0xd6, 0x27, 0x28);   // red
constexpr qreal   kDefaultProfileEndpointPenWidth   = 3.0;

// ── MeasureTool defaults ──────────────────────────────────────────────────
const QColor kDefaultMeasureLineColor(Qt::red);
const QColor kDefaultMeasureFillColor(100, 149, 237);  // cornflower blue
constexpr const char *kDefaultMeasureLabelFontFamily = "sans-serif";
constexpr int         kDefaultMeasureLabelFontSize   = 8;
constexpr int         kDefaultMeasureLabelDecimals   = 2;
constexpr int         kDefaultMeasureFillOpacity     = 30;

// Plot numeric precision: X defaults to 0 decimals (distance/index axes
// read cleanly as whole numbers, matching the legacy profile X look),
// Y to 2 decimals (matches the point-label default).
constexpr int         kDefaultPlotXAxisFormatMode    = 0;  // NumberFormatMode::Decimals
constexpr int         kDefaultPlotXAxisPrecision     = 0;
constexpr int         kDefaultPlotYAxisFormatMode    = 0;  // NumberFormatMode::Decimals
constexpr int         kDefaultPlotYAxisPrecision     = 2;

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
    // One-shot migrations from legacy *Color keys into the new *Pen
    // (and, for selection, *Brush) shapes. Each lift preserves the
    // user-customised colour but layers it onto the type-appropriate
    // defaults (width/cap/join for pens, alpha for fills). After
    // migration the legacy key is deleted so subsequent launches go
    // straight through the new branch.

    // Rendering/LinkColor/<Type> → Rendering/LinkPen/<Type>
    {
        const QStringList legacyKeys = {
            QStringLiteral("Conduit"),
            QStringLiteral("Pump"),
            QStringLiteral("Orifice"),
            QStringLiteral("Weir"),
            QStringLiteral("Outlet"),
        };
        for (const QString &k : legacyKeys) {
            const QString oldKey = QStringLiteral("%1/Rendering/LinkColor/%2")
                                       .arg(QString::fromLatin1(kGroupRoot), k);
            if (!m_settings.contains(oldKey)) continue;
            const QColor c = m_settings.value(oldKey).value<QColor>();
            m_settings.remove(oldKey);
            if (!c.isValid()) continue;

            const QString newKey = QStringLiteral("%1/Rendering/LinkPen/%2")
                                       .arg(QString::fromLatin1(kGroupRoot), k);
            if (m_settings.contains(newKey)) continue;

            QPen pen = linkPen(k);
            pen.setColor(c);
            m_settings.setValue(newKey, QVariant::fromValue(pen));
        }
    }

    // Selection/Color/<Class> → Selection/Pen/<Class> + Selection/Brush/<Class>
    {
        const QStringList legacyKeys = {
            QStringLiteral("Link"),
            QStringLiteral("Node"),
            QStringLiteral("Subcatchment"),
            QStringLiteral("Gage"),
        };
        for (const QString &k : legacyKeys) {
            const QString oldKey = QStringLiteral("%1/Selection/Color/%2")
                                       .arg(QString::fromLatin1(kGroupRoot), k);
            if (!m_settings.contains(oldKey)) continue;
            const QColor c = m_settings.value(oldKey).value<QColor>();
            m_settings.remove(oldKey);
            if (!c.isValid()) continue;

            const QString penKey = QStringLiteral("%1/Selection/Pen/%2")
                                       .arg(QString::fromLatin1(kGroupRoot), k);
            if (!m_settings.contains(penKey)) {
                QPen pen = selectionPen(k);
                pen.setColor(c);
                m_settings.setValue(penKey, QVariant::fromValue(pen));
            }
            // Brush only applies to filled classes — skip Link.
            if (k == QLatin1String("Link")) continue;
            const QString brushKey = QStringLiteral("%1/Selection/Brush/%2")
                                         .arg(QString::fromLatin1(kGroupRoot), k);
            if (!m_settings.contains(brushKey)) {
                QBrush brush = selectionBrush(k);
                QColor bc = c;
                if (k == QLatin1String("Subcatchment")) bc.setAlpha(180);
                brush.setColor(bc);
                m_settings.setValue(brushKey, QVariant::fromValue(brush));
            }
        }
    }
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

bool PreferencesManager::autoBuildRasterOverviews() const
{
    return m_settings.value(QStringLiteral("%1/Raster/AutoBuildOverviews")
                                .arg(kGroupRoot),
                            kDefaultAutoBuildRasterOverviews).toBool();
}

void PreferencesManager::setAutoBuildRasterOverviews(bool enabled)
{
    if (enabled == autoBuildRasterOverviews()) return;
    m_settings.setValue(QStringLiteral("%1/Raster/AutoBuildOverviews")
                            .arg(kGroupRoot), enabled);
    emit preferenceChanged(QStringLiteral("Raster"),
                           QStringLiteral("AutoBuildOverviews"));
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

namespace {
// Yellow across the board is the conventional "selected" cue. The
// dialog lets users pick distinct per-class pens and brushes when they
// want to differentiate selections of mixed feature types.
const QColor kDefaultSelectionColor(255, 255, 0);

QPen defaultSelectionPen(const QString &canonicalKey)
{
    if (canonicalKey == QLatin1String("Link")) {
        // ADDITIVE width over the base link pen (see header docs):
        // 2.0 keeps the historical "halo is +2 px wider than the
        // conduit/pump/… pen" look. Cap/join inherit from the base
        // pen unless the user overrides them here.
        QPen p(kDefaultSelectionColor, 2.0);
        p.setCapStyle(Qt::FlatCap);
        p.setJoinStyle(Qt::BevelJoin);
        return p;
    }
    if (canonicalKey == QLatin1String("Subcatchment")) {
        QPen p(kDefaultSelectionColor, 1.5);
        p.setCapStyle(Qt::RoundCap);
        p.setJoinStyle(Qt::RoundJoin);
        return p;
    }
    // Node / Gage / Default — thin yellow outline around the glyph.
    return QPen(kDefaultSelectionColor, 1.0);
}

QBrush defaultSelectionBrush(const QString &canonicalKey)
{
    // Subcatchment fills are translucent so the underlying basemap and
    // overlapping objects still read; node/gage glyphs are filled
    // opaque so the highlight is unambiguous. Links never fill.
    if (canonicalKey == QLatin1String("Link"))
        return QBrush(Qt::NoBrush);
    if (canonicalKey == QLatin1String("Subcatchment")) {
        QColor c = kDefaultSelectionColor;
        c.setAlpha(180);
        return QBrush(c);
    }
    return QBrush(kDefaultSelectionColor);
}
} // anonymous

QPen PreferencesManager::selectionPen(const QString &className) const
{
    const QString k = canonicalSelectionClass(className);
    const QString key = QStringLiteral("%1/Selection/Pen/%2")
                            .arg(QString::fromLatin1(kGroupRoot), k);
    const QVariant v = m_settings.value(key);
    if (v.isValid() && v.canConvert<QPen>()) {
        const QPen p = v.value<QPen>();
        if (p.color().isValid()) return p;
    }
    return defaultSelectionPen(k);
}

void PreferencesManager::setSelectionPen(const QString &className, const QPen &pen)
{
    if (!pen.color().isValid()) return;
    const QString k = canonicalSelectionClass(className);
    if (k == QLatin1String("Default")) return;
    if (selectionPen(className) == pen) return;
    const QString key = QStringLiteral("%1/Selection/Pen/%2")
                            .arg(QString::fromLatin1(kGroupRoot), k);
    m_settings.setValue(key, QVariant::fromValue(pen));
    emit preferenceChanged(QStringLiteral("Selection"),
                           QStringLiteral("Pen/%1").arg(k));
}

QBrush PreferencesManager::selectionBrush(const QString &className) const
{
    const QString k = canonicalSelectionClass(className);
    if (k == QLatin1String("Link")) return QBrush(Qt::NoBrush);
    const QString key = QStringLiteral("%1/Selection/Brush/%2")
                            .arg(QString::fromLatin1(kGroupRoot), k);
    const QVariant v = m_settings.value(key);
    if (v.isValid() && v.canConvert<QBrush>()) {
        const QBrush b = v.value<QBrush>();
        if (b.color().isValid()) return b;
    }
    return defaultSelectionBrush(k);
}

void PreferencesManager::setSelectionBrush(const QString &className, const QBrush &brush)
{
    if (!brush.color().isValid()) return;
    const QString k = canonicalSelectionClass(className);
    if (k == QLatin1String("Default") || k == QLatin1String("Link")) return;
    if (selectionBrush(className) == brush) return;
    const QString key = QStringLiteral("%1/Selection/Brush/%2")
                            .arg(QString::fromLatin1(kGroupRoot), k);
    m_settings.setValue(key, QVariant::fromValue(brush));
    emit preferenceChanged(QStringLiteral("Selection"),
                           QStringLiteral("Brush/%1").arg(k));
}

QColor PreferencesManager::selectionColor(const QString &className) const
{
    return selectionPen(className).color();
}

void PreferencesManager::resetSelectionStyleToDefault(const QString &className)
{
    const QString k = canonicalSelectionClass(className);
    if (k == QLatin1String("Default")) return;

    const QString penKey = QStringLiteral("%1/Selection/Pen/%2")
                               .arg(QString::fromLatin1(kGroupRoot), k);
    const QString brushKey = QStringLiteral("%1/Selection/Brush/%2")
                                 .arg(QString::fromLatin1(kGroupRoot), k);

    const bool hadPen   = m_settings.contains(penKey);
    const bool hadBrush = m_settings.contains(brushKey);
    if (hadPen)   m_settings.remove(penKey);
    if (hadBrush) m_settings.remove(brushKey);
    if (hadPen)
        emit preferenceChanged(QStringLiteral("Selection"),
                               QStringLiteral("Pen/%1").arg(k));
    if (hadBrush)
        emit preferenceChanged(QStringLiteral("Selection"),
                               QStringLiteral("Brush/%1").arg(k));
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

QString PreferencesManager::defaultCrsMode() const
{
    return m_settings.value(QStringLiteral("%1/Canvas/DefaultCrsMode").arg(kGroupRoot),
                            QStringLiteral("LocalAuto")).toString();
}

void PreferencesManager::setDefaultCrsMode(const QString &mode)
{
    if (mode == defaultCrsMode()) return;
    m_settings.setValue(QStringLiteral("%1/Canvas/DefaultCrsMode").arg(kGroupRoot), mode);
    emit preferenceChanged(QStringLiteral("Canvas"), QStringLiteral("DefaultCrsMode"));
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
// Snapping
// ---------------------------------------------------------------------------

bool PreferencesManager::qsgRenderEnabled() const
{
    return m_settings.value(QStringLiteral("%1/Rendering/QsgEnabled").arg(kGroupRoot),
                            true).toBool();
}
void PreferencesManager::setQsgRenderEnabled(bool enabled)
{
    if (enabled == qsgRenderEnabled()) return;
    m_settings.setValue(QStringLiteral("%1/Rendering/QsgEnabled").arg(kGroupRoot), enabled);
    emit preferenceChanged(QStringLiteral("Rendering"), QStringLiteral("QsgEnabled"));
}

bool PreferencesManager::snapEnabled() const
{
    return m_settings.value(QStringLiteral("%1/Snapping/Enabled").arg(kGroupRoot),
                            true).toBool();
}
void PreferencesManager::setSnapEnabled(bool enabled)
{
    if (enabled == snapEnabled()) return;
    m_settings.setValue(QStringLiteral("%1/Snapping/Enabled").arg(kGroupRoot), enabled);
    emit preferenceChanged(QStringLiteral("Snapping"), QStringLiteral("Enabled"));
}

int PreferencesManager::snapTolerancePx() const
{
    return m_settings.value(QStringLiteral("%1/Snapping/TolerancePx").arg(kGroupRoot),
                            12).toInt();
}
void PreferencesManager::setSnapTolerancePx(int px)
{
    if (px == snapTolerancePx()) return;
    m_settings.setValue(QStringLiteral("%1/Snapping/TolerancePx").arg(kGroupRoot), px);
    emit preferenceChanged(QStringLiteral("Snapping"), QStringLiteral("TolerancePx"));
}

bool PreferencesManager::snapToVertices() const
{
    return m_settings.value(QStringLiteral("%1/Snapping/ToVertices").arg(kGroupRoot),
                            true).toBool();
}
void PreferencesManager::setSnapToVertices(bool enabled)
{
    if (enabled == snapToVertices()) return;
    m_settings.setValue(QStringLiteral("%1/Snapping/ToVertices").arg(kGroupRoot), enabled);
    emit preferenceChanged(QStringLiteral("Snapping"), QStringLiteral("ToVertices"));
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

const LinkPenDefault &defaultLinkPenForKey(const QString &canonicalKey)
{
    if (canonicalKey == QLatin1String("Pump"))    return kPumpPenDefault;
    if (canonicalKey == QLatin1String("Orifice")) return kOrificePenDefault;
    if (canonicalKey == QLatin1String("Weir"))    return kWeirPenDefault;
    if (canonicalKey == QLatin1String("Outlet"))  return kOutletPenDefault;
    return kConduitPenDefault;
}
} // anonymous

QPen PreferencesManager::linkPen(const QString &linkType) const
{
    const QString k = canonicalLinkType(linkType);
    const QString key = QStringLiteral("%1/Rendering/LinkPen/%2")
                            .arg(QString::fromLatin1(kGroupRoot), k);
    const QVariant v = m_settings.value(key);
    if (v.isValid() && v.canConvert<QPen>()) {
        const QPen p = v.value<QPen>();
        if (p.color().isValid()) return p;
    }
    const LinkPenDefault &d = defaultLinkPenForKey(k);
    QPen p(d.color, d.width);
    p.setCapStyle(d.cap);
    p.setJoinStyle(d.join);
    p.setStyle(Qt::SolidLine);
    return p;
}

void PreferencesManager::setLinkPen(const QString &linkType, const QPen &pen)
{
    if (!pen.color().isValid()) return;
    const QString k = canonicalLinkType(linkType);
    if (linkPen(k) == pen) return;
    const QString key = QStringLiteral("%1/Rendering/LinkPen/%2")
                            .arg(QString::fromLatin1(kGroupRoot), k);
    m_settings.setValue(key, QVariant::fromValue(pen));
    emit preferenceChanged(QStringLiteral("Rendering"),
                           QStringLiteral("LinkPen/%1").arg(k));
}

QColor PreferencesManager::linkColor(const QString &linkType) const
{
    return linkPen(linkType).color();
}

void PreferencesManager::resetLinkPenToDefault(const QString &linkType)
{
    const QString k = canonicalLinkType(linkType);
    const QString key = QStringLiteral("%1/Rendering/LinkPen/%2")
                            .arg(QString::fromLatin1(kGroupRoot), k);
    if (!m_settings.contains(key)) return;
    m_settings.remove(key);
    emit preferenceChanged(QStringLiteral("Rendering"),
                           QStringLiteral("LinkPen/%1").arg(k));
}

// ---------------------------------------------------------------------------
// Rendering / Node symbols (pen + brush + size, per node-type)
// ---------------------------------------------------------------------------

namespace {
QString canonicalNodeType(const QString &nodeType)
{
    const QString k = nodeType.trimmed().toLower();
    if (k == QLatin1String("outfall")  || k == QLatin1String("outfalls"))
        return QStringLiteral("Outfall");
    if (k == QLatin1String("storage")  || k == QLatin1String("storages")
        || k == QLatin1String("storageunit"))
        return QStringLiteral("Storage");
    if (k == QLatin1String("divider")  || k == QLatin1String("dividers"))
        return QStringLiteral("Divider");
    return QStringLiteral("Junction");
}

const NodeStyleDefault &defaultNodeStyleForKey(const QString &canonicalKey)
{
    if (canonicalKey == QLatin1String("Outfall")) return kOutfallNodeDefault;
    if (canonicalKey == QLatin1String("Storage")) return kStorageNodeDefault;
    if (canonicalKey == QLatin1String("Divider")) return kDividerNodeDefault;
    return kJunctionNodeDefault;
}
} // anonymous

QPen PreferencesManager::nodePen(const QString &nodeType) const
{
    const QString k = canonicalNodeType(nodeType);
    const QString key = QStringLiteral("%1/Rendering/NodePen/%2")
                            .arg(QString::fromLatin1(kGroupRoot), k);
    const QVariant v = m_settings.value(key);
    if (v.isValid() && v.canConvert<QPen>()) {
        const QPen p = v.value<QPen>();
        if (p.color().isValid()) return p;
    }
    const NodeStyleDefault &d = defaultNodeStyleForKey(k);
    QPen p(d.outlineColor, d.outlineWidth);
    p.setStyle(Qt::SolidLine);
    return p;
}

void PreferencesManager::setNodePen(const QString &nodeType, const QPen &pen)
{
    if (!pen.color().isValid()) return;
    const QString k = canonicalNodeType(nodeType);
    if (nodePen(k) == pen) return;
    const QString key = QStringLiteral("%1/Rendering/NodePen/%2")
                            .arg(QString::fromLatin1(kGroupRoot), k);
    m_settings.setValue(key, QVariant::fromValue(pen));
    emit preferenceChanged(QStringLiteral("Rendering"),
                           QStringLiteral("NodePen/%1").arg(k));
}

QBrush PreferencesManager::nodeBrush(const QString &nodeType) const
{
    const QString k = canonicalNodeType(nodeType);
    const QString key = QStringLiteral("%1/Rendering/NodeBrush/%2")
                            .arg(QString::fromLatin1(kGroupRoot), k);
    const QVariant v = m_settings.value(key);
    if (v.isValid() && v.canConvert<QBrush>()) {
        const QBrush b = v.value<QBrush>();
        if (b.color().isValid()) return b;
    }
    return QBrush(defaultNodeStyleForKey(k).fill);
}

void PreferencesManager::setNodeBrush(const QString &nodeType, const QBrush &brush)
{
    if (!brush.color().isValid()) return;
    const QString k = canonicalNodeType(nodeType);
    if (nodeBrush(k) == brush) return;
    const QString key = QStringLiteral("%1/Rendering/NodeBrush/%2")
                            .arg(QString::fromLatin1(kGroupRoot), k);
    m_settings.setValue(key, QVariant::fromValue(brush));
    emit preferenceChanged(QStringLiteral("Rendering"),
                           QStringLiteral("NodeBrush/%1").arg(k));
}

double PreferencesManager::nodeSize(const QString &nodeType) const
{
    const QString k = canonicalNodeType(nodeType);
    const QString key = QStringLiteral("%1/Rendering/NodeSize/%2")
                            .arg(QString::fromLatin1(kGroupRoot), k);
    const QVariant v = m_settings.value(key);
    if (v.isValid()) {
        bool ok = false;
        const double s = v.toDouble(&ok);
        if (ok && s > 0.0) return s;
    }
    return defaultNodeStyleForKey(k).sizePx;
}

void PreferencesManager::setNodeSize(const QString &nodeType, double sizePx)
{
    if (sizePx < 1.0 || sizePx > 64.0) return;
    const QString k = canonicalNodeType(nodeType);
    if (qFuzzyCompare(sizePx, nodeSize(k))) return;
    const QString key = QStringLiteral("%1/Rendering/NodeSize/%2")
                            .arg(QString::fromLatin1(kGroupRoot), k);
    m_settings.setValue(key, sizePx);
    emit preferenceChanged(QStringLiteral("Rendering"),
                           QStringLiteral("NodeSize/%1").arg(k));
}

void PreferencesManager::resetNodeStyleToDefault(const QString &nodeType)
{
    const QString k = canonicalNodeType(nodeType);
    const QString penKey   = QStringLiteral("%1/Rendering/NodePen/%2")
                                 .arg(QString::fromLatin1(kGroupRoot), k);
    const QString brushKey = QStringLiteral("%1/Rendering/NodeBrush/%2")
                                 .arg(QString::fromLatin1(kGroupRoot), k);
    const QString sizeKey  = QStringLiteral("%1/Rendering/NodeSize/%2")
                                 .arg(QString::fromLatin1(kGroupRoot), k);
    const bool hadPen   = m_settings.contains(penKey);
    const bool hadBrush = m_settings.contains(brushKey);
    const bool hadSize  = m_settings.contains(sizeKey);
    if (hadPen)   m_settings.remove(penKey);
    if (hadBrush) m_settings.remove(brushKey);
    if (hadSize)  m_settings.remove(sizeKey);
    if (hadPen)
        emit preferenceChanged(QStringLiteral("Rendering"),
                               QStringLiteral("NodePen/%1").arg(k));
    if (hadBrush)
        emit preferenceChanged(QStringLiteral("Rendering"),
                               QStringLiteral("NodeBrush/%1").arg(k));
    if (hadSize)
        emit preferenceChanged(QStringLiteral("Rendering"),
                               QStringLiteral("NodeSize/%1").arg(k));
}

// ---------------------------------------------------------------------------
// Rendering / Custom color ramps  (Slice BB-α)
// ---------------------------------------------------------------------------

QMap<QString, RasterColorRamp> PreferencesManager::customColorRamps() const
{
    QMap<QString, RasterColorRamp> out;
    const QString key = QStringLiteral("%1/Rendering/CustomRamps")
                            .arg(QString::fromLatin1(kGroupRoot));
    const QString blob = m_settings.value(key).toString();
    if (blob.isEmpty()) return out;
    const QJsonDocument doc = QJsonDocument::fromJson(blob.toUtf8());
    if (!doc.isArray()) return out;
    for (const QJsonValue &v : doc.array())
    {
        const QJsonObject obj = v.toObject();
        const QString name = obj.value(QStringLiteral("name")).toString().trimmed();
        if (name.isEmpty()) continue;
        out.insert(name, RasterColorRamp::fromJson(obj));
    }
    return out;
}

void PreferencesManager::saveCustomColorRamp(const QString &name, const RasterColorRamp &ramp)
{
    const QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty()) return;

    auto map = customColorRamps();
    map.insert(trimmedName, ramp);

    QJsonArray arr;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
    {
        QJsonObject obj = it.value().toJson();
        obj.insert(QStringLiteral("name"), it.key());
        arr.append(obj);
    }
    const QString key = QStringLiteral("%1/Rendering/CustomRamps")
                            .arg(QString::fromLatin1(kGroupRoot));
    m_settings.setValue(key, QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
    emit preferenceChanged(QStringLiteral("Rendering"), QStringLiteral("CustomRamps"));
}

void PreferencesManager::removeCustomColorRamp(const QString &name)
{
    const QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty()) return;
    auto map = customColorRamps();
    if (!map.contains(trimmedName)) return;
    map.remove(trimmedName);

    QJsonArray arr;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
    {
        QJsonObject obj = it.value().toJson();
        obj.insert(QStringLiteral("name"), it.key());
        arr.append(obj);
    }
    const QString key = QStringLiteral("%1/Rendering/CustomRamps")
                            .arg(QString::fromLatin1(kGroupRoot));
    m_settings.setValue(key, QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
    emit preferenceChanged(QStringLiteral("Rendering"), QStringLiteral("CustomRamps"));
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

double PreferencesManager::animationSpeed() const
{
    return m_settings.value(QStringLiteral("%1/Simulation/AnimationSpeed")
                                .arg(kGroupRoot),
                            kDefaultAnimationSpeed).toDouble();
}

void PreferencesManager::setAnimationSpeed(double speed)
{
    // Clamp to the toolbar's supported multiplier set.
    static constexpr double kValid[] = {0.25, 0.5, 1.0, 2.0, 4.0, 8.0};
    bool ok = false;
    for (double v : kValid) { if (qFuzzyCompare(v, speed)) { ok = true; break; } }
    if (!ok) return;
    if (qFuzzyCompare(speed, animationSpeed())) return;
    m_settings.setValue(QStringLiteral("%1/Simulation/AnimationSpeed")
                            .arg(kGroupRoot), speed);
    emit preferenceChanged(QStringLiteral("Simulation"),
                           QStringLiteral("AnimationSpeed"));
}

// ---------------------------------------------------------------------------
// Profile plot path discovery
// ---------------------------------------------------------------------------

int PreferencesManager::profileMaxPaths() const
{
    return m_settings.value(QStringLiteral("%1/ProfilePlot/MaxPaths")
                                .arg(kGroupRoot),
                            kDefaultProfileMaxPaths).toInt();
}

void PreferencesManager::setProfileMaxPaths(int n)
{
    if (n < 1 || n > 1000000) return;
    if (n == profileMaxPaths()) return;
    m_settings.setValue(QStringLiteral("%1/ProfilePlot/MaxPaths")
                            .arg(kGroupRoot), n);
    emit preferenceChanged(QStringLiteral("ProfilePlot"),
                           QStringLiteral("MaxPaths"));
}

int PreferencesManager::profileEndpointHaloRadiusPx() const
{
    return m_settings.value(QStringLiteral("%1/ProfilePlot/HaloRadiusPx")
                                .arg(kGroupRoot),
                            kDefaultProfileEndpointHaloRadiusPx).toInt();
}

void PreferencesManager::setProfileEndpointHaloRadiusPx(int px)
{
    if (px < 1 || px > 200) return;
    if (px == profileEndpointHaloRadiusPx()) return;
    m_settings.setValue(QStringLiteral("%1/ProfilePlot/HaloRadiusPx")
                            .arg(kGroupRoot), px);
    emit preferenceChanged(QStringLiteral("ProfilePlot"),
                           QStringLiteral("HaloRadiusPx"));
}

QPen PreferencesManager::profileStartEndpointPen() const
{
    const QString key = QStringLiteral("%1/ProfilePlot/StartPen").arg(kGroupRoot);
    if (m_settings.contains(key)) {
        const QVariant v = m_settings.value(key);
        if (v.canConvert<QPen>()) return v.value<QPen>();
    }
    QPen p(kDefaultProfileStartColor);
    p.setWidthF(kDefaultProfileEndpointPenWidth);
    p.setCosmetic(true);
    return p;
}

void PreferencesManager::setProfileStartEndpointPen(const QPen &pen)
{
    m_settings.setValue(QStringLiteral("%1/ProfilePlot/StartPen").arg(kGroupRoot),
                        QVariant::fromValue(pen));
    emit preferenceChanged(QStringLiteral("ProfilePlot"),
                           QStringLiteral("StartPen"));
}

QPen PreferencesManager::profileEndEndpointPen() const
{
    const QString key = QStringLiteral("%1/ProfilePlot/EndPen").arg(kGroupRoot);
    if (m_settings.contains(key)) {
        const QVariant v = m_settings.value(key);
        if (v.canConvert<QPen>()) return v.value<QPen>();
    }
    QPen p(kDefaultProfileEndColor);
    p.setWidthF(kDefaultProfileEndpointPenWidth);
    p.setCosmetic(true);
    return p;
}

void PreferencesManager::setProfileEndEndpointPen(const QPen &pen)
{
    m_settings.setValue(QStringLiteral("%1/ProfilePlot/EndPen").arg(kGroupRoot),
                        QVariant::fromValue(pen));
    emit preferenceChanged(QStringLiteral("ProfilePlot"),
                           QStringLiteral("EndPen"));
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

// ── Plots: default numeric precision ───────────────────────────────────────

int PreferencesManager::plotXAxisFormatMode() const
{
    return m_settings.value(QStringLiteral("%1/Plots/XAxisFormatMode").arg(kGroupRoot),
                            kDefaultPlotXAxisFormatMode).toInt();
}
void PreferencesManager::setPlotXAxisFormatMode(int mode)
{
    if ((mode != 0 && mode != 1) || mode == plotXAxisFormatMode()) return;
    m_settings.setValue(QStringLiteral("%1/Plots/XAxisFormatMode").arg(kGroupRoot), mode);
    emit preferenceChanged(QStringLiteral("Plots"), QStringLiteral("XAxisFormatMode"));
}

int PreferencesManager::plotXAxisPrecision() const
{
    return m_settings.value(QStringLiteral("%1/Plots/XAxisPrecision").arg(kGroupRoot),
                            kDefaultPlotXAxisPrecision).toInt();
}
void PreferencesManager::setPlotXAxisPrecision(int count)
{
    if (count < 0 || count > 10 || count == plotXAxisPrecision()) return;
    m_settings.setValue(QStringLiteral("%1/Plots/XAxisPrecision").arg(kGroupRoot), count);
    emit preferenceChanged(QStringLiteral("Plots"), QStringLiteral("XAxisPrecision"));
}

int PreferencesManager::plotYAxisFormatMode() const
{
    return m_settings.value(QStringLiteral("%1/Plots/YAxisFormatMode").arg(kGroupRoot),
                            kDefaultPlotYAxisFormatMode).toInt();
}
void PreferencesManager::setPlotYAxisFormatMode(int mode)
{
    if ((mode != 0 && mode != 1) || mode == plotYAxisFormatMode()) return;
    m_settings.setValue(QStringLiteral("%1/Plots/YAxisFormatMode").arg(kGroupRoot), mode);
    emit preferenceChanged(QStringLiteral("Plots"), QStringLiteral("YAxisFormatMode"));
}

int PreferencesManager::plotYAxisPrecision() const
{
    return m_settings.value(QStringLiteral("%1/Plots/YAxisPrecision").arg(kGroupRoot),
                            kDefaultPlotYAxisPrecision).toInt();
}
void PreferencesManager::setPlotYAxisPrecision(int count)
{
    if (count < 0 || count > 10 || count == plotYAxisPrecision()) return;
    m_settings.setValue(QStringLiteral("%1/Plots/YAxisPrecision").arg(kGroupRoot), count);
    emit preferenceChanged(QStringLiteral("Plots"), QStringLiteral("YAxisPrecision"));
}

openswmmvis::plot::NumberFormat PreferencesManager::plotXAxisFormat() const
{
    return { static_cast<openswmmvis::plot::NumberFormatMode>(plotXAxisFormatMode()),
             plotXAxisPrecision() };
}
openswmmvis::plot::NumberFormat PreferencesManager::plotYAxisFormat() const
{
    return { static_cast<openswmmvis::plot::NumberFormatMode>(plotYAxisFormatMode()),
             plotYAxisPrecision() };
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

// ── Terrain editing defaults ─────────────────────────────────────────────────

double PreferencesManager::terrainDefaultNodeOffset() const
{
    return m_settings.value(
        QStringLiteral("%1/Terrain/NodeOffset").arg(QLatin1String(kGroupRoot)),
        0.0).toDouble();
}

void PreferencesManager::setTerrainDefaultNodeOffset(double offset)
{
    if (offset == terrainDefaultNodeOffset()) return;
    m_settings.setValue(
        QStringLiteral("%1/Terrain/NodeOffset").arg(QLatin1String(kGroupRoot)), offset);
    emit preferenceChanged(QStringLiteral("Terrain"), QStringLiteral("NodeOffset"));
}

double PreferencesManager::terrainDefaultLinkOffset() const
{
    return m_settings.value(
        QStringLiteral("%1/Terrain/LinkOffset").arg(QLatin1String(kGroupRoot)),
        0.0).toDouble();
}

void PreferencesManager::setTerrainDefaultLinkOffset(double offset)
{
    if (offset == terrainDefaultLinkOffset()) return;
    m_settings.setValue(
        QStringLiteral("%1/Terrain/LinkOffset").arg(QLatin1String(kGroupRoot)), offset);
    emit preferenceChanged(QStringLiteral("Terrain"), QStringLiteral("LinkOffset"));
}

// ---------------------------------------------------------------------------
// Application defaults applied on project creation
// ---------------------------------------------------------------------------

bool PreferencesManager::autoLengthEnabled() const
{
    return m_settings.value(
        QStringLiteral("%1/Defaults/AutoLength").arg(QLatin1String(kGroupRoot)),
        true).toBool();
}

void PreferencesManager::setAutoLengthEnabled(bool enabled)
{
    if (enabled == autoLengthEnabled()) return;
    m_settings.setValue(
        QStringLiteral("%1/Defaults/AutoLength").arg(QLatin1String(kGroupRoot)), enabled);
    emit preferenceChanged(QStringLiteral("Defaults"), QStringLiteral("AutoLength"));
}

QString PreferencesManager::defaultEngineMode() const
{
    // Empty default → caller substitutes SWMM_VERSION at use site; the
    // manager has no compile-time access to the engine version macros.
    return m_settings.value(
        QStringLiteral("%1/Defaults/EngineMode").arg(QLatin1String(kGroupRoot)),
        QString()).toString();
}

void PreferencesManager::setDefaultEngineMode(const QString &version)
{
    if (version == defaultEngineMode()) return;
    m_settings.setValue(
        QStringLiteral("%1/Defaults/EngineMode").arg(QLatin1String(kGroupRoot)), version);
    emit preferenceChanged(QStringLiteral("Defaults"), QStringLiteral("EngineMode"));
}

// ---------------------------------------------------------------------------
// Simulation defaults — bundle for clarity. One settings group, many keys.
// ---------------------------------------------------------------------------

namespace {
constexpr const char *kSimDefaultsGroup = "SWMMVis/Preferences/SimulationDefaults";

template<typename T>
T readSetting(QSettings &s, const QString &key, const T &fallback) {
    const QVariant v = s.value(QStringLiteral("%1/%2")
                                   .arg(QLatin1String(kSimDefaultsGroup), key));
    return v.isValid() ? v.value<T>() : fallback;
}
} // anonymous

PreferencesManager::SimulationDefaults
PreferencesManager::simulationDefaults() const
{
    SimulationDefaults d;   // compiled-in defaults seed the struct

    auto &s = const_cast<QSettings &>(m_settings);
    d.flowUnits          = readSetting<QString>(s, QStringLiteral("FlowUnits"),          d.flowUnits);
    d.infiltrationModel  = readSetting<QString>(s, QStringLiteral("Infiltration"),       d.infiltrationModel);
    d.flowRouting        = readSetting<QString>(s, QStringLiteral("FlowRouting"),        d.flowRouting);

    d.ignoreRainfall     = readSetting<bool>(s, QStringLiteral("IgnoreRainfall"),       d.ignoreRainfall);
    d.ignoreRdii         = readSetting<bool>(s, QStringLiteral("IgnoreRdii"),           d.ignoreRdii);
    d.ignoreSnowmelt     = readSetting<bool>(s, QStringLiteral("IgnoreSnowmelt"),       d.ignoreSnowmelt);
    d.ignoreGroundwater  = readSetting<bool>(s, QStringLiteral("IgnoreGroundwater"),    d.ignoreGroundwater);
    d.ignoreQuality      = readSetting<bool>(s, QStringLiteral("IgnoreQuality"),        d.ignoreQuality);
    d.ignoreRouting      = readSetting<bool>(s, QStringLiteral("IgnoreRouting"),        d.ignoreRouting);
    d.module2DEnabled    = readSetting<bool>(s, QStringLiteral("Module2D"),             d.module2DEnabled);

    d.allowPonding       = readSetting<bool>(s,   QStringLiteral("AllowPonding"),       d.allowPonding);
    d.skipSteadyState    = readSetting<bool>(s,   QStringLiteral("SkipSteadyState"),    d.skipSteadyState);
    d.minSlopePct        = readSetting<double>(s, QStringLiteral("MinSlopePct"),        d.minSlopePct);

    d.sweepStart         = readSetting<QString>(s, QStringLiteral("SweepStart"),        d.sweepStart);
    d.sweepEnd           = readSetting<QString>(s, QStringLiteral("SweepEnd"),          d.sweepEnd);
    d.dryDays            = readSetting<double>(s,  QStringLiteral("DryDays"),           d.dryDays);

    d.reportStepSec      = readSetting<int>(s,    QStringLiteral("ReportStepSec"),      d.reportStepSec);
    d.dryStepSec         = readSetting<int>(s,    QStringLiteral("DryStepSec"),         d.dryStepSec);
    d.wetStepSec         = readSetting<int>(s,    QStringLiteral("WetStepSec"),         d.wetStepSec);
    d.ruleStepSec        = readSetting<int>(s,    QStringLiteral("RuleStepSec"),        d.ruleStepSec);
    d.routingStepSec     = readSetting<double>(s, QStringLiteral("RoutingStepSec"),     d.routingStepSec);

    d.maxTrials          = readSetting<int>(s,    QStringLiteral("MaxTrials"),          d.maxTrials);
    d.headTolerance      = readSetting<double>(s, QStringLiteral("HeadTolerance"),      d.headTolerance);
    d.sysFlowTolPct      = readSetting<double>(s, QStringLiteral("SysFlowTolPct"),      d.sysFlowTolPct);
    d.latFlowTolPct      = readSetting<double>(s, QStringLiteral("LatFlowTolPct"),      d.latFlowTolPct);

    d.inertialDamping    = readSetting<QString>(s, QStringLiteral("InertialDamping"),    d.inertialDamping);
    d.normalFlowLimited  = readSetting<QString>(s, QStringLiteral("NormalFlowLimited"),  d.normalFlowLimited);
    d.forceMainEquation  = readSetting<QString>(s, QStringLiteral("ForceMainEquation"),  d.forceMainEquation);
    d.surchargeMethod    = readSetting<QString>(s, QStringLiteral("SurchargeMethod"),    d.surchargeMethod);
    d.variableStepOn     = readSetting<bool>(s,    QStringLiteral("VariableStepOn"),     d.variableStepOn);
    d.variableStepFactor = readSetting<double>(s,  QStringLiteral("VariableStepFactor"), d.variableStepFactor);
    d.minRoutingStepSec  = readSetting<double>(s,  QStringLiteral("MinRoutingStepSec"),  d.minRoutingStepSec);
    d.lengtheningStepSec = readSetting<double>(s,  QStringLiteral("LengtheningStepSec"), d.lengtheningStepSec);

    d.nodeContinuity     = readSetting<QString>(s, QStringLiteral("NodeContinuity"),     d.nodeContinuity);
    d.andersonAccel      = readSetting<bool>(s,    QStringLiteral("AndersonAccel"),      d.andersonAccel);

    d.threads            = readSetting<int>(s,    QStringLiteral("Threads"),             d.threads);

    return d;
}

void PreferencesManager::setSimulationDefaults(const SimulationDefaults &d)
{
    auto put = [this](const QString &key, const QVariant &v) {
        m_settings.setValue(
            QStringLiteral("%1/%2").arg(QLatin1String(kSimDefaultsGroup), key), v);
    };
    put(QStringLiteral("FlowUnits"),          d.flowUnits);
    put(QStringLiteral("Infiltration"),       d.infiltrationModel);
    put(QStringLiteral("FlowRouting"),        d.flowRouting);

    put(QStringLiteral("IgnoreRainfall"),     d.ignoreRainfall);
    put(QStringLiteral("IgnoreRdii"),         d.ignoreRdii);
    put(QStringLiteral("IgnoreSnowmelt"),     d.ignoreSnowmelt);
    put(QStringLiteral("IgnoreGroundwater"),  d.ignoreGroundwater);
    put(QStringLiteral("IgnoreQuality"),      d.ignoreQuality);
    put(QStringLiteral("IgnoreRouting"),      d.ignoreRouting);
    put(QStringLiteral("Module2D"),           d.module2DEnabled);

    put(QStringLiteral("AllowPonding"),       d.allowPonding);
    put(QStringLiteral("SkipSteadyState"),    d.skipSteadyState);
    put(QStringLiteral("MinSlopePct"),        d.minSlopePct);

    put(QStringLiteral("SweepStart"),         d.sweepStart);
    put(QStringLiteral("SweepEnd"),           d.sweepEnd);
    put(QStringLiteral("DryDays"),            d.dryDays);

    put(QStringLiteral("ReportStepSec"),      d.reportStepSec);
    put(QStringLiteral("DryStepSec"),         d.dryStepSec);
    put(QStringLiteral("WetStepSec"),         d.wetStepSec);
    put(QStringLiteral("RuleStepSec"),        d.ruleStepSec);
    put(QStringLiteral("RoutingStepSec"),     d.routingStepSec);

    put(QStringLiteral("MaxTrials"),          d.maxTrials);
    put(QStringLiteral("HeadTolerance"),      d.headTolerance);
    put(QStringLiteral("SysFlowTolPct"),      d.sysFlowTolPct);
    put(QStringLiteral("LatFlowTolPct"),      d.latFlowTolPct);

    put(QStringLiteral("InertialDamping"),    d.inertialDamping);
    put(QStringLiteral("NormalFlowLimited"),  d.normalFlowLimited);
    put(QStringLiteral("ForceMainEquation"),  d.forceMainEquation);
    put(QStringLiteral("SurchargeMethod"),    d.surchargeMethod);
    put(QStringLiteral("VariableStepOn"),     d.variableStepOn);
    put(QStringLiteral("VariableStepFactor"), d.variableStepFactor);
    put(QStringLiteral("MinRoutingStepSec"),  d.minRoutingStepSec);
    put(QStringLiteral("LengtheningStepSec"), d.lengtheningStepSec);

    put(QStringLiteral("NodeContinuity"),     d.nodeContinuity);
    put(QStringLiteral("AndersonAccel"),      d.andersonAccel);

    put(QStringLiteral("Threads"),            d.threads);

    emit preferenceChanged(QStringLiteral("Defaults"),
                           QStringLiteral("SimulationDefaults"));
}
