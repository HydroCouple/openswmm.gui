#include "ui/theme/iconfactory.h"

#include "ui/theme/thememanager.h"
#include "ui/theme/themetokens.h"

#include <QFile>
#include <QHash>
#include <QIconEngine>
#include <QPainter>
#include <QPixmapCache>
#include <QSvgRenderer>

namespace openswmmvis::ui {

namespace {

/*! The monochrome glyph grays baked into resources/images/*.svg. Only
 *  these are substituted — deliberate colors (rule-status red/green,
 *  thermometer blues, pure white/black) are left untouched. */
const char *kGlyphGrays[] = {
    "#777777", "#626262", "#989898", "#A4A0A0", "#9E9E9E",
};

QColor mixed(const QColor &fg, const QColor &bg, qreal fgShare)
{
    const qreal bgShare = 1.0 - fgShare;
    return QColor(int(fg.red()   * fgShare + bg.red()   * bgShare),
                  int(fg.green() * fgShare + bg.green() * bgShare),
                  int(fg.blue()  * fgShare + bg.blue()  * bgShare));
}

QColor glyphColorFor(QIcon::Mode mode)
{
    const ThemeColors &c = ThemeManager::instance()->colors();
    switch (mode) {
    case QIcon::Active:      return c.text;
    case QIcon::Selected:    return c.selectionText;   // painted on Highlight
    case QIcon::Disabled:    return mixed(c.hintText, c.surfaceWindow, 0.5);
    case QIcon::Normal:
    default:                 return c.hintText;
    }
}

class ThemedIconEngine : public QIconEngine
{
public:
    explicit ThemedIconEngine(const QString &alias)
        : mAlias(alias)
    {
    }

    QIconEngine *clone() const override
    {
        return new ThemedIconEngine(mAlias);
    }

    void paint(QPainter *painter, const QRect &rect,
               QIcon::Mode mode, QIcon::State state) override
    {
        const qreal dpr = painter->device()
                              ? painter->device()->devicePixelRatio()
                              : 1.0;
        const QPixmap pm = scaledPixmap(rect.size(), mode, state, dpr);
        if (!pm.isNull())
            painter->drawPixmap(rect, pm);
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode,
                   QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode,
                         QIcon::State state, qreal scale) override
    {
        Q_UNUSED(state);
        if (size.isEmpty())
            return {};

        const ThemeManager *theme = ThemeManager::instance();
        const QString cacheKey =
            QStringLiteral("themedicon|%1|%2|%3|%4x%5|%6")
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

    bool isNull() override
    {
        return sourceSvg().isEmpty();
    }

private:
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

}   // namespace

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

    const QIcon icon(new ThemedIconEngine(alias));   // QIcon takes ownership
    s_icons.insert(alias, icon);
    return icon;
}

}   // namespace openswmmvis::ui
