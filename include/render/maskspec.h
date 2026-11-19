/**
 * @file      maskspec.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Typed config for the Layer Properties → Mask tab (Slice Z.14).
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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
    ClipInside = 0,  /*!< Paint only what falls inside the source polygons. */
    ClipOutside = 1, /*!< Paint only what falls outside (inverted mask). */
};

/** @brief Encode a mask mode for JSON persistence.
 * @param m Mode to encode; unrecognized enum values fall back to ClipInside.
 * @return clipInside or clipOutside token. */
[[nodiscard]] QString maskModeToString(MaskMode m);
/** @brief Decode an exact, case-sensitive mask-mode token.
 * @param s Token without trimming or case conversion.
 * @return Recognized mode, otherwise ClipInside. */
[[nodiscard]] MaskMode maskModeFromString(const QString &s);

/*!
 * \struct MaskSpec
 * \brief Per-layer mask configuration.
 */
struct MaskSpec {
    /*! \brief Master switch. When false, paint is unmasked. */
    bool enabled = false;

    /*! \brief Layer id of the polygon source. Empty when no source has
     *         been picked yet — paint falls through to unmasked even
     *         with enabled=true so the user sees the layer while
     *         picking a mask. */
    QString sourceLayerId;

    /*! \brief Which side paints. */
    MaskMode mode = MaskMode::ClipInside;

    /** @brief Serialize mask configuration without resolving the source layer.
     * @return Owned object omitting default fields; source ID text is preserved verbatim. */
    [[nodiscard]] QJsonObject toJson() const;
    /** @brief Load mask settings with defaults for absent or mistyped fields.
     * @param j Mask JSON; unknown mode tokens select ClipInside.
     * @return Owned settings, without checking that the source ID exists or refers
     * to polygons. Empty source IDs are retained even when enabled is true. */
    static MaskSpec fromJson(const QJsonObject &j);

    /** @brief Compare the enabled flag, exact source ID and mode.
     * @param other Mask definition to compare without modification.
     * @return True when all three stored fields match. */
    [[nodiscard]] bool operator==(const MaskSpec &other) const;
    /** @brief Test whether mask definitions differ.
     * @param other Definition to compare without modification.
     * @return Negation of operator==(). */
    [[nodiscard]] bool operator!=(const MaskSpec &other) const { return !(*this == other); }
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_MASKSPEC_H
