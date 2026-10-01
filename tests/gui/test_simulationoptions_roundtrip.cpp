/*!
 * \file   test_simulationoptions_roundtrip.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Baseline: opening the Simulation Options dialog and pressing Apply
 *         with NO edits must write nothing.
 *
 * This is the safety net for the dialog restructure
 * (workplans/SIMULATION_OPTIONS_AND_PROCESS_GATING_PLAN_2026-09-04.md §3.4,
 * EXECUTION_PLAN_… P0/P3): every page's read()/write() pair must be the
 * identity on an unedited model. Two independent assertions per deck:
 *
 *   1. `wroteAnyChanges()` is false after Apply — the dialog's own dirty count
 *      (writeIfChanged's numeric-aware compare) saw nothing to write.
 *   2. The model serialised BEFORE the dialog existed and AFTER Apply is
 *      byte-identical in every [OPTIONS]-class section — [OPTIONS],
 *      [REPORT], [FILES], [EVENTS], [2D_OPTIONS], [TITLE] — so a write the
 *      dirty count missed (e.g. a value re-formatted to the same number)
 *      still fails here, with a readable diff of the offending section.
 *
 * Decks: a 1D DYNWAVE deck, a 2D deck, and an FV variant derived from the 1D
 * deck (there is no FV fixture yet). All outputs go next to the fixtures
 * (reviewable, CLAUDE.md §4.1).
 */
#include "layers/swmmmodellayer.h"
#include "project/openswmmvisworkspace.h"
#include "swmmvisprojectwindow.h"
#include "ui/dialogs/simulationoptionsdialog.h"
#include "ui/dialogs/wateragesourcesdialog.h"
#include "ui/dialogs/nodecompoundeditdialog.h"
#include "ui/dialogs/linkcompoundeditdialog.h"
#include "ui/dialogs/subcatchcompoundeditdialog.h"
#include "ui/dialogs/groundwaterexchangedialog.h"
#include "ui/dialogs/streeteditordialog.h"
#include "ui/dialogs/inleteditordialog.h"
#include "ui/dialogs/hydrographgroupeditor.h"
#include "curve/curveregistry.h"
#include "ui/dialogs/curveeditordialog.h"
#include "pattern/patternregistry.h"
#include "ui/dialogs/patterneditordialog.h"
#include "timeseries/timeseriesregistry.h"
#include "ui/dialogs/timeserieseditordialog.h"
#include "transect/transectregistry.h"
#include "ui/dialogs/transecteditordialog.h"
#include "street/streetregistry.h"
#include "inlet/inletregistry.h"
#include <QListView>
#include <QLineEdit>
#include <QTableView>
#include <QUndoStack>

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_water_age.h>

#include <QApplication>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QSettings>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QLabel>
#include <QObject>
#include <QPushButton>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QTimer>

#include <memory>

