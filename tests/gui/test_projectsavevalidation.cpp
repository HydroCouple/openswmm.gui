// SPDX-License-Identifier: GPL-3.0-or-later
#include "project/projectsavevalidation.h"
#include <QTest>
#include <QDir>
#include <QFile>

namespace {
QString caseDir(const QString &name) {
    const QString root = qEnvironmentVariable("SWMMVIS_SAVE_VALIDATION_OUTPUT",
        QStringLiteral("tests/gui/data/projectsavevalidation_output"));
    const QString path = QDir(root).absoluteFilePath(name);
    QDir().mkpath(path);
    return path;
}
bool write(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() && f.flush();
}
QByteArray read(const QString &path) {
    QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
QByteArray deck() {
    return "[TITLE]\nSave validation fixture\n[OPTIONS]\nFLOW_UNITS CMS\nFLOW_ROUTING DYNWAVE\n"
           "START_DATE 01/01/2026\nEND_DATE 01/02/2026\n"
           "[JUNCTIONS]\nJ 10 10 0 0 0\n[OUTFALLS]\nO 0 FREE NO\n"
           "[CONDUITS]\nC J O 100 0.013 0 0 0\n[XSECTIONS]\nC CIRCULAR 1 0 0 0\n";
}
QByteArray mesh() {
    return ";; UNITS: SI (m)\n[2D_VERTICES]\n0 0 0\n10 0 0\n10 10 0\n0 10 0\n20 0 0\n"
           "[2D_TRIANGLES]\n1 4 2 0.035 0\n[2D_QUADS]\n0 1 2 3 0.035 0\n";
}
}

class TestProjectSaveValidation : public QObject {
    Q_OBJECT
private slots:
    void stagedResourcesOverrideFinalBytes() {
        const QString dir = caseDir("staged_resources");
        const QString final = dir + "/model.inp", external = dir + "/river mesh.2dm";
        const QString config = dir + "/reaction.rxn";
        QVERIFY(write(final, "old model")); QVERIFY(write(external, "old mesh"));
        QVERIFY(write(config, "old component"));
        ProjectSaveOutputs out;
        const QString mainStage = out.stage(final, ProjectSaveOutputs::Model);
        const QString meshStage = out.stage(external, ProjectSaveOutputs::Mesh);
        const QString configStage = out.stage(config, ProjectSaveOutputs::Component);
        const QByteArray model = deck() + "[2D_MESH_FILE]\nFILE \"river mesh.2dm\"\n"
            "[PROCESS_COMPONENTS]\norg.hydrocouple.openswmm.reactions config=\"reaction.rxn\"\n";
        QVERIFY(write(mainStage, model)); QVERIFY(write(meshStage, mesh()));
        QVERIFY(write(configStage, "[REACTION_OPTIONS]\nSOLVER RK5\n[REACTION_SPECIES]\nBULK A MG\n"));
        QString err; QStringList warnings;
        QVERIFY2(ProjectSaveValidation::validate(out, nullptr, &err, &warnings, 5, 2, 1), qPrintable(err));
        QCOMPARE(read(final), QByteArray("old model")); QCOMPARE(read(external), QByteArray("old mesh"));
        QCOMPARE(read(config), QByteArray("old component")); QCOMPARE(read(mainStage), model);
        QVERIFY(QDir(dir).entryList({".openswmm-validate-*"}, QDir::Files | QDir::Hidden).isEmpty());
    }
    void invalidPreparedResource_data() {
        QTest::addColumn<int>("which");
        QTest::newRow("malformed-mesh") << 0;
        QTest::newRow("malformed-config") << 1;
        QTest::newRow("missing-config") << 2;
        QTest::newRow("empty-mesh") << 3;
        QTest::newRow("nested-config") << 4;
        QTest::newRow("unknown-component-missing-config") << 5;
        QTest::newRow("nonempty-file-without-mesh") << 6;
    }
    void invalidPreparedResource() {
        QFETCH(int, which);
        const QString dir = caseDir(QString::fromLatin1(QTest::currentDataTag()));
        const QString final = dir + "/model.inp";
        QVERIFY(write(final, "saved sentinel"));
        ProjectSaveOutputs out;
        const QString mainStage = out.stage(final, ProjectSaveOutputs::Model);
        QByteArray model = deck();
        if (which == 0 || which == 3 || which == 6) {
            model += "[2D_MESH_FILE]\nFILE \"mesh.2dm\"\n";
            const QString stage = out.stage(dir + "/mesh.2dm", ProjectSaveOutputs::Mesh);
            QVERIFY(write(stage, which == 0 ? QByteArray("[2D_VERTICES]\nbad 0 0\n") :
                which == 6 ? QByteArray("[TITLE]\nwrong file, no mesh payload\n") : QByteArray()));
        } else {
            model += which == 5
                ? "[PROCESS_COMPONENTS]\nunknown.component config=\"reaction.rxn\"\n"
                : "[PROCESS_COMPONENTS]\norg.hydrocouple.openswmm.reactions config=\"reaction.rxn\"\n";
            if (which != 2 && which != 5) {
                const QString stage = out.stage(dir + "/reaction.rxn", ProjectSaveOutputs::Component);
                QVERIFY(write(stage, which == 4 ? QByteArray("[PROCESS_COMPONENTS]\nnested\n") :
                    QByteArray("[REACTION_SPECIES]\nBROKEN A MG\n")));
            }
        }
        QVERIFY(write(mainStage, model));
        QString err; QStringList warnings;
        QVERIFY(!ProjectSaveValidation::validate(out, nullptr, &err, &warnings));
        QVERIFY2(!err.isEmpty(), qPrintable(err)); QVERIFY(!err.contains(".openswmm-stage-"));
        QCOMPARE(read(final), QByteArray("saved sentinel"));
        QVERIFY(!out.publicationStarted());
    }
    void missingAndUnchangedRelativeSeries_data() {
        QTest::addColumn<bool>("missing");
        QTest::newRow("readable") << false;
        QTest::newRow("missing") << true;
    }
    void missingAndUnchangedRelativeSeries() {
        QFETCH(bool, missing);
        const QString dir = caseDir(QStringLiteral("series_%1").arg(missing));
        if (!missing) QVERIFY(write(dir + "/rainfall.dat", "01/01/2026 00:00 1\n01/01/2026 01:00 2\n"));
        ProjectSaveOutputs out;
        const QString stage = out.stage(dir + "/model.inp", ProjectSaveOutputs::Model);
        QVERIFY(write(stage, deck() + "[TIMESERIES]\nRAIN FILE \"rainfall.dat\"\n"));
        QString err; QStringList warnings;
        QCOMPARE(ProjectSaveValidation::validate(out, nullptr, &err, &warnings), !missing);
        if (missing) QVERIFY2(err.contains("RAIN"), qPrintable(err));
    }
    void draftDiagnosticsDoNotPreventSave_data() {
        QTest::addColumn<QByteArray>("missingNode");
        QTest::newRow("missing-node") << QByteArray("ABSENT");
        QTest::newRow("config-is-an-object-name") << QByteArray("config");
    }
    void draftDiagnosticsDoNotPreventSave() {
        QFETCH(QByteArray, missingNode);
        const QString dir = caseDir(QStringLiteral("draft_%1").arg(QString::fromLatin1(QTest::currentDataTag())));
        ProjectSaveOutputs out;
        const QString stage = out.stage(dir + "/model.inp", ProjectSaveOutputs::Model);
        QByteArray model = deck(); model.replace("C J O 100", "C J " + missingNode + " 100");
        QVERIFY(write(stage, model));
        QString err; QStringList warnings;
        QVERIFY2(ProjectSaveValidation::validate(out, nullptr, &err, &warnings), qPrintable(err));
        QVERIFY(!warnings.isEmpty());
    }
    void outsideComponentDiagnosticUsesFinalPath() {
        const QString dir = caseDir("outside_component_diagnostic");
        QVERIFY(QDir().mkpath(dir + "/model"));
        QVERIFY(QDir().mkpath(dir + "/components"));
        const QString finalConfig = dir + "/components/reaction.rxn";
        ProjectSaveOutputs out;
        const QString stage = out.stage(dir + "/model/model.inp", ProjectSaveOutputs::Model);
        const QString config = out.stage(finalConfig, ProjectSaveOutputs::Component);
        QVERIFY(write(stage, deck() + "[PROCESS_COMPONENTS]\norg.hydrocouple.openswmm.reactions config=\"../components/reaction.rxn\"\n"));
        // The registry reports source_path for content before the first
        // header, retaining the '/model/../components/' path spelling.
        QVERIFY(write(config, "malformed before section header\n"));
        QString err;
        QVERIFY(!ProjectSaveValidation::validate(out, nullptr, &err, nullptr));
        QVERIFY2(err.contains(finalConfig), qPrintable(err));
        QVERIFY(!err.contains(".openswmm-stage-"));
        QVERIFY(!err.contains(".openswmm-validate-"));
    }
    void newNestedUnicodeResourcesAndSettings() {
        const QString dir = caseDir(QStringLiteral("new resources é"));
        const QString final = dir + "/model.inp";
        ProjectSaveOutputs out;
        const QString stage = out.stage(final, ProjectSaveOutputs::Model);
        const QString config = out.stage(dir + QStringLiteral("/nested_é/reaction.rxn"), ProjectSaveOutputs::Component);
        const QString settings = out.stage(dir + "/model.oswp", ProjectSaveOutputs::Settings);
        QVERIFY(!config.isEmpty());
        QVERIFY(write(stage, deck() + QStringLiteral("[PROCESS_COMPONENTS]\n  org.hydrocouple.openswmm.reactions config=\"nested_é/reaction.rxn\" ; preserved comment\n").toUtf8()));
        QVERIFY(write(config, "[REACTION_SPECIES]\nBULK A MG\n"));
        QVERIFY(write(settings, "{\"schemaVersion\":5,\"sessions\":[{\"inpPath\":\"model.inp\"}]}"));
        QString err;
        QVERIFY2(ProjectSaveValidation::validate(out, nullptr, &err, nullptr), qPrintable(err));
        QVERIFY(!QFileInfo::exists(final));
        QVERIFY(!QFileInfo::exists(dir + QStringLiteral("/nested_é")));
    }
    void pluginsAndRuntimeOutputsAreNotExecuted() {
        const QString dir = caseDir("no_runtime");
        const QString output = dir + "/protected.h5", detailed = dir + "/detail.csv";
        QVERIFY(write(output, "output sentinel")); QVERIFY(write(detailed, "detail sentinel"));
        ProjectSaveOutputs out;
        const QString stage = out.stage(dir + "/model.inp", ProjectSaveOutputs::Model);
        const QString config = out.stage(dir + "/transport.cfg", ProjectSaveOutputs::Component);
        const QByteArray bytes = deck() + "[  pLuGiNs  ] ; deliberately spaced header\n/no/such/library.dylib\n"
            "[2D_OPTIONS]\nOUTPUT_FILE protected.h5\n[PROCESS_COMPONENTS]\n"
            "org.hydrocouple.openswmm.transport.ard config=\"transport.cfg\"\n";
        QVERIFY(write(stage, bytes));
        QVERIFY(write(config, "[TRANSPORT_OPTIONS]\nDETAILED_OUTPUT detail.csv\n"));
        QString err; QStringList warnings;
        QVERIFY2(ProjectSaveValidation::validate(out, nullptr, &err, &warnings), qPrintable(err));
        QVERIFY(warnings.join('\n').contains("plugin", Qt::CaseInsensitive));
        QCOMPARE(read(stage), bytes); QCOMPARE(read(output), QByteArray("output sentinel"));
        QCOMPARE(read(detailed), QByteArray("detail sentinel"));
    }
    void whitespaceInPublishedComponentTokenIsNotDisguised() {
        const QString dir = caseDir("unsupported_component_token");
        ProjectSaveOutputs out;
        const QString stage = out.stage(dir + "/model.inp", ProjectSaveOutputs::Model);
        const QString config = out.stage(dir + "/reaction config.rxn", ProjectSaveOutputs::Component);
        QVERIFY(write(stage, deck() + "[PROCESS_COMPONENTS]\norg.hydrocouple.openswmm.reactions config=\"reaction config.rxn\"\n"));
        QVERIFY(write(config, "[REACTION_SPECIES]\nBULK A MG\n"));
        QString err;
        QVERIFY(!ProjectSaveValidation::validate(out, nullptr, &err, nullptr));
        QVERIFY2(err.contains("whitespace") && err.contains("reaction config.rxn"), qPrintable(err));
    }
    void emptyUntitledAuthoringModelCanBeSaved() {
        const QString dir = caseDir("empty_untitled");
        const auto destroy = [](void *engine) { swmm_engine_close(engine); swmm_engine_destroy(engine); };
        std::unique_ptr<void, decltype(destroy)> source(swmm_engine_new(), destroy);
        QVERIFY(source);
        QCOMPARE(swmm_node_count(source.get()), 0);
        ProjectSaveOutputs out;
        const QString stage = out.stage(dir + "/model.inp", ProjectSaveOutputs::Model);
        QCOMPARE(swmm_model_write(source.get(), stage.toUtf8().constData()), 0);
        QString err;
        QVERIFY2(ProjectSaveValidation::validate(out, source.get(), &err, nullptr), qPrintable(err));
    }
    void expectedMeshCannotDisappear() {
        const QString dir = caseDir("absent_mesh");
        ProjectSaveOutputs out;
        QVERIFY(write(out.stage(dir + "/model.inp", ProjectSaveOutputs::Model), deck()));
        QString err;
        QVERIFY(!ProjectSaveValidation::validate(out, nullptr, &err, nullptr, 5, 2, 1));
        QVERIFY2(err.contains("vertices") && err.contains("read 0"), qPrintable(err));
    }
    void countMismatchRefusesPublication() {
        const QString dir = caseDir("count_mismatch");
        ProjectSaveOutputs out;
        const QString stage = out.stage(dir + "/model.inp", ProjectSaveOutputs::Model);
        QVERIFY(write(stage, deck() + mesh()));
        QString err;
        QVERIFY(!ProjectSaveValidation::validate(out, nullptr, &err, nullptr, 6, 2, 1));
        QVERIFY2(err.contains("vertices"), qPrintable(err));
    }
    void sourceCountComparisonDoesNotMutateSource() {
        const QString dir = caseDir("source_counts");
        const QString original = dir + "/original.inp";
        QVERIFY(write(original, deck()));
        const auto destroy = [](void *engine) { swmm_engine_close(engine); swmm_engine_destroy(engine); };
        std::unique_ptr<void, decltype(destroy)> source(swmm_engine_create(), destroy);
        QVERIFY(source);
        QCOMPARE(swmm_engine_open(source.get(), original.toUtf8().constData(), nullptr, nullptr, nullptr), 0);
        ProjectSaveOutputs out;
        const QString stage = out.stage(dir + "/model.inp", ProjectSaveOutputs::Model);
        QVERIFY(write(stage, "[TITLE]\nempty but editable project\n[OPTIONS]\nFLOW_UNITS CMS\n"));
        QString err;
        QVERIFY(!ProjectSaveValidation::validate(out, source.get(), &err, nullptr));
        QVERIFY2(err.contains("nodes"), qPrintable(err));
        QCOMPARE(swmm_node_count(source.get()), 2);
        QCOMPARE(swmm_link_count(source.get()), 1);
    }
    void malformedSettingsRefused_data() {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("truncated-json") << QByteArray("{\"schemaVersion\":");
        QTest::newRow("array-root") << QByteArray("[]");
        QTest::newRow("missing-sessions") << QByteArray("{\"schemaVersion\":5}");
        QTest::newRow("wrong-model-reference") << QByteArray("{\"schemaVersion\":5,\"sessions\":[{\"inpPath\":\"other.inp\"}]}");
    }
    void malformedSettingsRefused() {
        QFETCH(QByteArray, bytes);
        const QString dir = caseDir(QString::fromLatin1(QTest::currentDataTag()));
        ProjectSaveOutputs out;
        QVERIFY(write(out.stage(dir + "/model.inp", ProjectSaveOutputs::Model), deck()));
        QVERIFY(write(out.stage(dir + "/model.oswp", ProjectSaveOutputs::Settings), bytes));
        QString err;
        QVERIFY(!ProjectSaveValidation::validate(out, nullptr, &err, nullptr));
        QVERIFY2(err.contains("settings", Qt::CaseInsensitive), qPrintable(err));
    }
};
QTEST_GUILESS_MAIN(TestProjectSaveValidation)
#include "test_projectsavevalidation.moc"
