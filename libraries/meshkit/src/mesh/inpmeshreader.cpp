/**
 * @file      inpmeshreader.cpp
 * @brief     Read the `[2D_*]` mesh sections — `[2D_VERTICES]`, `[2D_TRIANGLES]`, `[2D_QUADS]`
 *            (mixed meshes: quads appended after the triangles, cell index = n_triangles + j; a
 *            triangle row after a quad row is an error), `[2D_VERTEX_NODE_MAP]`,
 *            `[2D_TRIANGLE_NODE_MAP]`, the per-edge `[2D_BOUNDARY_CONDITIONS]` /
 *            `[2D_EDGE_CONVEYANCE]` pair and the GG0a per-cell `[2D_INFILTRATION_OPTIONS]` /
 *            `[2D_INFILTRATION_DEFAULTS]` / `[2D_INFILTRATION]` family — from a SWMM `.inp`,
 *            resolving the `[2D_MESH_FILE]` indirection when present.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   Apache-2.0
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "mesh/inpmeshreader.h"
#include "mesh/meshbctype.h"
#include "mesh/meshcellgeom.h"
#include "mesh/meshinfil.h"

#include <QByteArray>
#include <QChar>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>
#ifdef __APPLE__
#include <xlocale.h>
#else
#include <charconv>
#endif

namespace mesh
{

namespace
{

constexpr const char *kSecVertices = "[2D_VERTICES]";
constexpr const char *kSecTriangles = "[2D_TRIANGLES]";
constexpr const char *kSecQuads = "[2D_QUADS]"; // engine 2026-09-06 mixed meshes
constexpr const char *kSecMeshFile = "[2D_MESH_FILE]";
constexpr const char *kSecVertexNodeMap = "[2D_VERTEX_NODE_MAP]";     // 1D<->2D coupling
constexpr const char *kSecTriangleNodeMap = "[2D_TRIANGLE_NODE_MAP]"; // node→cell coupling (Part C)
constexpr const char *kSecBC = "[2D_BOUNDARY_CONDITIONS]";            // §V.VD.1
constexpr const char *kSecConveyance = "[2D_EDGE_CONVEYANCE]";        // Engine §11A
constexpr const char *kSecInfilOptions = "[2D_INFILTRATION_OPTIONS]"; // GG0a
constexpr const char *kSecInfilDefaults = "[2D_INFILTRATION_DEFAULTS]"; // GG0a
constexpr const char *kSecInfil = "[2D_INFILTRATION]";                  // GG0a
constexpr const char *kSecInitHgl = "[2D_INITIAL_HGL]"; // initial water-surface elevation

/*! Per-row pre-mesh BC accumulator: (cell, local edge, value). Resolved to
 *  a flat slot (`edgeSlot(cell, e)`) in a sized QVector<MeshEdgeBC> after the
 *  mesh is known — only then can `e < cell.vertexCount()` be checked (a quad
 *  has edges 0..3, a triangle 0..2). */
struct BCRow {
    int cell = -1;
    int e = -1;
    MeshEdgeBC bc;
};
using BCRowList = QVector<BCRow>;

/*! Per-row pre-mesh conveyance accumulator: (FROM_VERTEX, TO_VERTEX, value).
 *  Engine §11A format — the row keys an edge by its endpoints, not by
 *  (tri, eLocal), so resolution requires walking the mesh after it loads
 *  and writing the value into every matching slot (covers the interior-
 *  edge symmetry mirror in one pass). */
struct ConveyanceRow {
    int va = -1;
    int vb = -1;
    double conv = 1.0;
};
using ConveyanceRowList = QVector<ConveyanceRow>;

/*! One `[2D_INITIAL_HGL]` row: `CELL HGL`, CELL a zero-based cell index or
 *  `*` (cell = -1) for every cell. The section may precede the cell sections
 *  or sit in the other file of an external mesh, so rows are collected and
 *  applied, in file order, once the cells are known. */
struct InitHglRow {
    int cell = -1;
    double hgl = 0.0;
};
using InitHglRowList = QVector<InitHglRow>;

/**
 * @brief Read a whole existing file as raw bytes, dropping every carriage return.
 * @param[in] path Borrowed filesystem pathname, unchanged; opened read-only and closed on return.
 * @param[out] out Required nonnull borrowed buffer, assigned only after exists/open checks pass;
 * untouched on checked failure. Stays NUL-terminated (QByteArray guarantee).
 * @param[out] err Optional borrowed diagnostic, overwritten for missing/open failure and not
 * cleared on success.
 * @return True after readAll(), false on checked missing/open failure, with the same messages as
 * the former text reader. No decoding: lines are split and decoded later, one at a time.
 *
 * @details Every '\r' is removed, not only CRLF pairs, because that is what the former
 * QIODevice::Text read did (QIODevicePrivate::read drops each '\r'), so a stray CR inside a line
 * still joins its neighbours exactly as before.
 */
bool readFileBytes(const QString &path, QByteArray *out, QString *err)
{
    QFile f(path);
    if (!f.exists()) {
        if (err)
            *err = QStringLiteral("File does not exist: %1").arg(path);
        return false;
    }
    if (!f.open(QIODevice::ReadOnly)) {
        if (err)
            *err = QStringLiteral("Cannot open %1 for reading.").arg(path);
        return false;
    }
    *out = f.readAll();
    if (std::memchr(out->constData(), '\r', size_t(out->size()))) {
        char *b = out->data();
        out->truncate(std::remove(b, b + out->size(), '\r') - b);
    }
    return true;
}

/*! QChar::isSpace restricted to ASCII: U+0009..U+000D and U+0020. Lines containing any byte
 *  >= 0x80 never reach the byte path (they are decoded and take the QString path), so this is
 *  the whole whitespace set the byte path can meet. */
inline bool isAsciiSpace(char c) noexcept
{
    return c == ' ' || (c >= '\t' && c <= '\r');
}

inline bool isAsciiDigit(char c) noexcept
{
    return c >= '0' && c <= '9';
}

#ifdef __APPLE__
/*! Immutable per-process C numeric locale for strtod_l — the meshkit smsio.cpp pattern: Apple's
 *  floating-point std::from_chars needs macOS 26, below the shipped deployment targets. */
locale_t cNumericLocale()
{
    struct Numeric {
        locale_t value = newlocale(LC_NUMERIC_MASK, "C", nullptr);
        ~Numeric()
        {
            if (value)
                freelocale(value);
        }
    };
    static const Numeric locale;
    return locale.value;
}
#endif

/**
 * @brief Convert a plain decimal token without building a QString; refuse anything else.
 * @param[in] b Token start inside a NUL-terminated buffer; the byte after the token is ASCII
 * whitespace, ';', '\n' or the terminator, so a C converter stops exactly at the token end.
 * @param[in] n Token length in bytes.
 * @param[out] v Converted value, written only on success.
 * @return True only for `[+-]? D+ ( . D+ )? ( [eE] [+-]? D+ )?` up to 64 bytes that converts to a
 * finite value without range error. Every other token — inf/nan names, hex, `.5`/`5.`, overflow,
 * underflow — returns false so the caller asks QString::toDouble, which keeps Qt's exact
 * acceptance rules. On the accepted grammar both converters round correctly, so the value is
 * bit-identical to QString::toDouble's.
 */
bool fastToDouble(const char *b, qsizetype n, double *v) noexcept
{
    if (n <= 0 || n > 64)
        return false;
    const char *p = b;
    const char *const e = b + n;
    if (*p == '+' || *p == '-')
        ++p;
    const char *q = p;
    while (p < e && isAsciiDigit(*p))
        ++p;
    if (p == q)
        return false;
    if (p < e && *p == '.') {
        q = ++p;
        while (p < e && isAsciiDigit(*p))
            ++p;
        if (p == q)
            return false;
    }
    if (p < e && (*p == 'e' || *p == 'E')) {
        ++p;
        if (p < e && (*p == '+' || *p == '-'))
            ++p;
        q = p;
        while (p < e && isAsciiDigit(*p))
            ++p;
        if (p == q)
            return false;
    }
    if (p != e)
        return false;
#ifdef __APPLE__
    const locale_t loc = cNumericLocale();
    if (!loc)
        return false;
    char *stop = nullptr;
    errno = 0;
    const double d = strtod_l(b, &stop, loc);
    if (stop != e || errno != 0 || !std::isfinite(d))
        return false;
#else
    double d = 0.0;
    const char *first = (*b == '+') ? b + 1 : b; // from_chars refuses a leading '+'
    const auto r = std::from_chars(first, e, d, std::chars_format::general);
    if (r.ec != std::errc{} || r.ptr != e || !std::isfinite(d))
        return false;
#endif
    *v = d;
    return true;
}

