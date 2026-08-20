// SPDX-License-Identifier: Apache-2.0
//
// Copyright 2026 Caleb Buahin
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/*! \file meshinfil.cpp
 *  \brief Implementation of the per-cell 2D infiltration value types,
 *         resolution and token grammar declared in meshinfil.h.
 *
 *  Tokens, positional parameter order and destination spellings are copied
 *  from the engine's `src/engine/2d/infil/Infil2D.cpp`
 *  (`parseInfil2DMethod` / `infil2DMethodToken` / `infil2DParamCount`) — the
 *  two sides read and write the same `[2D_INFILTRATION*]` sections, so any
 *  divergence here is a silent data-loss bug.
 */
#include "mesh/meshinfil.h"

#include "mesh/meshresult.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>

#include <cmath>

namespace mesh {

namespace {

const auto tr = [](const char *s) {
    return QCoreApplication::translate("MeshInfil", s);
};

/*! Slot-by-slot equality that treats two NaNs (both "unset") as equal.
 *  Plain `==` would report every unset slot as different from itself. */
bool sameParam(double a, double b)
{
    if (std::isnan(a) && std::isnan(b)) return true;
    return a == b;
}

} // namespace

bool InfilRow::operator==(const InfilRow &o) const
{
    if (method != o.method || dest != o.dest) return false;
    for (int k = 0; k < kInfilMaxParams; ++k)
        if (!sameParam(p[k], o.p[k])) return false;
    return true;
}

// ---------------------------------------------------------------------------
// Resolution
// ---------------------------------------------------------------------------

int indexOfDefault(const MeshResult &mesh, const QString &tag)
{
    for (int i = 0; i < mesh.infilDefaults.size(); ++i)
        if (mesh.infilDefaults[i].tag == tag) return i;
    return -1;
}

InfilRow starDefault(const MeshResult &mesh)
{
    const int i = indexOfDefault(mesh, QStringLiteral("*"));
    return i >= 0 ? mesh.infilDefaults[i].row : InfilRow{};
}

/*! Engine D-I3 precedence: `override > tag row > '*' row > none`.
 *
 *  Provenance names the row's SOURCE, not whether a model ends up running:
 *  a tag row (or an override) spelling `NONE` still resolves to Tag /
 *  Override with an `isNone()` row. That distinction is load-bearing twice
 *  over — MeshSetTriangleInfilCommand::undo() uses it to decide between
 *  restoring a per-cell row and erasing the override back to inheritance,
 *  and the attribute table uses it to render a region-supplied "None" in the
 *  muted/italic inherited style rather than as a per-cell decision. The
 *  engine's own `Infil2D::prov_` collapses those cases to NONE because it is
 *  solver-facing and only cares whether a kernel runs. */
ResolvedInfil resolveInfil(const MeshResult &mesh, int tri)
{
    ResolvedInfil out;
    if (tri < 0 || tri >= mesh.triangles.size()) return out;

    const auto ov = mesh.infilOverrides.constFind(tri);
    if (ov != mesh.infilOverrides.constEnd()) {
        out.row        = ov.value();
        out.provenance = InfilProvenance::Override;
        return out;
    }

    const QString &tag = mesh.triangles[tri].tag;
    if (!tag.isEmpty()) {
        const int i = indexOfDefault(mesh, tag);
        if (i >= 0) {
            out.row        = mesh.infilDefaults[i].row;
            out.provenance = InfilProvenance::Tag;
            out.sourceTag  = tag;
            return out;
        }
    }

    const int s = indexOfDefault(mesh, QStringLiteral("*"));
    if (s >= 0) {
        out.row        = mesh.infilDefaults[s].row;
        out.provenance = InfilProvenance::Star;
        out.sourceTag  = QStringLiteral("*");
    }
    return out;
}

// ---------------------------------------------------------------------------
// Method <-> parameter masks
// ---------------------------------------------------------------------------

bool infilUsesParam(InfilMethod method, int slot)
{
    if (slot < 0 || slot >= kInfilMaxParams) return false;
    switch (method) {
    case InfilMethod::None:         return false;
    case InfilMethod::Horton:
    case InfilMethod::ModHorton:    return true;                 // f0 fmin decay dry Fmax
    case InfilMethod::GreenAmpt:
    case InfilMethod::ModGreenAmpt: return slot <= 2;            // S Ks IMD
    // Slot 1 is the legacy [INFILTRATION] no-op column: Curve Number reads
    // CN from slot 0 and the drying time from slot 2, so the middle column
    // is a placeholder the writer emits as "-".
    case InfilMethod::CurveNumber:  return slot == 0 || slot == 2;
    case InfilMethod::Constant:     return slot == 0;            // rate
    }
    return false;
}

int infilParamCount(InfilMethod method)
{
    switch (method) {
    case InfilMethod::None:         return 0;
    case InfilMethod::Horton:
    case InfilMethod::ModHorton:    return 5;
    case InfilMethod::GreenAmpt:
    case InfilMethod::ModGreenAmpt: return 3;
    // Three COLUMNS, not three values — the middle one is the unused
    // placeholder (see infilUsesParam).
    case InfilMethod::CurveNumber:  return 3;
    case InfilMethod::Constant:     return 1;
    }
    return 0;
}

QString infilParamLabel(InfilMethod method, int slot)
{
    if (!infilUsesParam(method, slot)) return {};
    switch (method) {
    case InfilMethod::Horton:
    case InfilMethod::ModHorton:
        switch (slot) {
        case 0:  return tr("Max. Infil. Rate (f0)");
        case 1:  return tr("Min. Infil. Rate (fmin)");
        case 2:  return tr("Decay Constant (1/hr)");
        case 3:  return tr("Drying Time (days)");
        default: return tr("Max. Infil. Volume (Fmax)");
        }
    case InfilMethod::GreenAmpt:
    case InfilMethod::ModGreenAmpt:
        switch (slot) {
        case 0:  return tr("Suction Head");
        case 1:  return tr("Conductivity (Ks)");
        default: return tr("Initial Deficit (IMD)");
        }
    case InfilMethod::CurveNumber:
        return slot == 0 ? tr("Curve Number (CN)") : tr("Drying Time (days)");
    case InfilMethod::Constant:
        return tr("Infiltration Rate");
    case InfilMethod::None:
        break;
    }
    return {};
}

QByteArray infilParamKey(InfilMethod method, int slot)
{
    if (!infilUsesParam(method, slot)) return {};
    switch (method) {
    case InfilMethod::Horton:
    case InfilMethod::ModHorton:
        switch (slot) {
        case 0:  return QByteArrayLiteral("infil.f0");
        case 1:  return QByteArrayLiteral("infil.fmin");
        case 2:  return QByteArrayLiteral("infil.decay");
        case 3:  return QByteArrayLiteral("infil.dryTime");
        default: return QByteArrayLiteral("infil.Fmax");
        }
    case InfilMethod::GreenAmpt:
    case InfilMethod::ModGreenAmpt:
        switch (slot) {
        case 0:  return QByteArrayLiteral("infil.suction");
        case 1:  return QByteArrayLiteral("infil.Ks");
        default: return QByteArrayLiteral("infil.IMD");
        }
    // Curve Number shares `infil.dryTime` with Horton but at slot 2, not
    // slot 3 — which is precisely why the key <-> slot mapping has to stay
    // method-dependent rather than becoming a flat table.
    case InfilMethod::CurveNumber:
        return slot == 0 ? QByteArrayLiteral("infil.CN")
                         : QByteArrayLiteral("infil.dryTime");
    case InfilMethod::Constant:
        return QByteArrayLiteral("infil.rate");
    case InfilMethod::None:
        break;
    }
    return {};
}

int infilSlotForKey(InfilMethod method, const QByteArray &key)
{
    if (key.isEmpty()) return -1;
    for (int slot = 0; slot < kInfilMaxParams; ++slot)
        if (infilParamKey(method, slot) == key) return slot;
    return -1;
}

const QVector<QByteArray> &infilParamKeys()
{
    static const QVector<QByteArray> keys = {
        QByteArrayLiteral("infil.f0"),
        QByteArrayLiteral("infil.fmin"),
        QByteArrayLiteral("infil.decay"),
        QByteArrayLiteral("infil.dryTime"),
        QByteArrayLiteral("infil.Fmax"),
        QByteArrayLiteral("infil.suction"),
        QByteArrayLiteral("infil.Ks"),
        QByteArrayLiteral("infil.IMD"),
        QByteArrayLiteral("infil.CN"),
        QByteArrayLiteral("infil.rate"),
    };
    return keys;
}

// ---------------------------------------------------------------------------
// Tokens — must match the engine's Infil2D.cpp exactly
// ---------------------------------------------------------------------------

QString infilMethodToken(InfilMethod m)
{
    switch (m) {
    case InfilMethod::Horton:       return QStringLiteral("HORTON");
    case InfilMethod::ModHorton:    return QStringLiteral("MODIFIED_HORTON");
    case InfilMethod::GreenAmpt:    return QStringLiteral("GREEN_AMPT");
    case InfilMethod::ModGreenAmpt: return QStringLiteral("MODIFIED_GREEN_AMPT");
    case InfilMethod::CurveNumber:  return QStringLiteral("CURVE_NUMBER");
    case InfilMethod::Constant:     return QStringLiteral("CONSTANT");
    case InfilMethod::None:         break;
    }
    return QStringLiteral("NONE");
}

InfilMethod infilMethodFromToken(const QString &token, bool *ok)
{
    if (ok) *ok = true;
    const QString t = token.trimmed();
    if (t.compare(QLatin1String("NONE"), Qt::CaseInsensitive) == 0)
        return InfilMethod::None;
    if (t.compare(QLatin1String("HORTON"), Qt::CaseInsensitive) == 0)
        return InfilMethod::Horton;
    if (t.compare(QLatin1String("MODIFIED_HORTON"), Qt::CaseInsensitive) == 0)
        return InfilMethod::ModHorton;
    if (t.compare(QLatin1String("GREEN_AMPT"), Qt::CaseInsensitive) == 0)
        return InfilMethod::GreenAmpt;
    if (t.compare(QLatin1String("MODIFIED_GREEN_AMPT"), Qt::CaseInsensitive) == 0)
        return InfilMethod::ModGreenAmpt;
    if (t.compare(QLatin1String("CURVE_NUMBER"), Qt::CaseInsensitive) == 0)
        return InfilMethod::CurveNumber;
    if (t.compare(QLatin1String("CONSTANT"), Qt::CaseInsensitive) == 0)
        return InfilMethod::Constant;
    if (ok) *ok = false;
    return InfilMethod::None;
}

QString infilMethodLabel(InfilMethod m)
{
    switch (m) {
    case InfilMethod::Horton:       return tr("Horton");
    case InfilMethod::ModHorton:    return tr("Modified Horton");
    case InfilMethod::GreenAmpt:    return tr("Green-Ampt");
    case InfilMethod::ModGreenAmpt: return tr("Modified Green-Ampt");
    case InfilMethod::CurveNumber:  return tr("Curve Number");
    case InfilMethod::Constant:     return tr("Constant");
    case InfilMethod::None:         break;
    }
    return tr("None");
}

QStringList infilMethodLabels()
{
    QStringList out;
    for (int m = int(InfilMethod::None); m <= int(InfilMethod::Constant); ++m)
        out << infilMethodLabel(static_cast<InfilMethod>(m));
    return out;
}

QString infilDestToken(InfilDest d)
{
    switch (d) {
    case InfilDest::SubcatchAquifer: return QStringLiteral("SUBCATCH_AQUIFER");
    case InfilDest::Aquifer2D:       return QStringLiteral("AQUIFER_2D");
    case InfilDest::Lost:            break;
    }
    return QStringLiteral("LOST");
}

InfilDest infilDestFromToken(const QString &token, bool *ok)
{
    if (ok) *ok = true;
    const QString t = token.trimmed();
    if (t.compare(QLatin1String("LOST"), Qt::CaseInsensitive) == 0)
        return InfilDest::Lost;
    if (t.compare(QLatin1String("SUBCATCH_AQUIFER"), Qt::CaseInsensitive) == 0)
        return InfilDest::SubcatchAquifer;
    if (t.compare(QLatin1String("AQUIFER_2D"), Qt::CaseInsensitive) == 0)
        return InfilDest::Aquifer2D;
    if (ok) *ok = false;
    return InfilDest::Lost;
}

QString infilDestLabel(InfilDest d)
{
    switch (d) {
    case InfilDest::SubcatchAquifer: return tr("Subcatchment Aquifer");
    case InfilDest::Aquifer2D:       return tr("2D Aquifer");
    case InfilDest::Lost:            break;
    }
    return tr("Lost");
}

QStringList infilDestLabels()
{
    QStringList out;
    for (int d = int(InfilDest::Lost); d <= int(InfilDest::Aquifer2D); ++d)
        out << infilDestLabel(static_cast<InfilDest>(d));
    return out;
}

bool infilDestSupported(InfilDest d)
{
    // Engine D-I4 — the others parse so the grammar is stable, and are
    // rejected at validation with a "not supported in this release" message.
    return d == InfilDest::Lost;
}

// ---------------------------------------------------------------------------
// Classified-lookup tables
// ---------------------------------------------------------------------------

InfilRow InfilLookupTable::lookup(const QString &k1, const QString &k2,
                                  bool *matched) const
{
    for (const InfilLookupEntry &e : entries) {
        if (e.key1.compare(k1, Qt::CaseInsensitive) != 0) continue;
        if (twoKey && e.key2.compare(k2, Qt::CaseInsensitive) != 0) continue;
        if (matched) *matched = true;
        return e.row;
    }
    if (matched) *matched = false;
    return fallback;
}

namespace {

constexpr const char *kCsvName = "# NAME:";
constexpr const char *kCsvKey1 = "# KEY1:";
constexpr const char *kCsvKey2 = "# KEY2:";

/*! One CSV field per positional parameter, `-` for unset. Trailing columns
 *  the method does not use are still written (the CSV grid is fixed-width,
 *  unlike the .inp row form) so a spreadsheet round-trips cleanly. */
QString csvParam(double v)
{
    return std::isnan(v) ? QStringLiteral("-") : QString::number(v, 'g', 12);
}

/*! `method,p0,p1,p2,p3,p4,dest` — the tail shared by entry and fallback rows. */
QString csvRowTail(const InfilRow &row)
{
    QString out = infilMethodToken(row.method);
    for (int k = 0; k < kInfilMaxParams; ++k)
        out += QChar(',') + csvParam(row.p[k]);
    out += QChar(',') + infilDestToken(row.dest);
    return out;
}

/*! Parse `method,p0..p4,dest` starting at \p first. Returns false when the
 *  method token is unknown — which is also how a header row is detected. */
bool parseCsvRowTail(const QStringList &f, int first, InfilRow *row)
{
    if (f.size() < first + 1) return false;
    bool okm = false;
    const InfilMethod m = infilMethodFromToken(f[first], &okm);
    if (!okm) return false;
    row->method = m;
    for (int k = 0; k < kInfilMaxParams; ++k) {
        const int idx = first + 1 + k;
        if (idx >= f.size()) break;
        const QString t = f[idx].trimmed();
        if (t.isEmpty() || t == QLatin1String("-")) continue;
        bool okv = false;
        const double v = t.toDouble(&okv);
        if (okv) row->p[k] = v;
    }
    const int destIdx = first + 1 + kInfilMaxParams;
    if (destIdx < f.size() && !f[destIdx].trimmed().isEmpty())
        row->dest = infilDestFromToken(f[destIdx], nullptr);
    return true;
}

} // namespace

bool saveLookupTableCsv(const InfilLookupTable &table, const QString &path,
                        QString *err)
{
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (err) *err = QCoreApplication::translate(
            "MeshInfil", "Cannot open %1 for writing.").arg(path);
        return false;
    }

