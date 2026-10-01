#include "io/mesh2dvariableexport.h"
#include "layers/swmm2dresultslayer.h"

#include <QDir>
#include <QDateTime>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringList>
#include <cmath>
#include <exception>
#include <filesystem>
#include <limits>

namespace openswmmvis::io {
namespace {
QString csvString(QString value)
{
    value.replace('"', QStringLiteral("\"\""));
    return '"' + value + '"';
}

// The equivalent checks used by the project/run manifests are private or
// linked to the runner. Keep this widget-free export's filesystem check local.
bool aliases(const QString& source, const QString& destination)
{
    if (source.isEmpty()) return false;
    const auto native = [](const QString& path) {
#ifdef Q_OS_WIN
        return std::filesystem::path(path.toStdWString());
#else
        return std::filesystem::path(path.toUtf8().constData());
#endif
    };
    std::error_code ec;
    if (std::filesystem::equivalent(native(source), native(destination), ec)) return true;
    const auto a = std::filesystem::weakly_canonical(native(source), ec);
    if (!ec) {
        const auto b = std::filesystem::weakly_canonical(native(destination), ec);
        if (!ec && a == b) return true;
    }
    const QString aPath = QDir::cleanPath(QFileInfo(source).absoluteFilePath());
    const QString bPath = QDir::cleanPath(QFileInfo(destination).absoluteFilePath());
#ifdef Q_OS_WIN
    return aPath.compare(bPath, Qt::CaseInsensitive) == 0;
#else
    return aPath == bPath;
#endif
}
}

bool exportMesh2DVariableCsv(IMesh2DSource& source, const QString& key, int frame,
                             const QString& path, QString* error)
{
    if (error) error->clear();
    const auto fail = [error](const QString& message) {
        if (error) *error = message;
        return false;
    };
    try {
        if (path.trimmed().isEmpty()) return fail(QStringLiteral("Choose a CSV destination."));
        const QString sourcePath = source.sourcePath();
        if (aliases(sourcePath, path)) return fail(QStringLiteral("The CSV destination would overwrite the result source."));
        Mesh2DResultVariable variable;
        int matches = 0;
        for (const auto& candidate : source.faceVariables())
            if (!key.isEmpty() && candidate.key() == key) { variable = candidate; ++matches; }
        if (matches != 1) return fail(QStringLiteral("The selected result identity is missing or ambiguous."));
        if (!variable.unitsKnown || variable.units.trimmed().isEmpty())
            return fail(QStringLiteral("This result has unresolved units. Raw inspection is available, but scientific CSV export requires declared units."));
        using V = Mesh2DResultVariable;
        using S = Mesh2DValueStatus;
        const bool independent = variable.temporal == V::Temporal::Static || variable.temporal == V::Temporal::Envelope;
        if (!independent && (frame < 0 || frame >= source.timeCount() || frame >= variable.frameCount))
            return fail(QStringLiteral("The selected report frame is unavailable."));
        const int cells = source.triangleCount();
        const int generation = source.historyGeneration();
        const QDateTime time = independent ? QDateTime() : source.simTimeAt(frame);
        std::vector<float> values;
        std::vector<S> status;
        if (!source.readFaceVariableAt(variable, frame, values, status))
            return fail(QStringLiteral("Could not read the selected result frame."));
        if (cells <= 0 || values.size() != std::size_t(cells) || status.size() != std::size_t(cells))
            return fail(QStringLiteral("Result values and validity do not match the source cell count."));
        for (std::size_t cell = 0; cell < values.size(); ++cell) {
            if (status[cell] != S::Valid && status[cell] != S::Missing
                && status[cell] != S::Waterless && status[cell] != S::NotApplicable)
                return fail(QStringLiteral("The source returned an unsupported validity state."));
            if (status[cell] != S::Missing && !std::isfinite(values[cell]))
                return fail(QStringLiteral("The source marked a nonfinite value as usable."));
        }
        const auto unchanged = [&] {
            return source.sourcePath() == sourcePath && source.triangleCount() == cells
                && source.historyGeneration() == generation
                && (independent || (frame < source.timeCount() && source.simTimeAt(frame) == time));
        };
        if (!unchanged()) return fail(QStringLiteral("The result source changed while the frame was being read."));
        if (aliases(sourcePath, path)) return fail(QStringLiteral("The CSV destination would overwrite the result source."));

        QSaveFile output(path);
        output.setDirectWriteFallback(false);
        if (!output.open(QIODevice::WriteOnly)) return fail(output.errorString());
        const auto write = [&](const QByteArray& bytes) { return output.write(bytes) == bytes.size(); };
        if (!write("source,key,dataset,species,domain,zone,layer_index_0based,temporal,requested_frame,frame,time,cell_index_0based,value,status,units\n"))
            return fail(output.errorString());
        const QString domain = variable.domain == V::Domain::Groundwater ? "groundwater" : "surface";
        const QString zone = variable.zone == V::Zone::Saturated ? "saturated"
                           : variable.zone == V::Zone::Unsaturated ? "unsaturated"
                           : variable.zone == V::Zone::Sigma ? "sigma" : "none";
        const QString temporal = variable.temporal == V::Temporal::Held ? "held"
                               : variable.temporal == V::Temporal::Static ? "static"
                               : variable.temporal == V::Temporal::Envelope ? "envelope" : "reported";
        const QString prefix = QStringList{
            csvString(sourcePath), csvString(key), csvString(variable.dataset), csvString(variable.species),
            domain, zone, variable.layer >= 0 ? QString::number(variable.layer) : QString(),
            temporal, QString::number(frame), independent ? QString() : QString::number(frame),
            time.isValid() ? csvString(time.toString(Qt::ISODateWithMs)) : QString()
        }.join(',') + ',';
        for (int cell = 0; cell < cells; ++cell) {
            const S state = status[std::size_t(cell)];
            const QString label = state == S::Missing ? "missing" : state == S::Waterless ? "waterless"
                                : state == S::NotApplicable ? "not_applicable" : "valid";
            const QString number = state == S::Missing ? QString()
                : QString::number(values[std::size_t(cell)], 'g', std::numeric_limits<float>::max_digits10);
            const QByteArray row = (prefix + QString::number(cell) + ',' + number + ','
                                    + label + ',' + csvString(variable.units) + '\n').toUtf8();
            if (!write(row)) return fail(output.errorString());
        }
        if (!unchanged() || aliases(sourcePath, path))
            return fail(QStringLiteral("The result source or destination changed before CSV publication."));
        if (!output.commit()) return fail(output.errorString());
        return true;
    } catch (const std::exception& exception) {
        return fail(QStringLiteral("CSV export failed: %1").arg(QString::fromUtf8(exception.what())));
    } catch (...) {
        return fail(QStringLiteral("CSV export failed with an unexpected exception."));
    }
}
} // namespace openswmmvis::io
