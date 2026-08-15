/*!
 * \file   palettedrasterrenderer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  VS.6 — categorical (paletted) raster renderer.
 *
 *         Maps discrete integer class values (land-use / soil / zone codes)
 *         to colours, the raster analogue of CategorizedRenderer on the
 *         feature side. Each class carries a value, a human label, and a
 *         colour. Classes not explicitly listed fall back to a wrapping
 *         CategoricalPalette lookup so an unclassified raster still draws.
 *
 *         Implements IRasterRenderer so GISRasterLayer can swap it in via
 *         setRasterRenderer().
 */

#ifndef OPENSWMM_RENDER_PALETTEDRASTERRENDERER_H
#define OPENSWMM_RENDER_PALETTEDRASTERRENDERER_H

#include "render/irasterrenderer.h"

#include <QColor>
#include <QHash>
#include <QList>
#include <QString>

namespace OpenSWMM::Render
{

/*!
 * \class PalettedRasterRenderer
 * \brief Discrete value → colour raster renderer (categorical rasters).
 */
class PalettedRasterRenderer final : public IRasterRenderer
{
public:
    /*! One classified raster value. */
    struct Class
    {
        int     value = 0;     /*!< The raster cell value (rounded to int). */
        QString label;         /*!< Legend label; defaults to the value. */
        QColor  color;         /*!< Swatch + pixel colour. */
    };

    PalettedRasterRenderer() = default;
    ~PalettedRasterRenderer() override = default;

    [[nodiscard]] const QList<Class> &classes() const { return m_classes; }
    void setClasses(QList<Class> classes);

    /*! Palette used to auto-assign colours to classes that have none and to
     *  colour values not present in the class list. CategoricalPalette name
     *  (e.g. "Tab10"); unknown names fall back to Tab10. */
    [[nodiscard]] QString paletteName() const { return m_paletteName; }
    void setPaletteName(QString name);

    /*! Build the class list automatically from a set of observed integer
     *  values, assigning labels = the value and colours from the palette. */
    void buildClassesFromValues(const QList<int> &uniqueValues);

    // IRasterRenderer.
    [[nodiscard]] QString rendererId() const override
    {
        return QStringLiteral("palettedraster");
    }
    [[nodiscard]] QColor colorForValue(double value,
                                       bool isNoData = false) const override;
    [[nodiscard]] QList<LegendSymbolItem> legendSymbolItems() const override;
    [[nodiscard]] QJsonObject toJson() const override;
    void fromJson(const QJsonObject &j) override;
    [[nodiscard]] std::unique_ptr<IRasterRenderer> clone() const override;

private:
    [[nodiscard]] QColor paletteColor(int index) const;
    void rebuildIndex();

    QList<Class>      m_classes;
    QHash<int, int>   m_valueToIndex;   /*!< value → index into m_classes (cache). */
    QString           m_paletteName = QStringLiteral("Tab10");
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_PALETTEDRASTERRENDERER_H
