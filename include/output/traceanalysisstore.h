// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "output/tracedata.h"
#include <atomic>
namespace openswmmvis::trace
{
class AnalysisStore
{
  public:
    // Every call owns/closes its dataset on the calling thread. No GDAL handle
    // is transferred to a worker or retained by a map layer.
    static bool prepare(const QString &path, const Dataset &, QString *error,
                        std::atomic_bool *cancel = nullptr);
    static bool save(const QString &path, const Result &, QString *error,
                     std::atomic_bool *cancel = nullptr);
    static std::shared_ptr<Dataset> readDataset(const QString &path, QString *error);
    static QVector<QJsonObject> analyses(const QString &path, QString *error);
    static std::shared_ptr<Result> read(const QString &path, const QString &id, QString *error);
    static bool saveStyle(const QString &path, const QString &id, const QJsonObject &,
                          QString *error);
};
} // namespace openswmmvis::trace
