/*!
 * \file   preferencesmanager.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license MIT
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

    // ── Simulation ───────────────────────────────────────────────────────
    /*! Progress-tick interval (ms) for live UI updates while a
     *  simulation runs. 1 Hz by default — short enough to feel live,
     *  long enough to not starve the event loop on small models. */
    [[nodiscard]] int progressTickMs() const;
    void setProgressTickMs(int ms);

signals:
    /*! Emitted after any successful set*. `group` is one of
     *  "Selection" / "Canvas" / "Rendering" / "Simulation" /
     *  "Appearance" / "Log"; `key` identifies the specific
     *  setting. */
    void preferenceChanged(const QString &group, const QString &key);

private:
    explicit PreferencesManager(QObject *parent = nullptr);

    QSettings m_settings;
};

#endif // PREFERENCESMANAGER_H
