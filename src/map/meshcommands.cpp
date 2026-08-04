/*!
 * \file   meshcommands.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "map/meshcommands.h"

#include "layers/swmm2dmeshlayer.h"
#include "map/mapcanvas.h"
#include "mesh/meshcellparams.h"

#include <QCoreApplication>

#include <cmath>

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
            mesh::applyCellParam(m_layer, m_tris[i], m_key, v);
        } else if (const mesh::CellParamSpec *s = mesh::cellParamSpec(m_key)) {
            mesh::applyCellParam(m_layer, m_tris[i], m_key, s->defaultValue);
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

} // namespace mesh
