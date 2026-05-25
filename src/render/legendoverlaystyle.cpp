/*!
 * \file   legendoverlaystyle.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "render/legendoverlaystyle.h"

#include <QHash>
#include <QJsonValue>
#include <QMetaEnum>

namespace OpenSWMM::Render
{

namespace {

template <typename Enum>
Enum enumFromString(const QString &name, Enum fallback)
{
    const QMetaEnum me = QMetaEnum::fromType<Enum>();
    bool ok = false;
    const int v = me.keyToValue(name.toUtf8().constData(), &ok);
    return ok ? static_cast<Enum>(v) : fallback;
}

template <typename Enum>
QString enumToString(Enum v)
{
    return QString::fromLatin1(QMetaEnum::fromType<Enum>().valueToKey(static_cast<int>(v)));
}

// Structured font serialisation — QFont::toString/fromString is brittle
// when the family is empty (the default-constructed app font on macOS
// before QGuiApplication initialises), so write each meaningful field.
QJsonObject fontToJson(const QFont &f)
{
    QJsonObject j;
    j[QStringLiteral("family")]    = f.family();
    j[QStringLiteral("pointSize")] = f.pointSizeF();
    j[QStringLiteral("weight")]    = int(f.weight());
    j[QStringLiteral("italic")]    = f.italic();
    j[QStringLiteral("underline")] = f.underline();
    j[QStringLiteral("strikeOut")] = f.strikeOut();
    return j;
}

QFont fontFromJson(const QJsonObject &j, const QFont &fallback)
{
    QFont f = fallback;
    if (j.contains(QStringLiteral("family")))    f.setFamily(j.value(QStringLiteral("family")).toString());
    if (j.contains(QStringLiteral("pointSize"))) {
        const double ps = j.value(QStringLiteral("pointSize")).toDouble(-1.0);
        if (ps > 0.0) f.setPointSizeF(ps);
    }
    if (j.contains(QStringLiteral("weight")))    f.setWeight(QFont::Weight(j.value(QStringLiteral("weight")).toInt(int(f.weight()))));
    if (j.contains(QStringLiteral("italic")))    f.setItalic(j.value(QStringLiteral("italic")).toBool());
    if (j.contains(QStringLiteral("underline"))) f.setUnderline(j.value(QStringLiteral("underline")).toBool());
    if (j.contains(QStringLiteral("strikeOut"))) f.setStrikeOut(j.value(QStringLiteral("strikeOut")).toBool());
    return f;
}

} // namespace

LegendOverlayStyle::LegendOverlayStyle(QObject *parent)
    : QObject(parent)
{
    // The default-constructed QFont resolves to the application font, which
    // is what users expect: the legend follows their system theme.
}

void LegendOverlayStyle::resetToDefaults()
{
    // Reassign defaults; setters fire the per-property signals + changed().
    setShowTitle(false);
    setTitle({});
    setTitleFont(QFont{});
    setTitleColor(QColor(20, 20, 20));
    setItemFont(QFont{});
    setItemColor(QColor(20, 20, 20));
    setLayerHeaderFont(QFont{});
    setLayerHeaderColor(QColor(20, 20, 20));
    setRowSpacing(2);
    setSwatchSize(14);
    setPadding(8);
    setAnchor(Anchor::BottomRight);
    setOpacity(1.0);

    setShowFrame(true);
    setFrameColor(QColor(80, 80, 80, 180));
    setFrameWidth(1.0);
    setCornerRadius(6);

    setBackgroundMode(BackgroundMode::Solid);
    setBackgroundColor(QColor(255, 255, 255, 225));
    setGradientEndColor(QColor(240, 240, 240, 225));
    setGradientOrientation(Qt::Vertical);
}

QString LegendOverlayStyle::displayLabelFor(const QString &propertyName) const
{
    static const QHash<QString, QString> labels = {
        // General
        { QStringLiteral("showTitle"),         QStringLiteral("General — Show title") },
        { QStringLiteral("title"),             QStringLiteral("General — Title") },
        { QStringLiteral("titleFont"),         QStringLiteral("General — Title font") },
        { QStringLiteral("titleColor"),        QStringLiteral("General — Title color") },
        { QStringLiteral("itemFont"),          QStringLiteral("General — Item font") },
        { QStringLiteral("itemColor"),         QStringLiteral("General — Item color") },
        { QStringLiteral("layerHeaderFont"),   QStringLiteral("General — Layer header font") },
        { QStringLiteral("layerHeaderColor"),  QStringLiteral("General — Layer header color") },
        { QStringLiteral("rowSpacing"),        QStringLiteral("General — Row spacing") },
        { QStringLiteral("swatchSize"),        QStringLiteral("General — Swatch size") },
        { QStringLiteral("padding"),           QStringLiteral("General — Padding") },
        { QStringLiteral("anchor"),            QStringLiteral("General — Anchor") },
        { QStringLiteral("opacity"),           QStringLiteral("General — Opacity") },

        // Frame
        { QStringLiteral("showFrame"),         QStringLiteral("Frame — Show frame") },
        { QStringLiteral("frameColor"),        QStringLiteral("Frame — Color") },
        { QStringLiteral("frameWidth"),        QStringLiteral("Frame — Width") },
        { QStringLiteral("cornerRadius"),      QStringLiteral("Frame — Corner radius") },

        // Background
        { QStringLiteral("backgroundMode"),        QStringLiteral("Background — Mode") },
        { QStringLiteral("backgroundColor"),       QStringLiteral("Background — Color") },
        { QStringLiteral("gradientEndColor"),      QStringLiteral("Background — Gradient end color") },
        { QStringLiteral("gradientOrientation"),   QStringLiteral("Background — Gradient orientation") },
    };
    const auto it = labels.constFind(propertyName);
    return it != labels.constEnd() ? it.value() : propertyName;
}

QJsonObject LegendOverlayStyle::toJson() const
{
    QJsonObject j;
    // General
    j[QStringLiteral("showTitle")]        = m_showTitle;
    j[QStringLiteral("title")]            = m_title;
    j[QStringLiteral("titleFont")]        = fontToJson(m_titleFont);
    j[QStringLiteral("titleColor")]       = m_titleColor.name(QColor::HexArgb);
    j[QStringLiteral("itemFont")]         = fontToJson(m_itemFont);
    j[QStringLiteral("itemColor")]        = m_itemColor.name(QColor::HexArgb);
    j[QStringLiteral("layerHeaderFont")]  = fontToJson(m_layerHeaderFont);
    j[QStringLiteral("layerHeaderColor")] = m_layerHeaderColor.name(QColor::HexArgb);
    j[QStringLiteral("rowSpacing")]       = m_rowSpacing;
    j[QStringLiteral("swatchSize")]       = m_swatchSize;
    j[QStringLiteral("padding")]          = m_padding;
    j[QStringLiteral("anchor")]           = enumToString(m_anchor);
    j[QStringLiteral("opacity")]          = m_opacity;

    // Frame
    j[QStringLiteral("showFrame")]        = m_showFrame;
    j[QStringLiteral("frameColor")]       = m_frameColor.name(QColor::HexArgb);
    j[QStringLiteral("frameWidth")]       = m_frameWidth;
    j[QStringLiteral("cornerRadius")]     = m_cornerRadius;

    // Background
    j[QStringLiteral("backgroundMode")]      = enumToString(m_backgroundMode);
    j[QStringLiteral("backgroundColor")]     = m_backgroundColor.name(QColor::HexArgb);
    j[QStringLiteral("gradientEndColor")]    = m_gradientEndColor.name(QColor::HexArgb);
    j[QStringLiteral("gradientOrientation")] =
        m_gradientOrientation == Qt::Horizontal ? QStringLiteral("Horizontal")
                                                 : QStringLiteral("Vertical");
    return j;
}

void LegendOverlayStyle::fromJson(const QJsonObject &j)
{
    // General — missing keys keep current value (callers reset first if they
    // want defaults).
    if (j.contains(QStringLiteral("showTitle")))        setShowTitle(j.value(QStringLiteral("showTitle")).toBool());
    if (j.contains(QStringLiteral("title")))            setTitle(j.value(QStringLiteral("title")).toString());
    if (j.contains(QStringLiteral("titleFont")))
        setTitleFont(fontFromJson(j.value(QStringLiteral("titleFont")).toObject(), m_titleFont));
    if (j.contains(QStringLiteral("titleColor")))       setTitleColor(QColor(j.value(QStringLiteral("titleColor")).toString()));
    if (j.contains(QStringLiteral("itemFont")))
        setItemFont(fontFromJson(j.value(QStringLiteral("itemFont")).toObject(), m_itemFont));
    if (j.contains(QStringLiteral("itemColor")))        setItemColor(QColor(j.value(QStringLiteral("itemColor")).toString()));
    if (j.contains(QStringLiteral("layerHeaderFont")))
        setLayerHeaderFont(fontFromJson(j.value(QStringLiteral("layerHeaderFont")).toObject(), m_layerHeaderFont));
    if (j.contains(QStringLiteral("layerHeaderColor"))) setLayerHeaderColor(QColor(j.value(QStringLiteral("layerHeaderColor")).toString()));
    if (j.contains(QStringLiteral("rowSpacing")))       setRowSpacing(j.value(QStringLiteral("rowSpacing")).toInt(m_rowSpacing));
    if (j.contains(QStringLiteral("swatchSize")))       setSwatchSize(j.value(QStringLiteral("swatchSize")).toInt(m_swatchSize));
    if (j.contains(QStringLiteral("padding")))          setPadding(j.value(QStringLiteral("padding")).toInt(m_padding));
    if (j.contains(QStringLiteral("anchor")))           setAnchor(enumFromString<Anchor>(j.value(QStringLiteral("anchor")).toString(), m_anchor));
    if (j.contains(QStringLiteral("opacity")))          setOpacity(j.value(QStringLiteral("opacity")).toDouble(m_opacity));

    // Frame
    if (j.contains(QStringLiteral("showFrame")))        setShowFrame(j.value(QStringLiteral("showFrame")).toBool());
    if (j.contains(QStringLiteral("frameColor")))       setFrameColor(QColor(j.value(QStringLiteral("frameColor")).toString()));
    if (j.contains(QStringLiteral("frameWidth")))       setFrameWidth(j.value(QStringLiteral("frameWidth")).toDouble(m_frameWidth));
    if (j.contains(QStringLiteral("cornerRadius")))     setCornerRadius(j.value(QStringLiteral("cornerRadius")).toInt(m_cornerRadius));

    // Background
    if (j.contains(QStringLiteral("backgroundMode")))    setBackgroundMode(enumFromString<BackgroundMode>(j.value(QStringLiteral("backgroundMode")).toString(), m_backgroundMode));
    if (j.contains(QStringLiteral("backgroundColor")))   setBackgroundColor(QColor(j.value(QStringLiteral("backgroundColor")).toString()));
    if (j.contains(QStringLiteral("gradientEndColor"))) setGradientEndColor(QColor(j.value(QStringLiteral("gradientEndColor")).toString()));
    if (j.contains(QStringLiteral("gradientOrientation"))) {
        const QString s = j.value(QStringLiteral("gradientOrientation")).toString();
        setGradientOrientation(s == QLatin1String("Horizontal") ? Qt::Horizontal : Qt::Vertical);
    }
}

// ── Setters ─────────────────────────────────────────────────────────────
// Each setter follows the same pattern: bail out on no-op, store, emit the
// per-property signal, emit the canonical changed() aggregate.

#define SETTER(Type, name, member, signal)                                 \
    void LegendOverlayStyle::set##name(Type v) {                            \
        if (member == v) return;                                            \
        member = v;                                                         \
        emit signal(v);                                                     \
        emit changed();                                                     \
    }

SETTER(bool,            ShowTitle,        m_showTitle,        showTitleChanged)
SETTER(const QString &, Title,            m_title,            titleChanged)
SETTER(const QFont &,   TitleFont,        m_titleFont,        titleFontChanged)
SETTER(const QColor &,  TitleColor,       m_titleColor,       titleColorChanged)
SETTER(const QFont &,   ItemFont,         m_itemFont,         itemFontChanged)
SETTER(const QColor &,  ItemColor,        m_itemColor,        itemColorChanged)
SETTER(const QFont &,   LayerHeaderFont,  m_layerHeaderFont,  layerHeaderFontChanged)
SETTER(const QColor &,  LayerHeaderColor, m_layerHeaderColor, layerHeaderColorChanged)
SETTER(int,             RowSpacing,       m_rowSpacing,       rowSpacingChanged)
SETTER(int,             SwatchSize,       m_swatchSize,       swatchSizeChanged)
SETTER(int,             Padding,          m_padding,          paddingChanged)
SETTER(LegendOverlayStyle::Anchor, Anchor,m_anchor,           anchorChanged)
SETTER(qreal,           Opacity,          m_opacity,          opacityChanged)

SETTER(bool,            ShowFrame,        m_showFrame,        showFrameChanged)
SETTER(const QColor &,  FrameColor,       m_frameColor,       frameColorChanged)
SETTER(qreal,           FrameWidth,       m_frameWidth,       frameWidthChanged)
SETTER(int,             CornerRadius,     m_cornerRadius,     cornerRadiusChanged)

SETTER(LegendOverlayStyle::BackgroundMode, BackgroundMode, m_backgroundMode, backgroundModeChanged)
SETTER(const QColor &,  BackgroundColor,     m_backgroundColor,     backgroundColorChanged)
SETTER(const QColor &,  GradientEndColor,    m_gradientEndColor,    gradientEndColorChanged)
SETTER(Qt::Orientation, GradientOrientation, m_gradientOrientation, gradientOrientationChanged)

#undef SETTER

} // namespace OpenSWMM::Render
