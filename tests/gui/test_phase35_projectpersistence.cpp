/**
 * @file      test_phase35_projectpersistence.cpp
 * @brief     Tests for phase35 projectpersistence.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "project/profilesectionstore.h"
#include "project/groundwaterrecipestore.h"
#include "project/projectserializer.h"
#include "project/openswmmvisworkspace.h"
#include "swmmvisprojectwindow.h"
#include "core/unitsystem.h"
#include <QFile>
#include <QJsonDocument>
#include <QtTest>
#include <memory>
#include <QQmlEngine>
#include "map/swmmlayerqsgrenderer.h"
#include "map/swmm2dmeshqsgrenderer.h"
#include "map/swmm2dresultsqsgrenderer.h"
class TestPhase35ProjectPersistence : public QObject
{
    Q_OBJECT
    /** @brief Resolve a retained phase-35 output path and attempt directory creation.
     * @param[in] name Path appended under PHASE35_TEST_OUTPUT without sanitization.
     * @return Joined path; failure to create the output directory is not checked here. */
    QString output(const QString &name)
    {
        const QString dir = QDir::cleanPath(QStringLiteral(PHASE35_TEST_OUTPUT));
        QDir().mkpath(dir);
        return QDir(dir).filePath(name);
    }
    /** @brief Initialize a borrowed blank Phase 35 project with a one-hour January 1, 2026 window.
     * @param[in,out] window Project initialized through its production blank-model path.
     * @return Initialization result; warnings/errors remain local and are discarded. */
    bool initialize(SWMMVisProjectWindow &window)
    {
        SWMMModelLayer::NewProjectSpec spec;
        spec.name = "Phase 35";
        spec.startDateTime = QDateTime(QDate(2026, 1, 1), QTime(0, 0));
        spec.endDateTime = spec.startDateTime.addSecs(3600);
        QList<QString> w, e;
        return window.initializeBlankModel(spec, w, e);
    }
    /** @brief Build a profile definition referencing an unavailable retained run.
     * @return Independent section-1 definition with a ten-metre EPSG:32618 polyline and one source path; attempts output-directory creation. */
    ProfileSection::Definition definition()
    {
        ProfileSection::Definition d;
        d.id = "section-1";
        d.title = "Groundwater and surface";
        d.scenePolyline = {{0, 0}, {10, 0}};
        d.sceneCRS = "EPSG:32618";
        d.horizontalUnits = "m";
        d.primarySourceId = "source-1";
        d.sources = {{"source-1", output("unavailable-run.h5"), {}}};
        return d;
    }
    /** @brief Build a snapshot-only groundwater Ks assignment history entry.
     * @return Independent version-one JSON recipe referencing an unavailable raster and cells zero/two; attempts output-directory creation. */
    QJsonObject recipe()
    {
        return {{"version", 1},
                {"refreshPolicy", "snapshot-only"},
                {"target", "Ks"},
                {"sourcePath", output("unavailable-input.tif")},
                {"cells", QJsonArray{0, 2}}};
    }
    /** @brief Read retained evidence bytes.
     * @param[in] path File opened read-only.
     * @return Entire file contents, or empty if opening fails; an empty file is indistinguishable from failure. */
    QByteArray bytes(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return file.readAll();
    }
