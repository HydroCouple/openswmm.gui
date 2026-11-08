/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "simulation/simulationrunner.h"
#include <QJsonObject>
#include <QProcess>
#include <QFile>
#include <QSet>
#include <QElapsedTimer>

namespace openswmmvis::simulation {
// Project configuration contains stable identities; host locations stay in preferences.
class ComponentEngineRegistry final : public QObject {
    Q_OBJECT
public:
    explicit ComponentEngineRegistry(QObject *project);
    static ComponentEngineRegistry *forProject(QObject *project);
    QJsonObject settings() const { return settings_; }
    QString error() const { return error_; }
    QString mode() const { return settings_.value("mode").toString("native"); }
    bool setSettings(const QJsonObject &, QString *error);
    static bool validate(const QJsonObject &, QString *error);
public slots:
    void reload();
signals:
    void settingsChanged();
private:
    QJsonObject settings_;
    QString error_;
};

struct ComponentRunRequest {
    QString model, directory, program, components;
    QJsonObject settings;
    bool surface = false;
    QString networkDirectory() const;
    QString report() const;
    QString binary() const;
    QString mesh() const;
    QString document() const;
    QString manifest() const;
    QJsonObject composition() const;
};

// The numerical owner is the external Composer process. Only verified saved results are adopted.
class ComposerSimulationRunner final : public SimulationRunner {
    Q_OBJECT
public:
    ComposerSimulationRunner(int job, const QString &name, ComponentRunRequest request, QObject *parent = nullptr);
    ~ComposerSimulationRunner() override;
    void start() override;
    void cancel() override;
    void setPaused(bool) override;
    bool supportsPause() const override { return false; }
    bool supportsPartialResults() const override { return false; }
    QStringList outputPaths() const override;
    const ComponentRunRequest &request() const { return request_; }
private:
    void begin();
    void consume();
    void line(const QByteArray &);
    void stopWithError(const QString &);
    void complete(int, QProcess::ExitStatus);
    void publish(bool, const QString &);
    bool verifyResults(QString *);
    void retainLog(const QByteArray &);
    ComponentRunRequest request_;
    QProcess process_;
    QFile log_;
    QByteArray pending_, digest_;
    std::unique_ptr<RunPathLease> lease_;
    QSet<QString> ids_;
    QString runId_, failure_, finalManifest_;
    qint64 sequence_ = -1, logged_ = 0;
    double epoch_ = 0, end_ = 0, current_ = 0;
    QDateTime startDate_, endDate_;
    QElapsedTimer progressTimer_;
    bool launched_ = false, cancelled_ = false, published_ = false, final_ = false, succeeded_ = false;
};
} // namespace openswmmvis::simulation
