/**
 * @file      palettedrasterrenderer.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     VS.6 — categorical (paletted) raster renderer.
 *
 *         Maps discrete integer class values (land-use / soil / zone codes)
 *         to colours, the raster analogue of CategorizedRenderer on the
 *         feature side. Each class carries a value, a human label, and a
 *         colour. Classes not explicitly listed fall back to a wrapping
 *         CategoricalPalette lookup so an unclassified raster still draws.
 *
 *         Implements IRasterRenderer so GISRasterLayer can swap it in via
 *         setRasterRenderer().
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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
    struct Class {
        int value = 0; /*!< The raster cell value (rounded to int). */
        QString label; /*!< Legend label; defaults to the value. */
        QColor color;  /*!< Swatch + pixel colour. */
    };

    /** @brief Construct no explicit classes and a Tab10 fallback palette. */
    PalettedRasterRenderer() = default;
    /** @brief Destroy class storage and its lookup cache. */
    ~PalettedRasterRenderer() override = default;

    /** @brief Inspect explicit classes in legend order.
     * @return Borrowed list valid until replacement/loading/destruction. */
    [[nodiscard]] const QList<Class> &classes() const { return m_classes; }
    /** @brief Replace explicit classes and rebuild their value lookup.
     * @param classes Ordered list moved without sorting, deduplication or color
     * validation. Duplicate values remain in legends; the last duplicate wins
     * pixel lookup. Invalid colors are retained without palette substitution. */
    void setClasses(QList<Class> classes);

    /** @brief Read the palette used for automatic class building and unlisted values.
     * @return Stored name; CategoricalPalette::byName() resolves unknown names to Tab10.
     * Explicit invalid class colors are not automatically replaced. */
    [[nodiscard]] QString paletteName() const { return m_paletteName; }
    /** @brief Replace palette metadata without recoloring existing classes.
     * @param name Name moved verbatim, resolved through CategoricalPalette::byName()
     * only when colors are requested. No repaint notification is emitted. */
    void setPaletteName(QString name);

    /*! Build the class list automatically from a set of observed integer
     *  values, assigning labels = the value and colours from the palette.
     * @param uniqueValues Ordered input values, copied without enforcing uniqueness
     * or sorting. Colors cycle by list position, not the integer value. Replaces
     * existing classes and rebuilds the cache; duplicate lookup uses the last row. */
    void buildClassesFromValues(const QList<int> &uniqueValues);

    // IRasterRenderer.
    /** @brief Identify the persisted renderer type.
     * @return Exact token palettedraster. */
    [[nodiscard]] QString rendererId() const override { return QStringLiteral("palettedraster"); }
    /** @brief Round a raw value and select its explicit or fallback palette color.
     * @param value Raw category scalar; NaN is transparent. Other non-no-data input
     * must be finite and round to an int-representable value. Infinities and
     * out-of-range conversions are not guarded by the implementation.
     * @param isNoData True to return transparent without converting value.
     * @return Explicit last-matching class color, including invalid stored colors,
     * otherwise the cyclic palette color indexed by std::lround(value), with
     * halfway values rounded away from zero. Negative integers wrap the palette. */
    [[nodiscard]] QColor colorForValue(double value, bool isNoData = false) const override;
    /** @brief Build a reference row per explicit class, without a fallback-palette row.
     * @return Owned rows in list order with stored/nonempty labels or integer text,
     * class-value keys and fill swatches. Duplicate values produce duplicate rows. */
    [[nodiscard]] QList<LegendSymbolItem> legendSymbolItems() const override;
    /** @brief Serialize palette name and every explicit class in its current order.
     * @return Owned object with integer values, verbatim labels and ARGB color strings. */
    [[nodiscard]] QJsonObject toJson() const override;
    /** @brief Replace palette/class configuration and rebuild its lookup cache.
     * @param j Object whose id is ignored. Missing/mistyped palette becomes Tab10,
     * missing/nonarray classes becomes empty, and malformed rows use value zero,
     * empty labels, and gray (150,150,150) for invalid colors. No deduplication occurs. */
    void fromJson(const QJsonObject &j) override;
    /** @brief Copy explicit classes, palette metadata and the lookup cache.
     * @return Sole ownership of the independent renderer snapshot. */
    [[nodiscard]] std::unique_ptr<IRasterRenderer> clone() const override;

private:
    /** @brief Resolve one cyclic fallback-palette color.
     * @param index Signed palette index; negative values wrap into range.
     * @return Owned color from the resolved palette, or gray if it is empty. */
    [[nodiscard]] QColor paletteColor(int index) const;
    /** @brief Rebuild value-to-row mappings, with later duplicates replacing earlier indices. */
    void rebuildIndex();

    QList<Class> m_classes;
    QHash<int, int> m_valueToIndex; /*!< value → index into m_classes (cache). */
    QString m_paletteName = QStringLiteral("Tab10");
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_PALETTEDRASTERRENDERER_H
