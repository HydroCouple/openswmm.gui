/**
 * @file      sublayerstyle.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     QObject base class for per-sublayer style property bags.
 *
 *         Plan reference: RENDERING_OUTPUT_SUBLAYERS_PLAN.md §2 Decision 4
 *         and §4.2 (each ISublayer exposes a SublayerStyle subclass via
 *         style() returning a QObject*; the existing QPropertyModel
 *         framework — PropertiesPanel / LegendPropertiesDialog — drives the
 *         editor UI from the subclass's Q_PROPERTY declarations).
 *
 *         Style edits emit styleChanged() exactly once per property setter.
 *         Listeners are the parent ISublayer (which schedules a QSG
 *         re-upload) and the LegendDock (which refreshes the affected
 *         swatch). This is the single source of truth required by
 *         CLAUDE.md §5.1 (MVC — same data, multiple views, signal sync).
 *
 *         Q_CLASSINFO group convention: subclasses tag related properties
 *         into named sections that the QPropertyModel dialog can collapse:
 *
 *            Q_CLASSINFO("group:lineWidthPx",   "Symbology")
 *            Q_CLASSINFO("group:colorRamp",     "Classification")
 *            Q_CLASSINFO("group:floodedColor",  "Highlights")
 *
 *         Cross-slice: Slice S1 (sublayer foundation). Consumed by every
 *         concrete style bag in S2 (e.g. ConduitLineStyle, NodeMarkerStyle,
 *         DepthColorRampStyle, IsolineStyle, …).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_SUBLAYERSTYLE_H
#define OPENSWMM_RENDER_SUBLAYERSTYLE_H

#include <QJsonObject>
#include <QObject>

namespace OpenSWMM::Render
{

/*!
 * \class SublayerStyle
 * \brief Base class for per-sublayer style property bags.
 *
 *         Subclasses declare Q_PROPERTYs for the user-editable knobs
 *         specific to that sublayer kind (line width, colour ramp, marker
 *         shape, …). Setters must call setDirty() so styleChanged() is
 *         emitted exactly once per logical user edit.
 *
 *         JSON round-trip is mandatory so styles persist into .oswp and
 *         .swmm-style.json. Subclasses override toJson()/fromJson() —
 *         the base provides no automatic Q_PROPERTY → JSON walker because
 *         several property types (ColorRamp, IntervalBinner) carry their
 *         own non-trivial JSON schemas.
 */
class SublayerStyle : public QObject
{
    Q_OBJECT

public:
    /** @brief Construct a style base with ordinary Qt parent ownership.
     * @param parent Optional QObject owner, which destroys the style with itself.
     * Construction emits no styleChanged() signal. */
    explicit SublayerStyle(QObject *parent = nullptr) : QObject(parent) {}
    /** @brief Destroy the style and its QObject-owned children. */
    ~SublayerStyle() override = default;

    /*!
     * \brief Serialize the style to JSON.
     *
     *        Default implementation returns an empty object; subclasses
     *        override and write their Q_PROPERTYs out. The schemaVersion
     *        field is the subclass's responsibility.
     * @return Empty owned object in the base; concrete classes define their schema.
     */
    [[nodiscard]] virtual QJsonObject toJson() const { return {}; }

    /*!
     * \brief Restore the style from JSON.
     *
     *        Default implementation does nothing. Subclasses override to
     *        consume the schema they wrote in toJson().
     *
     *        Subclasses must call setDirty() once at the end so listeners
     *        re-read the entire style atomically (rather than once per
     *        individual property setter during deserialization).
     * The unnamed QJsonObject argument is the borrowed style configuration.
     * The base ignores it and emits no signal; concrete implementations define
     * field validation, retained defaults, and restoration notifications.
     */
    virtual void fromJson(const QJsonObject &) {}

signals:
    /*!
     * \brief Emitted when any property changes.
     *
     *        Subclass setters call setDirty() which emits this signal.
     *        Listeners (ISublayer renderer + LegendDock) refresh on this
     *        signal alone — they do not subscribe to individual property
     *        change signals.
     */
    void styleChanged();

protected:
    /** @brief Emit styleChanged() immediately without tracking or comparing any state.
     * Subclasses determine whether a logical change occurred before calling this.
     * Repeated calls emit repeatedly; the base provides no batching or dirty flag. */
    void setDirty() { emit styleChanged(); }
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_SUBLAYERSTYLE_H
