/*!
 * \file   meshcommands.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "map/meshcommands.h"

#include "layers/swmm2dmeshlayer.h"
#include "map/mapcanvas.h"
#include "mesh/meshbctype.h"
#include "mesh/meshcellparams.h"

#include <QCoreApplication>

#include <cmath>

namespace {

/*! Write \p value to triangle \p tri through the layer's apply* helper, which
 *  emits `attributeChanged` so every view refreshes. Refuses unknown keys and
 *  the engine-pending ones (the `gw.*` group). */
bool applyCellParam(SWMM2DMeshLayer *layer, int tri, const QByteArray &key,
                    double value)
{
    if (!layer) return false;
    const mesh::CellParamSpec *s = mesh::cellParamSpec(key);
    if (!s || !s->enabled) return false;
    if (key == "mannings")  return layer->applyMeshTriangleMannings(tri, value);
    if (key == "initDepth") return layer->applyMeshTriangleInitDepth(tri, value);
    return false;   // engine support pending — see the registry's gw.* entries
}

} // namespace

// ===========================================================================
// MeshSetTriangleAttributeCommand
// ===========================================================================

MeshSetTriangleAttributeCommand::MeshSetTriangleAttributeCommand(
        SWMM2DMeshLayer *layer, QByteArray key, QVector<int> triangles,
        QVector<double> newValues, QVector<double> oldValues,
        const QString &text, MapCanvas *canvas, QUndoCommand *parent)
    : MapCommand(text, canvas, parent),
      m_layer(layer),
      m_key(std::move(key)),
      m_tris(std::move(triangles)),
      m_newValues(std::move(newValues)),
      m_oldValues(std::move(oldValues))
{
}

void MeshSetTriangleAttributeCommand::apply(const QVector<double> &values)
{
    if (!m_layer) return;   // layer closed — nothing to restore onto
    for (int i = 0; i < m_tris.size() && i < values.size(); ++i) {
        const double v = values[i];
        // NaN = "was unset". The layer has no way to clear an attribute back
        // to unset, so restore the spec default instead of writing NaN (which
        // applyMeshTriangle* would reject and leave the edited value in place).
        if (std::isfinite(v)) {
            applyCellParam(m_layer, m_tris[i], m_key, v);
        } else if (const mesh::CellParamSpec *s = mesh::cellParamSpec(m_key)) {
            applyCellParam(m_layer, m_tris[i], m_key, s->defaultValue);
        }
    }
}

void MeshSetTriangleAttributeCommand::undo() { apply(m_oldValues); }
void MeshSetTriangleAttributeCommand::redo() { apply(m_newValues); }

// ===========================================================================
// MeshSetTriangleTagCommand
// ===========================================================================

MeshSetTriangleTagCommand::MeshSetTriangleTagCommand(
        SWMM2DMeshLayer *layer, QVector<int> triangles, QString newTag,
        QVector<QString> oldTags, const QString &text, MapCanvas *canvas,
        QUndoCommand *parent)
    : MapCommand(text, canvas, parent),
      m_layer(layer),
      m_tris(std::move(triangles)),
      m_newTag(std::move(newTag)),
      m_oldTags(std::move(oldTags))
{
}

void MeshSetTriangleTagCommand::redo()
{
    if (!m_layer) return;
    for (int t : m_tris) m_layer->applyMeshTriangleTag(t, m_newTag);
}

void MeshSetTriangleTagCommand::undo()
{
    if (!m_layer) return;
    for (int i = 0; i < m_tris.size() && i < m_oldTags.size(); ++i)
        m_layer->applyMeshTriangleTag(m_tris[i], m_oldTags[i]);
}

// ===========================================================================
// MeshSetVertexAttributeCommand
// ===========================================================================

