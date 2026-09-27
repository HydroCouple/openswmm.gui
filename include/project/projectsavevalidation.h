// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "projectsaveoutputs.h"
#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_2d.h>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

// A read-only parse of the prepared built-in save. This checks reference and
// object-count coherence; it deliberately does not initialize a simulation or
// require an unfinished authoring model to be ready to run.
class ProjectSaveValidation
{
public:
    static bool validate(const ProjectSaveOutputs &outputs, SWMM_Engine source,
                         QString *error, QStringList *warnings,
                         int expectedVertices = -1, int expectedCells = -1,
                         int expectedQuads = -1) {
        if (error) error->clear();
        const auto prepared = outputs.preparedOutputs();
        QString model, stage;
        for (const auto &entry : prepared) {
            if (entry.role == ProjectSaveOutputs::Model) {
                model = entry.finalPath; stage = entry.stagedPath;
            }
        }
        const auto fail = [&](const QString &why) {
            if (error) *error = why;
            return false;
        };
        if (model.isEmpty()) return fail(QStringLiteral("No prepared model to validate."));
        for (const auto &entry : prepared) {
            if (entry.role != ProjectSaveOutputs::Settings) continue;
            QFile settings(entry.stagedPath);
            if (!settings.open(QIODevice::ReadOnly))
                return fail(QStringLiteral("Cannot read prepared project settings %1: %2")
                            .arg(entry.finalPath, settings.errorString()));
            QJsonParseError parseError;
            const auto doc = QJsonDocument::fromJson(settings.readAll(), &parseError);
            const auto root = doc.object();
            const auto sessions = root.value(QStringLiteral("sessions")).toArray();
            if (settings.error() != QFileDevice::NoError || parseError.error != QJsonParseError::NoError
                || !doc.isObject() || root.value(QStringLiteral("schemaVersion")).toInt() < 1
                || sessions.isEmpty() || !sessions.first().isObject()
                || !sessions.first().toObject().value(QStringLiteral("inpPath")).isString())
                return fail(QStringLiteral("Invalid prepared project settings %1: %2")
                            .arg(entry.finalPath, parseError.error == QJsonParseError::NoError
                                 ? QStringLiteral("missing project schema or session") : parseError.errorString()));
            const QString reference = sessions.first().toObject().value(QStringLiteral("inpPath")).toString();
            const QString settingsModel = QFileInfo(reference).isAbsolute() ? reference
                : QFileInfo(entry.finalPath).absoluteDir().absoluteFilePath(reference);
            if (reference.isEmpty() || identity(settingsModel) != identity(model))
                return fail(QStringLiteral("Prepared project settings %1 reference a different model: %2")
                            .arg(entry.finalPath, reference));
        }
        QFile input(stage);
        if (!input.open(QIODevice::ReadOnly))
            return fail(QStringLiteral("Cannot read prepared model %1: %2").arg(model, input.errorString()));
        QTemporaryFile validation(QFileInfo(model).absoluteDir().filePath(QStringLiteral(".openswmm-validate-XXXXXX")));
        if (!validation.open())
            return fail(QStringLiteral("Cannot prepare validation for %1: %2").arg(model, validation.errorString()));

        const auto normalize = [&](QString text) {
            text.replace(validation.fileName(), model);
            const QDir modelDirectory = QFileInfo(model).absoluteDir();
            for (const auto &entry : prepared) {
                // Component reads preserve '..' segments when anchoring our
                // relative stage token; normalize that spelling as well.
                text.replace(modelDirectory.filePath(modelDirectory.relativeFilePath(entry.stagedPath)), entry.finalPath);
                text.replace(entry.stagedPath, entry.finalPath);
            }
            return text;
        };
        const auto warn = [&](const QString &message) {
            const QString normalized = normalize(message);
            if (warnings && !warnings->contains(normalized)) warnings->append(normalized);
        };
        QSet<QString> componentSections;
        QString resourceError;
        const auto resource = [&](const QString &token, ProjectSaveOutputs::Role role) -> QString {
            const QString final = QDir::cleanPath(QFileInfo(token).isAbsolute()
                ? token : QFileInfo(model).absoluteDir().absoluteFilePath(token));
            QString physical = final;
            for (const auto &entry : prepared) {
                if (identity(entry.finalPath) != identity(final)) continue;
                if (entry.role != role) {
                    resourceError = QStringLiteral("Reference %1 conflicts with a different prepared output.").arg(final);
                    return {};
                }
                physical = entry.stagedPath;
                break;
            }
            QFile file(physical);
            if (!QFileInfo(physical).isFile() || !file.open(QIODevice::ReadOnly) || file.size() == 0) {
                resourceError = QStringLiteral("Cannot validate %1: referenced %2 is missing, unreadable or empty.")
                    .arg(model, final);
                return {};
            }
            if (role == ProjectSaveOutputs::Component) {
                // The current built-in components read only this config. Its
                // section names identify config diagnostics emitted by their
                // apply hooks, including malformed rows under lenient open.
                while (!file.atEnd()) {
                    const auto match = header().match(QString::fromUtf8(file.readLine()).trimmed());
                    if (match.hasMatch()) componentSections.insert(match.captured(1).trimmed().toUpper());
                }
                if (file.error() != QFileDevice::NoError) {
                    resourceError = QStringLiteral("Cannot finish reading component configuration %1.").arg(final);
                    return {};
                }
            }
            return physical;
        };

        QString section;
        bool hasSection = false, skippedPlugins = false, externalMeshDeclared = false;
        const QRegularExpression config(QStringLiteral("(?:^|\\s)config=(\"[^\"]*\"|[^\\s;]+)"),
                                        QRegularExpression::CaseInsensitiveOption);
        while (!input.atEnd()) {
            QByteArray line = input.readLine();
            QString text = QString::fromUtf8(line);
            const auto match = header().match(text.trimmed());
            if (match.hasMatch()) {
                section = match.captured(1).trimmed().toUpper();
                hasSection = true;
            }
            // open() itself loads and initializes external plugins. Their
            // declarations remain untouched in the actual publishable stage.
            if (section == QStringLiteral("PLUGINS")) {
                skippedPlugins = true;
                continue;
            }
            const QString data = withoutComment(text).trimmed();
            if (!match.hasMatch() && !data.isEmpty()) {
                if (section == QStringLiteral("2D_MESH_FILE")) {
                    const QRegularExpression fileRow(QStringLiteral("^FILE\\s+(.+)$"), QRegularExpression::CaseInsensitiveOption);
                    const auto ref = fileRow.match(data);
                    if (ref.hasMatch()) {
                        externalMeshDeclared = true;
                        const QString mapped = resource(unquote(ref.captured(1)), ProjectSaveOutputs::Mesh);
                        if (mapped.isEmpty()) return fail(resourceError);
                        line = QStringLiteral("FILE \"%1\"\n").arg(mapped).toUtf8();
                    }
                } else if (section == QStringLiteral("PROCESS_COMPONENTS")) {
                    const auto ref = config.match(withoutComment(text));
                    if (ref.hasMatch()) {
                        const QString token = unquote(ref.captured(1));
                        // The current engine tokenizes key="value" arguments
                        // at whitespace even inside those quotes. Do not hide
                        // a final reference that would fail on ordinary Open.
                        if (token.contains(QRegularExpression(QStringLiteral("\\s"))))
                            return fail(QStringLiteral("Component configuration reference cannot be reopened by the current engine because it contains whitespace: %1").arg(token));
                        const QString mapped = resource(token, ProjectSaveOutputs::Component);
                        if (mapped.isEmpty()) return fail(resourceError);
                        // A relative stage token avoids introducing the same
                        // limitation solely because the project folder itself
                        // contains spaces; the input remains its true sibling.
                        const QString relative = QFileInfo(model).absoluteDir().relativeFilePath(mapped);
                        if (relative.contains(QRegularExpression(QStringLiteral("\\s"))))
                            return fail(QStringLiteral("Cannot safely map component configuration %1 for validation.").arg(token));
                        text.replace(ref.capturedStart(1), ref.capturedLength(1), QStringLiteral("\"%1\"").arg(relative));
                        line = text.toUtf8();
                    }
                }
            }
            if (validation.write(line) != line.size())
                return fail(QStringLiteral("Cannot write validation input for %1: %2").arg(model, validation.errorString()));
        }
        if (input.error() != QFileDevice::NoError || !hasSection || !validation.flush())
            return fail(QStringLiteral("Cannot finish reading the prepared model %1.").arg(model));
        validation.close();
        if (skippedPlugins)
            warn(QStringLiteral("Save validation checks built-in model data only; external plugins are preserved but are not loaded or validated."));

        struct EngineDeleter {
            void operator()(void *engine) const {
                swmm_engine_close(engine);
                swmm_engine_destroy(engine);
            }
        };
        std::unique_ptr<void, EngineDeleter> engine(swmm_engine_create());
        if (!engine) return fail(QStringLiteral("Cannot create an engine to validate the prepared model."));
        swmm_engine_set_lenient_open(engine.get(), 1);
        const int result = swmm_engine_open(engine.get(), validation.fileName().toUtf8().constData(), nullptr, nullptr, nullptr);
        if (result != 0)
            return fail(QStringLiteral("Prepared model %1 could not be parsed: %2")
                        .arg(model, normalize(QString::fromUtf8(swmm_get_last_error_msg(engine.get())))));
        for (int i = 0; i < swmm_get_error_count(engine.get()); ++i) {
            const QString diagnostic = normalize(QString::fromUtf8(swmm_get_error_at(engine.get(), i)));
            bool resourceFailure = diagnostic.startsWith(QStringLiteral("2D_MESH_FILE:"))
                || diagnostic.startsWith(QStringLiteral("Component config"), Qt::CaseInsensitive)
                || diagnostic.startsWith(QStringLiteral("Process component config"), Qt::CaseInsensitive)
                || diagnostic.startsWith(QStringLiteral("Reactions config"), Qt::CaseInsensitive)
                || diagnostic.startsWith(QStringLiteral("[PROCESS_COMPONENTS]"))
                || diagnostic.startsWith(QStringLiteral("[INITIAL_QUALITY] FILE"));
            for (const auto &tag : componentSections)
                resourceFailure |= diagnostic.contains(QStringLiteral("[%1]").arg(tag));
            // Engine error codes for rainfall and external time-series reads.
            static const QRegularExpression externalError(QStringLiteral("\\bERROR\\s+(317|318|319|361|363)\\b"));
            resourceFailure |= externalError.match(diagnostic).hasMatch();
            if (resourceFailure)
                return fail(QStringLiteral("Prepared model resource validation failed: %1").arg(diagnostic));
            // A draft can retain unresolved authoring references. Reparse
            // errors are surfaced, not treated as proof of simulation readiness.
            warn(QStringLiteral("Saved draft validation: %1").arg(diagnostic));
        }
        const auto compareCount = [&](const QString &what, int expected, int actual) {
            if (expected < 0 || expected == actual) return true;
            return fail(QStringLiteral("Prepared model %1 changed the number of %2 (expected %3, read %4).")
                        .arg(model, what).arg(expected).arg(actual));
        };
        if (source && (!compareCount(QStringLiteral("nodes"), swmm_node_count(source), swmm_node_count(engine.get()))
                    || !compareCount(QStringLiteral("links"), swmm_link_count(source), swmm_link_count(engine.get()))
                    || !compareCount(QStringLiteral("subcatchments"), swmm_subcatch_count(source), swmm_subcatch_count(engine.get()))))
            return false;
        using Count = int (*)(SWMM_Engine, int *);
        const auto meshCount = [](SWMM_Engine handle, Count get, int &count) {
            const int result = get(handle, &count);
            // These three APIs use CHECK_2D_MESH: with a valid handle and
            // output pointer, BADPARAM means n_vertices() <= 0. Represent
            // that absent mesh as zero; positive expected counts still fail.
            if (result == SWMM_ERR_BADPARAM) { count = 0; return true; }
            return result == SWMM_OK;
        };
        const auto compareMesh = [&](const QString &what, int expected, Count get) {
            int actual = -1;
            if (!meshCount(engine.get(), get, actual))
                return fail(QStringLiteral("Cannot inspect prepared model %1.").arg(what));
            if (externalMeshDeclared && get == swmm_2d_vertex_count && actual == 0)
                return fail(QStringLiteral("Prepared model %1 references an external mesh with no readable mesh vertices.").arg(model));
            if (expected < 0 && source && !meshCount(source, get, expected))
                return fail(QStringLiteral("Cannot inspect source model %1.").arg(what));
            return compareCount(what, expected, actual);
        };
        if (!compareMesh(QStringLiteral("vertices"), expectedVertices, swmm_2d_vertex_count)
            || !compareMesh(QStringLiteral("cells"), expectedCells, swmm_2d_cell_count)
            || !compareMesh(QStringLiteral("quadrilateral cells"), expectedQuads, swmm_2d_quad_count)) return false;
        return true;
    }
private:
    static const QRegularExpression &header() {
        static const QRegularExpression pattern(QStringLiteral("^\\[([^]\\r\\n]+)\\]\\s*(?:;.*)?$"));
        return pattern;
    }
    static QString withoutComment(const QString &line) {
        bool quoted = false;
        for (qsizetype i = 0; i < line.size(); ++i) {
            if (line[i] == QLatin1Char('"')) quoted = !quoted;
            else if (!quoted && line[i] == QLatin1Char(';')) return line.left(i);
        }
        return line;
    }
    static QString unquote(QString text) {
        text = text.trimmed();
        if (text.startsWith(QLatin1Char('"')) && text.endsWith(QLatin1Char('"'))) return text.mid(1, text.size() - 2);
        return text;
    }
    static QString identity(const QString &path) {
        const QString absolute = QFileInfo(path).absoluteFilePath();
        std::error_code ec;
#ifdef Q_OS_WIN
        const auto canonical = std::filesystem::weakly_canonical(std::filesystem::path(absolute.toStdWString()), ec);
        return ec ? QDir::cleanPath(absolute) : QString::fromStdWString(canonical.wstring());
#else
        const auto canonical = std::filesystem::weakly_canonical(std::filesystem::path(absolute.toUtf8().constData()), ec);
        return ec ? QDir::cleanPath(absolute) : QString::fromUtf8(canonical.string().c_str());
#endif
    }
};
