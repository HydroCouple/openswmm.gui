/*!
 * \file   palettedrasterrenderer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  VS.6 — categorical (paletted) raster renderer.
 */

#include "render/renderers/palettedrasterrenderer.h"

#include "render/categoricalpalette.h"
#include "render/fillsymbollayer.h"
#include "render/symbolstyle.h"

#include <QJsonArray>
#include <QJsonObject>

#include <cmath>

namespace OpenSWMM::Render
{

namespace
{
// Build a single-fill legend swatch SymbolStyle for a class colour. Stores
// both the fill spec props and a "color" hex (the legend colour readers look
// for "color" on the first layer).
SymbolStyle swatchFor(const QColor &c)
{
    FillSymbolLayerSpec fill;
    fill.fillColor = c;
    fill.outlineColor = QColor(60, 60, 60);
    fill.outlineWidth = 0.5;
    SymbolLayer sl = fill.toSymbolLayer();
    sl.props.insert(QStringLiteral("color"), c.name(QColor::HexArgb));
    SymbolStyle s;
    s.layers.append(sl);
    return s;
}
} // namespace

void PalettedRasterRenderer::setClasses(QList<Class> classes)
{
    m_classes = std::move(classes);
    rebuildIndex();
}

void PalettedRasterRenderer::setPaletteName(QString name)
{
    m_paletteName = std::move(name);
}

void PalettedRasterRenderer::rebuildIndex()
{
    m_valueToIndex.clear();
    for (int i = 0; i < m_classes.size(); ++i)
        m_valueToIndex.insert(m_classes.at(i).value, i);
}

QColor PalettedRasterRenderer::paletteColor(int index) const
{
    const QList<QColor> pal = CategoricalPalette::byName(m_paletteName);
    if (pal.isEmpty())
        return QColor(150, 150, 150);
    const int n = pal.size();
    const int i = ((index % n) + n) % n;   // wrap, tolerate negatives
    return pal.at(i);
}

void PalettedRasterRenderer::buildClassesFromValues(const QList<int> &uniqueValues)
{
    QList<Class> built;
    built.reserve(uniqueValues.size());
    int idx = 0;
    for (int v : uniqueValues) {
        Class c;
        c.value = v;
        c.label = QString::number(v);
        c.color = paletteColor(idx++);
        built.append(c);
    }
    setClasses(std::move(built));
}

QColor PalettedRasterRenderer::colorForValue(double value, bool isNoData) const
{
    if (isNoData || std::isnan(value))
        return QColor(0, 0, 0, 0);
    const int k = static_cast<int>(std::lround(value));
    const auto it = m_valueToIndex.constFind(k);
    if (it != m_valueToIndex.constEnd())
        return m_classes.at(it.value()).color;
    // Unlisted value: derive a stable colour from the palette so the raster
    // still draws rather than vanishing.
    return paletteColor(k);
}

QList<LegendSymbolItem> PalettedRasterRenderer::legendSymbolItems() const
{
    QList<LegendSymbolItem> items;
    items.reserve(m_classes.size());
    int i = 0;
    for (const Class &c : m_classes) {
        LegendSymbolItem item;
        item.label     = c.label.isEmpty() ? QString::number(c.value) : c.label;
        item.symbol    = swatchFor(c.color);
        item.sortIndex = i;
        item.classKey  = QString::number(c.value);
        items.append(item);
        ++i;
    }
    return items;
}

QJsonObject PalettedRasterRenderer::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), rendererId());
    obj.insert(QStringLiteral("paletteName"), m_paletteName);
    QJsonArray arr;
    for (const Class &c : m_classes) {
        QJsonObject jc;
        jc.insert(QStringLiteral("value"), c.value);
        jc.insert(QStringLiteral("label"), c.label);
        jc.insert(QStringLiteral("color"), c.color.name(QColor::HexArgb));
        arr.append(jc);
    }
    obj.insert(QStringLiteral("classes"), arr);
    return obj;
}

void PalettedRasterRenderer::fromJson(const QJsonObject &j)
{
    m_paletteName = j.value(QStringLiteral("paletteName"))
                        .toString(QStringLiteral("Tab10"));
    QList<Class> built;
    const QJsonArray arr = j.value(QStringLiteral("classes")).toArray();
    built.reserve(arr.size());
    for (const QJsonValue &v : arr) {
        const QJsonObject jc = v.toObject();
        Class c;
        c.value = jc.value(QStringLiteral("value")).toInt();
        c.label = jc.value(QStringLiteral("label")).toString();
        const QColor col(jc.value(QStringLiteral("color")).toString());
        c.color = col.isValid() ? col : QColor(150, 150, 150);
        built.append(c);
    }
    setClasses(std::move(built));
}

std::unique_ptr<IRasterRenderer> PalettedRasterRenderer::clone() const
{
    return std::make_unique<PalettedRasterRenderer>(*this);
}

} // namespace OpenSWMM::Render