MeshSetVertexAttributeCommand::MeshSetVertexAttributeCommand(
        SWMM2DMeshLayer *layer, QByteArray key, QVector<int> vertices,
        QVector<mesh::MeshVertex> newAttrs, QVector<mesh::MeshVertex> oldAttrs,
        const QString &text, MapCanvas *canvas, QUndoCommand *parent)
    : MapCommand(text, canvas, parent),
      m_layer(layer),
      m_key(std::move(key)),
      m_verts(std::move(vertices)),
      m_newAttrs(std::move(newAttrs)),
      m_oldAttrs(std::move(oldAttrs))
{
}

void MeshSetVertexAttributeCommand::apply(const QVector<mesh::MeshVertex> &attrs)
{
    if (!m_layer) return;   // layer closed — nothing to restore onto
    for (int i = 0; i < m_verts.size() && i < attrs.size(); ++i) {
        const int v = m_verts[i];
        const mesh::MeshVertex &a = attrs[i];
        // Order matters: applyMeshVertexCouplingCd/Area reject an uncoupled
        // vertex, and clearing coupledNode resets both back to the engine
        // defaults — so the node id has to land first.
        m_layer->applyMeshVertexTag(v, a.tag);
        m_layer->applyMeshVertexCoupledNode(v, a.coupledNode);
        m_layer->applyMeshVertexCouplingCd(v, a.couplingCd);
        m_layer->applyMeshVertexCouplingArea(v, a.couplingArea);
        m_layer->applyMeshVertexZ(v, a.z);
    }
}

void MeshSetVertexAttributeCommand::undo() { apply(m_oldAttrs); }
void MeshSetVertexAttributeCommand::redo() { apply(m_newAttrs); }

// ===========================================================================
// MeshSetEdgeAttributeCommand
// ===========================================================================

MeshSetEdgeAttributeCommand::MeshSetEdgeAttributeCommand(
        SWMM2DMeshLayer *layer, QByteArray key, QVector<int> edgeSlots,
        QVector<mesh::MeshEdgeBC> newBCs, QVector<mesh::MeshEdgeBC> oldBCs,
        const QString &text, MapCanvas *canvas, QUndoCommand *parent)
    : MapCommand(text, canvas, parent),
      m_layer(layer),
      m_key(std::move(key)),
      m_slots(std::move(edgeSlots)),
      m_newBCs(std::move(newBCs)),
      m_oldBCs(std::move(oldBCs))
{
}

void MeshSetEdgeAttributeCommand::apply(const QVector<mesh::MeshEdgeBC> &bcs)
{
    if (!m_layer) return;
    const bool conveyance = (m_key == "conveyance");
    for (int i = 0; i < m_slots.size() && i < bcs.size(); ++i) {
        const int tri = m_slots[i] / 3;
        const int e   = m_slots[i] % 3;
        if (conveyance) {
            // Goes through the mirroring helper so the neighbour half of an
            // interior edge follows the value back on undo, exactly as it
            // followed it forward on redo.
            m_layer->applyMeshEdgeConveyance(tri, e, bcs[i].conveyance);
        } else {
            m_layer->applyMeshEdgeBC(tri, e, bcs[i]);
        }
    }
}

void MeshSetEdgeAttributeCommand::undo() { apply(m_oldBCs); }
void MeshSetEdgeAttributeCommand::redo() { apply(m_newBCs); }

// ===========================================================================
// Push helpers
// ===========================================================================

