#include "ui/theme/thememanager.h"
#include "ui/theme/themetokens.h"

#include <QApplication>
#include <QGuiApplication>
#include <QPalette>
#include <QStyleHints>

namespace openswmmvis::ui {

namespace {

/*! Blend \a fg toward \a bg — used to derive the Disabled palette group
 *  from the enabled tokens instead of hand-picking more colors. */
QColor mixed(const QColor &fg, const QColor &bg, qreal fgShare)
{
    const qreal bgShare = 1.0 - fgShare;
    return QColor(int(fg.red()   * fgShare + bg.red()   * bgShare),
                  int(fg.green() * fgShare + bg.green() * bgShare),
                  int(fg.blue()  * fgShare + bg.blue()  * bgShare));
}

}   // namespace

ThemeManager::ThemeManager(QObject *parent)
    : QObject(parent)
{
    // In System mode, follow the OS appearance live.
    if (auto *hints = QGuiApplication::styleHints()) {
        connect(hints, &QStyleHints::colorSchemeChanged, this, [this]() {
            if (mMode == Mode::System)
                apply();
        });
    }
}

ThemeManager *ThemeManager::instance()
{
    static ThemeManager *s_instance = nullptr;
    if (!s_instance)
        s_instance = new ThemeManager(QCoreApplication::instance());
    return s_instance;
}

void ThemeManager::setMode(Mode mode)
{
    if (mMode == mode)
        return;
    mMode = mode;
    apply();
}

Qt::ColorScheme ThemeManager::effectiveScheme() const
{
    switch (mMode) {
    case Mode::Light:
        return Qt::ColorScheme::Light;
    case Mode::Dark:
        return Qt::ColorScheme::Dark;
    case Mode::System:
        break;
    }
    const auto *hints = QGuiApplication::styleHints();
    const Qt::ColorScheme system =
        hints ? hints->colorScheme() : Qt::ColorScheme::Unknown;
    // Offscreen/minimal platforms report Unknown — light is the safe floor.
    return system == Qt::ColorScheme::Dark ? Qt::ColorScheme::Dark
                                           : Qt::ColorScheme::Light;
}

const ThemeColors &ThemeManager::colors() const
{
    return effectiveScheme() == Qt::ColorScheme::Dark ? darkColors()
                                                      : lightColors();
}

void ThemeManager::apply()
{
    auto *app = qobject_cast<QApplication *>(QCoreApplication::instance());
    if (!app)
        return;

    const Qt::ColorScheme scheme = effectiveScheme();
    const ThemeColors &c = colors();

    QPalette pal;
    pal.setColor(QPalette::Window,          c.surfaceWindow);
    pal.setColor(QPalette::WindowText,      c.text);
    pal.setColor(QPalette::Base,            c.surfaceRaised);
    pal.setColor(QPalette::AlternateBase,   c.surfaceSunken);
    pal.setColor(QPalette::Text,            c.text);
    pal.setColor(QPalette::Button,          c.surfaceWindow);
    pal.setColor(QPalette::ButtonText,      c.text);
    pal.setColor(QPalette::ToolTipBase,     c.surfaceRaised);
    pal.setColor(QPalette::ToolTipText,     c.text);
    pal.setColor(QPalette::PlaceholderText, c.hintText);
    pal.setColor(QPalette::Highlight,       c.selectionFill);
    pal.setColor(QPalette::HighlightedText, c.selectionText);
    pal.setColor(QPalette::Link,            c.accent);
    pal.setColor(QPalette::BrightText,      c.surfaceRaised);
    // Mid carries hint text for the pre-existing `palette(mid)` stylesheet
    // sites — with this role tokenized they become theme-correct as-is.
    pal.setColor(QPalette::Mid,             c.hintText);
    pal.setColor(QPalette::Dark,            c.border);
    pal.setColor(QPalette::Midlight,        mixed(c.border, c.surfaceWindow, 0.5));
    pal.setColor(QPalette::Light,           c.surfaceRaised);
    pal.setColor(QPalette::Shadow,          mixed(c.text, c.surfaceSunken, 0.5));

    const QColor disabledText = mixed(c.text, c.surfaceWindow, 0.45);
    pal.setColor(QPalette::Disabled, QPalette::WindowText,      disabledText);
    pal.setColor(QPalette::Disabled, QPalette::Text,            disabledText);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText,      disabledText);
    pal.setColor(QPalette::Disabled, QPalette::PlaceholderText, disabledText);
    pal.setColor(QPalette::Disabled, QPalette::Highlight,       c.border);
    pal.setColor(QPalette::Disabled, QPalette::HighlightedText, disabledText);

    app->setPalette(pal);

    // Minimal overlay — NOT a re-skin. Only what the palette cannot
    // express: a visible keyboard-focus ring under Fusion (whose default
    // focus rect is a faint dotted line) and toolbar breathing room.
    const QString focus = c.focusRing.name(QColor::HexRgb);
    app->setStyleSheet(QStringLiteral(
        "QToolButton:focus, QPushButton:focus, QComboBox:focus, "
        "QTabBar::tab:focus { outline: 2px solid %1; outline-offset: -2px; }\n"
        "QToolBar { spacing: 3px; }\n")
            .arg(focus));

    if (scheme != mAppliedScheme) {
        mAppliedScheme = scheme;
        emit themeChanged();
    }
}

QString ThemeManager::modeToString(Mode mode)
{
    switch (mode) {
    case Mode::Light:  return QStringLiteral("Light");
    case Mode::Dark:   return QStringLiteral("Dark");
    case Mode::System: break;
    }
    return QStringLiteral("System");
}

ThemeManager::Mode ThemeManager::modeFromString(const QString &text)
{
    if (text.compare(QStringLiteral("Light"), Qt::CaseInsensitive) == 0)
        return Mode::Light;
    if (text.compare(QStringLiteral("Dark"), Qt::CaseInsensitive) == 0)
        return Mode::Dark;
    return Mode::System;
}

}   // namespace openswmmvis::ui