/**
 * @brief Convert a plain decimal integer token without building a QString; refuse anything else.
 * @return True only for `[+-]? D{1,18}` whose value fits int; every other token returns false so
 * the caller asks QString::toInt (which then decides, e.g. out of range -> not ok).
 */
bool fastToInt(const char *b, qsizetype n, int *v) noexcept
{
    if (n <= 0)
        return false;
    const char *p = b;
    const char *const e = b + n;
    bool neg = false;
    if (p < e && (*p == '+' || *p == '-'))
        neg = (*p++ == '-');
    if (p == e || e - p > 18)
        return false;
    long long acc = 0;
    for (; p < e; ++p) {
        if (!isAsciiDigit(*p))
            return false;
        acc = acc * 10 + (*p - '0');
    }
    if (neg)
        acc = -acc;
    if (acc < INT_MIN || acc > INT_MAX)
        return false;
    *v = int(acc);
    return true;
}

/*! Tokens of one ASCII row, as byte ranges into the file buffer. The first kKeep are kept (the
 *  row parsers read at most index 6); `count` is the full token count, so size checks see the
 *  same number tokenize() would. */
struct ByteRow {
    static constexpr int kKeep = 8;
    const char *begin[kKeep];
    qsizetype len[kKeep];
    qsizetype count = 0;
};

/*! tokenize() on bytes: drop everything from the first ';', split on ASCII whitespace. */
void tokenizeBytes(const char *s, const char *e, ByteRow &row) noexcept
{
    if (const void *semi = std::memchr(s, ';', size_t(e - s)))
        e = static_cast<const char *>(semi);
    row.count = 0;
    while (s < e) {
        while (s < e && isAsciiSpace(*s))
            ++s;
        if (s >= e)
            break;
        const char *start = s;
        while (s < e && !isAsciiSpace(*s))
            ++s;
        if (row.count < ByteRow::kKeep) {
            row.begin[row.count] = start;
            row.len[row.count] = s - start;
        }
        ++row.count;
    }
}

/*! Row-token adaptor over a ByteRow: fast numeric paths, QString fallback for everything the
 *  fast paths refuse, so every conversion answers exactly as QString::toDouble/toInt would. */
struct ByteTokens {
    const ByteRow &row;
    qsizetype size() const noexcept { return row.count; }
    QString str(int k) const { return QString::fromLatin1(row.begin[k], row.len[k]); }
    double toDouble(int k, bool *ok) const
    {
        double v = 0.0;
        if (fastToDouble(row.begin[k], row.len[k], &v)) {
            *ok = true;
            return v;
        }
        return str(k).toDouble(ok);
    }
    int toInt(int k, bool *ok) const
    {
        int v = 0;
        if (fastToInt(row.begin[k], row.len[k], &v)) {
            *ok = true;
            return v;
        }
        return str(k).toInt(ok);
    }
};

/*! Row-token adaptor over tokenize() output — the path for rows containing non-ASCII bytes. */
struct StringTokens {
    const QStringList &tok;
    qsizetype size() const noexcept { return tok.size(); }
    QString str(int k) const { return tok[k]; }
    double toDouble(int k, bool *ok) const { return tok[k].toDouble(ok); }
    int toInt(int k, bool *ok) const { return tok[k].toInt(ok); }
};

/**
 * @brief Append one `[2D_VERTICES]` row `X Y Z [TAG]`.
 * @param[in] tok Row tokens (ByteTokens or StringTokens); a blank/comment row has none.
 * @param[in] rawTrimmed Callable returning the trimmed raw line, used only for diagnostics.
 * @param[in,out] out Mesh; one vertex appended on success, untouched on error.
 * @return Empty on success or blank row, else the diagnostic the former reader produced.
 */
template <typename Tokens, typename RawFn>
QString parseVertexRow(const Tokens &tok, const RawFn &rawTrimmed, MeshResult &out)
{
    if (tok.size() == 0)
        return {};
    if (tok.size() < 3)
        return QStringLiteral("[2D_VERTICES] needs X Y Z (got: %1)").arg(rawTrimmed());
    bool okx = false, oky = false, okz = false;
    const double x = tok.toDouble(0, &okx);
    const double y = tok.toDouble(1, &oky);
    const double z = tok.toDouble(2, &okz);
    if (!okx || !oky || !okz)
        return QStringLiteral("[2D_VERTICES] non-numeric value (got: %1)").arg(rawTrimmed());
    MeshVertex v;
    v.xy = QPointF(x, y);
    v.z = z;
    if (tok.size() >= 4)
        v.tag = tok.str(3);
    out.vertices.append(std::move(v));
    return {};
}

/**
 * @brief Append one `[2D_TRIANGLES]` `V1 V2 V3 MANNINGS_N [INIT_DEPTH] [TAG]` or `[2D_QUADS]`
 * `V1 V2 V3 V4 MANNINGS_N [INIT_DEPTH] [TAG]` row.
 * @param[in] tok Row tokens (ByteTokens or StringTokens); a blank/comment row has none.
 * @param[in] isQuads True for a [2D_QUADS] row.
 * @param[in] rawTrimmed Callable returning the trimmed raw line, used only for diagnostics.
 * @param[in,out] out Mesh; one cell appended on success, untouched on error.
 * @return Empty on success or blank row, else the diagnostic the former reader produced.
 *
 * @details Cells land in MeshResult::triangles in the engine's order — triangles first, then
 * quads — so a [2D_TRIANGLES] row after any [2D_QUADS] row is an error (same rule as the engine),
 * otherwise every cell-addressed section behind it would silently renumber. tok[nvert] is
 * MANNINGS_N; tok[nvert+1] is INIT_DEPTH when numeric, otherwise the historical TAG; tok[nvert+2]
 * is TAG after a depth.
 */
template <typename Tokens, typename RawFn>
QString parseCellRow(const Tokens &tok, bool isQuads, const RawFn &rawTrimmed, MeshResult &out)
{
    if (tok.size() == 0)
        return {};
    const int nvert = isQuads ? 4 : 3;
    const char *sec = isQuads ? kSecQuads : kSecTriangles;
    if (tok.size() < nvert)
        return QStringLiteral("%1 needs %2 (got: %3)")
            .arg(QLatin1String(sec),
                 isQuads ? QStringLiteral("V1 V2 V3 V4") : QStringLiteral("V1 V2 V3"),
                 rawTrimmed());
    if (!isQuads && !out.triangles.isEmpty() && out.triangles.last().isQuad())
        return QStringLiteral("[2D_TRIANGLES] row after a [2D_QUADS] row: cells "
                              "are numbered triangles first, then quads (got: %1)")
            .arg(rawTrimmed());
    MeshTriangle t;
    for (int k = 0; k < nvert; ++k) {
        bool okv = false;
        const int v = tok.toInt(k, &okv);
        if (!okv)
            return QStringLiteral("%1 non-integer vertex index (got: %2)")
                .arg(QLatin1String(sec), rawTrimmed());
        t.setVertex(k, v);
    }
    if (tok.size() >= nvert + 1) {
        bool okn = false;
        const double n = tok.toDouble(nvert, &okn);
        if (okn)
            t.mannings = n;
    }
    if (tok.size() >= nvert + 2) {
        bool okd = false;
        const double d = tok.toDouble(nvert + 1, &okd);
        if (okd) {
            t.initDepth = d;
            if (tok.size() >= nvert + 3)
                t.tag = tok.str(nvert + 2);
        } else {
            t.tag = tok.str(nvert + 1);
        }
    }
    out.triangles.append(std::move(t));
    return {};
}

/**
 * @brief Split a numeric/attribute mesh row on QString whitespace after stripping its first
 * semicolon tail.
 * @param[in] raw Borrowed line, unchanged; semicolon always begins comment even inside quote
 * characters.
 * @return Owned tokens in original case, empty for blank/comment-only content. No
 * quoting/escaping, number parsing or field validation; file-reference rows use their separate
 * raw-tail parser. Allocates token strings/list, retains no views or external pointers and
 * changes no source state.
 *
 * @details Token-split a data line. Returns empty vector for comment/blank lines.
 *
 * Hand-rolled rather than `split(QRegularExpression("\\s+"))`: this runs once per line, i.e.
 * ~2.3M times on a 1.5M-triangle `.2dm`, and the regex path allocated a fresh QStringList plus a
 * match context every time. Splitting on QChar::isSpace over a view of the original string is
 * behaviourally identical (`\s` and isSpace agree on the whitespace this format uses) and
 * allocates only the tokens themselves.
 */
