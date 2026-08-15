#ifndef THEMEMANAGER_H
#define THEMEMANAGER_H

/*!
 * \file thememanager.h
 *
 * UI redesign P2 — applies the design tokens (themetokens.h) as the
 * application-wide QPalette plus a minimal QSS overlay, and tracks the
 * OS appearance when the mode is System (QStyleHints::colorScheme).
 *
 * Persistence is deliberately NOT owned here: SWMMVisApplication seeds
 * the initial mode from PreferencesManager, and the Preferences
 * Appearance page writes the pref and calls setMode() — keeping this
 * class free of settings coupling so tests can drive it directly.
 */

#include <QObject>
#include <Qt>

namespace openswmmvis::ui {

struct ThemeColors;

class ThemeManager : public QObject
{
    Q_OBJECT

public:
    enum class Mode { System, Light, Dark };
    Q_ENUM(Mode)

    static ThemeManager *instance();

    Mode mode() const { return mMode; }
    /*! Change the mode and re-apply the palette. Emits themeChanged()
     *  when the effective scheme actually changes. */
    void setMode(Mode mode);

    /*! The scheme in force: the OS scheme in System mode (falling back
     *  to Light when the platform reports Unknown — e.g. the offscreen
     *  QPA), otherwise the explicit choice. */
    Qt::ColorScheme effectiveScheme() const;

    /*! Tokens for the effective scheme. */
    const ThemeColors &colors() const;

    /*! Build + install the QPalette and QSS overlay for the effective
     *  scheme onto qApp. Safe to call any time after QApplication
     *  construction. */
    void apply();

    /*! Round-trip helpers for the persisted preference string
     *  ("System" / "Light" / "Dark"; unknown input falls back to System). */
    static QString modeToString(Mode mode);
    static Mode modeFromString(const QString &text);

signals:
    /*! Emitted after the palette/overlay for a new effective scheme has
     *  been applied — widgets holding token-derived pens repaint on this. */
    void themeChanged();

private:
    explicit ThemeManager(QObject *parent = nullptr);

    Mode mMode = Mode::System;
    Qt::ColorScheme mAppliedScheme = Qt::ColorScheme::Unknown;
};

}   // namespace openswmmvis::ui

#endif // THEMEMANAGER_H