namespace {

QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

QString fixture(const QString &name) { return QDir(dataDir()).filePath(name); }

std::unique_ptr<SWMMModelLayer> openLayer(const QString &path)
{
    auto layer = std::make_unique<SWMMModelLayer>(path, nullptr);
    QList<QString> warnings, errors;
    if (!layer->loadModel(warnings, errors)) return nullptr;
    return layer;
}

std::unique_ptr<SWMMVisProjectWindow> openProjectWindow(const QString &path, QString *error)
{
    auto *workspace = OpenSWMMVisWorkspace::newInstance(QString(), nullptr);
    auto window = std::make_unique<SWMMVisProjectWindow>(workspace, path, nullptr);
    workspace->setParent(window.get());
    QList<QString> warnings, errors;
    if (!window->loadModel(warnings, errors)) {
        if (error) *error = errors.join(QStringLiteral("; "));
        return nullptr;
    }
    return window;
}

QString readAll(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(f.readAll());
}

//! Sections the dialog owns — the only ones whose text must be unchanged.
//! Object sections ([JUNCTIONS] …) are deliberately excluded so an engine
//! writer quirk elsewhere cannot fail this test.
const QStringList kOwnedSections = {
    QStringLiteral("TITLE"), QStringLiteral("OPTIONS"), QStringLiteral("REPORT"),
    QStringLiteral("FILES"), QStringLiteral("EVENTS"), QStringLiteral("2D_OPTIONS"),
};

//! Section name → its normalised body (trimmed lines, comments/blank removed).
QMap<QString, QStringList> ownedSections(const QString &text)
{
    QMap<QString, QStringList> out;
    QString current;
    for (const QString &raw : text.split('\n')) {
        const QString line = raw.trimmed();
        if (line.startsWith('[')) {
            current = line.mid(1, line.indexOf(']') - 1).toUpper();
            continue;
        }
        if (current.isEmpty() || line.isEmpty() || line.startsWith(';')) continue;
        if (kOwnedSections.contains(current))
            out[current] << line.simplified();
    }
    return out;
}

//! Copy \p src to \p dst with FLOW_ROUTING rewritten (FV variant).
bool writeRoutingVariant(const QString &src, const QString &dst, const QString &routing)
{
    QString text = readAll(src);
    if (text.isEmpty()) return false;
    text.replace(QRegularExpression(QStringLiteral("^FLOW_ROUTING\\s+\\S+"),
                                    QRegularExpression::MultilineOption),
                 QStringLiteral("FLOW_ROUTING         %1").arg(routing));
    QFile f(dst);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return false;
    f.write(text.toUtf8());
    return true;
}

} // namespace

class TestSimulationOptionsRoundTrip : public QObject
{
    Q_OBJECT

private:
    //! The whole contract for one deck.
    void roundTrip(const QString &deck, const QString &stem)
    {
        auto layer = openLayer(deck);
        QVERIFY2(layer, qPrintable(QStringLiteral("cannot open %1").arg(deck)));
        SWMM_Engine e = layer->engine();
        QVERIFY(e);

        // 1. Serialise BEFORE the dialog touches anything.
        const QString before = fixture(stem + QStringLiteral("_before.inp"));
        QCOMPARE(swmm_model_write(e, before.toUtf8().constData()), 0);

        // 2. Open the dialog exactly as SWMMVis::onSimulationOptions does
        //    (no project window: notes/HTML persistence is guarded on it).
        SimulationOptionsDialog dlg(e, layer.get(), QStringLiteral("6.0.0"),
                                    /*projectWindow*/ nullptr, /*parent*/ nullptr);
        QVERIFY(!dlg.wroteAnyChanges());

        // 3. Apply with no edits. onApply is a private slot; the meta-call is
        //    how the OK/Apply buttons reach it.
        QVERIFY(QMetaObject::invokeMethod(&dlg, "onApply", Qt::DirectConnection));

        // 4a. The dialog's own dirty count saw nothing.
        QVERIFY2(!dlg.wroteAnyChanges(),
                 "Apply with no edits reported changes — some page's write() "
                 "is not the identity of its read()");

        // 4b. The owned sections are textually unchanged.
        const QString after = fixture(stem + QStringLiteral("_after.inp"));
        QCOMPARE(swmm_model_write(e, after.toUtf8().constData()), 0);
        const auto a = ownedSections(readAll(before));
        const auto b = ownedSections(readAll(after));
        for (const QString &sec : kOwnedSections) {
            const QStringList la = a.value(sec), lb = b.value(sec);
            if (la == lb) continue;
            // Readable diff: the first differing / missing lines of each side.
            QStringList onlyBefore, onlyAfter;
            for (const QString &l : la) if (!lb.contains(l)) onlyBefore << l;
            for (const QString &l : lb) if (!la.contains(l)) onlyAfter << l;
            QFAIL(qPrintable(QStringLiteral(
                "[%1] changed by an unedited Apply on %2\n  before-only: %3\n  after-only:  %4")
                .arg(sec, QFileInfo(deck).fileName(),
                     onlyBefore.mid(0, 6).join(QStringLiteral(" | ")),
                     onlyAfter.mid(0, 6).join(QStringLiteral(" | ")))));
        }
    }

private slots:

    void modelEditorOwnerLifetime_data()
    {
        QTest::addColumn<QString>("family");
        QTest::addColumn<bool>("destroyOwner");
        for (const QString &family : {QStringLiteral("node"), QStringLiteral("link"),
             QStringLiteral("subcatchment"), QStringLiteral("groundwater"), QStringLiteral("simulation"),
             QStringLiteral("street"), QStringLiteral("inlet"), QStringLiteral("hydrograph"),
             QStringLiteral("curve"), QStringLiteral("pattern"), QStringLiteral("timeseries"), QStringLiteral("transect")}) {
            QTest::newRow(qPrintable(family + "-close-engine")) << family << false;
            QTest::newRow(qPrintable(family + "-delete-layer")) << family << true;
        }
    }

    void modelEditorOwnerLifetime()
    {
        QFETCH(QString, family);
        QFETCH(bool, destroyOwner);
        auto layer = openLayer(fixture("gw_exchange_fixture.inp"));
        QVERIFY(layer);
        std::unique_ptr<QDialog> dialog;
        const auto engine = layer->engine();
        if (family == "node") {
            NodeCompoundEditRef ref; ref.engine = engine; ref.layer = layer.get(); ref.nodeName = "J1";
            dialog = std::make_unique<NodeCompoundEditDialog>(ref);
        } else if (family == "link") {
            LinkCompoundEditRef ref; ref.engine = engine; ref.layer = layer.get(); ref.linkName = "C1";
            dialog = std::make_unique<LinkCompoundEditDialog>(ref);
        } else if (family == "subcatchment" || family == "groundwater") {
            SubcatchCompoundEditRef ref; ref.engine = engine; ref.layer = layer.get(); ref.subName = "S1";
            if (family == "groundwater") dialog = std::make_unique<GroundwaterExchangeDialog>(ref);
            else dialog = std::make_unique<SubcatchCompoundEditDialog>(ref);
        } else if (family == "simulation") {
            dialog = std::make_unique<SimulationOptionsDialog>(engine, layer.get());
        } else if (family == "curve") {
            auto *reg = qobject_cast<openswmmvis::curve::CurveRegistry *>(layer->ensureCurveRegistry());
            QVERIFY(reg);
            dialog = std::make_unique<openswmmvis::ui::CurveEditorDialog>(reg, nullptr);
        } else if (family == "pattern") {
            auto *reg = qobject_cast<openswmmvis::pattern::PatternRegistry *>(layer->ensurePatternRegistry());
            QVERIFY(reg);
            dialog = std::make_unique<openswmmvis::ui::PatternEditorDialog>(reg, nullptr);
        } else if (family == "timeseries") {
            auto *reg = qobject_cast<openswmmvis::timeseries::TimeseriesRegistry *>(layer->ensureTimeseriesRegistry());
            QVERIFY(reg);
            dialog = std::make_unique<openswmmvis::ui::TimeseriesEditorDialog>(reg, nullptr);
        } else if (family == "transect") {
            auto *reg = qobject_cast<openswmmvis::transect::TransectRegistry *>(layer->ensureTransectRegistry());
            QVERIFY(reg);
            dialog = std::make_unique<openswmmvis::ui::TransectEditorDialog>(reg, layer.get(), nullptr);
        } else if (family == "street") {
            auto *reg = qobject_cast<openswmmvis::street::StreetRegistry *>(layer->ensureStreetRegistry());
            QVERIFY(reg); QVERIFY(reg->create("LifetimeStreet"));
            dialog = std::make_unique<openswmmvis::ui::StreetEditorDialog>(reg, layer.get());
        } else if (family == "inlet") {
            auto *reg = qobject_cast<openswmmvis::inlet::InletRegistry *>(layer->ensureInletRegistry());
            QVERIFY(reg); QVERIFY(reg->create("LifetimeInlet"));
            dialog = std::make_unique<openswmmvis::ui::InletEditorDialog>(reg, layer.get(), nullptr);
        } else {
            QVERIFY(layer->applyHydrographAddGroup("LifetimeUH", QString(), 0));
            dialog = std::make_unique<HydrographGroupEditor>(layer.get());
        }
        dialog->show();
        QVERIFY(dialog->isEnabled());
        bool sawLiveEngine = false;
        QObject closeObserver;
        SWMMModelLayer *owner = layer.get();
        // A dialog may disconnect all of its layer callbacks while invalidating.
        // Observe the engine contract independently of those recipient connections.
        connect(owner, &SWMMModelLayer::engineAboutToClose, &closeObserver, [&] {
            sawLiveEngine = owner->engine() == engine;
        });
        if (destroyOwner) layer.reset(); else layer->closeEngine();
        QVERIFY(sawLiveEngine);
        QVERIFY(!dialog->isEnabled());
        QVERIFY(!dialog->isVisible());
        QCOMPARE(dialog->result(), int(QDialog::Rejected));
        // Includes pending preview/debounce callbacks after the engine is gone.
        QTest::qWait(350);
        QVERIFY(!dialog->isEnabled());
    }