QStringList tokenize(const QString &raw)
{
    const QStringView line = QStringView(raw).left([&raw] {
        const qsizetype semi = raw.indexOf(QChar(';'));
        return semi >= 0 ? semi : raw.size();
    }());

    QStringList out;
    qsizetype i = 0;
    const qsizetype n = line.size();
    while (i < n) {
        while (i < n && line[i].isSpace())
            ++i;
        if (i >= n)
            break;
        const qsizetype start = i;
        while (i < n && !line[i].isSpace())
            ++i;
        out.append(line.mid(start, i - start).toString());
    }
    return out;
}

// File references have different rules from numeric mesh rows: the engine
// preserves the raw path tail and whitespace/comments inside quotes.
/**
 * @brief Parse a FILE row while preserving spaces/semicolons inside double-quoted path tails.
 * @param[in] raw Borrowed line, unchanged; double quotes toggle comment protection without
 * escape handling.
 * @return Owned path after case-insensitive FILE plus required whitespace, trimmed and stripped
 * of one matching outer quote pair; empty for no matching/nonempty tail. First unquoted
 * semicolon starts comment. Unmatched/internal quote structure is not validated, unquoted spaces
 * are retained, no existence/path resolution or IO occurs. Allocates text only, no source
 * mutation or ownership retention.
 */
QString meshFileReference(const QString &raw)
{
    QString line = raw;
    bool quoted = false;
    for (qsizetype i = 0; i < line.size(); ++i) {
        if (line[i] == QLatin1Char('"'))
            quoted = !quoted;
        else if (line[i] == QLatin1Char(';') && !quoted) {
            line.truncate(i);
            break;
        }
    }
    line = line.trimmed();
    if (line.size() <= 4 || !line.startsWith(QLatin1String("FILE"), Qt::CaseInsensitive) ||
        !line[4].isSpace())
        return {};
    const QString path = line.mid(4).trimmed();
    if (path.size() >= 2 && path.front() == QLatin1Char('"') && path.back() == QLatin1Char('"'))
        return path.mid(1, path.size() - 2);
    return path;
}

/**
 * @brief Parse method, up to five positional parameters and optional trailing destination into a
 * row.
 * @param[in] tok Borrowed token list, unchanged; numeric/file-token units are project authoring
 * units.
 * @param[in] first Nonnegative valid method position, checked for missing upper extent but not
 * negative indexing.
 * @param[in] secName Required NUL-terminated Latin-1 section label for diagnostics, borrowed/not
 * retained.
 * @param[in,out] row Borrowed row, method/destination/parsed numeric slots overwritten as
 * encountered; missing or '-' slots keep prior values, so callers normally supply a fresh
 * NaN-initialized row.
 * @return Empty string on recognized method/destination and parseable <=five parameters,
 * otherwise owned diagnostic. Errors can leave partial row mutation; method may already be reset
 * to None on unknown token. Last nonnumeric/non-dash token is interpreted as destination,
 * including legacy AQUIFER_2D for review, not current runtime support. No method-specific
 * count/range/finite/physical validation, unit conversion, initialization of skipped fields, IO
 * or pointer retention; allocates diagnostics as needed.
 *
 * @details GG0a — parse `METHOD [P1..P5] [DEST]` starting at \p first into \p row.
 *
 * Mirrors the engine's `parseInfil2DRowTail` (SectionHandlers2D.cpp): parameter columns are
 * POSITIONAL and in PROJECT units, `-` means unset (left as NaN here rather than 0, so the GUI
 * can tell "not written" from "written as zero"), trailing unused columns may be omitted, and
 * DEST is the final token when it is neither numeric nor `-`. Returns an empty string on
 * success, an error message otherwise.
 */
QString parseInfilRowTail(const QStringList &tok, int first, const char *secName, InfilRow &row)
{
    const QString sec = QString::fromLatin1(secName);
    if (tok.size() <= first)
        return QStringLiteral("%1 missing METHOD").arg(sec);

    bool okm = false;
    row.method = infilMethodFromToken(tok[first], &okm);
    if (!okm)
        return QStringLiteral("%1 unknown METHOD: %2").arg(sec, tok[first]);

    int end = int(tok.size());
    if (end > first + 1) {
        const QString &last = tok[end - 1];
        bool numeric = false;
        (void)last.toDouble(&numeric);
        if (!numeric && last != QLatin1String("-")) {
            bool okd = false;
            row.dest = infilDestFromToken(last, &okd);
            if (!okd)
                return QStringLiteral("%1 unknown DEST: %2").arg(sec, last);
            --end;
        }
    }

    for (int k = first + 1; k < end; ++k) {
        const int idx = k - (first + 1);
        if (idx >= kInfilMaxParams)
            return QStringLiteral("%1 too many parameter columns (max %2)")
                .arg(sec)
                .arg(kInfilMaxParams);
        if (tok[k] == QLatin1String("-"))
            continue; // unset
        bool okv = false;
        const double v = tok[k].toDouble(&okv);
        if (!okv)
            return QStringLiteral("%1 invalid parameter: %2").arg(sec, tok[k]);
        row.p[idx] = v;
    }
    return {};
}

/**
 * @brief Parse numeric seconds or the first three colon-separated hour/minute/second components.
 * @param[in] token Borrowed duration text, unchanged; direct QString::toDouble is attempted
 * first.
 * @return Plain numeric value in seconds, or sum of up to three colon fields times 3600/60/1; -1
 * for an unconvertible considered component. Extra fields after the third are ignored,
 * individual signs/ ranges are not constrained and no explicit finite/nonnegative check is
 * performed. Caller rejects a negative total, which also conflates negative numeric input with
 * failure. Allocates split fields, no time-zone/date interpretation, state mutation or ownership
 * retention.
 *
 * @details GG0a — `INFIL_STEP` value: `h:mm:ss`, `h:mm`, or plain seconds. Matches the engine's
 * `openswmm::input::parse_time_seconds`. Returns a negative value when the token is not a
 * duration at all.
 */
double parseInfilStepSeconds(const QString &token)
{
    bool ok = false;
    const double plain = token.toDouble(&ok);
    if (ok)
        return plain; // plain number = seconds

    const QStringList parts = token.split(QChar(':'));
    if (parts.isEmpty())
        return -1.0;
    double secs = 0.0;
    const double scale[3] = {3600.0, 60.0, 1.0};
    for (int i = 0; i < parts.size() && i < 3; ++i) {
        bool okp = false;
        const double v = parts[i].toDouble(&okp);
        if (!okp)
            return -1.0;
        secs += v * scale[i];
    }
    return secs;
}

/**
 * @brief Append one recognized geometry/coupling/infiltration section into a mutable authoring
 * mesh.
 * @param[in] sectionName Borrowed complete bracketed section name, compared case-insensitively,
 * unchanged.
 * @param[in] bodyLines Borrowed raw rows, unchanged; whitespace/comment tokenization and
 * project-unit values retained.
 * @param[in,out] out Borrowed result, appended/overwritten incrementally with no rollback on
 * failure. Vertices append raw numeric xyz/tag; cells append zero-based stored IDs, optional
 * numeric roughness/ depth/tag, enforcing triangles before any quad row. Index bounds, finite
 * values, distinctness and topology are not checked. Coupling references try valid numeric index
 * then first exact tag match, unresolved/short rows skip; vertex rows overwrite names/authoring
 * flags, cell rows append. Optional omitted coupling values stay unauthored, not physically
 * validated. Defaults append in order, explicit infiltration CELL is one-based and stored key
 * cell-1 without upper-bound check, duplicate overrides replace; options recognize INFIL_STEP.
 * @return Empty string for successful/ignored unknown section, otherwise first owned
 * syntax/order/token error with partial out retained. Does not retain unknown sections, resolve
 * node existence/engine ownership, convert units or perform IO. Allocates arrays/tag
 * indexes/diagnostics; caller owns synchronized result access.
 *
 * @details Parse the body lines of a section into the mesh result.
 *
 * Returns empty string on success, an error message otherwise. Unknown section names are
 * silently ignored so we round-trip whatever options / coupling-map sections happen to share the
 * file with the mesh.
 */
