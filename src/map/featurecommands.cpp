/*!
 * \file   featurecommands.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "map/featurecommands.h"
#include "map/mapcanvas.h"

#include <QCoreApplication>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcFeatureCmd, "openswmmvis.feature.command")

using namespace openswmmvis::feature;

namespace openswmmvis::map {

namespace {

QString tr_(const char *s)
{
    return QCoreApplication::translate("FeatureCommands", s);
}

/*! Ask the canvas to redraw after a layer write. Scene | Overlay is the
 *  channel pair OpenSWMMVisMapToolAddSubcatchment::commit uses
 *  (maptooladdsubcatchment.cpp:222-223): the layer's content changed, so the
 *  cached scene buffers must miss, not just the decorations. */
void repaint(MapCanvas *canvas, const char *reason)
{
    if (canvas)
        canvas->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                           QString::fromLatin1(reason));
}

}   // namespace

// ---------------------------------------------------------------------------
// FeatureCommandBase
// ---------------------------------------------------------------------------

FeatureCommandBase::FeatureCommandBase(const QString &text, FeatureLayer *layer,
                                       MapCanvas *canvas, QUndoCommand *parent)
    : MapCommand(text, canvas, parent)
    , m_layer(layer)
{
}

void FeatureCommandBase::recordFailure(const QString &err) const
{
    m_lastError = err;
    qCWarning(lcFeatureCmd).noquote() << err;
}

// ---------------------------------------------------------------------------
// AddFeatureCommand
// ---------------------------------------------------------------------------

AddFeatureCommand::AddFeatureCommand(FeatureLayer *layer, const Feature &feature,
                                     MapCanvas *canvas, QUndoCommand *parent)
    : FeatureCommandBase(tr_("Add feature"), layer, canvas, parent)
    , m_feature(feature)
{
}

void AddFeatureCommand::redo()
{
    FeatureLayer *l = layer();
    if (!l) return;

    QString err;
    const FeatureId id = l->addFeature(m_feature, &err);
    if (id == kInvalidFeatureId) {
        recordFailure(tr_("Could not add the feature: %1").arg(err));
        return;
    }
    // Keep the id so a later redo reuses it (stable-id contract).
    m_feature.id = id;
    repaint(m_canvas, "feature-add");
}

void AddFeatureCommand::undo()
{
    FeatureLayer *l = layer();
    if (!l || m_feature.id == kInvalidFeatureId) return;

    QString err;
    if (!l->removeFeature(m_feature.id, &err))
        recordFailure(tr_("Could not undo the added feature: %1").arg(err));
    repaint(m_canvas, "feature-add-undo");
}

// ---------------------------------------------------------------------------
// DeleteFeaturesCommand
// ---------------------------------------------------------------------------

DeleteFeaturesCommand::DeleteFeaturesCommand(FeatureLayer *layer,
                                             const QVector<FeatureId> &ids,
                                             MapCanvas *canvas, QUndoCommand *parent)
    : FeatureCommandBase(ids.size() == 1 ? tr_("Delete feature")
                                         : tr_("Delete features"),
                         layer, canvas, parent)
{
    // Snapshot at CONSTRUCTION, not at redo: by the time redo runs the caller
    // may already have cleared its selection, and a snapshot taken then would
    // be of features that no longer exist.
    if (!layer) return;
    m_removed.reserve(ids.size());
    for (FeatureId id : ids) {
        Feature f;
        if (layer->feature(id, f)) m_removed.append(f);
    }
}

void DeleteFeaturesCommand::redo()
{
    FeatureLayer *l = layer();
    if (!l) return;

    for (const Feature &f : m_removed) {
        QString err;
        if (!l->removeFeature(f.id, &err))
            recordFailure(tr_("Could not delete feature %1: %2").arg(f.id).arg(err));
    }
    repaint(m_canvas, "feature-delete");
}

void DeleteFeaturesCommand::undo()
{
    FeatureLayer *l = layer();
    if (!l) return;

    for (const Feature &f : m_removed) {
        QString err;
        if (l->addFeature(f, &err) == kInvalidFeatureId)
            recordFailure(tr_("Could not restore feature %1: %2").arg(f.id).arg(err));
    }
    repaint(m_canvas, "feature-delete-undo");
}

