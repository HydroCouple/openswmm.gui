// SPDX-License-Identifier: Apache-2.0
// Edge-case snapshots for mesh::InpMeshReader::read (workplans/MESH_OPEN_IO_PERF_PLAN_2026-11-11.md
// §5 Phase 1 (b)). Each case below is written to a scratch folder and read; the whole
// InpMeshReadResult is serialized canonically and compared with
// tests/unit/data/inpmeshreader_edgecases/<case>.txt.
//
// The snapshots were generated on 2026-11-11 from the single-pass byte reader in the same build in
// which it was proven field-identical to the former QString reader (4984a9932) on every case, the
// repository corpus and the Phase 0 fixtures up to 10 M cells — so they pin the former reader's
// behaviour (error texts, Qt number rules, BOM/CR/Unicode handling, overlay precedence).
//
// Environment:
//   MESH_EDGE_OUT_DIR=<dir>   where the case files are written
//                             (default <repo>/tests/output/inpmeshreader_edgecases)
//   MESH_EDGE_GOLDEN_WRITE=1  (re)write the snapshots instead of comparing — only for a deliberate
//                             behaviour change, reviewed in the diff of the .txt files.
#include "mesh/inpmeshreader.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <utility>
#include <vector>

namespace
{

QString sourceDir()
{
    return QStringLiteral(MESH_EDGE_SOURCE_DIR);
}

QString envOr(const char *name, const QString &fallback)
{
    const QString v = qEnvironmentVariable(name);
    return v.isEmpty() ? fallback : v;
}

// ---- canonical serialization ----------------------------------------------------------------

/*! Escape a string to printable ASCII: backslash, control and non-ASCII characters as \uXXXX, so
 *  snapshots stay diffable and exact. \p base (the case folder) is replaced by "$CASE" so the
 *  text does not depend on where the files were written. */
QString esc(QString s, const QString &base)
{
    if (!base.isEmpty())
        s.replace(base, QStringLiteral("$CASE"));
    QString out;
    out.reserve(s.size() + 2);
    out += QLatin1Char('"');
    for (const QChar c : s) {
        const ushort u = c.unicode();
        if (u == '\\' || u == '"')
            out += QLatin1Char('\\') + QString(c);
        else if (u >= 0x20 && u < 0x7F)
            out += c;
        else
            out += QStringLiteral("\\u%1").arg(u, 4, 16, QLatin1Char('0'));
    }
    out += QLatin1Char('"');
    return out;
}

/*! A double as its exact bits plus a readable value. */
QString dbl(double v)
{
    quint64 bits = 0;
    std::memcpy(&bits, &v, sizeof v);
    return QStringLiteral("%1/0x%2").arg(v, 0, 'g', 17).arg(bits, 16, 16, QLatin1Char('0'));
}

QString b01(bool v)
{
    return v ? QStringLiteral("1") : QStringLiteral("0");
}

/*! Small arrays are listed element by element; long ones are reduced to count + SHA-256 of the same
 *  element lines, which is just as exact and keeps the snapshot readable. */
void emitArray(QStringList &out, const QString &name, const QStringList &rows)
{
    if (rows.size() <= 64) {
        out << QStringLiteral("%1 n=%2").arg(name).arg(rows.size());
        for (qsizetype i = 0; i < rows.size(); ++i)
            out << QStringLiteral("  [%1] %2").arg(i).arg(rows[i]);
        return;
    }
    const QByteArray joined = rows.join(QLatin1Char('\n')).toUtf8();
    out << QStringLiteral("%1 n=%2 sha256=%3")
               .arg(name)
               .arg(rows.size())
               .arg(QString::fromLatin1(
                   QCryptographicHash::hash(joined, QCryptographicHash::Sha256).toHex()));
}

QString infilRow(const mesh::InfilRow &r)
{
    QStringList p;
    for (int k = 0; k < mesh::kInfilMaxParams; ++k)
        p << dbl(r.p[k]);
    return QStringLiteral("method=%1 dest=%2 p=%3")
        .arg(int(r.method))
        .arg(int(r.dest))
        .arg(p.join(QLatin1Char(',')));
}

QString serialize(const mesh::InpMeshReadResult &r, const QString &base)
{
    QStringList out;
    out << QStringLiteral("hasMesh=%1 isExternal=%2 mesh.ok=%3")
               .arg(b01(r.hasMesh), b01(r.isExternal), b01(r.mesh.ok));
    out << QStringLiteral("sourcePath=") + esc(r.sourcePath, base);
    out << QStringLiteral("errorMsg=") + esc(r.errorMsg, base);
    out << QStringLiteral("warning=") + esc(r.warning, base);
    out << QStringLiteral("unitsHeader=") + esc(r.unitsHeader, base);
    out << QStringLiteral("sourceCrsTag=") + esc(r.sourceCrsTag, base);
    out << QStringLiteral("mesh.errorMsg=") + esc(r.mesh.errorMsg, base);

    const mesh::MeshResult &m = r.mesh;
    QStringList rows;
    for (const auto &v : m.vertices)
        rows << QStringLiteral("x=%1 y=%2 z=%3 marker=%4 tag=%5 node=%6 cd=%7 area=%8 cdSet=%9 areaSet=%10")
                    .arg(dbl(v.xy.x()), dbl(v.xy.y()), dbl(v.z))
                    .arg(v.marker)
                    .arg(esc(v.tag, base), esc(v.coupledNode, base), dbl(v.couplingCd),
                         dbl(v.couplingArea), b01(v.couplingCdSet), b01(v.couplingAreaSet));
    emitArray(out, QStringLiteral("vertices"), rows);
    rows.clear();
    for (const auto &t : m.triangles)
        rows << QStringLiteral("v=%1,%2,%3,%4 tag=%5 n=%6 depth=%7")
                    .arg(t.v0)
                    .arg(t.v1)
                    .arg(t.v2)
                    .arg(t.v3)
                    .arg(esc(t.tag, base), dbl(t.mannings), dbl(t.initDepth));
    emitArray(out, QStringLiteral("cells"), rows);
    rows.clear();
    for (const auto &e : m.boundaryEdges)
        rows << QStringLiteral("v=%1,%2 marker=%3 tag=%4").arg(e.v0).arg(e.v1).arg(e.marker).arg(esc(e.tag, base));
    emitArray(out, QStringLiteral("boundaryEdges"), rows);
    rows.clear();
    for (const auto &c : m.cellCouplings)
        rows << QStringLiteral("tri=%1 node=%2 cd=%3 area=%4 cdSet=%5 areaSet=%6")
                    .arg(c.tri)
                    .arg(esc(c.nodeId, base), dbl(c.cd), dbl(c.area), b01(c.cdSet), b01(c.areaSet));
    emitArray(out, QStringLiteral("cellCouplings"), rows);
    rows.clear();
    for (const auto &d : m.infilDefaults)
        rows << QStringLiteral("tag=%1 %2").arg(esc(d.tag, base), infilRow(d.row));
    emitArray(out, QStringLiteral("infilDefaults"), rows);
    rows.clear();
    QList<int> keys = m.infilOverrides.keys();
    std::sort(keys.begin(), keys.end());
    for (int k : keys)
        rows << QStringLiteral("cell=%1 %2").arg(k).arg(infilRow(m.infilOverrides.value(k)));
    emitArray(out, QStringLiteral("infilOverrides"), rows);
    out << QStringLiteral("infilStep=") + dbl(m.infilOptions.infilStep);
    out << QStringLiteral("surface/infiltration previews: %1 %2 %3 %4 %5 %6")
               .arg(m.surfaceMeshWeatherPercent.size())
               .arg(m.surfaceSourcePerviousPercent.size())
               .arg(m.surfaceOwnershipStatus.size())
               .arg(m.infiltrationOwner.size())
               .arg(m.infiltrationConflict.size())
               .arg(m.infiltrationSource.size());
    rows.clear();
    for (const auto &bc : r.edgeBCs)
        rows << QStringLiteral("type=%1 head=%2 slope=%3 flow=%4 ts=%5 curve=%6 group=%7 conv=%8 drag=%9")
                    .arg(int(bc.type))
                    .arg(dbl(bc.head), dbl(bc.slope), dbl(bc.flow), esc(bc.tseries, base),
                         esc(bc.curve, base), esc(bc.group, base), dbl(bc.conveyance),
                         dbl(bc.wallDrag));
    emitArray(out, QStringLiteral("edgeBCs"), rows);
    return out.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

// ---- edge cases -----------------------------------------------------------------------------

struct EdgeCase {
    const char *name;
    QByteArray inp;
    std::vector<std::pair<QString, QByteArray>> extra; // relative path -> bytes
    std::function<void(const mesh::InpMeshReadResult &)> expect = {};
};

const QByteArray kVerts = "[2D_VERTICES]\n"
                          "0 0 1.5\n"
                          "10 0 1.25\n"
                          "10 10 1\n"
                          "0 10 0.75\n";
const QByteArray kTris = "[2D_TRIANGLES]\n"
                         "0 1 2 0.035\n"
                         "0 2 3 0.035\n";

QByteArray crlf(QByteArray s)
{
    return s.replace("\n", "\r\n");
}

std::vector<EdgeCase> edgeCases()
{
    std::vector<EdgeCase> c;
    const QByteArray base = kVerts + kTris;
    c.push_back({"plain_inline", base, {}, [](const auto &r) {
                     EXPECT_TRUE(r.hasMesh);
                     EXPECT_EQ(r.mesh.vertices.size(), 4);
                     EXPECT_EQ(r.mesh.triangles.size(), 2);
                     EXPECT_EQ(r.edgeBCs.size(), 8);
                 }});
    c.push_back({"leading_plus",
                 "[2D_VERTICES]\n+0 +0 +1.5\n10 0 +1e+00\n10 10 1\n0 10 0.75\n"
                 "[2D_TRIANGLES]\n+0 +1 +2 +0.035\n0 2 3 0.035 +0.25 tagA\n",
                 {},
                 [](const auto &r) {
                     ASSERT_TRUE(r.hasMesh);
                     EXPECT_EQ(r.mesh.vertices[0].z, 1.5);
                     EXPECT_EQ(r.mesh.triangles[1].initDepth, 0.25);
                 }});
    c.push_back({"crlf", crlf(base), {}, [](const auto &r) { EXPECT_TRUE(r.hasMesh); }});
    c.push_back({"stray_cr_joins_tokens",
                 "[2D_VERTICES]\n0 0 1.\r5\n10 0 1\r\n10 10 1\n0 10 1\n" + kTris});
    c.push_back({"tabs_vt_ff", "[2D_VERTICES]\n0\t0\t1\n10\v0\f1\n\t10 10 1\t\n0 10 1\n" + kTris});
    c.push_back({"trailing_comments",
                 "[2D_VERTICES]\n;; X Y Z TAG\n0 0 1 ; first\n10 0 1;;x\n10 10 1 tagV;c\n;\n"
                 "0 10 1\n[2D_TRIANGLES]\n;;V1 V2 V3\n0 1 2 0.03 ; n\n0 2 3;0.5\n"});
    c.push_back({"blank_lines", "\n\n[2D_VERTICES]\n\n0 0 1\n   \n10 0 1\n\t\n10 10 1\n0 10 1\n\n" +
                                    kTris + "\n\n"});
    c.push_back({"utf8_tags",
                 "[2D_VERTICES]\n0 0 1 zon\xC3\xA9\n10 0 1 \xCE\xA9\n10 10 1\n0 10 1 N\xC2\xB0"
                 "1\n[2D_TRIANGLES]\n0 1 2 0.03 r\xC3\xA9gion\n0 2 3 0.03 0.1 \xE6\xB2\xB3\n",
                 {},
                 [](const auto &r) {
                     ASSERT_TRUE(r.hasMesh);
                     EXPECT_EQ(r.mesh.vertices[0].tag, QStringLiteral("zoné"));
                     EXPECT_EQ(r.mesh.triangles[1].tag, QStringLiteral("河"));
                 }});
    c.push_back({"nbsp_separator", "[2D_VERTICES]\n0\xC2\xA0" "0 1\n10 0 1\n10 10 1\n0 10 1\n" + kTris});
    c.push_back({"ideographic_space_separator",
                 "[2D_VERTICES]\n0 0\xE3\x80\x80" "1\n10 0 1\n10 10 1\n0 10 1\n" + kTris});
    c.push_back({"missing_optional_columns",
                 kVerts + "[2D_TRIANGLES]\n0 1 2\n0 2 3 0.03\n0 1 3 0.03 0.5\n1 2 3 0.03 tagT\n"
                          "0 1 2 0.03 0.5 tagU\n0 1 2 0.03 0.5 tagU extra cols\n0 1 2 x\n"});
    c.push_back({"triangle_after_quad",
                 kVerts + "[2D_QUADS]\n0 1 2 3 0.03\n[2D_TRIANGLES]\n0 1 2 0.03\n",
                 {},
                 [](const auto &r) { EXPECT_FALSE(r.errorMsg.isEmpty()); }});
    c.push_back({"quads_only", kVerts + "[2D_QUADS]\n0 1 2 3 0.03 0.1 q\n"});
    c.push_back({"mixed_tri_quad",
                 "[2D_VERTICES]\n0 0 0\n1 0 0\n2 0 0\n0 1 0\n1 1 0\n2 1 0\n"
                 "[2D_TRIANGLES]\n0 1 4 0.03 0.2 a\n0 4 3 0.03 b\n[2D_QUADS]\n1 2 5 4 0.04\n"
                 "[2D_BOUNDARY_CONDITIONS]\n2 3 NORMAL_FLOW 0.001 * out\n2 0 WALL\n"});
    c.push_back({"malformed_number_vertex", "[2D_VERTICES]\n1.2.3 0 0 ; bad\n" + kTris,
                 {},
                 [](const auto &r) {
                     EXPECT_EQ(r.errorMsg,
                               QStringLiteral("[2D_VERTICES] non-numeric value (got: 1.2.3 0 0 ; bad)"));
                 }});
    c.push_back({"malformed_index", kVerts + "[2D_TRIANGLES]\n0 1 x 0.03\n",
                 {},
                 [](const auto &r) { EXPECT_FALSE(r.errorMsg.isEmpty()); }});
    c.push_back({"short_vertex_row", "[2D_VERTICES]\n  1 2 ; only two\t\n" + kTris});
    c.push_back({"short_triangle_row", kVerts + "[2D_TRIANGLES]\n0 1\n"});
    c.push_back({"short_quad_row", kVerts + "[2D_QUADS]\n0 1 2\n"});
    c.push_back({"nan_inf_tokens",
                 "[2D_VERTICES]\n0 0 nan\n10 0 inf\n10 10 -inf\n0 10 NaN\n1 1 INF\n2 2 +inf\n"
                 "[2D_TRIANGLES]\n0 1 2 nan\n0 2 3 0.03 inf t\n0 1 3 Infinity\n0 1 2 0.03 +nan\n"});
    c.push_back({"nan_vertex_variants_error", "[2D_VERTICES]\n0 0 +nan\n" + kTris});
    c.push_back({"overflow_tokens",
                 "[2D_VERTICES]\n0 0 1e999\n10 0 -1e999\n10 10 1e-999\n0 10 4.9e-324\n"
                 "[2D_TRIANGLES]\n0 1 2 1e999\n0 2 3 0.03 1e-400 t\n"});
    c.push_back({"overflow_vertex_error", "[2D_VERTICES]\n0 0 1e999\n" + kTris});
    c.push_back({"underflow_vertex_error", "[2D_VERTICES]\n0 0 1e-999\n" + kTris});
    c.push_back({"int_range",
                 kVerts + "[2D_TRIANGLES]\n0 1 2147483647 0.03\n-0 +1 -2147483648 0.03\n"});
    c.push_back({"int_overflow_error", kVerts + "[2D_TRIANGLES]\n0 1 2147483648 0.03\n"});
    c.push_back({"int_long_error", kVerts + "[2D_TRIANGLES]\n0 1 99999999999999999999 0.03\n"});
    c.push_back({"number_forms",
                 "[2D_VERTICES]\n.5 5. 1E5\n1e-05 1e+05 007\n-0 0.0 -0.0\n"
                 "1.0000000000000002 2.2250738585072014e-308 1.7976931348623157e308\n"
                 "123456789012345678901234567890 0.1 9007199254740993\n"
                 "[2D_TRIANGLES]\n0 1 2 .5\n0 2 3 5.\n0 1 3 1e5 0x10 t\n1 2 3 0.03 1,5\n"
                 "0 1 2 0.03 1_000\n0 1 2 0.03 1e\n0 1 2 0.03 e5\n0 1 2 0.03 --1\n"
                 "0 1 2 0.03 1d5\n0 1 2 0.03 \xD9\xA1\n0 1 2 0.03 1.5e+3x\n007 01 2 0.03\n"});
    c.push_back({"hex_index_error", kVerts + "[2D_TRIANGLES]\n0x0 1 2 0.03\n"});
    c.push_back({"long_tokens",
                 "[2D_VERTICES]\n0." + QByteArray(70, '1') + " 0 1\n" + QByteArray(64, '9') +
                     " 0 1\n10 10 1\n0 10 1\n" + kTris});
    c.push_back({"bom_first_line", "\xEF\xBB\xBF" + base});
    c.push_back({"bom_later_vertex_line",
                 "[2D_VERTICES]\n0 0 1\n\xEF\xBB\xBF" "10 0 1\n10 10 1\n0 10 1\n" + kTris});
    c.push_back({"bom_before_header", kVerts + "\xEF\xBB\xBF[2D_TRIANGLES]\n0 1 2 0.03\n"});
    c.push_back({"double_bom_later_line",
                 "[2D_VERTICES]\n0 0 1\n\xEF\xBB\xBF\xEF\xBB\xBF" "10 0 1\n10 10 1\n0 10 1\n" + kTris});
    c.push_back({"double_bom_tag_line",
                 base + "[2D_INFILTRATION_DEFAULTS]\n\xEF\xBB\xBF\xEF\xBB\xBFzone HORTON 3 0.5 4 7 0\n"});
    c.push_back({"double_bom_first_line", "\xEF\xBB\xBF\xEF\xBB\xBF" + base});
    {
        // Repeated vertex / cell headers (hand-concatenated tiles): must stay linear.
        QByteArray tiles;
        for (int i = 0; i < 20000; ++i)
            tiles += (i % 3 == 0 ? "\v[2D_VERTICES]\n" : "[2D_VERTICES]\n") +
                     QByteArray::number(i) + " 0 1\n";
        for (int i = 0; i < 5000; ++i)
            tiles += "[2D_TRIANGLES]\n0 1 2 0.03\n";
        tiles += "[2D_QUADS]\n0 1 2 3 0.03\n[2D_QUADS]\n0 1 2 3 0.04\n";
        c.push_back({"repeated_headers", tiles, {}, [](const auto &r) {
                         EXPECT_EQ(r.mesh.vertices.size(), 20000);
                         EXPECT_EQ(r.mesh.triangles.size(), 5002);
                     }});
    }
    c.push_back({"invalid_utf8",
                 "[2D_VERTICES]\n0 0 1 t\xFF\xFE\n10 0 1 \xC3\n10 10 1\n0 10 1 \xE2\x82\n" + kTris});
    static const char kNul[] = "[2D_VERTICES]\n0 0 1 t\0x\n10 0 1\n10 10 1\n0 10 1\n";
    c.push_back({"nul_byte", QByteArray(kNul, sizeof(kNul) - 1) + kTris});
    c.push_back({"header_variants",
                 "\v[2d_vertices]\f\n0 0 1\n10 0 1\n[2D_VERTICES] ; not a header\n"
                 "  [2D_VERTICES]  \n10 10 1\n0 10 1\n[2D_Triangles]\n0 1 2 0.03\n"
                 "[2D_TRIANGLES]\n0 2 3 0.03\n"});
    c.push_back({"ignored_sections",
                 "[TITLE]\nx\n[COORDINATES]\nJ1 1 2\n" + kVerts + "[OPTIONS]\nFLOW_UNITS CMS\n" +
                     kTris + "[2D_OPTIONS]\nINTEGRATOR EXPLICIT\n"});
    c.push_back({"rows_before_any_section", "0 0 1\n0 1 2 0.03\n" + base});
    c.push_back({"units_crs_headers",
                 ";; UNITS: ft\n;;SOURCE_CRS:EPSG:1234\n" + base +
                     ";;  units:  SI (m) \n  ;; Source_CRS: EPSG:25832  \n;; UNITS:\n;; UNITS: m\n"});
    c.push_back({"units_empty_last", ";; UNITS: m\n" + base + ";; UNITS:\n",
                 {},
                 [](const auto &r) { EXPECT_TRUE(r.unitsHeader.isEmpty()); }});
    c.push_back({"units_after_error",
                 "[2D_VERTICES]\n0 0 x\n" + kTris + ";; UNITS: m\n;; SOURCE_CRS: EPSG:4326\n",
                 {},
                 [](const auto &r) {
                     EXPECT_FALSE(r.errorMsg.isEmpty());
                     EXPECT_EQ(r.unitsHeader, QStringLiteral("m"));
                 }});
    c.push_back({"units_after_body_error",
                 base + "[2D_BOUNDARY_CONDITIONS]\n0 0 NOT_A_TYPE\n[2D_OPTIONS]\n;; UNITS: m\n"});
    c.push_back({"no_trailing_newline", kVerts + "[2D_TRIANGLES]\n0 1 2 0.03\n0 2 3 1e5"});
    c.push_back({"number_then_semicolon", kVerts + "[2D_TRIANGLES]\n0 1 2 1e5;c\n0 2 3 0.5;\n"});
    c.push_back({"empty_mesh_sections", "[2D_VERTICES]\n[2D_TRIANGLES]\n"});
    c.push_back({"no_mesh", "[TITLE]\nnothing\n[JUNCTIONS]\nJ1 0 0\n"});
    c.push_back({"only_carriage_returns", "\r\r\r"});
    c.push_back({"node_maps",
                 "[2D_VERTICES]\n0 0 1 J1\n10 0 1\n10 10 1 J3\n0 10 1\n" + kTris +
                     "[2D_VERTEX_NODE_MAP]\n0 J1\nJ3 J3 0.5\n2 J2 0.6 3\nnosuch X\n99 Y\n"
                     "[2D_TRIANGLE_NODE_MAP]\n0 J1\n1 J2 0.7 4\n"});
    c.push_back({"bc_conveyance",
                 base + "[2D_BOUNDARY_CONDITIONS]\n0 0 NORMAL_FLOW 0.001 * outlet\n"
                        "0 1 WALL_DRAG 0.5\n1 2 SPECIFIED_STAGE_TS ts1\n7 0 WALL\n1 3 WALL\n"
                        "[2D_EDGE_CONVEYANCE]\n0 2 0.5\n3 0 0.25\n"});
    c.push_back({"conveyance_overlay_rules",
                 "[2D_VERTICES]\n0 0 0\n1 0 0\n2 0 0\n0 1 0\n1 1 0\n2 1 0\n"
                 "[2D_TRIANGLES]\n0 1 4 0.03\n0 4 3 0.03\n1 2 5 0.03\n1 5 4 0.03\n"
                 "4 4 3 0.03\n-1 4 9 0.03\n"
                 "[2D_EDGE_CONVEYANCE]\n1 4 0.5\n4 1 0.25\n0 4 0.1\n3 4 0.75\n9 4 0.2\n7 8 0.3\n"
                 "4 1 0.6\n"});
    c.push_back({"bc_error", base + "[2D_BOUNDARY_CONDITIONS]\n0 0 NOT_A_TYPE\n"});
    c.push_back({"conveyance_error", base + "[2D_EDGE_CONVEYANCE]\n0 2 1.5\n"});
    c.push_back({"infiltration",
                 base + "[2D_INFILTRATION_OPTIONS]\nINFIL_STEP 0:05:00\nOTHER 1\n"
                        "[2D_INFILTRATION_DEFAULTS]\n* HORTON 3 0.5 4 7 0\n"
                        "[2D_INFILTRATION]\n1 HORTON 1 - 2\n2 NONE\n"});
    c.push_back({"infiltration_error", base + "[2D_INFILTRATION]\n0 HORTON 1\n"});

    // External mesh files.
    const QByteArray ext = ";; UNITS: SI (m)\n;; SOURCE_CRS: EPSG:25832\n" + base +
                           "[2D_BOUNDARY_CONDITIONS]\n0 0 NORMAL_FLOW 0.002 * g\n"
                           "[2D_EDGE_CONVEYANCE]\n0 1 0.5\n";
    c.push_back({"external_quoted_path",
                 ";; UNITS: ft\n[2D_MESH_FILE]\n; note\nFILE \"sub dir/mesh;1.2dm\" ; comment\n"
                 "[2D_BOUNDARY_CONDITIONS]\n0 0 SPECIFIED_STAGE 3.5\n",
                 {{QStringLiteral("sub dir/mesh;1.2dm"), ext}},
                 [](const auto &r) {
                     EXPECT_TRUE(r.hasMesh);
                     EXPECT_TRUE(r.isExternal);
                     EXPECT_EQ(r.unitsHeader, QStringLiteral("SI (m)"));
                 }});
    c.push_back({"external_crlf_bom", "[2D_MESH_FILE]\nFILE m.2dm\n",
                 {{QStringLiteral("m.2dm"), "\xEF\xBB\xBF" + crlf(ext)}}});
    c.push_back({"external_missing", "[2D_MESH_FILE]\nFILE nope.2dm\n", {},
                 [](const auto &r) { EXPECT_FALSE(r.errorMsg.isEmpty()); }});
    c.push_back({"external_error", "[2D_MESH_FILE]\nFILE bad.2dm\n",
                 {{QStringLiteral("bad.2dm"), "[2D_VERTICES]\n0 0\n;; UNITS: m\n"}}});
    c.push_back({"external_without_geometry", base + "[2D_MESH_FILE]\nFILE empty.2dm\n",
                 {{QStringLiteral("empty.2dm"), ";; nothing\n"}}});
    c.push_back({"external_infil_fallback",
                 "[2D_MESH_FILE]\nFILE m.2dm\n[2D_INFILTRATION_DEFAULTS]\n* HORTON 3 0.5 4 7 0\n"
                 "[2D_INFILTRATION_OPTIONS]\nINFIL_STEP 60\n",
                 {{QStringLiteral("m.2dm"), base}}});
    return c;
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return f.write(bytes) == bytes.size();
}

} // namespace

TEST(InpMeshReaderEdgeCases, MatchSnapshots)
{
    const QString root =
        envOr("MESH_EDGE_OUT_DIR", sourceDir() + QStringLiteral("/tests/output/inpmeshreader_edgecases"));
    const QString goldenDir = sourceDir() + QStringLiteral("/tests/unit/data/inpmeshreader_edgecases");
    const bool write = qEnvironmentVariable("MESH_EDGE_GOLDEN_WRITE") == QLatin1String("1");
    if (write)
        QDir().mkpath(goldenDir);
    int n = 0;
    for (const EdgeCase &ec : edgeCases()) {
        const QString dir = root + QLatin1Char('/') + QLatin1String(ec.name);
        QDir(dir).removeRecursively();
        ASSERT_TRUE(writeFile(dir + QStringLiteral("/model.inp"), ec.inp)) << ec.name;
        for (const auto &[rel, bytes] : ec.extra)
            ASSERT_TRUE(writeFile(dir + QLatin1Char('/') + rel, bytes)) << ec.name;
        const mesh::InpMeshReadResult r = mesh::InpMeshReader::read(dir + QStringLiteral("/model.inp"));
        const QByteArray text = serialize(r, dir).toUtf8();
        const QString golden = goldenDir + QLatin1Char('/') + QLatin1String(ec.name) + QStringLiteral(".txt");
        if (write) {
            ASSERT_TRUE(writeFile(golden, text)) << ec.name;
        } else {
            QFile g(golden);
            ASSERT_TRUE(g.open(QIODevice::ReadOnly)) << "missing snapshot " << qPrintable(golden);
            QByteArray want = g.readAll();
            want.replace("\r", ""); // tolerate CRLF checkouts; the text never holds a raw CR
            EXPECT_EQ(text.toStdString(), want.toStdString()) << ec.name;
        }
        if (ec.expect) {
            SCOPED_TRACE(ec.name);
            ec.expect(r);
        }
        ++n;
    }
    // Every snapshot on disk must still have a case (a removed case leaves a stale file).
    const QStringList onDisk = QDir(goldenDir).entryList({QStringLiteral("*.txt")}, QDir::Files);
    EXPECT_EQ(onDisk.size(), n) << "snapshot files and cases differ in number";
    if (write)
        GTEST_SKIP() << "wrote " << n << " snapshots to " << qPrintable(goldenDir);
}