QString parseSection(const QString &sectionName, const QStringList &bodyLines, MeshResult &out)
{
    // [2D_VERTICES] / [2D_TRIANGLES] / [2D_QUADS] rows are parsed one at a time as the file is
    // walked (parseVertexRow / parseCellRow), never through here.

    // [2D_VERTEX_NODE_MAP] — VERTEX NODE [CD AREA]. The engine's authoritative
    // 1D<->2D coupling, distinct from the [2D_VERTICES] TAG column. Lands on
    // MeshVertex::coupledNode (the descriptive tag keeps its own field); the
    // optional CD / AREA columns land on couplingCd / couplingArea, keeping
    // the engine defaults (0.65 / 1.0 m²) when omitted.
    if (sectionName.compare(QLatin1String(kSecVertexNodeMap), Qt::CaseInsensitive) == 0) {
        // Lazily built on the first tag-form row (see below). Stays empty for
        // an all-index-form map, so index-form files pay nothing.
        QHash<QString, int> tagToVertex;
        for (const QString &raw : bodyLines) {
            const QStringList tok = tokenize(raw);
            if (tok.size() < 2)
                continue;
            // VERTEX_INDEX_OR_TAG: numeric index first; anything else — or a
            // number out of range — is resolved against the vertex TAG column.
            // The writer PREFERS the tag form (robust to renumbering), and
            // generated junction vertices always carry tag == node id, so
            // without this fallback a GUI-written map never round-tripped
            // through this reader (it only survived via the engine).
            bool okv = false;
            int v = tok[0].toInt(&okv);
            if (okv && (v < 0 || v >= out.vertices.size()))
                okv = false; // numeric but not a valid index — try as tag
            if (!okv) {
                // Tag form. The old code scanned every vertex per row — and
                // did NOT break on a miss — so a saved mesh (the writer
                // PREFERS tag form) cost nCoupled x nVertices string compares:
                // ~66 s at 100k coupled rows over 760k vertices, inside the
                // worker with no progress output, i.e. indistinguishable from
                // a hang. Build the index once, lazily, on the first tag row.
                if (tagToVertex.isEmpty() && !out.vertices.isEmpty()) {
                    tagToVertex.reserve(out.vertices.size());
                    // First occurrence wins, matching the old forward scan.
                    for (int i = out.vertices.size() - 1; i >= 0; --i)
                        if (!out.vertices[i].tag.isEmpty())
                            tagToVertex.insert(out.vertices[i].tag, i);
                }
                v = tagToVertex.value(tok[0], -1);
                if (v < 0)
                    continue;
            }
            out.vertices[v].coupledNode = tok[1];
            // A missing token stays unauthored so the save writes the row
            // back as it was read (the engine applies COUPLING_CD / the
            // node's coupling structure).
            out.vertices[v].couplingCdSet = out.vertices[v].couplingAreaSet = false;
            if (tok.size() >= 3) {
                bool okc = false;
                const double cd = tok[2].toDouble(&okc);
                if (okc) {
                    out.vertices[v].couplingCd = cd;
                    out.vertices[v].couplingCdSet = true;
                }
            }
            if (tok.size() >= 4) {
                bool oka = false;
                const double a = tok[3].toDouble(&oka);
                if (oka) {
                    out.vertices[v].couplingArea = a;
                    out.vertices[v].couplingAreaSet = true;
                }
            }
        }
        return {};
    }

    // [2D_TRIANGLE_NODE_MAP] — TRIANGLE NODE [CD AREA], repeated-row form
    // (Plan Part C): several nodes may couple to one triangle, one row each.
    // Rows land on MeshResult::cellCouplings so the layer / toolbar can show
    // and edit them. Tag-form triangle references (subcatchment tags) are
    // resolved to the FIRST triangle carrying the tag — same rule as the
    // engine parser. Legacy single-node rows parse identically.
    if (sectionName.compare(QLatin1String(kSecTriangleNodeMap), Qt::CaseInsensitive) == 0) {
        // Same lazy tag index as [2D_VERTEX_NODE_MAP]. This scan is latent
        // today (patchAttributeSections leaves triangleToNode empty, so only
        // index-form rows get written) but fires the moment triangle coupling
        // is emitted — over 1.5M triangles.
        QHash<QString, int> tagToTriangle;
        for (const QString &raw : bodyLines) {
            const QStringList tok = tokenize(raw);
            if (tok.size() < 2)
                continue;
            bool okt = false;
            int t = tok[0].toInt(&okt);
            if (okt && (t < 0 || t >= out.triangles.size()))
                okt = false;
            if (!okt) {
                if (tagToTriangle.isEmpty() && !out.triangles.isEmpty()) {
                    // First triangle carrying the tag wins — same rule as the
                    // engine parser and as the old forward scan.
                    for (int i = out.triangles.size() - 1; i >= 0; --i)
                        if (!out.triangles[i].tag.isEmpty())
                            tagToTriangle.insert(out.triangles[i].tag, i);
                }
                t = tagToTriangle.value(tok[0], -1);
                if (t < 0)
                    continue;
            }
            CellCoupling cc;
            cc.tri = t;
            cc.nodeId = tok[1];
            cc.cdSet = cc.areaSet = false; // as read (see the vertex map)
            if (tok.size() >= 3) {
                bool okc = false;
                const double cd = tok[2].toDouble(&okc);
                if (okc) {
                    cc.cd = cd;
                    cc.cdSet = true;
                }
            }
            if (tok.size() >= 4) {
                bool oka = false;
                const double a = tok[3].toDouble(&oka);
                if (oka) {
                    cc.area = a;
                    cc.areaSet = true;
                }
            }
            out.cellCouplings.append(cc);
        }
        return {};
    }

    // GG0a — [2D_INFILTRATION_OPTIONS] — PARAMETER VALUE. Only INFIL_STEP is
    // defined; anything else is ignored rather than rejected, so a file
    // written by a newer engine still loads.
    if (sectionName.compare(QLatin1String(kSecInfilOptions), Qt::CaseInsensitive) == 0) {
        for (const QString &raw : bodyLines) {
            const QStringList tok = tokenize(raw);
            if (tok.size() < 2)
                continue;
            if (tok[0].compare(QLatin1String("INFIL_STEP"), Qt::CaseInsensitive) != 0)
                continue;
            const double s = parseInfilStepSeconds(tok[1]);
            if (s < 0.0)
                return QStringLiteral("[2D_INFILTRATION_OPTIONS] invalid "
                                      "INFIL_STEP (expected hh:mm:ss >= 0): %1")
                    .arg(tok[1]);
            out.infilOptions.infilStep = s;
        }
        return {};
    }

    // GG0a — [2D_INFILTRATION_DEFAULTS] — TAG METHOD [P1..P5] [DEST].
    // `*` is the mesh-wide fallback and may appear anywhere in the section;
    // rows are kept in file order so a save re-emits the same shape.
    if (sectionName.compare(QLatin1String(kSecInfilDefaults), Qt::CaseInsensitive) == 0) {
        for (const QString &raw : bodyLines) {
            const QStringList tok = tokenize(raw);
            if (tok.isEmpty())
                continue;
            if (tok.size() < 2)
                return QStringLiteral("[2D_INFILTRATION_DEFAULTS] needs TAG "
                                      "METHOD [P1..P5] [DEST] (got: %1)")
                    .arg(raw.trimmed());
            InfilDefaultRow d;
            d.tag = tok[0];
            const QString err = parseInfilRowTail(tok, 1, kSecInfilDefaults, d.row);
            if (!err.isEmpty())
                return err;
            out.infilDefaults.append(d);
        }
        return {};
    }

    // GG0a — [2D_INFILTRATION] — CELL METHOD [P1..P5] [DEST]. CELL is
    // 1-BASED in the file and 0-based in MeshResult::infilOverrides. The
    // upper bound is not checked here: the section may precede
    // [2D_TRIANGLES] in the file (and, for an external mesh, live in a
    // different file altogether), so out.triangles is not populated yet.
    if (sectionName.compare(QLatin1String(kSecInfil), Qt::CaseInsensitive) == 0) {
        for (const QString &raw : bodyLines) {
            const QStringList tok = tokenize(raw);
            if (tok.isEmpty())
                continue;
            if (tok.size() < 2)
                return QStringLiteral("[2D_INFILTRATION] needs CELL METHOD "
                                      "[P1..P5] [DEST] (got: %1)")
                    .arg(raw.trimmed());
            bool okc = false;
            const int cell = tok[0].toInt(&okc);
            if (!okc || cell < 1)
                return QStringLiteral("[2D_INFILTRATION] invalid CELL index "
                                      "(1-based): %1")
                    .arg(tok[0]);
            InfilRow row;
            const QString err = parseInfilRowTail(tok, 1, kSecInfil, row);
            if (!err.isEmpty())
                return err;
            out.infilOverrides.insert(cell - 1, row);
        }
        return {};
    }

    // Other sections (options) carry no rendering data.
    return {};
}