    void nestedRegistryAndUndoLifetime_data()
    {
        QTest::addColumn<QString>("owner");
        QTest::newRow("street-registry") << QStringLiteral("street");
        QTest::newRow("inlet-registry") << QStringLiteral("inlet");
        QTest::newRow("inlet-undo-stack") << QStringLiteral("undo");
    }

    void nestedRegistryAndUndoLifetime()
    {
        QFETCH(QString, owner);
        std::unique_ptr<QDialog> dialog;
        auto streets = std::make_unique<openswmmvis::street::StreetRegistry>();
        auto inlets = std::make_unique<openswmmvis::inlet::InletRegistry>();
        auto undo = std::make_unique<QUndoStack>();
        QVERIFY(streets->create("Street")); QVERIFY(inlets->create("Inlet"));
        if (owner == "street") dialog = std::make_unique<openswmmvis::ui::StreetEditorDialog>(streets.get(), nullptr);
        else dialog = std::make_unique<openswmmvis::ui::InletEditorDialog>(inlets.get(), nullptr, undo.get());
        dialog->show();
        if (owner == "street") streets.reset();
        else if (owner == "inlet") inlets.reset();
        else undo.reset();
        QVERIFY(!dialog->isEnabled());
        QVERIFY(!dialog->isVisible());
        // Check the provider list, not QComboBox's internal popup QListViews:
        // static inlet-type choices legitimately remain present in a disabled editor.
        QListView *providerList = nullptr;
        if (owner == "street") {
            auto *editor = qobject_cast<openswmmvis::ui::StreetEditorDialog *>(dialog.get());
            QVERIFY(editor);
            providerList = editor->listView();
            QVERIFY(!editor->currentProvider());
            QVERIFY(editor->nameEdit()->text().isEmpty());
        } else {
            auto *editor = qobject_cast<openswmmvis::ui::InletEditorDialog *>(dialog.get());
            QVERIFY(editor);
            providerList = editor->listView();
            QVERIFY(!editor->currentProvider());
            QVERIFY(editor->nameEdit()->text().isEmpty());
        }
        QVERIFY(providerList);
        QVERIFY(providerList->model());
        QCOMPARE(providerList->model()->rowCount(), 0);
        QTest::qWait(350);
    }
    /*! Point QSettings at a scratch store under the test data directory.
     *
     *  The dialog keeps a per-project "is the 2D module on" preference in
     *  QSettings, and consults it while deciding whether the 2D checkbox
     *  carries a real user intent. Without this redirect the test reads
     *  whatever the developer's machine happens to have stored for these
     *  fixture paths, so "Apply with no edits writes nothing" became a
     *  question about local history rather than about the dialog — which is
     *  exactly how the long-standing IGNORE_2D red hid for so long. Must run
     *  before any QSettings is constructed.
     */
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("openswmm-test"));
        QCoreApplication::setApplicationName(
            QStringLiteral("test_simulationoptions_roundtrip"));
        // IniFormat is mandatory: setPath() is ignored for NativeFormat on macOS.
        QSettings::setDefaultFormat(QSettings::IniFormat);
        const QString scratch =
            QDir(dataDir()).filePath(QStringLiteral("simopts_roundtrip_settings"));
        QDir().mkpath(scratch);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, scratch);
    }

    void dynwave1D()
    {
        roundTrip(fixture(QStringLiteral("typed_selection_fixture.inp")),
                  QStringLiteral("simopts_roundtrip_dynwave"));
    }

    void twoD()
    {
        roundTrip(fixture(QStringLiteral("mini_2d.inp")),
                  QStringLiteral("simopts_roundtrip_2d"));
    }

    void finiteVolume()
    {
        const QString variant = fixture(QStringLiteral("simopts_roundtrip_fv_variant.inp"));
        QVERIFY(writeRoutingVariant(fixture(QStringLiteral("typed_selection_fixture.inp")),
                                    variant, QStringLiteral("FV")));
        roundTrip(variant, QStringLiteral("simopts_roundtrip_fv"));
    }

    //! A real edit must still be detected — guards against the test passing
    //! because writeToEngine silently writes nothing.
    void editIsDetected()
    {
        auto layer = openLayer(fixture(QStringLiteral("typed_selection_fixture.inp")));
        QVERIFY(layer);
        SimulationOptionsDialog dlg(layer->engine(), layer.get(), QStringLiteral("6.0.0"),
                                    nullptr, nullptr);
        // Flip a key on the engine behind the dialog's back, then Apply: the
        // dialog writes its (stale) widget value back and must count it.
        QCOMPARE(swmm_options_set(layer->engine(), "ROUTING_STEP", "17"), 0);
        QVERIFY(QMetaObject::invokeMethod(&dlg, "onApply", Qt::DirectConnection));
        QVERIFY(dlg.wroteAnyChanges());
    }

    void nestedWaterAgeEditorTracksOwningProject_data()
    {
        QTest::addColumn<QString>("action");
        QTest::addColumn<bool>("expectDirty");
        QTest::newRow("accept-edit") << QStringLiteral("edit") << true;
        QTest::newRow("accept-no-op") << QStringLiteral("no-op") << false;
        QTest::newRow("cancel-edit") << QStringLiteral("cancel") << false;
        QTest::newRow("refuse-duplicate-then-cancel") << QStringLiteral("duplicate") << false;
    }

    void nestedWaterAgeEditorTracksOwningProject()
    {
        QFETCH(QString, action);
        QFETCH(bool, expectDirty);
        const QString output = qEnvironmentVariable("SWMMVIS_OPTIONS_CHILD_TEST_OUTPUT",
            QDir(dataDir()).filePath(QStringLiteral("options_child_output")));
        const QString caseDir = QDir(output).filePath(QString::fromLatin1(QTest::currentDataTag()));
        QVERIFY(QDir().mkpath(caseDir));
        const QString ownerPath = QDir(caseDir).filePath(QStringLiteral("owner.inp"));
        const QString otherPath = QDir(caseDir).filePath(QStringLiteral("other.inp"));
        for (const auto &path : {ownerPath, otherPath}) {
            if (QFile::exists(path)) QVERIFY(QFile::remove(path));
            QVERIFY(QFile::copy(fixture(QStringLiteral("typed_selection_fixture.inp")), path));
        }
        const QString ownerFileBefore = readAll(ownerPath);
        QString error;
        auto owner = openProjectWindow(ownerPath, &error);
        QVERIFY2(owner, qPrintable(error));
        auto other = openProjectWindow(otherPath, &error);
        QVERIFY2(other, qPrintable(error));
        SWMM_Engine engine = owner->modelLayer()->engine();
        QVERIFY(engine && other->modelLayer()->engine() != engine);
        QCOMPARE(swmm_water_age_set_global_source(engine, SWMM_AGE_SRC_GW, 4.0), SWMM_OK);
        int overrideCount = -1;
        QCOMPARE(swmm_water_age_override_count(engine, &overrideCount), SWMM_OK);
        QCOMPARE(overrideCount, 0);
        double otherAgeBefore = 0.0;
        QCOMPARE(swmm_water_age_get_global_source(other->modelLayer()->engine(),
            SWMM_AGE_SRC_GW, &otherAgeBefore), SWMM_OK);
        owner->setHasChanges(false);
        other->setHasChanges(false);

        SimulationOptionsDialog outer(engine, owner->modelLayer(), QStringLiteral("6.0.0"),
                                      owner.get(), nullptr);
        auto *launch = outer.findChild<QPushButton *>(QStringLiteral("qt_editAgeSourcesBtn"));
        QVERIFY(launch && launch->isEnabled());
        QVERIFY(!owner->hasChanges());
        QVERIFY(!other->hasChanges());
        bool handledChild = false;
        bool invalidRefused = false;
        bool childWrote = false;
        int childResult = -1;
        QString interactionError;
        QTimer::singleShot(0, &outer, [&] {
            auto *child = qobject_cast<OpenSWMMVis::WaterAgeSourcesDialog *>(QApplication::activeModalWidget());
            if (!child) {
                interactionError = QStringLiteral("Water Age Sources was not the active modal dialog");
                if (auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget())) modal->reject();
                return;
            }
            const auto dismissOnFailure = qScopeGuard([child] { if (child->isVisible()) child->reject(); });
            auto *spin = child->findChild<QDoubleSpinBox *>(
                QStringLiteral("wa_globalSpin_%1").arg(SWMM_AGE_SRC_GW));
            auto *buttons = child->findChild<QDialogButtonBox *>();
            if (!spin || !buttons) {
                interactionError = QStringLiteral("Water Age Sources controls are missing");
                return;
            }
            if (action != QStringLiteral("no-op")) spin->setValue(8.0);
            if (action == QStringLiteral("duplicate")) {
                auto *add = child->findChild<QPushButton *>(QStringLiteral("wa_addBtn"));
                if (!add) { interactionError = QStringLiteral("Override Add button is missing"); return; }
                add->click();
                add->click();
                buttons->button(QDialogButtonBox::Ok)->click();
                auto *validation = child->findChild<QLabel *>(QStringLiteral("wa_validationError"));
                invalidRefused = child->isVisible() && !child->wroteAnyChanges()
                    && child->lastWriteCount() == 0 && validation && !validation->text().isEmpty();
                if (child->isVisible()) buttons->button(QDialogButtonBox::Cancel)->click();
            } else {
                buttons->button(action == QStringLiteral("cancel")
                    ? QDialogButtonBox::Cancel : QDialogButtonBox::Ok)->click();
            }
            childWrote = child->wroteAnyChanges();
            childResult = child->result();
            handledChild = true;
        });
        launch->click(); // Real Quality-page launch, including the child's modal event loop.
        QVERIFY2(interactionError.isEmpty(), qPrintable(interactionError));
        QVERIFY(handledChild);
        if (action == QStringLiteral("duplicate")) QVERIFY(invalidRefused);
        QCOMPARE(childWrote, expectDirty);
        QCOMPARE(childResult, int(action == QStringLiteral("cancel") || action == QStringLiteral("duplicate")
            ? QDialog::Rejected : QDialog::Accepted));
        // This assertion precedes outer Apply/OK/Cancel: the child has already
        // committed, and the project must warn about those unsaved edits now.
        QCOMPARE(owner->hasChanges(), expectDirty);
        QVERIFY(!other->hasChanges());
        double age = 0.0;
        QCOMPARE(swmm_water_age_get_global_source(engine, SWMM_AGE_SRC_GW, &age), SWMM_OK);
        QCOMPARE(age, expectDirty ? 8.0 : 4.0);
        QCOMPARE(swmm_water_age_override_count(engine, &overrideCount), SWMM_OK);
        QCOMPARE(overrideCount, 0);

        outer.reject();
        QCOMPARE(owner->hasChanges(), expectDirty);
        QVERIFY(!other->hasChanges());
        QCOMPARE(swmm_water_age_get_global_source(engine, SWMM_AGE_SRC_GW, &age), SWMM_OK);
        QCOMPARE(age, expectDirty ? 8.0 : 4.0);
        QCOMPARE(swmm_water_age_get_global_source(other->modelLayer()->engine(),
            SWMM_AGE_SRC_GW, &age), SWMM_OK);
        QCOMPARE(age, otherAgeBefore);
        QCOMPARE(readAll(ownerPath), ownerFileBefore); // No Save is implied by either dialog.
    }
};

QTEST_MAIN(TestSimulationOptionsRoundTrip)
#include "test_simulationoptions_roundtrip.moc"