    QString text;
    QTextStream s(&text);
    s << "# OpenSWMM 2D infiltration lookup table\n";
    s << kCsvName << ' ' << table.name << '\n';
    s << kCsvKey1 << ' ' << table.key1Label << '\n';
    if (table.twoKey)
        s << kCsvKey2 << ' ' << table.key2Label << '\n';
    // Header row, emitted as a comment so a reader that ignores '#' lines and
    // one that skips a leading header row both do the right thing.
    s << "# key1";
    if (table.twoKey) s << ",key2";
    s << ",method,p0,p1,p2,p3,p4,dest\n";

    for (const InfilLookupEntry &e : table.entries) {
        s << e.key1;
        if (table.twoKey) s << ',' << e.key2;
        s << ',' << csvRowTail(e.row) << '\n';
    }
    // The fallback rides as a '*' keyed row — same spelling the
    // [2D_INFILTRATION_DEFAULTS] section uses for its mesh-wide default.
    if (!table.fallback.isNone()) {
        s << '*';
        if (table.twoKey) s << ",*";
        s << ',' << csvRowTail(table.fallback) << '\n';
    }
    s.flush();

    out.write(text.toUtf8());
    if (!out.commit()) {
        if (err) *err = QCoreApplication::translate(
            "MeshInfil", "Atomic save failed for %1: %2")
            .arg(path, out.errorString());
        return false;
    }
    return true;
}

