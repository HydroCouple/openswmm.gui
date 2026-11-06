#ifndef THEMEMANAGER_H
#define THEMEMANAGER_H

/**
 * @file      thememanager.h
 * @brief     UI redesign P2 — applies the design tokens (themetokens.h) as the application-wide
 *            QPalette plus a minimal QSS overlay, and tracks the OS appearance when the mode is
 *            System (QStyleHints::colorScheme).
 *
 * UI redesign P2 — applies the design tokens (themetokens.h) as the
 * application-wide QPalette plus a minimal QSS overlay, and tracks the
 * OS appearance when the mode is System (QStyleHints::colorScheme).
 *
 * Persistence is deliberately NOT owned here: SWMMVisApplication seeds
 * the initial mode from PreferencesManager, and the Preferences
 * Appearance page writes the pref and calls setMode() — keeping this
 * class free of settings coupling so tests can drive it directly.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <QObject>
#include <Qt>

namespace openswmmvis::ui
{

struct ThemeColors;

class ThemeManager : public QObject
{
    Q_OBJECT

public:
    enum class Mode { System, Light, Dark };
    Q_ENUM(Mode)

    /**
     * @brief Lazily create the application-parented theme coordinator.
     * @return Borrowed singleton; use on the GUI thread and do not retain it after application shutdown. Creation/subsequent access are not synchronized.
     */
    static ThemeManager *instance();

    /**
     * @brief Inspect the requested appearance policy rather than the resolved OS scheme.
     * @return Stored System/Light/Dark mode, initially System.
     */
    Mode mode() const { return mMode; }
    /*! Change the mode and re-apply the palette. Emits themeChanged()
     *  when the effective scheme actually changes.
     * @param[in] mode Requested policy; an identical value is a no-op.
     * @note Calls apply() but does not persist preferences. A policy change resolving to the same colour scheme emits no themeChanged().
     */
    void setMode(Mode mode);

    /*! The scheme in force: the OS scheme in System mode (falling back
     *  to Light when the platform reports Unknown — e.g. the offscreen
     *  QPA), otherwise the explicit choice.
     * @return Dark only for explicit Dark or System with a Dark OS report; otherwise Light, including Unknown platform reports.
     */
    Qt::ColorScheme effectiveScheme() const;

    /*! Tokens for the effective scheme.
     * @return Borrowed immutable static light/dark token table for effectiveScheme(); no dynamic palette values are read.
     */
    const ThemeColors &colors() const;

    /*! Build + install the QPalette and QSS overlay for the effective
     *  scheme onto qApp. Safe to call any time after QApplication
     *  construction.
     * @note No-op without a QApplication. Replaces the application palette and its complete stylesheet with the theme overlay, even if the scheme is unchanged. Emits themeChanged() only when the resolved scheme differs from the last applied one. Call on the GUI thread.
     */
    void apply();

    /*! Round-trip helpers for the persisted preference string
     *  ("System" / "Light" / "Dark"; unknown input falls back to System).
     * @param[in] mode Requested enum; unsupported values fall back to System.
     * @return Stable untranslated preference token Light, Dark or System.
     */
    static QString modeToString(Mode mode);
    /**
     * @param[in] text Case-insensitive Light/Dark token; no trimming occurs.
     * @return Light/Dark when matched, otherwise System.
     */
    static Mode modeFromString(const QString &text);

signals:
    /*! Emitted after the palette/overlay for a new effective scheme has
     *  been applied — widgets holding token-derived pens repaint on this. */
    void themeChanged();

private:
    /**
     * @brief Create a System-policy coordinator and subscribe to OS scheme changes.
     * @param[in] parent Optional QObject owner, normally the application. OS changes call apply() only while System is selected; constructor itself does not install a palette.
     */
    explicit ThemeManager(QObject *parent = nullptr);

    Mode mMode = Mode::System;
    Qt::ColorScheme mAppliedScheme = Qt::ColorScheme::Unknown;
};

} // namespace openswmmvis::ui

#endif // THEMEMANAGER_H