/**
 * @brief Decode one native boundary row into a deferred cell/local-edge assignment.
 * @param[in] raw Borrowed text, unchanged; blank/comment row returns success without changing
 * row.
 * @param[in,out] row Borrowed accumulator, must normally be default initialized. After valid
 * cell/edge/type, fields are assigned for the selected variant, group at token 5 and optional
 * parameter at token 3; missing/'*' strings clear those selected names/group. Unused previous
 * fields are not reset here.
 * @return Empty on recognized row or blank, diagnostic for too few fields, negative/noninteger
 * cell/edge, edge>=4 or unknown type. Cell upper bound/triangle padding checked later. Numeric
 * slope/head/flow use toDouble without success flag, so missing/malformed parameter becomes
 * zero; physical/finite/name reference validation is deferred. Values remain authored project
 * units, no IO/engine edits or pointer retention.
 *
 * @details Parse one `[2D_BOUNDARY_CONDITIONS]` body line into a BCRow. Returns empty string on
 * success, an error message on malformed input.
 */
QString parseBCLine(const QString &raw, BCRow &row)
{
    const QStringList tok = tokenize(raw);
    if (tok.isEmpty())
        return {}; // comment / blank — skip silently
    if (tok.size() < 3)
        return QStringLiteral(
                   "[2D_BOUNDARY_CONDITIONS] needs TRI EDGE TYPE [PARAM_1 [PARAM_2 [GROUP]]] (got: %1)")
            .arg(raw.trimmed());

    // TRI is the unified cell index (triangles first, then quads); EDGE is
    // 0..2 for a triangle, 0..3 for a quad — the per-cell bound is checked
    // once the mesh is known (resolveBCRows).
    bool okt = false, oke = false;
    const int tri = tok[0].toInt(&okt);
    const int e = tok[1].toInt(&oke);
    if (!okt || !oke || tri < 0 || e < 0 || e >= kEdgeStride)
        return QStringLiteral("[2D_BOUNDARY_CONDITIONS] TRI/EDGE invalid (got: %1)")
            .arg(raw.trimmed());

    bool typeOk = false;
    const auto type = MeshBCTypes::fromInpToken(tok[2], &typeOk);
    if (!typeOk)
        return QStringLiteral("[2D_BOUNDARY_CONDITIONS] unknown TYPE '%1'").arg(tok[2]);

    row.cell = tri;
    row.e = e;
    row.bc.type = type;

    auto paramOrStar = [&](int idx) -> QString {
        if (tok.size() <= idx)
            return QString();
        const QString s = tok[idx];
        return (s == QStringLiteral("*")) ? QString() : s;
    };
    const QString p1 = paramOrStar(3);
    // tok[4] is PARAM_2 reserved — currently always "*".
    const QString grp = paramOrStar(5);
    row.bc.group = grp;

    switch (type) {
    case MeshBCTypes::Type::Wall:
        if (tok[2].compare(QLatin1String("WALL_DRAG"), Qt::CaseInsensitive) == 0) {
            bool ok = false;
            row.bc.wallDrag = p1.toDouble(&ok);
            if (!ok || !std::isfinite(row.bc.wallDrag) || row.bc.wallDrag < 0)
                return QStringLiteral("WALL_DRAG requires a finite nonnegative Cw");
        }
        break;
    case MeshBCTypes::Type::NormalFlow:
        row.bc.slope = p1.toDouble();
        break;
    case MeshBCTypes::Type::SpecifiedStageConst:
        row.bc.head = p1.toDouble();
        break;
    case MeshBCTypes::Type::SpecifiedStageTS:
    case MeshBCTypes::Type::SpecifiedFlowTS:
        row.bc.tseries = p1;
        break;
    case MeshBCTypes::Type::SpecifiedFlowConst:
        row.bc.flow = p1.toDouble();
        break;
    case MeshBCTypes::Type::RatingCurve:
        row.bc.curve = p1;
        break;
    }
    return {};
}

/**
 * @brief Decode an endpoint-keyed conveyance factor with strict [0,1] numeric bounds.
 * @param[in] raw Borrowed text, unchanged; whitespace/comment row succeeds with accumulator
 * untouched.
 * @param[out] row Borrowed record, assigned only after both distinct nonnegative endpoint ints
 * and factor parse successfully. Any checked failure leaves record untouched.
 * @return Empty for valid/blank row, otherwise owned diagnostic. No upper vertex bound,
 * actual-edge, mesh topology or name resolution check; extra tokens ignored. NaN/infinity fail
 * range comparisons. Factor dimensionless, no file/engine state mutation or retained references.
 *
 * @details Engine §11A — parse one `[2D_EDGE_CONVEYANCE]` line. Format: FROM_VERTEX  TO_VERTEX
 * CONVEYANCE CONVEYANCE must be in [0, 1] (strict, matches engine validation).
 */
QString parseConveyanceLine(const QString &raw, ConveyanceRow &row)
{
    const QStringList tok = tokenize(raw);
    if (tok.isEmpty())
        return {};
    if (tok.size() < 3)
        return QStringLiteral(
                   "[2D_EDGE_CONVEYANCE] needs FROM_VERTEX TO_VERTEX CONVEYANCE (got: %1)")
            .arg(raw.trimmed());

    bool okA = false, okB = false, okC = false;
    const int va = tok[0].toInt(&okA);
    const int vb = tok[1].toInt(&okB);
    const double cv = tok[2].toDouble(&okC);
    if (!okA || !okB || va < 0 || vb < 0 || va == vb)
        return QStringLiteral("[2D_EDGE_CONVEYANCE] invalid FROM/TO vertex (got: %1)")
            .arg(raw.trimmed());
    if (!okC || !(cv >= 0.0 && cv <= 1.0))
        return QStringLiteral(
                   "[2D_EDGE_CONVEYANCE] CONVEYANCE must be a number in [0, 1] (got: %1)")
            .arg(raw.trimmed());

    row.va = va;
    row.vb = vb;
    row.conv = cv;
    return {};
}

/*! Last `;; UNITS:` / `;; SOURCE_CRS:` values seen in one file (empty when absent). */
struct HeaderComments {
    QString units;
    QString crs;
};

/**
 * @brief Apply one trimmed line to the `;; UNITS:` / `;; SOURCE_CRS:` header scan.
 * @param[in] t Trimmed line. Only lines starting with ";;" qualify; keys match
 * case-insensitively after the prefix and following whitespace are dropped.
 * @param[in,out] h Last value wins for each key (an empty value also replaces), exactly like the
 * former whole-file scanUnitsHeader.
 *
 * @details The writer appends these comments BELOW the existing sections in inline (.inp) mode,
 * so every line of the file is offered, not just the lines before the first section.
 */
void noteHeaderComment(const QString &t, HeaderComments &h)
{
    if (!t.startsWith(QStringLiteral(";;")))
        return;
    const QString rest = t.mid(2).trimmed();
    if (rest.startsWith(QStringLiteral("UNITS:"), Qt::CaseInsensitive))
        h.units = rest.mid(QStringLiteral("UNITS:").size()).trimmed();
    else if (rest.startsWith(QStringLiteral("SOURCE_CRS:"), Qt::CaseInsensitive))
        h.crs = rest.mid(QStringLiteral("SOURCE_CRS:").size()).trimmed();
}

/*! How the walk treats the body of the current section. */
enum class SectionKind {
    None,       ///< Before the first header: lines are ignored.
    Vertices,   ///< Rows parsed one at a time (parseVertexRow).
    Triangles,  ///< Rows parsed one at a time (parseCellRow).
    Quads,      ///< Rows parsed one at a time (parseCellRow).
    MeshFile,   ///< Body collected, first FILE row wins.
    BC,         ///< Body collected into deferred BC rows.
    Conveyance, ///< Body collected into deferred conveyance rows.
    InitHgl,    ///< Body collected into deferred initial-HGL rows.
    Body,       ///< Body collected and handed to parseSection.
    Ignored,    ///< Any other section: parseSection would ignore it, so nothing is kept.
};