private slots:
    /** @brief Register all three production QML canvas renderer types before constructing project windows. */
    void initTestCase()
    {
        qmlRegisterType<SWMMLayerQSGRenderer>("OpenSWMM", 1, 0, "SWMMLayerQSGRenderer");
        qmlRegisterType<SWMM2DMeshQSGRenderer>("OpenSWMM", 1, 0, "SWMM2DMeshQSGRenderer");
        qmlRegisterType<SWMM2DResultsQSGRenderer>("OpenSWMM", 1, 0, "SWMM2DResultsQSGRenderer");
    }
    /** @brief Verify selecting the current engine version is silent while changing it marks the project dirty exactly once. */
    void engineVersionOnlyRecordsActualUserChanges()
    {
        std::unique_ptr<OpenSWMMVisWorkspace> workspace(
            OpenSWMMVisWorkspace::newInstance({}, nullptr));
        SWMMVisProjectWindow window(workspace.get(), {});
        QVERIFY(initialize(window));
        window.setHasChanges(false);
        QSignalSpy edits(&window, &SWMMVisProjectWindow::hasChangesChanged);
        window.setEngineVersion(window.engineVersion());
        QVERIFY(!window.hasChanges());
        QCOMPARE(edits.count(), 0);
        const QString changed =
            window.engineVersion() == "5.2.4" ? QString("6.0.0") : QString("5.2.4");
        window.setEngineVersion(changed);
        QCOMPARE(window.engineVersion(), changed);
        QVERIFY(window.hasChanges());
        QCOMPARE(edits.count(), 1);
    }
    /** @brief Register same/different persisted engine versions with initially clean and dirty projects. */
    void engineVersionHydrationPreservesDirtyState_data()
    {
        QTest::addColumn<bool>("dirty");
        QTest::addColumn<bool>("differentVersion");
        QTest::newRow("clean-same-version") << false << false;
        QTest::newRow("clean-different-version") << false << true;
        QTest::newRow("dirty-different-version") << true << true;
    }
    /** @brief Verify sidecar engine-version hydration changes selection without altering dirty state or emitting edit notifications. */
    void engineVersionHydrationPreservesDirtyState()
    {
        QFETCH(bool, dirty);
        QFETCH(bool, differentVersion);
        std::unique_ptr<OpenSWMMVisWorkspace> workspace(
            OpenSWMMVisWorkspace::newInstance({}, nullptr));
        SWMMVisProjectWindow window(workspace.get(), {});
        QVERIFY(initialize(window));
        const QString original = window.engineVersion();
        const QString restored = differentVersion
                                     ? (original == "5.2.4" ? QString("6.0.0") : QString("5.2.4"))
                                     : original;
        QJsonObject session{{"inpPath", ""}, {"engineVersion", restored}};
        const auto path = output(QString("engine-version-%1.oswp").arg(QTest::currentDataTag()));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const auto data =
            QJsonDocument(QJsonObject{{"schemaVersion", 5}, {"sessions", QJsonArray{session}}})
                .toJson();
        QCOMPARE(file.write(data), qint64(data.size()));
        file.close();
        window.setHasChanges(dirty);
        QSignalSpy edits(&window, &SWMMVisProjectWindow::hasChangesChanged);
        QString error;
        QStringList warnings;
        QVERIFY2(ProjectSerializer::applyFromFile(path, &window, &error, &warnings),
                 qPrintable(error));
        QCOMPARE(window.engineVersion(), restored);
        QCOMPARE(window.hasChanges(), dirty);
        QCOMPARE(edits.count(), 0);
    }
    /** @brief Register initially clean and already edited terrain metadata hydration. */
    void terrainHydrationPreservesDirtyState_data()
    {
        QTest::addColumn<bool>("dirty");
        QTest::newRow("clean") << false;
        QTest::newRow("existing-edit") << true;
    }
    /** @brief Verify terrain unit/offset hydration preserves dirty state while notifying terrain listeners.
     * Checks metre-to-project conversion and confirms a later user unit change dirties the project once. */
    void terrainHydrationPreservesDirtyState()
    {
        QFETCH(bool, dirty);
        std::unique_ptr<OpenSWMMVisWorkspace> workspace(
            OpenSWMMVisWorkspace::newInstance({}, nullptr));
        SWMMVisProjectWindow window(workspace.get(), {});
        QVERIFY(initialize(window));
        window.setTerrainVerticalUnit("ft");
        window.setHasChanges(dirty);
        QSignalSpy dirtyChanges(&window, &SWMMVisProjectWindow::hasChangesChanged);
        QSignalSpy terrainChanges(&window, &SWMMVisProjectWindow::activeTerrainChanged);
        window.restoreTerrainState({}, 1.25, 2.5, "m");
        QCoreApplication::processEvents();
        QCOMPARE(window.terrainVerticalUnit(), QString("m"));
        QCOMPARE(window.terrainVertFactor(), window.unitSystem()->isSI() ? 1.0 : 1.0 / 0.3048);
        QCOMPARE(window.terrainNodeOffset(), 1.25);
        QCOMPARE(window.terrainLinkOffset(), 2.5);
        QCOMPARE(window.hasChanges(), dirty);
        QCOMPARE(dirtyChanges.count(), 0);
        QVERIFY(terrainChanges.count() > 0);
        window.setHasChanges(false);
        dirtyChanges.clear();
        window.setTerrainVerticalUnit("ft");
        QVERIFY(window.hasChanges());
        QCOMPARE(dirtyChanges.count(), 1);
    }
    /** @brief Verify profile sections and groundwater history persist source paths and restore without edit signals.
     * Confirms editing stores dirties the first project and applying a legacy sidecar without these fields clears the second stores without dirtying it. */
    void savedSectionsAndAssignmentHistoryRoundTrip()
    {
        std::unique_ptr<OpenSWMMVisWorkspace> workspace(
            OpenSWMMVisWorkspace::newInstance({}, nullptr));
        SWMMVisProjectWindow first(workspace.get(), {}), second(workspace.get(), {});
        QVERIFY(initialize(first));
        QVERIFY(initialize(second));
        first.setHasChanges(false);
        QString error;
        QVERIFY(ProfileSectionStore::forOwner(&first)->saveDefinition(definition(), &error));
        QVERIFY(first.hasChanges());
        QVERIFY(GroundwaterRecipeStore::forOwner(&first)->append(recipe(), &error));
        const auto path = output("sections-and-assignment.oswp");
        QVERIFY2(ProjectSerializer::saveToFile(path, &first, &error), qPrintable(error));
        const auto root = QJsonDocument::fromJson(bytes(path)).object();
        const auto session = root["sessions"].toArray()[0].toObject();
        QCOMPARE(session["profileSections"].toArray().size(), 1);
        QCOMPARE(session["groundwaterAssignmentHistory"].toArray().size(), 1);
        second.setHasChanges(false);
        QStringList warnings;
        QSignalSpy sectionEdits(ProfileSectionStore::forOwner(&second),
                                &ProfileSectionStore::edited);
        QSignalSpy assignmentEdits(GroundwaterRecipeStore::forOwner(&second),
                                   &GroundwaterRecipeStore::edited);
        QVERIFY2(ProjectSerializer::applyFromFile(path, &second, &error, &warnings),
                 qPrintable(error));
        const auto restored = ProfileSectionStore::forOwner(&second)->definitions();
        QCOMPARE(restored.size(), 1);
        QCOMPARE(restored[0].sources[0].path, definition().sources[0].path);
        QCOMPARE(GroundwaterRecipeStore::forOwner(&second)
                     ->events()[0]
                     .toObject()["sourcePath"]
                     .toString(),
                 recipe()["sourcePath"].toString());
        // Metadata and store hydration are read-only, including engine selection.
        QCoreApplication::processEvents();
        QVERIFY(!second.hasChanges());
        QCOMPARE(sectionEdits.count(), 0);
        QCOMPARE(assignmentEdits.count(), 0);
        auto legacy = root;
        auto sessions = legacy["sessions"].toArray();
        auto old = sessions[0].toObject();
        old.remove("profileSections");
        old.remove("groundwaterAssignmentHistory");
        sessions[0] = old;
        legacy["sessions"] = sessions;
        const auto legacyPath = output("legacy-no-sections.oswp");
        QFile legacyFile(legacyPath);
        QVERIFY(legacyFile.open(QIODevice::WriteOnly));
        legacyFile.write(QJsonDocument(legacy).toJson());
        legacyFile.close();
        QVERIFY(ProjectSerializer::applyFromFile(legacyPath, &second, &error, &warnings));
        QVERIFY(ProfileSectionStore::forOwner(&second)->definitions().isEmpty());
        QVERIFY(GroundwaterRecipeStore::forOwner(&second)->events().isEmpty());
        QCoreApplication::processEvents();
        QVERIFY(!second.hasChanges());
        QCOMPARE(sectionEdits.count(), 0);
        QCOMPARE(assignmentEdits.count(), 0);
    }
    /** @brief Verify malformed saved section metadata loads with warnings and prevents overwriting the original sidecar bytes. */
    void malformedSidecarWarnsAndCannotOverwriteOriginal()
    {
        std::unique_ptr<OpenSWMMVisWorkspace> workspace(
            OpenSWMMVisWorkspace::newInstance({}, nullptr));
        SWMMVisProjectWindow window(workspace.get(), {});
        QVERIFY(initialize(window));
        QString error;
        const auto path = output("malformed-retained.oswp");
        QJsonObject session{{"inpPath", ""}, {"profileSections", QJsonObject{{"future", true}}}};
        const QByteArray original =
            QJsonDocument(QJsonObject{{"schemaVersion", 5}, {"sessions", QJsonArray{session}}})
                .toJson();
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(original), qint64(original.size()));
        file.close();
        QStringList warnings;
        QVERIFY(ProjectSerializer::applyFromFile(path, &window, &error, &warnings));
        QVERIFY(!warnings.isEmpty());
        QVERIFY(!ProjectSerializer::saveToFile(path, &window, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(bytes(path), original);
    }
};
QTEST_MAIN(TestPhase35ProjectPersistence)
#include "test_phase35_projectpersistence.moc"
