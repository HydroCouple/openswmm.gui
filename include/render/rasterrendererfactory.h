/*!
 * \file   rasterrendererfactory.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  JSON id → IRasterRenderer construction, the raster-side sibling
 *         of RendererFactory::makeRenderer. Used wherever a raster style is
 *         restored from JSON: StyleFileIO (dialog Cancel / undo /
 *         .swmm-style.json) and ProjectSerializer (.oswp).
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
 */
[[nodiscard]] std::unique_ptr<IRasterRenderer> makeRasterRenderer(const QJsonObject &j);

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_RASTERRENDERERFACTORY_H