namespace mesh {

namespace {

/*! Build the changed-only subset and push it. Shared by both public helpers. */
int pushEdits(SWMM2DMeshLayer *layer, const QVector<int> &triangles,
              const QVector<double> &values, const QByteArray &key,
              const QString &text, MapCanvas *canvas)
{
    if (!layer) return 0;
    const CellParamSpec *spec = cellParamSpec(key);
    if (!spec || !spec->enabled) return 0;

    const MeshResult &m = layer->mesh();
    QVector<int>    tris;
    QVector<double> newV;
    QVector<double> oldV;
    tris.reserve(triangles.size());
    newV.reserve(triangles.size());
    oldV.reserve(triangles.size());

    for (int i = 0; i < triangles.size() && i < values.size(); ++i) {
        const int    t = triangles[i];
        const double v = values[i];
        if (t < 0 || t >= m.triangles.size() || !std::isfinite(v)) continue;
        const double prev = cellParamValue(m, t, key);
        if (std::isfinite(prev) && prev == v) continue;   // already there
        tris.append(t);
        newV.append(v);
        oldV.append(prev);
    }
    if (tris.isEmpty()) return 0;

    auto *cmd = new MeshSetTriangleAttributeCommand(
        layer, key, tris, newV, oldV, text, canvas);
    if (canvas && canvas->undoStack()) {
        canvas->undoStack()->push(cmd);   // push() runs redo()
    } else {
        // No stack (headless / detached canvas) — still perform the edit.
        cmd->redo();
        delete cmd;
    }
    return tris.size();
}

} // namespace

int pushCellParamEdit(SWMM2DMeshLayer *layer, const QVector<int> &triangles,
                      const QByteArray &key, double value, MapCanvas *canvas)
{
    const CellParamSpec *spec = cellParamSpec(key);
    const QString text = QCoreApplication::translate(
                             "MeshCellParams", "Set %1 on %n cell(s)", nullptr,
                             int(triangles.size()))
                             .arg(spec ? spec->label : QString::fromUtf8(key));
    return pushEdits(layer, triangles, QVector<double>(triangles.size(), value),
                     key, text, canvas);
}

int pushCellParamEdits(SWMM2DMeshLayer *layer, const QVector<int> &triangles,
                       const QVector<double> &values, const QByteArray &key,
                       const QString &text, MapCanvas *canvas)
{
    return pushEdits(layer, triangles, values, key, text, canvas);
}

int pushCellTagEdit(SWMM2DMeshLayer *layer, const QVector<int> &triangles,
                    const QString &tag, MapCanvas *canvas)
{
    if (!layer) return 0;
    const MeshResult &m = layer->mesh();
    QVector<int>     tris;
    QVector<QString> oldTags;
    for (int t : triangles) {
        if (t < 0 || t >= m.triangles.size()) continue;
        if (m.triangles[t].tag == tag) continue;    // already there
        tris.append(t);
        oldTags.append(m.triangles[t].tag);
    }
    if (tris.isEmpty()) return 0;

    const QString text = QCoreApplication::translate(
        "MeshCellParams", "Set tag on %n cell(s)", nullptr, int(tris.size()));
    auto *cmd = new MeshSetTriangleTagCommand(layer, tris, tag, oldTags, text,
                                              canvas);
    if (canvas && canvas->undoStack()) {
        canvas->undoStack()->push(cmd);
    } else {
        cmd->redo();
        delete cmd;
    }
    return tris.size();
}

namespace {

/*! Push \p cmd onto the canvas's stack (which runs redo()), or run it directly
 *  when there is no stack — headless tools and tests still get the edit. */
void pushOrRun(QUndoCommand *cmd, MapCanvas *canvas)
{
    if (canvas && canvas->undoStack()) {
        canvas->undoStack()->push(cmd);   // push() runs redo()
    } else {
        cmd->redo();
        delete cmd;
    }
}

/*! Human-readable name for a vertex/edge attribute key, for the undo label. */
QString attrLabel(const QByteArray &key)
{
    const auto tr = [](const char *s) {
        return QCoreApplication::translate("MeshCommands", s);
    };
    if (key == "z")            return tr("elevation");
    if (key == "tag")          return tr("tag");
    if (key == "coupledNode")  return tr("coupled node");
    if (key == "couplingCd")   return tr("coupling Cd");
    if (key == "couplingArea") return tr("coupling area");
    if (key == "conveyance")   return tr("conveyance");
    if (key == "bcType")       return tr("boundary type");
    if (key == "head")         return tr("stage");
    if (key == "slope")        return tr("bed slope");
    if (key == "flow")         return tr("flow");
    if (key == "tseries")      return tr("time series");
    if (key == "curve")        return tr("rating curve");
    if (key == "group")        return tr("group");
    if (key == "bc")           return tr("boundary condition");
    return QString::fromUtf8(key);
}

/*! Write \p value into \p attrs under \p key. Returns false for an unknown key
 *  or a value the layer would reject anyway (non-positive Cd / area). */
bool setVertexAttr(MeshVertex &attrs, const QByteArray &key,
                   const QVariant &value)
{
    if (key == "z") {
        bool ok = false;
        const double z = value.toDouble(&ok);
        if (!ok || !std::isfinite(z)) return false;
        attrs.z = z;
        return true;
    }
    if (key == "tag")         { attrs.tag         = value.toString(); return true; }
    if (key == "coupledNode") { attrs.coupledNode = value.toString(); return true; }
    if (key == "couplingCd") {
        const double cd = value.toDouble();
        if (!(cd > 0.0)) return false;
        attrs.couplingCd = cd;
        return true;
    }
    if (key == "couplingArea") {
        const double a = value.toDouble();
        if (!(a > 0.0)) return false;
        attrs.couplingArea = a;
        return true;
    }
    return false;
}

/*! Write \p value into \p bc under \p key. Returns false for an unknown key or
 *  an out-of-range value. */
bool setEdgeAttr(MeshEdgeBC &bc, const QByteArray &key, const QVariant &value)
{
    if (key == "conveyance") {
        const double c = value.toDouble();
        if (!(c >= 0.0 && c <= 1.0)) return false;
        bc.conveyance = c;
        return true;
    }
    if (key == "bcType") {
        bool ok = false;
        const int t = value.toInt(&ok);
        if (!ok || t < int(MeshBCTypes::Type::Wall)
                || t > int(MeshBCTypes::Type::RatingCurve))
            return false;
        bc.type = static_cast<MeshBCTypes::Type>(t);
        return true;
    }
    if (key == "head")    { bc.head    = value.toDouble(); return true; }
    if (key == "slope")   { bc.slope   = value.toDouble(); return true; }
    if (key == "flow")    { bc.flow    = value.toDouble(); return true; }
    if (key == "tseries") { bc.tseries = value.toString(); return true; }
    if (key == "curve")   { bc.curve   = value.toString(); return true; }
    if (key == "group")   { bc.group   = value.toString(); return true; }
    return false;
}

/*! The editable-attribute subset of a vertex, as the undo command snapshots it. */
MeshVertex vertexAttrsOf(const MeshResult &m, int v)
{
    MeshVertex a;
    if (v >= 0 && v < m.vertices.size()) {
        const MeshVertex &src = m.vertices[v];
        a.z            = src.z;
        a.tag          = src.tag;
        a.coupledNode  = src.coupledNode;
        a.couplingCd   = src.couplingCd;
        a.couplingArea = src.couplingArea;
    }
    return a;
}

/*! True when the two snapshots would produce identical vertex state. */
bool sameVertexAttrs(const MeshVertex &a, const MeshVertex &b)
{
    return a.z == b.z && a.tag == b.tag && a.coupledNode == b.coupledNode
        && a.couplingCd == b.couplingCd && a.couplingArea == b.couplingArea;
}

} // namespace

int pushVertexParamEdit(SWMM2DMeshLayer *layer, const QVector<int> &vertices,
                        const QByteArray &key, const QVariant &value,
                        MapCanvas *canvas)
{
    if (!layer) return 0;
    const MeshResult &m = layer->mesh();

    QVector<int>        verts;
    QVector<MeshVertex> newA;
    QVector<MeshVertex> oldA;
    verts.reserve(vertices.size());
    newA.reserve(vertices.size());
    oldA.reserve(vertices.size());

    // Cd / Area exist only on a coupled vertex; the layer rejects them
    // elsewhere, so an uncoupled vertex in a mixed selection must not enter
    // the command — otherwise undo would "restore" a value never written.
    const bool couplingOnly = (key == "couplingCd" || key == "couplingArea");

    for (int v : vertices) {
        if (v < 0 || v >= m.vertices.size()) continue;
        if (couplingOnly && m.vertices[v].coupledNode.isEmpty()) continue;
        const MeshVertex before = vertexAttrsOf(m, v);
        MeshVertex after = before;
        if (!setVertexAttr(after, key, value)) continue;   // unknown / rejected
        if (sameVertexAttrs(before, after)) continue;      // already there
        verts.append(v);
        newA.append(after);
        oldA.append(before);
    }
    if (verts.isEmpty()) return 0;

    const QString text = QCoreApplication::translate(
                             "MeshCommands", "Set %1 on %n vertex(es)", nullptr,
                             int(verts.size()))
                             .arg(attrLabel(key));
    pushOrRun(new MeshSetVertexAttributeCommand(layer, key, verts, newA, oldA,
                                                text, canvas),
              canvas);
    return verts.size();
}

int pushEdgeParamEdit(SWMM2DMeshLayer *layer,
                      const QVector<QPair<int, int>> &edges,
                      const QByteArray &key, const QVariant &value,
                      MapCanvas *canvas)
{
    if (!layer) return 0;
    // Conveyance is a property of EVERY edge (it attenuates the flux across
    // interior faces too); the boundary-condition fields are meaningful only
    // where the mesh actually ends.
    const bool boundaryOnly = (key != "conveyance");
    const QVector<MeshEdgeBC> &existing = layer->edgeBCs();

    QVector<int>        edgeSlots;
    QVector<MeshEdgeBC> newB;
    QVector<MeshEdgeBC> oldB;

    for (const auto &pr : edges) {
        const int tri = pr.first, e = pr.second;
        if (tri < 0 || e < 0 || e > 2) continue;
        const int flat = tri * 3 + e;
        if (flat < 0 || flat >= existing.size()) continue;
        if (boundaryOnly && !layer->isBoundaryEdge(tri, e)) continue;
        const MeshEdgeBC before = existing[flat];
        MeshEdgeBC after = before;
        if (!setEdgeAttr(after, key, value)) continue;
        if (before == after) continue;
        edgeSlots.append(flat);
        newB.append(after);
        oldB.append(before);
    }
    if (edgeSlots.isEmpty()) return 0;

    const QString text = QCoreApplication::translate(
                             "MeshCommands", "Set %1 on %n edge(s)", nullptr,
                             int(edgeSlots.size()))
                             .arg(attrLabel(key));
    pushOrRun(new MeshSetEdgeAttributeCommand(layer, key, edgeSlots, newB, oldB,
                                              text, canvas),
              canvas);
    return edgeSlots.size();
}

int pushEdgeBCEdit(SWMM2DMeshLayer *layer,
                   const QVector<QPair<int, int>> &edges,
                   const MeshEdgeBC &bc, MapCanvas *canvas)
{
    if (!layer) return 0;
    const QVector<MeshEdgeBC> &existing = layer->edgeBCs();

    QVector<int>        edgeSlots;
    QVector<MeshEdgeBC> newB;
    QVector<MeshEdgeBC> oldB;

    for (const auto &pr : edges) {
        const int tri = pr.first, e = pr.second;
        if (tri < 0 || e < 0 || e > 2) continue;
        const int flat = tri * 3 + e;
        if (flat < 0 || flat >= existing.size()) continue;
        if (!layer->isBoundaryEdge(tri, e)) continue;   // BC = boundary only
        const MeshEdgeBC before = existing[flat];
        MeshEdgeBC after = bc;
        // group and conveyance are orthogonal to the BC type and are edited on
        // their own paths — carry each slot's own value across.
        after.group      = before.group;
        after.conveyance = before.conveyance;
        if (before == after) continue;
        edgeSlots.append(flat);
        newB.append(after);
        oldB.append(before);
    }
    if (edgeSlots.isEmpty()) return 0;

    const QString text = QCoreApplication::translate(
        "MeshCommands", "Set boundary condition on %n edge(s)", nullptr,
        int(edgeSlots.size()));
    pushOrRun(new MeshSetEdgeAttributeCommand(layer, "bc", edgeSlots, newB, oldB,
                                              text, canvas),
              canvas);
    return edgeSlots.size();
}

} // namespace mesh