SectionKind sectionKind(const QString &name)
{
    const auto is = [&name](const char *s) {
        return name.compare(QLatin1String(s), Qt::CaseInsensitive) == 0;
    };
    if (is(kSecVertices))
        return SectionKind::Vertices;
    if (is(kSecTriangles))
        return SectionKind::Triangles;
    if (is(kSecQuads))
        return SectionKind::Quads;
    if (is(kSecMeshFile))
        return SectionKind::MeshFile;
    if (is(kSecBC))
        return SectionKind::BC;
    if (is(kSecConveyance))
        return SectionKind::Conveyance;
    if (is(kSecInitHgl))
        return SectionKind::InitHgl;
    if (is(kSecVertexNodeMap) || is(kSecTriangleNodeMap) || is(kSecInfilOptions) ||
        is(kSecInfilDefaults) || is(kSecInfil))
        return SectionKind::Body;
    return SectionKind::Ignored;
}

/**
 * @brief Decode one line exactly as the former whole-file QString::fromUtf8 + split('\n') did.
 * @param[in] b,e Line bytes without the '\n'.
 * @param[in] firstLine True for the first line of the file.
 *
 * @details QString::fromUtf8 skips one UTF-8 BOM at the start of every input it is given. The
 * whole-file decode therefore dropped one BOM on the first line only; BOMs opening any later line
 * all stayed as U+FEFF. A later line's leading BOMs are counted and re-added here, so fromUtf8
 * never sees one at the start. '\n' never occurs inside a UTF-8 sequence, valid or not, so
 * decoding line by line yields the same characters otherwise.
 */
QString decodeLine(const char *b, const char *e, bool firstLine)
{
    if (firstLine)
        return QString::fromUtf8(b, e - b);
    qsizetype boms = 0;
    while (e - b >= 3 && uchar(b[0]) == 0xEF && uchar(b[1]) == 0xBB && uchar(b[2]) == 0xBF) {
        b += 3;
        ++boms;
    }
    if (boms == 0)
        return QString::fromUtf8(b, e - b);
    return QString(boms, QChar(0xFEFF)) + QString::fromUtf8(b, e - b);
}

/*! Upper bound on the rows of the section starting at \p p: lines until the next line whose first
 *  non-blank byte is '['. Used only to size the vertex / cell arrays. */
qsizetype countRowsUntilHeader(const char *p, const char *end)
{
    qsizetype rows = 0;
    while (p < end) {
        const char *q = p;
        while (q < end && *q != '\n' && isAsciiSpace(*q))
            ++q;
        if (q < end && *q == '[')
            break;
        const void *nl = std::memchr(p, '\n', size_t(end - p));
        p = nl ? static_cast<const char *>(nl) + 1 : end;
        ++rows;
    }
    return rows;
}

/**
 * @brief Walk one file's bracket-delimited native mesh sections in a single pass over its bytes,
 * accumulating authoring data, deferred edge rows and the `;;` header comments.
 * @param[in] bytes Borrowed whole file, '\r' already removed, NUL-terminated, unchanged. Only
 * trimmed lines beginning '[' and ending ']' start a section, so trailing header comments are not
 * stripped before recognition.
 * @param[in,out] out Borrowed mesh, existing contents retained and appended; no geometry/unit
 * validation or transactional rollback. Coupling tag/index resolution follows current
 * accumulated order and is not deferred until later vertex/cell sections.
 * @param[out] sawVertices Reset false, then true once a vertices section is seen, even empty.
 * @param[out] sawTriangles Reset false, then true once a triangles or quads section is seen.
 * @param[out] meshFileRefOut Optional, cleared first. First nonempty FILE within each
 * mesh-reference block wins, but a later such block can replace it; null ignores references.
 * @param[in,out] bcRowsOut Optional deferred BC vector, not cleared, valid nonblank rows
 * appended; null skips BC parsing/validation entirely.
 * @param[in,out] conveyOut Optional deferred endpoint-factor vector, not cleared, rows appended;
 * null skips its section parsing/validation entirely.
 * @param[out] headers Last `;; UNITS:` / `;; SOURCE_CRS:` values of the WHOLE file — the scan
 * continues past a parse error, as the former separate whole-file scan did.
 * @return First owned parse diagnostic, or empty for completed traversal. Any error leaves
 * partial flags, mesh and accumulated rows; unknown sections are skipped.
 *
 * @details Lines are split at '\n'. A line made only of ASCII bytes is handled on bytes: vertex
 * and cell rows are tokenized in place and converted without building QStrings (ByteTokens).
 * Any line containing a byte >= 0x80 is decoded (decodeLine) and handled with the QString rules —
 * tokenize(), QString::trimmed(), Unicode whitespace — so non-ASCII content behaves exactly as
 * before. Vertex and cell rows are parsed as they are met, in file order; other sections are
 * collected and parsed when the next header (or the end) is reached, also in file order, so the
 * first error reported is the same one the former split-then-parse walk reported.
 */
QString parseSectionsFromBytes(const QByteArray &bytes, MeshResult &out, bool &sawVertices,
                               bool &sawTriangles, QString *meshFileRefOut, BCRowList *bcRowsOut,
                               ConveyanceRowList *conveyOut, HeaderComments *headers,
                               InitHglRowList *hglOut)
{
    sawVertices = sawTriangles = false;
    if (meshFileRefOut)
        meshFileRefOut->clear();
    *headers = {};

    QString currentSection;
    SectionKind kind = SectionKind::None;
    QStringList body;
    QString err;

    auto flush = [&]() -> QString {
        switch (kind) {
        case SectionKind::MeshFile:
            // First "FILE <path>" token wins (mirrors the engine).
            if (meshFileRefOut) {
                for (const QString &raw : body) {
                    const QString path = meshFileReference(raw);
                    if (!path.isEmpty()) {
                        *meshFileRefOut = path;
                        break;
                    }
                }
            }
            break;
        case SectionKind::BC:
            // §V.VD.1 — accumulate BC rows. Resolution to the sized
            // edgeBCs vector happens after the mesh is known.
            if (bcRowsOut) {
                for (const QString &raw : body) {
                    BCRow row;
                    const QString e = parseBCLine(raw, row);
                    if (!e.isEmpty())
                        return e;
                    if (row.cell >= 0)
                        bcRowsOut->append(row);
                }
            }
            break;
        case SectionKind::Conveyance:
            // Engine §11A — accumulate conveyance rows keyed by vertex pair.
            // Mesh-side resolution (vertex pair → flat slot(s)) happens after
            // triangulation is known, so we just collect rows here.
            if (conveyOut) {
                for (const QString &raw : body) {
                    ConveyanceRow row;
                    const QString e = parseConveyanceLine(raw, row);
                    if (!e.isEmpty())
                        return e;
                    if (row.va >= 0)
                        conveyOut->append(row);
                }
            }
            break;
        case SectionKind::InitHgl:
            // CELL HGL — CELL is a zero-based cell index or `*`. The upper
            // bound is checked when the rows are applied to the loaded cells.
            if (hglOut) {
                for (const QString &raw : body) {
                    const QStringList tok = tokenize(raw);
                    if (tok.isEmpty())
                        continue;
                    InitHglRow row;
                    bool okc = tok[0] == QLatin1String("*");
                    if (!okc)
                        row.cell = tok[0].toInt(&okc);
                    bool okh = false;
                    if (tok.size() >= 2)
                        row.hgl = tok[1].toDouble(&okh);
                    if (!okc || (tok[0] != QLatin1String("*") && row.cell < 0) || !okh ||
                        !std::isfinite(row.hgl))
                        return QStringLiteral("[2D_INITIAL_HGL] needs CELL HGL with a "
                                              "zero-based cell index or * and a finite "
                                              "elevation (got: %1)")
                            .arg(raw.trimmed());
                    hglOut->append(row);
                }
            }
            break;
        case SectionKind::Body:
            return parseSection(currentSection, body, out);
        default:
            break;
        }
        return {};
    };

    const char *p = bytes.constData();
    const char *const end = p + bytes.size();
    const char *const fileStart = p;
    ByteRow row;
    while (p < end) {
        const char *const ls = p;
        const void *nl = std::memchr(p, '\n', size_t(end - p));
        const char *const le = nl ? static_cast<const char *>(nl) : end;
        p = nl ? le + 1 : end;

        unsigned char high = 0;
        for (const char *c = ls; c < le; ++c)
            high |= uchar(*c);

        if (high >= 0x80) {
            // Non-ASCII line: the QString rules, unchanged.
            const QString raw = decodeLine(ls, le, ls == fileStart);
            const QString trimmed = raw.trimmed();
            noteHeaderComment(trimmed, *headers);
            if (!err.isEmpty())
                continue; // past an error only the header scan continues
            if (trimmed.startsWith(QChar('[')) && trimmed.endsWith(QChar(']'))) {
                err = flush();
                body.clear();
                currentSection = trimmed;
                kind = sectionKind(currentSection);
                sawVertices |= kind == SectionKind::Vertices;
                sawTriangles |= kind == SectionKind::Triangles || kind == SectionKind::Quads;
                continue;
            }
            const auto rawTrimmed = [&trimmed] { return trimmed; };
            switch (kind) {
            case SectionKind::Vertices:
                err = parseVertexRow(StringTokens{tokenize(raw)}, rawTrimmed, out);
                break;
            case SectionKind::Triangles:
            case SectionKind::Quads:
                err = parseCellRow(StringTokens{tokenize(raw)}, kind == SectionKind::Quads,
                                   rawTrimmed, out);
                break;
            case SectionKind::MeshFile:
            case SectionKind::BC:
            case SectionKind::Conveyance:
            case SectionKind::InitHgl:
            case SectionKind::Body:
                body.append(raw);
                break;
            default:
                break;
            }
            continue;
        }

        // ASCII line.
        const char *ts = ls;
        const char *te = le;
        while (ts < te && isAsciiSpace(*ts))
            ++ts;
        while (te > ts && isAsciiSpace(te[-1]))
            --te;
        if (te - ts >= 2 && ts[0] == ';' && ts[1] == ';')
            noteHeaderComment(QString::fromLatin1(ts, te - ts), *headers);
        if (!err.isEmpty())
            continue;
        if (ts < te && *ts == '[' && te[-1] == ']') {
            err = flush();
            body.clear();
            currentSection = QString::fromLatin1(ts, te - ts);
            kind = sectionKind(currentSection);
            sawVertices |= kind == SectionKind::Vertices;
            sawTriangles |= kind == SectionKind::Triangles || kind == SectionKind::Quads;
            // Size the array for this section's rows. Grow at least geometrically: QList::reserve
            // allocates exactly what it is asked for and copies every element, so exact reserves
            // at many repeated headers would be quadratic.
            const auto reserveFor = [&](auto &list) {
                const qsizetype need = list.size() + countRowsUntilHeader(p, end);
                if (need > list.capacity())
                    list.reserve(std::max(need, 2 * list.capacity()));
            };
            if (kind == SectionKind::Vertices)
                reserveFor(out.vertices);
            else if (kind == SectionKind::Triangles || kind == SectionKind::Quads)
                reserveFor(out.triangles);
            continue;
        }
        const auto rawTrimmed = [ts, te] { return QString::fromLatin1(ts, te - ts); };
        switch (kind) {
        case SectionKind::Vertices:
            tokenizeBytes(ls, le, row);
            err = parseVertexRow(ByteTokens{row}, rawTrimmed, out);
            break;
        case SectionKind::Triangles:
        case SectionKind::Quads:
            tokenizeBytes(ls, le, row);
            err = parseCellRow(ByteTokens{row}, kind == SectionKind::Quads, rawTrimmed, out);
            break;
        case SectionKind::MeshFile:
        case SectionKind::BC:
        case SectionKind::Conveyance:
        case SectionKind::InitHgl:
        case SectionKind::Body:
            body.append(QString::fromLatin1(ls, le - ls));
            break;
        default:
            break;
        }
    }
    if (err.isEmpty())
        err = flush();
    return err;
}