// ---------------------------------------------------------------------------
// EditFeatureGeometryCommand
// ---------------------------------------------------------------------------

EditFeatureGeometryCommand::EditFeatureGeometryCommand(
        FeatureLayer *layer, FeatureId id,
        const FeatureGeometry &oldGeom, const FeatureGeometry &newGeom,
        const QString &text, bool mergeable,
        MapCanvas *canvas, QUndoCommand *parent)
    : FeatureCommandBase(text.isEmpty() ? tr_("Edit geometry") : text,
                         layer, canvas, parent)
    , m_id(id)
    , m_old(oldGeom)
    , m_new(newGeom)
    , m_mergeable(mergeable)
{
}

void EditFeatureGeometryCommand::redo()
{
    FeatureLayer *l = layer();
    if (!l) return;
    QString err;
    if (!l->setGeometry(m_id, m_new, &err))
        recordFailure(tr_("Could not change the geometry of feature %1: %2")
                          .arg(m_id).arg(err));
    repaint(m_canvas, "feature-geom");
}

void EditFeatureGeometryCommand::undo()
{
    FeatureLayer *l = layer();
    if (!l) return;
    QString err;
    if (!l->setGeometry(m_id, m_old, &err))
        recordFailure(tr_("Could not undo the geometry change on feature %1: %2")
                          .arg(m_id).arg(err));
    repaint(m_canvas, "feature-geom-undo");
}

bool EditFeatureGeometryCommand::mergeWith(const QUndoCommand *other)
{
    if (!m_mergeable) return false;
    const auto *o = dynamic_cast<const EditFeatureGeometryCommand *>(other);
    // Only merge consecutive edits of the same feature on the same layer —
    // otherwise a drag on one vertex would swallow an unrelated edit that
    // happened to follow it.
    if (!o || !o->m_mergeable || o->m_id != m_id || o->m_layer != m_layer)
        return false;
    m_new = o->m_new;   // keep our original "before", take their "after"
    return true;
}

// ---------------------------------------------------------------------------
// EditFeatureAttributesCommand
// ---------------------------------------------------------------------------

EditFeatureAttributesCommand::EditFeatureAttributesCommand(
        FeatureLayer *layer, FeatureId id,
        const QVariantMap &oldAttrs, const QVariantMap &newAttrs,
        MapCanvas *canvas, QUndoCommand *parent)
    : FeatureCommandBase(tr_("Edit attributes"), layer, canvas, parent)
    , m_id(id)
    , m_old(oldAttrs)
    , m_new(newAttrs)
{
}

void EditFeatureAttributesCommand::redo()
{
    FeatureLayer *l = layer();
    if (!l) return;
    QString err;
    if (!l->setAttributes(m_id, m_new, &err))
        recordFailure(tr_("Could not change the attributes of feature %1: %2")
                          .arg(m_id).arg(err));
    repaint(m_canvas, "feature-attrs");
}

void EditFeatureAttributesCommand::undo()
{
    FeatureLayer *l = layer();
    if (!l) return;
    QString err;
    if (!l->setAttributes(m_id, m_old, &err))
        recordFailure(tr_("Could not undo the attribute change on feature %1: %2")
                          .arg(m_id).arg(err));
    repaint(m_canvas, "feature-attrs-undo");
}

// ---------------------------------------------------------------------------
// AddFieldCommand
// ---------------------------------------------------------------------------

AddFieldCommand::AddFieldCommand(FeatureLayer *layer, const FieldDef &field,
                                 MapCanvas *canvas, QUndoCommand *parent)
    : FeatureCommandBase(tr_("Add column \"%1\"").arg(field.name),
                         layer, canvas, parent)
    , m_field(field)
{
}

void AddFieldCommand::redo()
{
    FeatureLayer *l = layer();
    if (!l) return;
    QString err;
    if (!l->addField(m_field, &err))
        recordFailure(tr_("Could not add the column \"%1\": %2")
                          .arg(m_field.name, err));
}

void AddFieldCommand::undo()
{
    FeatureLayer *l = layer();
    if (!l) return;
    QString err;
    if (!l->removeField(m_field.name, &err)) {
        // Not fatal: the column stays and carries default values. Reported so
        // the user is not left believing the undo took effect.
        recordFailure(tr_("The column \"%1\" could not be removed on undo: %2")
                          .arg(m_field.name, err));
    }
}

