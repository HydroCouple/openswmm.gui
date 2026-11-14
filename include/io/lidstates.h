/**
 * @file      lidstates.h
 * @brief     Value snapshots of LID node elevations, heads, and moisture states.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMMVIS_LIDSTATES_H
#define OPENSWMMVIS_LIDSTATES_H

#include <QDateTime>
#include <QFile>
#include <QStringList>
#include <QVector>
#include <cmath>
#include <limits>

namespace openswmmvis::io
{

// Absolute elevations / heads in the run's project length unit. Moisture and
// porosity are volume fractions. NaN pressure/head means unavailable (classical).
struct LidCellState {
    int layer = 0;
    double bottom = 0, top = 0, porosity = 0;
    double moisture = std::numeric_limits<double>::quiet_NaN();
    double pressure = std::numeric_limits<double>::quiet_NaN();
    double head = std::numeric_limits<double>::quiet_NaN();
};
struct LidNodeState {
    QString name;
    bool richards = false;
    QVector<LidCellState> cells; // porous cells only, top to bottom
};
struct LidStateFrame {
    QDateTime time;
    bool si = false;
    QVector<LidNodeState> nodes;
    QString status;
};
struct LidSaturationBoundary {
    double elevation;
    bool waterTable; // unsaturated above, saturated below; false = perched base
};
/**
 * @brief Interpolate zero-pressure crossings between touching Richards porous cells.
 * @param[in] node Top-to-bottom cells with elevations and pressure in the project length unit.
 * @return Crossings in cell order; empty for classical nodes. Water-table crossings have negative pressure above and nonnegative below; the reverse marks a perched base.
 * @details Requires finite pressures with opposite saturation signs and adjacent faces within 1e-6 project length units. Elevation is interpolated between cell midpoints.
 */
inline QVector<LidSaturationBoundary> lidSaturationBoundaries(const LidNodeState &node)
{
    QVector<LidSaturationBoundary> out;
    if (!node.richards)
        return out;
    for (int i = 1; i < node.cells.size(); ++i) {
        const auto &upper = node.cells[i - 1];
        const auto &lower = node.cells[i];
        if (!std::isfinite(upper.pressure) || !std::isfinite(lower.pressure) ||
            (upper.pressure >= 0) == (lower.pressure >= 0) ||
            std::abs(upper.bottom - lower.top) > 1.e-6)
            continue;
        const double a = (upper.bottom + upper.top) * .5;
        const double b = (lower.bottom + lower.top) * .5;
        const double f = -upper.pressure / (lower.pressure - upper.pressure);
        out.push_back({a + f * (b - a), upper.pressure < 0});
    }
    return out;
}

/**
 * @brief Derive the companion state-file path without accessing the filesystem.
 * @param[in] outputPath Engine output path, including its extension.
 * @return outputPath with the literal suffix .lidstate appended.
 */
QString lidStatePath(const QString &outputPath);

// Fixed-width records, explicit byte order/version. The reader binary-searches
// timestamps and reads just the requested nodes, never the entire history.
class LidStateWriter
{
public:
    /**
     * @brief Truncate/create the companion and write its versioned geometry header.
     * @param[in] outputPath Associated engine output path; its companion is overwritten.
     * @param[in] geometry Node names/order, porous-cell geometry, and project unit flag to persist; dynamic state values are not written here.
     * @return False when geometry metadata exceeds 16 MiB or the header cannot be written/flushed; inspect errorString().
     * @details Use a fresh writer for a new history: opening does not reset the remembered last timestamp. The writer owns its companion file handle.
     */
    bool open(const QString &outputPath, const LidStateFrame &geometry);
    /**
     * @brief Append and flush one completed timestamp with moisture, pressure, and head values.
     * @param[in] frame Valid timestamp and nodes matching the header name/order and cell counts; values are stored as single-precision numbers.
     * @return True after writing, or for a duplicate of the most recent timestamp without writing; false for older/invalid time, mismatched layout, closed file, or IO failure.
     * @details Write failures set a diagnostic and close the companion. Structural rejection need not set errorString(). Geometry and value finiteness are not revalidated.
     */
    bool append(const LidStateFrame &frame);
    /**
     * @brief Bind the companion to the final engine output size and SHA-256 digest.
     * @return True after updating and flushing the binding header; false for a closed writer, digest failure, or write failure.
     * @details Call after the engine closes its output. The companion closes after the header-write attempt; a hashing failure leaves it open.
     */
    bool finish(); // call after the engine closes its .out
    /**
     * @brief Read the latest stored writer diagnostic.
     * @return Diagnostic text, which is not necessarily set for structural append rejection.
     */
    QString errorString() const { return m_error; }

private:
    QFile m_file;
    QString m_output, m_error;
    QVector<LidNodeState> m_nodes;
    qint64 m_lastTime = std::numeric_limits<qint64>::min();
};

class LidStateReader
{
public:
    /**
     * @brief Read the last completed frame at or before a cursor, for selected node names.
     * @param[in] outputPath Associated engine output path; reads its .lidstate companion.
     * @param[in] live Permit an unfinalized companion and ignore an incomplete trailing frame while the engine is writing.
     * @param[in] cursor Valid time used for binary search; no interpolation is performed.
     * @param[in] nodeNames Exact names to include, in stored node order; an empty list selects no nodes.
     * @return Frame with UTC timestamp and project unit flag, or an empty-node frame with status explaining invalid input, incompatible geometry, missing history, output-binding mismatch, or IO failure.
     * @details Finalized files are checked against output size and SHA-256. Verification is cached by output path, size, modification time, and recorded digest; instances require externally serialized access.
     */
    LidStateFrame frameAt(const QString &outputPath, bool live, const QDateTime &cursor,
                          const QStringList &nodeNames);

private:
    QString m_verifiedPath;
    qint64 m_verifiedSize = -1, m_verifiedMtime = -1;
    QByteArray m_verifiedDigest;
};
} // namespace openswmmvis::io
#endif