/**
 * @brief Overlay deferred boundary assignments on a pre-sized stride-four edge vector.
 * @param[in] mesh Borrowed cell records, unchanged; cell count/slot arithmetic must fit int.
 * @param[in] rows Borrowed deferred records, unchanged; out-of-range cell/local edge (including
 * triangle padding) skipped, later valid rows replace earlier complete MeshEdgeBC values.
 * @param[in,out] edgeBCs Borrowed writable storage of at least edgeSlotCount(cellCount) entries,
 * unchecked; caller normally initializes Wall/defaults first. Assignment replaces conveyance
 * too, so endpoint conveyance overlays are applied afterward.
 * @note No geometry/index/reference/physical value validation, allocation of the main vector,
 * source retention, engine synchronization or file mutation; caller supplies exclusive
 * destination access.
 *
 * @details §V.VD.1 — resolve accumulated BC rows into the flat, stride-kEdgeStride edge vector
 * once the mesh is known. A row is dropped when its cell is out of range or its EDGE exceeds the
 * cell's edge count (2 for a triangle, 3 for a quad); later rows overwrite earlier ones.
 */
void applyBCRows(const MeshResult &mesh, const BCRowList &rows, QVector<MeshEdgeBC> &edgeBCs)
{
    for (const auto &r : rows) {
        if (r.cell < 0 || r.cell >= mesh.triangles.size())
            continue;
        if (r.e < 0 || r.e >= mesh.triangles[r.cell].vertexCount())
            continue;
        edgeBCs[edgeSlot(r.cell, r.e)] = r.bc;
    }
}

/**
 * @brief Overlay endpoint-pair conveyance onto every matching real cell-local edge slot.
 * @param[in] mesh Borrowed cell identifiers, unchanged; stride-four arithmetic/counts must fit
 * int. Only identifier pairs are examined, no coordinate-array lookup or topology certification.
 * @param[in] rows Borrowed deferred factors, unchanged; endpoint order normalized, unmatched
 * pairs skip, later matching rows replace earlier conveyance values. Parsed rows normally
 * already satisfy [0,1].
 * @param[in,out] edgeBCs Borrowed destination; only matching slots within its current size are
 * written, other boundary fields/padding unchanged. Can be shorter than full slot count safely
 * in this helper.
 * @note Allocates a row-pair map and a vertex-count byte filter when rows nonempty (no per-edge
 * index), applies all shared/nonmanifold
 * matching slots for symmetry, no source mutation, numeric revalidation, engine/file effects or
 * retained references.
 *
 * @details Engine §11A — apply each `[2D_EDGE_CONVEYANCE]` row to every (cell, e) slot whose
 * endpoints match the row's vertex pair. Interior edges have two such slots (one per
 * neighbouring cell); both receive the same value, matching the engine's symmetry invariant.
 * Rows whose vertex pair doesn't match any edge are silently dropped (the writer canonicalises
 * on the lower vertex pair, but the engine accepts either order).
 */
/*! Stamp collected `[2D_INITIAL_HGL]` rows onto the loaded cells, in file
 *  order (last row wins; `*` assigns every cell). A row naming a cell the
 *  mesh does not have is skipped, as BC rows are. */
void applyInitHglRows(MeshResult &mesh, const InitHglRowList &rows)
{
    for (const InitHglRow &row : rows) {
        if (row.cell < 0) {
            for (MeshTriangle &t : mesh.triangles)
                t.initHgl = row.hgl;
        } else if (row.cell < mesh.triangles.size()) {
            mesh.triangles[row.cell].initHgl = row.hgl;
        }
    }
}

void applyConveyanceRows(const MeshResult &mesh, const ConveyanceRowList &rows,
                         QVector<MeshEdgeBC> &edgeBCs)
{
    if (rows.isEmpty())
        return;
    const int nslots = edgeBCs.size();
    // Final value per sorted vertex pair. Later rows replace earlier ones, which is exactly what
    // writing the rows one after another into the same slots produced. The rows are few (a
    // section of the file); the mesh edges are many, so look the edges up in the rows rather than
    // indexing every edge of the mesh (the former QHash of per-edge vectors cost ~0.4 s per
    // million cells whenever any conveyance row was present).
    const auto pairKey = [](int a, int b) {
        const int lo = std::min(a, b), hi = std::max(a, b);
        return (quint64(quint32(lo)) << 32) | quint32(hi);
    };
    QHash<quint64, double> convByPair;
    convByPair.reserve(rows.size());
    for (const auto &r : rows)
        convByPair.insert(pairKey(r.va, r.vb), r.conv);
    // Cheap pre-filter: an edge can only match when both endpoints occur in some row. Rows hold
    // nonnegative indices (parseConveyanceLine); the filter covers indices below the vertex count
    // and anything outside it goes straight to the hash, so its size stays bounded by the mesh.
    std::vector<char> inRow(size_t(mesh.vertices.size()), 0);
    for (const auto &r : rows) {
        if (r.va < mesh.vertices.size())
            inRow[size_t(r.va)] = 1;
        if (r.vb < mesh.vertices.size())
            inRow[size_t(r.vb)] = 1;
    }
    const auto mayMatch = [&inRow](int v) {
        return v >= 0 && (size_t(v) >= inRow.size() || inRow[size_t(v)]);
    };
    for (int t = 0; t < mesh.triangles.size(); ++t) {
        const auto &tri = mesh.triangles[t];
        const int ne = tri.vertexCount();
        for (int e = 0; e < ne; ++e) {
            int va = 0, vb = 0;
            edgeEndpoints(tri, e, va, vb);
            if (!mayMatch(va) || !mayMatch(vb))
                continue;
            const auto it = convByPair.constFind(pairKey(va, vb));
            if (it == convByPair.constEnd())
                continue; // no row for this edge
            const int flat = edgeSlot(t, e);
            if (flat >= 0 && flat < nslots)
                edgeBCs[flat].conveyance = it.value();
        }
    }
}

} // namespace