// ---------------------------------------------------------------------------
// RemoveFieldCommand
// ---------------------------------------------------------------------------

RemoveFieldCommand::RemoveFieldCommand(FeatureLayer *layer, const QString &fieldName,
                                       MapCanvas *canvas, QUndoCommand *parent)
    : FeatureCommandBase(tr_("Remove column \"%1\"").arg(fieldName),
                         layer, canvas, parent)
{
    if (!layer) return;
    if (const FieldDef *f = layer->schema().field(fieldName))
        m_field = *f;

    // Snapshot every value before the column goes, so undo restores content
    // rather than an empty column.
    const QVector<Feature> feats = layer->allFeatures();
    for (const Feature &f : feats) {
        const auto it = f.attributes.constFind(fieldName);
        if (it != f.attributes.constEnd())
            m_values.insert(f.id, it.value());
    }
}

void RemoveFieldCommand::redo()
{
    FeatureLayer *l = layer();
    if (!l || !m_field.isValid()) return;
    QString err;
    if (!l->removeField(m_field.name, &err))
        recordFailure(tr_("Could not remove the column \"%1\": %2")
                          .arg(m_field.name, err));
}

void RemoveFieldCommand::undo()
{
    FeatureLayer *l = layer();
    if (!l || !m_field.isValid()) return;

    QString err;
    if (!l->addField(m_field, &err)) {
        recordFailure(tr_("Could not restore the column \"%1\": %2")
                          .arg(m_field.name, err));
        return;
    }
    for (auto it = m_values.constBegin(); it != m_values.constEnd(); ++it) {
        Feature f;
        if (!l->feature(it.key(), f)) continue;
        f.attributes.insert(m_field.name, it.value());
        QString setErr;
        if (!l->setAttributes(f.id, f.attributes, &setErr))
            recordFailure(tr_("Could not restore \"%1\" on feature %2: %3")
                              .arg(m_field.name).arg(it.key()).arg(setErr));
    }
}

// ---------------------------------------------------------------------------
// ResampleZCommand
// ---------------------------------------------------------------------------

ResampleZCommand::ResampleZCommand(FeatureLayer *layer,
                                   const QVector<FeatureId> &ids,
                                   MapCanvas *canvas, QUndoCommand *parent)
    : FeatureCommandBase(tr_("Resample Z"), layer, canvas, parent)
{
    if (!layer) return;
    m_ids = ids.isEmpty() ? layer->featureIds() : ids;

    m_before.reserve(m_ids.size());
    for (FeatureId id : std::as_const(m_ids)) {
        Feature f;
        m_before.append(layer->feature(id, f) ? f.geometry : FeatureGeometry{});
    }
}

void ResampleZCommand::redo()
{
    FeatureLayer *l = layer();
    if (!l) return;

    // The sampled result is computed once and cached: a later redo must not
    // re-sample, or an intervening change to the raster or the mesh would
    // silently give a different answer than the one the user saw.
    if (!m_captured) {
        m_after.reserve(m_ids.size());
        m_unsampled = 0;
        for (int i = 0; i < m_ids.size(); ++i) {
            FeatureGeometry g = m_before.value(i);
            m_unsampled += l->sampleZ(g, m_canvas, /*densify=*/true);
            m_after.append(g);
        }
        m_captured = true;
    }

    for (int i = 0; i < m_ids.size(); ++i) {
        QString err;
        if (!l->setGeometry(m_ids.at(i), m_after.value(i), &err))
            recordFailure(tr_("Could not resample feature %1: %2")
                              .arg(m_ids.at(i)).arg(err));
    }
    repaint(m_canvas, "feature-resample-z");
}

void ResampleZCommand::undo()
{
    FeatureLayer *l = layer();
    if (!l) return;
    for (int i = 0; i < m_ids.size(); ++i) {
        QString err;
        if (!l->setGeometry(m_ids.at(i), m_before.value(i), &err))
            recordFailure(tr_("Could not undo the resample of feature %1: %2")
                              .arg(m_ids.at(i)).arg(err));
    }
    repaint(m_canvas, "feature-resample-z-undo");
}

}   // namespace openswmmvis::map
