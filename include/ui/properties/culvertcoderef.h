/*!
 * \file   culvertcoderef.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Phase 0 of docs/ATTRIBUTE_EDITOR_WIRING_PLAN_2026-06-04.md — value
 * type for the conduit "Culvert Code" Property Browser row. Replaces
 * the former `LinkCompoundEditRef::CulvertCode` compound cell (which
 * opened `LinkCompoundEditDialog`): the culvert code is a single enum
 * value, so the cell now edits inline via `CulvertCodeComboBox`.
 *
 * Same registration dance as `LinkCompoundEditRef`: declared as a Qt
 * metatype so QPropertyModel stores it in a QVariant, a QString
 * converter renders the descriptive label when the row isn't in edit
 * mode, and a custom editor creator (registered in
 * `attributepanel.cpp`) hands out the combobox in edit mode.
 */

#ifndef CULVERTCODEREF_H
#define CULVERTCODEREF_H

#include <QMetaType>
#include <QString>

#include <openswmm/engine/openswmm_callbacks.h>  // SWMM_Engine typedef

class SWMMModelLayer;

/*! Identifies the culvert-code attribute on a single conduit. */
struct CulvertCodeRef
{
    SWMM_Engine     engine = nullptr;  ///< Engine handle (borrow, not owned)
    QString         linkName;          ///< Owning conduit id
    int             code   = 0;        ///< HDS-5 code; 0 = none
    /*! Owning model layer (borrow) — writes route through
     *  `SWMMModelLayer::applyLinkCulvertCode` so the Map + Attribute
     *  Table refresh via `attributeChanged(linkName)`. */
    SWMMModelLayer *layer  = nullptr;

    bool operator==(const CulvertCodeRef &other) const noexcept
    {
        return engine == other.engine && linkName == other.linkName
               && code == other.code && layer == other.layer;
    }
};

Q_DECLARE_METATYPE(CulvertCodeRef)

/*! Install the `CulvertCodeRef → QString` converter (renders
 *  `culvertCodeLabel(code)`). Idempotent. */
void registerCulvertCodeRefConverter();

#endif // CULVERTCODEREF_H