bool unitsHeaderIsSI(const QString &unitsHeader)
{
    // Keyword set copied from the engine's prescan2DUnitsHeader
    // (openswmm.engine/src/engine/2d/input/SectionHandlers2D.cpp). Keep the
    // two in step: a divergence silently changes whether the GUI thinks the
    // engine rescaled the mesh. Issue #155.
    static const QStringList kMetric = {
        QStringLiteral("SI (m)"), QStringLiteral("m"),     QStringLiteral("metre"),
        QStringLiteral("metres"), QStringLiteral("meter"), QStringLiteral("meters"),
    };
    const QString v = unitsHeader.trimmed();
    for (const QString &k : kMetric)
        if (v.compare(k, Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

InpMeshReadResult InpMeshReader::read(const QString &inpPath)
{
    InpMeshReadResult result;

    QByteArray inpBytes;
    if (!readFileBytes(inpPath, &inpBytes, &result.errorMsg))
        return result;

    // One pass on the .inp: capture any [2D_MESH_FILE] reference, parse
    // inline mesh sections opportunistically and collect the inline
    // `;; UNITS:` / `;; SOURCE_CRS:` headers. The engine prefers the external
    // file when both are present, so an external reference clears any inline
    // data we accumulated.
    MeshResult inlineMesh;
    bool sawInlineVerts = false;
    bool sawInlineTris = false;
    QString meshFileRef;
    BCRowList inlineBCs;          // §V.VD.1 — collected from any [2D_BOUNDARY_CONDITIONS]
    ConveyanceRowList inlineConv; // Engine §11A — [2D_EDGE_CONVEYANCE]
    HeaderComments inlineHeaders;
    InitHglRowList inlineHgl; // [2D_INITIAL_HGL]
    const QString inpErr =
        parseSectionsFromBytes(inpBytes, inlineMesh, sawInlineVerts, sawInlineTris, &meshFileRef,
                               &inlineBCs, &inlineConv, &inlineHeaders, &inlineHgl);
    inpBytes = QByteArray();

    // The inline headers apply even when the parse failed; the external-file
    // path may overwrite them when a .2dm is resolved further down. No
    // rejection here — the engine decides what to do with the value.
    if (!inlineHeaders.units.isEmpty())
        result.unitsHeader = inlineHeaders.units;
    if (!inlineHeaders.crs.isEmpty())
        result.sourceCrsTag = inlineHeaders.crs;

    if (!inpErr.isEmpty()) {
        result.errorMsg = inpErr;
        return result;
    }

    // Resolve [2D_MESH_FILE] if it was found. Relative paths are interpreted
    // against the .inp directory — same convention as the writer.
    if (!meshFileRef.isEmpty()) {
        QString absMeshPath = meshFileRef;
        QFileInfo fi(absMeshPath);
        if (fi.isRelative())
            absMeshPath = QFileInfo(inpPath).absoluteDir().absoluteFilePath(meshFileRef);

        QByteArray meshBytes;
        if (!readFileBytes(absMeshPath, &meshBytes, &result.errorMsg)) {
            // [2D_MESH_FILE] referenced but the file is missing — report it
            // so the user sees something is wrong rather than silently
            // dropping the mesh.
            result.errorMsg =
                QStringLiteral("[2D_MESH_FILE] points at %1 but the file could not be read: %2")
                    .arg(absMeshPath, result.errorMsg);
            return result;
        }

        MeshResult extMesh;
        bool sawExtVerts = false;
        bool sawExtTris = false;
        // §V.VD.1 — the external .2dm may carry its own [2D_BOUNDARY_CONDITIONS].
        // The .inp's BC rows still apply as overrides per the precedence
        // documented at the top of this file (external wins on geometry,
        // .inp wins on BC overrides since the user edits them there).
        BCRowList extBCs;
        ConveyanceRowList extConv; // Engine §11A — [2D_EDGE_CONVEYANCE]
        HeaderComments extHeaders;
        InitHglRowList extHgl;
        const QString extErr = parseSectionsFromBytes(meshBytes, extMesh, sawExtVerts, sawExtTris,
                                                      nullptr, &extBCs, &extConv, &extHeaders,
                                                      &extHgl);
        // Release the file bytes before the edge-BC array below is allocated.
        meshBytes = QByteArray();

        // External file's own ;; UNITS: / ;; SOURCE_CRS: headers take
        // precedence over the inline ones for the resolved mesh.
        if (!extHeaders.units.isEmpty())
            result.unitsHeader = extHeaders.units;
        if (!extHeaders.crs.isEmpty())
            result.sourceCrsTag = extHeaders.crs;

        if (!extErr.isEmpty()) {
            result.errorMsg = QStringLiteral("%1 (in %2)").arg(extErr, absMeshPath);
            return result;
        }

        if (sawExtVerts && sawExtTris && !extMesh.vertices.isEmpty() &&
            !extMesh.triangles.isEmpty()) {
            extMesh.ok = true;
            result.mesh = std::move(extMesh);
            result.sourcePath = absMeshPath;
            result.isExternal = true;
            result.hasMesh = true;

            // Infiltration fallback is value/container based: nonempty external
            // defaults/overrides win, and positive external INFIL_STEP wins.
            // Empty containers or a nonpositive step inherit the inline value,
            // even when an empty/zero-valued external section was present.
            if (result.mesh.infilDefaults.isEmpty())
                result.mesh.infilDefaults = inlineMesh.infilDefaults;
            if (result.mesh.infilOverrides.isEmpty())
                result.mesh.infilOverrides = inlineMesh.infilOverrides;
            if (!(result.mesh.infilOptions.infilStep > 0.0))
                result.mesh.infilOptions = inlineMesh.infilOptions;
            // Initial HGL: the external file's section replaces the inline one.
            applyInitHglRows(result.mesh, extHgl.isEmpty() ? inlineHgl : extHgl);

            // Resolve BCs: start Wall-defaults of size edgeSlotCount(n_cells)
            // (stride kEdgeStride; slot 3 of a triangle is padding), then
            // overlay external rows first, then .inp rows (so .inp overrides
            // on conflict).
            // NB: `slots` is a Qt keyword macro — use `nslots`.
            // resize() on the empty vector value-initialises every slot to
            // MeshEdgeBC{}, so no second fill pass is needed.
            const int nslots = edgeSlotCount(result.mesh.triangles.size());
            result.edgeBCs.resize(nslots);
            applyBCRows(result.mesh, extBCs, result.edgeBCs);
            applyBCRows(result.mesh, inlineBCs, result.edgeBCs);
            // Engine §11A — overlay external conveyance first, then .inp
            // (.inp wins on conflict; matches the BC precedence above).
            applyConveyanceRows(result.mesh, extConv, result.edgeBCs);
            applyConveyanceRows(result.mesh, inlineConv, result.edgeBCs);
        }
        return result;
    }

    if (sawInlineVerts && sawInlineTris && !inlineMesh.vertices.isEmpty() &&
        !inlineMesh.triangles.isEmpty()) {
        inlineMesh.ok = true;
        result.mesh = std::move(inlineMesh);
        result.sourcePath = inpPath;
        result.isExternal = false;
        result.hasMesh = true;
        applyInitHglRows(result.mesh, inlineHgl);

        const int nslots = edgeSlotCount(result.mesh.triangles.size());
        result.edgeBCs.resize(nslots); // value-initialised to MeshEdgeBC{}
        applyBCRows(result.mesh, inlineBCs, result.edgeBCs);
        applyConveyanceRows(result.mesh, inlineConv, result.edgeBCs);
    }

    // Legacy file note — pre-header files load fine (engine still applies
    // its FLOW_UNITS-based conversion), but surface the absence so the
    // user can regenerate to get a self-describing file.
    if (result.hasMesh && result.unitsHeader.isEmpty()) {
        result.warning =
            QStringLiteral("Mesh file %1 has no ';; UNITS:' header — engine will infer "
                           "units from SWMM FLOW_UNITS. Regenerate the mesh to embed the "
                           "unit descriptor.")
                .arg(result.sourcePath);
    }
    return result;
}

} // namespace mesh
