/*!
 * \file   maskspec.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Typed config for the Layer Properties → Mask tab (Slice Z.14).
 *
 *         A mask clips a layer's paint output to (or outside of) a
 *         polygon source layer's geometry. RENDERING_RULE_MODEL_PLAN.md
 *         §11.3 calls for two modes:
 *           - ClipInside  — paint only what falls inside the polygon
 *           - ClipOutside — paint only what falls outside (the "everything
 *                            except the study area" inverted-mask pattern)
 *
 *         Source is identified by layer id (`sourceLayerId`) — at paint
 *         time the layer-tree looks up the layer by id, walks its
 *         polygon geometry, builds a clip path, and intersects with
 *         this layer's paint. The mask source layer must be a polygon
 *         vector layer; non-polygon sources are silently ignored.
 *
 *         Slice Z.14-data ships the value type + JSON round-trip. The
 *         Mask tab widget + paint integration are Z.14-ui and Z.14-paint
 *         (separate slices).
 */

#ifndef OPENSWMM_RENDER_MASKSPEC_H
#define OPENSWMM_RENDER_MASKSPEC_H

#include <QJsonObject>
#include <QString>

namespace OpenSWMM::Render
{

/*!
 * \enum MaskMode
 * \brief Which side of the polygon source layer paints.
 */
enum class MaskMode : int {
    ClipInside  = 0,   /*!< Paint only what falls inside the source polygons. */
    ClipOutside = 1,   /*!< Paint only what falls outside (inverted mask). */
};

[[nodiscard]] QString maskModeToString(MaskMode m);
[[nodiscard]] MaskMode maskModeFromString(const QString &s);

/*!
 * \struct MaskSpec
 * \brief Per-layer mask configuration.
 */
struct MaskSpec
{
    /*! \brief Master switch. When false, paint is unmasked. */
    bool      enabled = false;

    /*! \brief Layer id of the polygon source. Empty when no source has
     *         been picked yet — paint falls through to unmasked even
     *         with enabled=true so the user sees the layer while
     *         picking a mask. */
    QString   sourceLayerId;

    /*! \brief Which side paints. */
    MaskMode  mode = MaskMode::ClipInside;

    [[nodiscard]] QJsonObject toJson() const;
    static MaskSpec           fromJson(const QJsonObject &j);

    [[nodiscard]] bool operator==(const MaskSpec &other) const;
    [[nodiscard]] bool operator!=(const MaskSpec &other) const
    { return !(*this == other); }
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_MASKSPEC_H
