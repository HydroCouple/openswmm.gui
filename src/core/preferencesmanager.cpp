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

constexpr int     kDefaultProgressTickMs        = 1000;

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
