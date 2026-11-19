// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      modelsnapshot.cpp
 * @brief     Independent model snapshots for extension edit scopes and Check Model.
 * @details   Moved unchanged from extensionmodelaccess.cpp (review G-09); only the snapshot
 *            folder name and the unsaved-model key became parameters.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "project/modelsnapshot.h"

#include "layers/swmmmodellayer.h"
#include "project/projectsaveoutputs.h"
#include "swmmvisprojectwindow.h"

#include <openswmm/engine/openswmm_model.h>

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>
#include <QUuid>

namespace openswmmvis::project
{

QString writeModelSnapshot(SWMMVisProjectWindow *window, const QString &purpose,
                           const QString &unsavedKey, QString *error)
{
    if (!window || window->isClosing() || !window->modelLayer() ||
        !window->modelLayer()->engine()) {
        if (error)
            *error = QStringLiteral("Open a model before taking a snapshot.");
        return {};
    }
    const QDir base(
        window->isUntitled() || window->modelFilePath().isEmpty()
            ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                  .filePath("unsaved/" + unsavedKey)
            : QFileInfo(window->modelFilePath()).absolutePath());
    const auto directory =
        QStringLiteral(".swmmvis/%1/%2").arg(purpose, QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!base.mkpath(directory)) {
        if (error)
            *error = QStringLiteral("Cannot create the project snapshot directory.");
        return {};
    }
    const auto path = base.filePath(directory + "/model.inp");
    ProjectSaveOutputs outputs;
    outputs.protect(window->modelFilePath(), -1);
    struct SnapshotMapper {
        ProjectSaveOutputs *outputs;
        QString root;
        QByteArray storage;
        QString error;
    } mapper{&outputs, QFileInfo(path).absolutePath() + '/', {}, {}};
    const auto mapOutput = [](void *context, const char *finalPath, int kind) -> const char * {
        auto &mapped = *static_cast<SnapshotMapper *>(context);
        const auto destination = QFileInfo(QString::fromUtf8(finalPath)).absoluteFilePath();
        if (kind < ProjectSaveOutputs::Model || kind > ProjectSaveOutputs::Component ||
            !destination.startsWith(mapped.root)) {
            mapped.error = QStringLiteral("Snapshot output would escape its recovery directory: %1")
                               .arg(destination);
            return nullptr;
        }
        mapped.storage = mapped.outputs
                             ->stage(destination, static_cast<ProjectSaveOutputs::Role>(kind),
                                     kind == ProjectSaveOutputs::Model)
                             .toUtf8();
        return mapped.storage.isEmpty() ? nullptr : mapped.storage.constData();
    };
    auto engine = window->modelLayer()->engine();
    if (!window->modelLayer()->flushDataObjectRegistries(error))
        return {};
    // Preserve the source mesh reference while requesting a self-contained inline snapshot.
    QByteArray meshReference(32768, '\0');
    const bool haveMeshReference = swmm_options_get_ext(engine, "MESH_FILE", meshReference.data(),
                                                        meshReference.size()) == SWMM_OK;
    if (haveMeshReference && int(qstrlen(meshReference.constData())) == meshReference.size() - 1) {
        if (error)
            *error = "The engine mesh reference is too long to snapshot safely.";
        return {};
    }
    if (haveMeshReference)
        meshReference.resize(int(qstrlen(meshReference.constData())));
    if (haveMeshReference && swmm_options_set_ext(engine, "MESH_FILE", "") != SWMM_OK) {
        if (error)
            *error = "The engine refused inline mesh serialization for a snapshot.";
        return {};
    }
    const int warningsBefore = swmm_get_warning_count(engine);
    const int result =
        swmm_model_write_staged(engine, path.toUtf8().constData(), mapOutput, &mapper);
    const int restored = haveMeshReference
                             ? swmm_options_set_ext(engine, "MESH_FILE", meshReference.constData())
                             : SWMM_OK;
    QStringList diagnostics;
    bool losesData = false;
    for (int i = warningsBefore; i < swmm_get_warning_count(engine); ++i) {
        const auto warning = QString::fromUtf8(swmm_get_warning_at(engine, i));
        diagnostics.append(warning);
        losesData = losesData || warning.contains("lost from this save");
    }
    if (result != SWMM_OK || restored != SWMM_OK || losesData || !outputs.publish()) {
        if (error)
            *error = QStringLiteral(
                         "Could not write the model snapshot (engine %1, mesh restore %2): %3 %4")
                         .arg(result)
                         .arg(restored)
                         .arg(mapper.error, outputs.error()) +
                     " " + diagnostics.join("; ");
        return {};
    }
    return path;
}

} // namespace openswmmvis::project
