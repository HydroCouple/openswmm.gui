/**
 * @file      iconfactory.cpp
 * @brief     UI redesign P3 — theme-aware icon rendering.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "ui/theme/iconfactory.h"

#include "ui/theme/thememanager.h"
#include "ui/theme/themetokens.h"

#include <QFile>
#include <QHash>
#include <QIconEngine>
#include <QPainter>
#include <QPixmapCache>
#include <QSvgRenderer>

namespace openswmmvis::ui
{

namespace
{

/*! The monochrome glyph grays baked into SVGs under resources/images. Only
 *  these are substituted — deliberate colors (rule-status red/green,
 *  thermometer blues, pure white/black) are left untouched. */
const char *kGlyphGrays[] = {
    "#777777", "#626262", "#989898", "#A4A0A0", "#9E9E9E",
};

/**
 * @brief Blend RGB channel values without retaining input alpha.
 * @param[in] fg Foreground colour.
 * @param[in] bg Background colour.
 * @param[in] fgShare Foreground weight, expected in [0,1]; no clamp is applied here.
 * @return Opaque QColor with integer-truncated weighted RGB channels.
 */
QColor mixed(const QColor &fg, const QColor &bg, qreal fgShare)
{
    const qreal bgShare = 1.0 - fgShare;
    return QColor(int(fg.red() * fgShare + bg.red() * bgShare),
                  int(fg.green() * fgShare + bg.green() * bgShare),
                  int(fg.blue() * fgShare + bg.blue() * bgShare));
}

/**
 * @brief Resolve a monochrome glyph colour from the current scheme and icon interaction mode.
 * @param[in] mode Active uses text, Selected selectionText, Disabled a 50% hint/window mix, Normal hintText.
 * @return Theme-derived colour; unknown modes follow Normal.
 */
QColor glyphColorFor(QIcon::Mode mode)
{
    const ThemeColors &c = ThemeManager::instance()->colors();
    switch (mode) {
    case QIcon::Active:
        return c.text;
    case QIcon::Selected:
        return c.selectionText; // painted on Highlight
    case QIcon::Disabled:
        return mixed(c.hintText, c.surfaceWindow, 0.5);
    case QIcon::Normal:
    default:
        return c.hintText;
    }
}

class ThemedIconEngine : public QIconEngine
{
public:
    /**
     * @brief Store a resource alias for lazy SVG loading.
     * @param[in] alias Exact :/swmmvis/ resource suffix, copied into this engine.
     */
    explicit ThemedIconEngine(const QString &alias) : mAlias(alias) {}

    /**
     * @brief Clone this alias-based engine without copying its lazy source cache.
     * @return Newly allocated engine ownership for the QIcon framework.
     */
    QIconEngine *clone() const override { return new ThemedIconEngine(mAlias); }

    /**
     * @brief Draw a rendered cached icon into the requested painter rectangle at device scale.
     * @param[in,out] painter Required painter; device DPR is read when available.
     * @param[in] rect Destination logical-pixel rectangle.
     * @param[in] mode Interaction mode selecting glyph colour.
     * @param[in] state On/Off state forwarded but ignored by the rendering implementation.
     */
    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State state) override
    {
        const qreal dpr = painter->device() ? painter->device()->devicePixelRatio() : 1.0;
        const QPixmap pm = scaledPixmap(rect.size(), mode, state, dpr);
        if (!pm.isNull())
            painter->drawPixmap(rect, pm);
    }

    /**
     * @brief Render/cache a standard-scale alias icon.
     * @param[in] size Logical icon dimensions.
     * @param[in] mode Interaction mode selecting glyph colour.
     * @param[in] state On/Off state, ignored.
     * @return Raster from scaledPixmap() at scale one.
     */
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }

    /**
     * @brief Load/recolour/rasterize SVG at a scale and cache it by alias/scheme/mode/size/scale.
     * @param[in] size Logical dimensions; empty returns a null pixmap.
     * @param[in] mode Glyph interaction colour.
     * @param[in] state On/Off state, ignored and not part of the cache key.
     * @param[in] scale Positive finite render scale/device-pixel ratio, expected valid.
     * @return Cached/new transparent-background raster, or null for missing/invalid SVG content.
     */
    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state,
                         qreal scale) override
    {
        Q_UNUSED(state);
        if (size.isEmpty())
            return {};

        const ThemeManager *theme = ThemeManager::instance();
        const QString cacheKey = QStringLiteral("themedicon|%1|%2|%3|%4x%5|%6")
                                     .arg(mAlias)
                                     .arg(int(theme->effectiveScheme()))
                                     .arg(int(mode))
                                     .arg(size.width())
                                     .arg(size.height())
                                     .arg(scale);
        QPixmap pm;
        if (QPixmapCache::find(cacheKey, &pm))
            return pm;

        const QByteArray svg = recoloredSvg(glyphColorFor(mode));
        if (svg.isEmpty())
            return {};
        QSvgRenderer renderer(svg);
        if (!renderer.isValid())
            return {};

        QImage image(size * scale, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter p(&image);
        renderer.render(&p, QRectF(QPointF(0, 0), QSizeF(size * scale)));
        p.end();
        image.setDevicePixelRatio(scale);

        pm = QPixmap::fromImage(image);
        QPixmapCache::insert(cacheKey, pm);
        return pm;
    }

    /**
     * @brief Test whether lazy source bytes are absent.
     * @return True for empty/unreadable source; a nonempty invalid SVG can report false here yet fail rendering.
     */
    bool isNull() override { return sourceSvg().isEmpty(); }

private:
    /**
     * @brief Read resource bytes once, retaining success or failure in the engine cache.
     * @return Engine-held source reference; an unreadable resource remains empty without retry.
     */
    const QByteArray &sourceSvg()
    {
        if (!mLoaded) {
            mLoaded = true;
            QFile file(QStringLiteral(":/swmmvis/%1").arg(mAlias));
            if (file.open(QIODevice::ReadOnly))
                mSvg = file.readAll();
        }
        return mSvg;
    }

    /**
     * @brief Copy source bytes and replace only the fixed monochrome hex family with the requested glyph RGB.
     * @param[in] glyph Replacement colour; alpha is omitted by HexRgb encoding.
     * @return Recoloured bytes or empty for absent source. Literal replacement is case-sensitive; no XML-wide colour parsing occurs.
     */
    QByteArray recoloredSvg(const QColor &glyph)
    {
        QByteArray svg = sourceSvg();
        if (svg.isEmpty())
            return svg;
        const QByteArray target = glyph.name(QColor::HexRgb).toLatin1();
        for (const char *gray : kGlyphGrays)
            svg.replace(gray, target.constData());
        return svg;
    }

    QString mAlias;
    QByteArray mSvg;
    bool mLoaded = false;
};

} // namespace

QIcon IconFactory::icon(const QString &alias)
{
    // One QIcon per alias — engines render per-theme via the pixmap cache
    // key, so a single shared icon serves every scheme and mode.
    static QHash<QString, QIcon> s_icons;
    auto it = s_icons.constFind(alias);
    if (it != s_icons.constEnd())
        return it.value();

    if (!QFile::exists(QStringLiteral(":/swmmvis/%1").arg(alias)))
        return QIcon();

    const QIcon icon(new ThemedIconEngine(alias)); // QIcon takes ownership
    s_icons.insert(alias, icon);
    return icon;
}

} // namespace openswmmvis::ui