bool loadLookupTableCsv(InfilLookupTable *table, const QString &path,
                        QString *err)
{
    if (!table) return false;
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (err) *err = QCoreApplication::translate(
            "MeshInfil", "Cannot open %1 for reading.").arg(path);
        return false;
    }

    *table = InfilLookupTable{};
    const QStringList lines =
        QString::fromUtf8(in.readAll()).split(QChar('\n'), Qt::SkipEmptyParts);

    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty()) continue;
        if (line.startsWith(QChar('#'))) {
            auto meta = [&line](const char *tagName, QString *out) {
                const QLatin1String tag(tagName);
                if (!line.startsWith(tag, Qt::CaseInsensitive)) return false;
                *out = line.mid(int(tag.size())).trimmed();
                return true;
            };
            if (meta(kCsvName, &table->name)) continue;
            if (meta(kCsvKey1, &table->key1Label)) continue;
            QString k2;
            if (meta(kCsvKey2, &k2)) { table->key2Label = k2; table->twoKey = true; }
            continue;
        }

        const QStringList f = line.split(QChar(','));
        // The key count distinguishes the two grids: single-key rows carry
        // 1 + 1 + 5 + 1 = 8 fields, two-key rows 9. The `# KEY2:` metadata
        // wins when present so a two-key row that omits its trailing DEST is
        // still read against the right column offsets.
        const int keys =
            (table->twoKey || f.size() >= 2 + 1 + kInfilMaxParams + 1) ? 2 : 1;
        InfilLookupEntry e;
        if (!parseCsvRowTail(f, keys, &e.row))
            continue;   // header row or unknown method — skip, do not fail
        if (keys == 2) table->twoKey = true;
        e.key1 = f.value(0).trimmed();
        if (keys == 2) e.key2 = f.value(1).trimmed();
        if (e.key1 == QLatin1String("*"))
            table->fallback = e.row;
        else
            table->entries.append(e);
    }

    if (table->name.isEmpty())
        table->name = QFileInfo(path).completeBaseName();
    return true;
}

} // namespace mesh
