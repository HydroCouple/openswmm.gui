#ifndef OPENSWMMVIS_LIDSTATES_H
#define OPENSWMMVIS_LIDSTATES_H

#include <QDateTime>
#include <QFile>
#include <QStringList>
#include <QVector>
#include <cmath>
#include <limits>

namespace openswmmvis::io {

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
inline QVector<LidSaturationBoundary> lidSaturationBoundaries(const LidNodeState &node)
{
    QVector<LidSaturationBoundary> out;
    if (!node.richards) return out;
    for (int i = 1; i < node.cells.size(); ++i) {
        const auto &upper = node.cells[i - 1]; const auto &lower = node.cells[i];
        if (!std::isfinite(upper.pressure) || !std::isfinite(lower.pressure)
            || (upper.pressure >= 0) == (lower.pressure >= 0)
            || std::abs(upper.bottom - lower.top) > 1.e-6) continue;
        const double a = (upper.bottom + upper.top) * .5;
        const double b = (lower.bottom + lower.top) * .5;
        const double f = -upper.pressure / (lower.pressure - upper.pressure);
        out.push_back({a + f * (b - a), upper.pressure < 0});
    }
    return out;
}

QString lidStatePath(const QString &outputPath);

// Fixed-width records, explicit byte order/version. The reader binary-searches
// timestamps and reads just the requested nodes, never the entire history.
class LidStateWriter {
public:
    bool open(const QString &outputPath, const LidStateFrame &geometry);
    bool append(const LidStateFrame &frame);
    bool finish(); // call after the engine closes its .out
    QString errorString() const { return m_error; }
private:
    QFile m_file;
    QString m_output, m_error;
    QVector<LidNodeState> m_nodes;
    qint64 m_lastTime = std::numeric_limits<qint64>::min();
};

class LidStateReader {
public:
    LidStateFrame frameAt(const QString &outputPath, bool live, const QDateTime &cursor,
                          const QStringList &nodeNames);
private:
    QString m_verifiedPath;
    qint64 m_verifiedSize = -1, m_verifiedMtime = -1;
    QByteArray m_verifiedDigest;
};
} // namespace openswmmvis::io
#endif
