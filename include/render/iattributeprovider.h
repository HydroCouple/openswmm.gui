/*!
 * \file   iattributeprovider.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Layer-side seam for "what attributes can I theme by?" — Slice DM.1.
 *
 *         The renderer panels (Graduated / Categorized / the AN.* raster
 *         adapters' attribute slot) need to populate their attribute
 *         combos. Before this interface they hardcoded per-category
 *         suggestion lists (categorizedrendererpanel.cpp:114). With
 *         IAttributeProvider, every themeable layer owns the source of
 *         truth — `availableAttributes(category)` returns the canonical
 *         names + display labels + dynamic-or-static flag + units.
 *
 *         Panels consume it via:
 *
 *             auto *p = qobject_cast<IAttributeProvider*>(ctx.hostLayer);
 *             if (p) combo->populate(p->availableAttributes(*ctx.category));
 *
 *         No provider available (Rule-based path with no host, GIS-vector
 *         layer that hasn't implemented it yet) — fall through to the
 *         legacy free-text QLineEdit behaviour.
 *
 *         See docs/RENDERING_DIALOG_DEMO_PLAN.md §2.
 */

#ifndef OPENSWMM_RENDER_IATTRIBUTEPROVIDER_H
#define OPENSWMM_RENDER_IATTRIBUTEPROVIDER_H

#include "layers/swmm_category.h"

#include <QMetaType>
#include <QString>
#include <QVariant>
#include <QVector>

namespace OpenSWMM::Render
{

/*!
 * \struct AttributeField
 * \brief One themeable attribute on a layer.
 *
 *        \c name is the canonical key the renderer's
 *        `setClassifyAttribute` / `setAttribute` consumes. It must round-
 *        trip through `.swmm-rule.json` unchanged. \c displayName is the
 *        i18n string the combo shows ("flow (m³/s)" rather than "flow").
 *        \c isDynamic marks attributes that vary per animation frame —
 *        Z.7 already gates "Recompute breaks per frame" on this flag.
 */
struct AttributeField
{
    QString          name;                          /*!< Canonical lookup key. */
    QString          displayName;                   /*!< i18n label for the picker. */
    QMetaType::Type  type = QMetaType::Double;      /*!< Storage type — drives delegate choice. */
    bool             isDynamic = false;             /*!< True if value varies per animation frame. */
    QString          unit;                          /*!< "m", "m/s", "m³/s", … (free-form). */
};

/*!
 * \class IAttributeProvider
 * \brief Interface every themeable layer implements so renderer panels
 *        can populate attribute combos without hardcoding the lists.
 *
 *        Implementations are normally inherited by OpenSWMMVisLayer
 *        subclasses (SWMMModelLayer / SWMMResultsLayer /
 *        SWMM2DResultsLayer / GISVectorLayer). The interface is a plain
 *        abstract base — no Q_OBJECT — so layers stay single-inheritance
 *        of QObject.
 */
class IAttributeProvider
{
public:
    virtual ~IAttributeProvider() = default;

    /*! Available themeable fields for \p cat. Empty list is a valid
     *  answer — means the layer doesn't surface per-feature attributes
     *  on this category (e.g. SWMMModelLayer::CatRainGages has no per-
     *  feature engine fields).
     *
     *  Order = preferred picker order. Callers should NOT re-sort. */
    [[nodiscard]] virtual QVector<AttributeField>
        availableAttributes(OpenSWMMVis::SwmmCategory cat) const = 0;
};

} // namespace OpenSWMM::Render

Q_DECLARE_INTERFACE(OpenSWMM::Render::IAttributeProvider,
                    "io.opensimulator.openswmm.IAttributeProvider/1.0")

Q_DECLARE_METATYPE(OpenSWMM::Render::AttributeField)

#endif // OPENSWMM_RENDER_IATTRIBUTEPROVIDER_H
