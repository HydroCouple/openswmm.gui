/**
 * @file      rasterrendererfactory.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     JSON id → IRasterRenderer construction, the raster-side sibling
 *         of RendererFactory::makeRenderer. Used wherever a raster style is
 *         restored from JSON: StyleFileIO (dialog Cancel / undo /
 *         .swmm-style.json) and ProjectSerializer (.oswp).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_RASTERRENDERERFACTORY_H
#define OPENSWMM_RENDER_RASTERRENDERERFACTORY_H

#include <QJsonObject>

#include <memory>

namespace OpenSWMM::Render
{

class IRasterRenderer;

/*!
 * \brief Construct the renderer whose rendererId() equals \p j["id"] and
 *        restore it from \p j via fromJson(). Known ids: "graduatedraster",
 *        "palettedraster", "multibandcolor", "singlebandpseudocolor".
 * \return nullptr when the id is absent or unknown.
 * @param j Borrowed renderer JSON with an exact case-sensitive supported id;
 * no trimming occurs and configuration is passed to the concrete fromJson().
 * The returned pointer owns an independently editable renderer. Exceptions from
 * allocation or the concrete loader propagate; no source object is retained.
 */
[[nodiscard]] std::unique_ptr<IRasterRenderer> makeRasterRenderer(const QJsonObject &j);

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_RASTERRENDERERFACTORY_H
