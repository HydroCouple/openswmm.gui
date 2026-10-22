/**
 * @file      swmmdataobjectpropertyadapter.h
 * @brief     Common base class for the 14 non-spatial Data Object property adapters.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice DA.2 — Common base class for the 14 non-spatial Data Object
 * property adapters. Provides the QObject + name + engine handle
 * plumbing that every adapter shares so the per-type subclasses only
 * declare their type-specific Q_PROPERTYs and accessors.
 *
 * Designed to mirror the contract of the spatial-side adapters
 * (`SWMMNodePropertyAdapter`, `SWMMLinkPropertyAdapter`,
 * `SWMMSubcatchPropertyAdapter`) so `PropertiesPanel::routeNonSpatialFeature`
 * can dispatch uniformly across the spatial / non-spatial split.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef SWMMDATAOBJECTPROPERTYADAPTER_H
#define SWMMDATAOBJECTPROPERTYADAPTER_H

#include <QObject>
#include <QString>

#include <openswmm/engine/openswmm_engine.h>

class SWMMModelLayer;

/*!
 * \class SWMMDataObjectPropertyAdapter
 * \brief Common base for non-spatial Data Object property adapters.
 *
 * Stores the engine handle + stored name + emits the renameRequested /
 * changed / displayLabelsChanged signals. Concrete subclasses add the
 * type-specific `Q_PROPERTY`s, override `index()` to resolve their
 * engine index, and call `emit changed()` on successful field writes.
 */
class SWMMDataObjectPropertyAdapter : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name WRITE setName)

public:
    /**
     * @brief Bind property reads to a named nonspatial engine object.
     * @param[in] engine Borrowed engine; must remain valid while the adapter is used.
     * @param[in] name Stored engine ID copied into the adapter and resolved afresh by subclass getters.
     * @param[in] parent Optional Qt owner; construct/use on the GUI thread.
     * @details Connects active-project unit changes to displayLabelsChanged; does not create or acquire ownership of model data.
     */
    SWMMDataObjectPropertyAdapter(SWMM_Engine engine, QString name, QObject *parent = nullptr);
    /**
     * @brief Destroy the QObject adapter without closing the borrowed engine or layer.
     */
    ~SWMMDataObjectPropertyAdapter() override = default;

    /**
     * @return Stored case-preserved object name; changed only by updateStoredName().
     */
    [[nodiscard]] QString name() const { return m_name; }
    /**
     * @return Borrowed engine handle, possibly nullptr; no lifetime extension occurs.
     */
    [[nodiscard]] SWMM_Engine engine() const { return m_engine; }

    /*! Slice BM.0-Browse-Edit — bind the active project's model layer
     *  so subclasses that surface `DataObjectRef` properties can
     *  construct refs with the right layer pointer (used by
     *  `DataObjectPickerEditor` to enumerate combo items + dispatch
     *  the "…" browse button). Optional — adapters that don't surface
     *  refs ignore it.
     * @param[in] layer Borrowed project layer used by reference pickers; nullptr detaches. Must outlive adapter use because this is a raw pointer.
     */
    void setModelLayer(SWMMModelLayer *layer) { m_layer = layer; }
    /**
     * @return Borrowed currently bound layer or nullptr.
     */
    [[nodiscard]] SWMMModelLayer *modelLayer() const { return m_layer; }

public slots:
    /*! Emits renameRequested(oldName, newName); the layer-side handler
     *  decides whether the rename can be applied (uniqueness check) and
     *  calls back via updateStoredName(). Mirrors the spatial adapter
     *  pattern.
     * @param[in] newName Proposed ID, trimmed before comparison; empty or unchanged names are ignored.
     * @details Emits a rename request only; engine and stored name remain unchanged until the receiver accepts and calls updateStoredName().
     */
    void setName(const QString &newName);

    /*! After a successful layer-side rename, the layer notifies the
     *  adapter via this slot so subsequent engine calls hit the new
     *  index.
     * @param[in] newName Accepted engine ID copied verbatim; this setter does not rename the engine or emit changed.
     */
    void updateStoredName(const QString &newName) { m_name = newName; }

    /*! Causes the bound Property Browser model to re-read every
     *  Q_PROPERTY. Used when the underlying engine state changes
     *  via a side channel (e.g. another panel's edit).
     * @details Emits changed unconditionally so the property model rereads live values; no engine write occurs.
     */
    void refresh() { emit changed(); }

signals:
    /*! Emitted when any field's underlying engine value changes. */
    void changed();

    /*! Emitted when the user types a new name in the panel. The
     *  receiver (SWMMModelLayer) decides whether to accept and then
     *  calls updateStoredName() back.
     * @param[in] oldName Stored ID being renamed.
     * @param[in] newName Nonempty trimmed requested replacement; receiver validates uniqueness and engine state.
     */
    void renameRequested(const QString &oldName, const QString &newName);

    /*! Emitted when the active unit system changes — the bound
     *  Property Browser re-reads display labels (e.g. "Area (ft²)"
     *  vs "Area (m²)"). */
    void displayLabelsChanged();

protected:
    SWMM_Engine m_engine;
    QString m_name;
    SWMMModelLayer *m_layer = nullptr; ///< Bound via setModelLayer.
};

#endif // SWMMDATAOBJECTPROPERTYADAPTER_H
