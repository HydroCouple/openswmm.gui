/*!
 * \file   meshexport.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "io/meshexport.h"

#include "feature/featureroles.h"
#include "mesh/meshbctype.h"
#include "mesh/meshcellgeom.h"
#include "mesh/meshcellparams.h"
#include "mesh/meshinfil.h"

#include <QCoreApplication>
#include <QHash>
#include <QLineF>

#include <cmath>

using openswmmvis::feature::FieldChoice;
using openswmmvis::feature::FieldType;

namespace openswmmvis::io {

namespace {

QString tr_(const char *s) { return QCoreApplication::translate("MeshExport", s); }

ExportField makeField(const QString &name, const QString &label, FieldType type,
                      const QString &unit = QString())
{
    ExportField f;
    f.name  = name;
    f.label = label;
    f.type  = type;
    f.unit  = unit;
    return f;
}

/*! NaN (the mesh's "unset") becomes null. */
QVariant real(double v) { return std::isfinite(v) ? QVariant(v) : QVariant(); }

/*! Output name for a cell parameter key: "infil.method" → "infil_method". */
QString paramFieldName(const QByteArray &key)
{
    QString n = QString::fromLatin1(key);
    n.replace(QLatin1Char('.'), QLatin1Char('_'));
    return n;
}

QVector<FieldChoice> infilMethodChoices(const mesh::CellParamSpec &spec)
{
    QVector<FieldChoice> out;
    for (int i = 0; i < spec.enumLabels.size(); ++i) {
        const auto m = static_cast<mesh::InfilMethod>(int(spec.min) + i);
        if (m == mesh::InfilMethod::None) continue;   // written as null
        out.append({mesh::infilMethodToken(m), spec.enumLabels.at(i)});
    }
    return out;
}

QVector<FieldChoice> bcTypeChoices()
{
    using T = mesh::MeshBCTypes::Type;
    QVector<FieldChoice> out;
    for (T t : {T::Wall, T::NormalFlow, T::SpecifiedStageConst, T::SpecifiedStageTS,
                T::SpecifiedFlowConst, T::SpecifiedFlowTS, T::RatingCurve})
        out.append({mesh::MeshBCTypes::inpToken(t), mesh::MeshBCTypes::label(t)});
    return out;
}

bool validVertex(const mesh::MeshResult &m, int v)
{
    return v >= 0 && v < m.vertices.size();
}

ExportTable cellTable(const mesh::MeshResult &m, const QString &lengthUnit)
{
    ExportTable t;
    t.name = QStringLiteral("mesh_cells");
    t.geometry = ExportGeometry::Polygon;
    t.fields << makeField(QStringLiteral("cell"), tr_("Cell index"), FieldType::Integer)
             << makeField(QStringLiteral("vertices"), tr_("Corner count"), FieldType::Integer)
             << makeField(QStringLiteral("tag"), tr_("Tag"), FieldType::Text);
    QVector<const mesh::CellParamSpec *> params;
    for (const mesh::CellParamSpec &s : mesh::cellParamSpecs()) {
        if (!s.enabled) continue;   // engine support pending: nothing to export
        params << &s;
        ExportField f = makeField(paramFieldName(s.key), s.label,
                                  s.kind == mesh::CellParamSpec::Kind::Enum ? FieldType::Text
                                                                            : FieldType::Real,
                                  s.lengthUnit ? lengthUnit : QString());
        if (s.kind == mesh::CellParamSpec::Kind::Enum) f.choices = infilMethodChoices(s);
        t.fields << f;
    }
    t.fields << makeField(QStringLiteral("bed_z"), tr_("Bed elevation (corner mean)"),
                          FieldType::Real, lengthUnit);

    t.rows.reserve(m.triangles.size());
    for (int c = 0; c < m.triangles.size(); ++c) {
        const mesh::MeshTriangle &cell = m.triangles.at(c);
        ExportRow row;
        double zSum = 0.0;
        bool ok = true;
        for (int k = 0; k < cell.vertexCount(); ++k) {
            const int v = cell.vertex(k);
            if (!validVertex(m, v)) { ok = false; break; }
            row.points << m.vertices.at(v).xy;
            zSum += m.vertices.at(v).z;
        }
        if (!ok) {
            t.skipped << tr_("Mesh cell %1 refers to a missing vertex.").arg(c);
            continue;
        }
        row.values << c << cell.vertexCount() << cell.tag;
        for (const mesh::CellParamSpec *s : std::as_const(params)) {
            const double v = mesh::cellParamValue(m, c, s->key);
            if (s->kind == mesh::CellParamSpec::Kind::Enum) {
                const auto method = std::isfinite(v) ? static_cast<mesh::InfilMethod>(int(v))
                                                     : mesh::InfilMethod::None;
                row.values << (method == mesh::InfilMethod::None
                                   ? QVariant() : QVariant(mesh::infilMethodToken(method)));
            } else {
                row.values << real(v);
            }
        }
        row.values << real(zSum / cell.vertexCount());
        t.rows << row;
    }
    return t;
}

ExportTable edgeTable(const mesh::MeshResult &m, const QVector<mesh::MeshEdgeBC> &bcs,
                      const QString &lengthUnit)
{
    ExportTable t;
    t.name = QStringLiteral("mesh_edges");
    t.geometry = ExportGeometry::LineString;
    ExportField bcType = makeField(QStringLiteral("bc_type"), tr_("Boundary condition"),
                                   FieldType::Text);
    bcType.choices = bcTypeChoices();
    t.fields << makeField(QStringLiteral("cell"), tr_("Owning cell"), FieldType::Integer)
             << makeField(QStringLiteral("edge"), tr_("Local edge"), FieldType::Integer)
             << makeField(QStringLiteral("boundary"), tr_("On the mesh boundary"), FieldType::Boolean)
             << makeField(QStringLiteral("length"), tr_("Length (map units)"), FieldType::Real)
             << makeField(QStringLiteral("conveyance"), tr_("Conveyance"), FieldType::Real)
             << bcType
             << makeField(QStringLiteral("head"), tr_("Stage"), FieldType::Real, lengthUnit)
             << makeField(QStringLiteral("slope"), tr_("Bed slope"), FieldType::Real)
             << makeField(QStringLiteral("flow"), tr_("Flow per unit length"), FieldType::Real)
             << makeField(QStringLiteral("tseries"), tr_("Time series"), FieldType::Text)
             << makeField(QStringLiteral("curve"), tr_("Rating curve"), FieldType::Text)
             << makeField(QStringLiteral("group"), tr_("Boundary group"), FieldType::Text);

    // Every undirected edge once, under its lowest slot; an edge seen once is
    // on the boundary (the outline or a hole).
    struct Seen { int slot = -1; int uses = 0; };
    QHash<quint64, Seen> edges;
    QVector<quint64> order;
    const auto keyOf = [](int a, int b) {
        const quint32 lo = quint32(std::min(a, b)), hi = quint32(std::max(a, b));
        return (quint64(lo) << 32) | hi;
    };
    for (int c = 0; c < m.triangles.size(); ++c) {
        const mesh::MeshTriangle &cell = m.triangles.at(c);
        for (int e = 0; e < cell.vertexCount(); ++e) {
            int a = -1, b = -1;
            mesh::edgeEndpoints(cell, e, a, b);
            if (!validVertex(m, a) || !validVertex(m, b)) continue;
            const quint64 key = keyOf(a, b);
            Seen &s = edges[key];
            if (s.slot < 0) { s.slot = mesh::edgeSlot(c, e); order << key; }
            ++s.uses;
        }
    }

    t.rows.reserve(order.size());
    for (const quint64 key : std::as_const(order)) {
        const Seen s = edges.value(key);
        const int c = mesh::slotCell(s.slot), e = mesh::slotLocal(s.slot);
        int a = -1, b = -1;
        mesh::edgeEndpoints(m.triangles.at(c), e, a, b);
        const QPointF pa = m.vertices.at(a).xy, pb = m.vertices.at(b).xy;
        const bool boundary = (s.uses == 1);
        const mesh::MeshEdgeBC bc = s.slot < bcs.size() ? bcs.at(s.slot) : mesh::MeshEdgeBC{};
        const QString token = mesh::MeshBCTypes::inpToken(bc.type);
        // A BC parameter the type does not read is null, as the mesh
        // attribute table shows it "—".
        const auto param = [&](const char *name, const QVariant &v) {
            return boundary && openswmmvis::feature::bcFieldApplies(token, QLatin1String(name))
                       ? v : QVariant();
        };
        ExportRow row;
        row.points << pa << pb;
        row.values << c << e << boundary << QLineF(pa, pb).length() << bc.conveyance
                   << (boundary ? QVariant(token) : QVariant())
                   << param("head", bc.head) << param("slope", bc.slope)
                   << param("flow", bc.flow) << param("tseries", bc.tseries)
                   << param("curve", bc.curve)
                   << (boundary && !bc.group.isEmpty() ? QVariant(bc.group) : QVariant());
        t.rows << row;
    }
    return t;
}

ExportTable vertexTable(const mesh::MeshResult &m, const QString &lengthUnit)
{
    ExportTable t;
    t.name = QStringLiteral("mesh_vertices");
    t.geometry = ExportGeometry::Point;
    t.fields << makeField(QStringLiteral("vertex"), tr_("Vertex index"), FieldType::Integer)
             << makeField(QStringLiteral("z"), tr_("Elevation"), FieldType::Real, lengthUnit)
             << makeField(QStringLiteral("tag"), tr_("Tag"), FieldType::Text)
             << makeField(QStringLiteral("coupled_node"), tr_("Coupled SWMM node"), FieldType::Text);
    t.rows.reserve(m.vertices.size());
    for (int v = 0; v < m.vertices.size(); ++v) {
        const mesh::MeshVertex &mv = m.vertices.at(v);
        ExportRow row;
        row.points << mv.xy;
        row.values << v << real(mv.z) << mv.tag
                   << (mv.coupledNode.isEmpty() ? QVariant() : QVariant(mv.coupledNode));
        t.rows << row;
    }
    return t;
}

}   // namespace

QVector<ExportTable> meshTables(const mesh::MeshResult &mesh,
                                const QVector<mesh::MeshEdgeBC> &bcs,
                                unsigned parts, const QString &lengthUnit)
{
    QVector<ExportTable> out;
    if (parts & MeshExportCells)    out << cellTable(mesh, lengthUnit);
    if (parts & MeshExportEdges)    out << edgeTable(mesh, bcs, lengthUnit);
    if (parts & MeshExportVertices) out << vertexTable(mesh, lengthUnit);
    return out;
}

}   // namespace openswmmvis::io
