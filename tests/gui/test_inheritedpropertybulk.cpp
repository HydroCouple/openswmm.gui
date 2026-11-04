#include "layers/inheritedpropertyedits.h"
#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "ui/panels/swmmaggregateselection.h"
#include "ui/panels/propertiespanel.h"
#include "ui/dialogs/inheritedpropertiesdialog.h"
#include "ui/models/userflagsmodel.h"
#include "ui/heatpropertyedit.h"
#include "ui/properties/initialqualityeditbutton.h"
#include "ui/properties/swmmnodepropertyadapter.h"
#include "render/renderers/categorizedrenderer.h"
#include "render/markersymbollayer.h"
#include <openswmm/engine/openswmm_initial_quality.h>
#include <openswmm/engine/openswmm_pollutants.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_edit.h>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTreeView>
#include <QTest>
#include <memory>
using Ref = SWMMObjectRef;
namespace {
QString folder() {
    return QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA")).absoluteFilePath("../../../workplans/bulk-inherited-properties/review");
}
std::unique_ptr<SWMMModelLayer> fixture(bool si = false, bool sidecar = false, bool reactions = false) {
    QDir().mkpath(folder());
    QByteArray text =
        "[OPTIONS]\nFLOW_UNITS CFS\nFLOW_ROUTING DYNWAVE\nSTART_DATE 01/01/2026\nEND_DATE 01/01/2026\nEND_TIME 01:00\nWATER_AGE YES\n"
        "[JUNCTIONS]\nUnused 11 10\nJ1 10 10\nJ2 9 10\nControl 10 10\n[OUTFALLS]\nO1 8 FREE NO\n"
        "[RAINGAGES]\nG1 INTENSITY 0:05 1 TIMESERIES Rain\nG2 INTENSITY 0:05 1 TIMESERIES Rain\nGC INTENSITY 0:05 1 TIMESERIES Rain\n"
        "[TIMESERIES]\nRain 0:00 0\nRain 1:00 0\n"
        "[SUBCATCHMENTS]\nS1 G1 J1 1 30 50 1 0\nS2 G2 J2 2 40 60 2 0\nSC GC Control 1 30 50 1 0\n"
        "[SUBAREAS]\nS1 .01 .1 1 2 0 OUTLET\nS2 .01 .1 1 2 0 OUTLET\nSC .01 .1 1 2 0 OUTLET\n"
        "[INFILTRATION]\nS1 3 .5 4 7 0\nS2 3 .5 4 7 0\nSC 3 .5 4 7 0\n"
        "[CONDUITS]\nUnusedLink Unused J2 100 .013 0 0\nJ1 J1 J2 100 .013 0 0\nC2 J2 O1 100 .014 0 0\nCC Control O1 100 .015 0 0\n"
        "[XSECTIONS]\nUnusedLink CIRCULAR 1 0 0 0 1\nJ1 CIRCULAR 1 0 0 0 1\nC2 CIRCULAR 2 0 0 0 1\nCC CIRCULAR 3 0 0 0 1\n"
        "[POLLUTANTS]\nUnusedP MG/L 0 0 0 0 NO * 0 0\nP1 MG/L 0 0 0 0 NO * 0 7\nP2 UG/L 0 0 0 0 NO * 0 8\n"
        "[INITIAL_QUALITY]\nNODE J1 P1 0\nLINK J1 P1 2.5\nNODE Control P1 9.5\nLINK CC P1 8.5\nNODE J1 P2 3.75\nNODE J1 __WATER_AGE__ 1\nLINK J1 __WATER_AGE__ 2\n"
        "[USER_FLAGS]\nCHECKED BOOLEAN Checked\nCOUNT INTEGER Count\nFACTOR REAL Factor\nLABEL STRING Label\n"
        "[USER_FLAG_VALUES]\nNODE J1 CHECKED NO\nLINK J1 CHECKED YES\nNODE Control CHECKED YES\n"
        "NODE J1 COUNT 0\nNODE J1 FACTOR 0.12345678901234566\nLINK J1 FACTOR 0.9876543210987654\nNODE J1 LABEL \"original text\"\n"
        "[COORDINATES]\nUnused -50 0\nJ1 0 0\nJ2 100 0\nControl 0 50\nO1 200 0\n"
        "[POLYGONS]\nS1 0 -50\nS1 50 -50\nS1 25 -10\nS2 60 -50\nS2 110 -50\nS2 85 -10\nSC 0 60\nSC 50 60\nSC 25 100\n";
    if (si) text.replace("FLOW_UNITS CFS", "FLOW_UNITS CMS");
    if (sidecar) {
        QFile csv(QDir(folder()).filePath("initial-seeds.csv"));
        if (!csv.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
        csv.write("scope,element,constituent,value\nNODE,J2,P1,12\n"); csv.close();
        text += "\n[INITIAL_QUALITY]\nFILE initial-seeds.csv\n";
    }
    if (reactions) {
        QFile rxn(QDir(folder()).filePath("initial-species.rxn"));
        if (!rxn.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
        rxn.write("[REACTION_OPTIONS]\nSOLVER BDF2\nRATE_UNITS HR\n[REACTION_SPECIES]\nBULK A MG\nWALL W MG\n[REACTION_PIPES]\nRATE A 0\nRATE W 0\n"); rxn.close();
        text += "\n[PROCESS_COMPONENTS]\norg.hydrocouple.openswmm.reactions config=\"initial-species.rxn\"\n";
    }
    const auto path = QDir(folder()).filePath(sidecar ? "Source_Owned.inp" : si ? "Inherited_SI.inp" : "Inherited_Properties_Review.inp");
    QFile file(path); if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    file.write(text); file.close();
    auto layer = std::make_unique<SWMMModelLayer>(path);
    QList<QString> warnings, errors;
    if (!layer->loadModel(warnings, errors)) { qWarning() << errors; return {}; }
    if (swmm_options_set(layer->engine(), "HEAT_TRANSPORT", "YES") != SWMM_OK) return {};
    return layer;
}
QVariant quality(SWMM_Engine e, Ref t, const QString &key) {
    const int element = t.objectType == Ref::Link ? swmm_link_index(e, t.name.toUtf8().constData()) : swmm_node_index(e, t.name.toUtf8().constData());
    for (int row = 0; row < swmm_init_quality_count(e); ++row) {
        int link = -1, index = -1; char cons[4096]{}; double value = 0;
        if (swmm_init_quality_get(e, row, &link, &index, cons, sizeof(cons), &value) == SWMM_OK && index == element &&
            (link != 0) == (t.objectType == Ref::Link) && key == QString::fromUtf8(cons)) return value;
    }
    return {};
}
QString scope(Ref t) { return t.objectType == Ref::Node ? "NODE" : t.objectType == Ref::Link ? "LINK" : t.objectType == Ref::Subcatchment ? "SUBCATCHMENT" : "GAGE"; }
QVariant flag(SWMMModelLayer *layer, Ref target, const QString &key) {
    bool found = false; auto value = layer->ensureUserFlagsModel()->value(scope(target), target.name, key, &found);
    return found ? QVariant(value) : QVariant();
}
QVariant heat(SWMM_Engine e, Ref target, int attr) {
    return OpenSWMMVis::authoredHeatProperty(e, attr, target.objectType == Ref::Link ? SWMM_HEAT_SCOPE_LINK : SWMM_HEAT_SCOPE_NODE, target.name);
}
QModelIndex field(SWMMAttributeTableModel &model, const QString &name, const QString &key) {
    return model.index(model.rowForName(name), model.columnKeys().indexOf(key));
}
QModelIndex property(QAbstractItemModel *model, const QString &name, const QModelIndex &parent = {}) {
    for (int r = 0; r < model->rowCount(parent); ++r) {
        auto i = model->index(r, 0, parent);
        if (i.data().toString() == name) return i.siblingAtColumn(1);
        auto child = property(model, name, i); if (child.isValid()) return child;
    }
    return {};
}
QVector<inheritedproperties::Edit> edits(const QList<Ref> &targets, const QString &key, const QVariant &value) {
    QVector<inheritedproperties::Edit> result; for (const auto &t : targets) result << inheritedproperties::Edit{t, key, value}; return result;
}
const QList<Ref> mixed{{Ref::Node, "J1"}, {Ref::Node, "J2"}, {Ref::Link, "J1"}};
}
class TestInheritedPropertyBulk : public QObject {
    Q_OBJECT
private slots:
    void initialValues_data() {
        QTest::addColumn<bool>("si"); QTest::addColumn<QString>("key"); QTest::addColumn<double>("value");
        for (bool si : {false, true}) for (auto key : {"P1", "__WATER_AGE__", "__TEMPERATURE__"})
            QTest::newRow(qPrintable(QString(key) + QString::number(si))) << si << QString(key) << (QString(key) == "P1" ? 0.0 : -2.5);
    }
    void initialValues() {
        QFETCH(bool, si); QFETCH(QString, key); QFETCH(double, value);
        auto layer = fixture(si); QVERIFY(layer); auto e = layer->engine(); QUndoStack undo;
        QList<QVariant> before; for (auto t : mixed) before << quality(e, t, key);
        const auto sibling = quality(e, {Ref::Node, "J1"}, "P2"), control = quality(e, {Ref::Node, "Control"}, "P1");
        SWMMAttributeTableModel source; source.setSource(layer.get(), SWMMModelLayer::CatJunctions);
        const auto cell = field(source, "J1", "initq:" + key);
        const auto displayedBefore = cell.data(); // Prime the same view's sparse-value cache.
        QString error;
        QVERIFY2(source.applyScalarEdits({{SWMMModelLayer::CatJunctions, {"J1", "J2"}}, {SWMMModelLayer::CatConduits, {"J1"}}}, {{"initq:" + key, value}}, &undo, &error), qPrintable(error));
        QCOMPARE(undo.count(), 1); for (auto t : mixed) QCOMPARE(quality(e, t, key).toDouble(), value);
        QCOMPARE(cell.data(), QVariant(value));
        QVERIFY(inheritedproperties::apply(layer.get(), edits(mixed, "initq:" + key, value), &undo)); QCOMPARE(undo.count(), 1);
        undo.undo(); for (int i = 0; i < mixed.size(); ++i) QCOMPARE(quality(e, mixed[i], key), before[i]);
        QCOMPARE(cell.data(), displayedBefore);
        undo.redo(); QCOMPARE(quality(e, {Ref::Node, "J1"}, "P2"), sibling); QCOMPARE(quality(e, {Ref::Node, "Control"}, "P1"), control);
        const auto path = QDir(folder()).filePath(QString("initial-%1-%2.inp").arg(key).arg(si));
        QCOMPARE(swmm_model_write(e, path.toUtf8().constData()), SWMM_OK);
        SWMMModelLayer reopened(path); QList<QString> warnings, errors; QVERIFY(reopened.loadModel(warnings, errors));
        for (auto t : mixed) QCOMPARE(quality(reopened.engine(), t, key), quality(e, t, key));
        QVERIFY(inheritedproperties::apply(layer.get(), edits(mixed, "initq:" + key, ""), &undo));
        for (auto t : mixed) QVERIFY(!quality(e, t, key).isValid());
        undo.undo(); for (auto t : mixed) QCOMPARE(quality(e, t, key).toDouble(), value);
    }
    void heatFields_data() {
        QTest::addColumn<bool>("si"); QTest::addColumn<int>("attr"); QTest::addColumn<double>("value");
        const double values[] = {0, .35, .6, .97, .8, -3.25, 1.23e-7, 0, .25, 2.5, -2.5, 0, 1525, 2001};
        for (bool si : {false, true}) for (int a = 0; a < 14; ++a)
            QTest::newRow(qPrintable(QString("heat-%1-%2").arg(a).arg(si))) << si << a << values[a];
    }
    void heatFields() {
        QFETCH(bool, si); QFETCH(int, attr); QFETCH(double, value);
        auto layer = fixture(si); QVERIFY(layer); auto e = layer->engine(); QUndoStack undo;
        const QList<Ref> targets = attr < 6 ? mixed : QList<Ref>{{Ref::Link, "J1"}, {Ref::Link, "C2"}};
        const auto key = "heat." + QString::fromLatin1(OpenSWMMVis::heatProperties()[attr].key);
        QList<QVariant> before; for (auto t : targets) before << heat(e, t, attr);
        const auto signature = OpenSWMMVis::HeatConfigDialog::configurationSignature(e);
        QString error; QVERIFY2(inheritedproperties::apply(layer.get(), edits(targets, key, value), &undo, &error), qPrintable(error));
        QCOMPARE(undo.count(), 1); for (auto t : targets) QCOMPARE(heat(e, t, attr), QVariant(value));
        QVERIFY(!heat(e, {Ref::Link, "CC"}, attr).isValid());
        QVERIFY(inheritedproperties::apply(layer.get(), edits(targets, key, value), &undo)); QCOMPARE(undo.count(), 1);
        undo.undo(); for (int i = 0; i < targets.size(); ++i) QCOMPARE(heat(e, targets[i], attr), before[i]);
        QCOMPARE(OpenSWMMVis::HeatConfigDialog::configurationSignature(e), signature);
        undo.redo(); for (auto t : targets) QCOMPARE(heat(e, t, attr), QVariant(value));
        const auto path = QDir(folder()).filePath(QString("heat-%1-%2.inp").arg(attr).arg(si));
        QCOMPARE(swmm_model_write(e, path.toUtf8().constData()), SWMM_OK);
        SWMMModelLayer reopened(path); QList<QString> warnings, errors; QVERIFY2(reopened.loadModel(warnings, errors), qPrintable(errors.join(' ')));
        for (auto t : targets) QCOMPARE(heat(reopened.engine(), t, attr), QVariant(value));
        QVERIFY(inheritedproperties::apply(layer.get(), edits(targets, key, ""), &undo)); for (auto t : targets) QVERIFY(!heat(e, t, attr).isValid());
        undo.undo(); for (auto t : targets) QCOMPARE(heat(e, t, attr), QVariant(value));
    }
    void flagTypes_data() {
        QTest::addColumn<bool>("si"); QTest::addColumn<QString>("key"); QTest::addColumn<QVariant>("value");
        for (bool si : {false, true}) {
            QTest::newRow(si ? "bool-SI" : "bool-US") << si << QString("CHECKED") << QVariant(0);
            QTest::newRow(si ? "int-SI" : "int-US") << si << QString("COUNT") << QVariant("-2147483648");
            QTest::newRow(si ? "real-SI" : "real-US") << si << QString("FACTOR") << QVariant("0.7654321098765432");
            QTest::newRow(si ? "text-SI" : "text-US") << si << QString("LABEL") << QVariant("common text");
        }
    }
    void flagTypes() {
        QFETCH(bool, si); QFETCH(QString, key); QFETCH(QVariant, value);
        auto layer = fixture(si); QVERIFY(layer); QUndoStack undo;
        QList<Ref> targets = mixed; targets << Ref{Ref::Subcatchment, "S1"} << Ref{Ref::RainGage, "G1"};
        QList<QVariant> before; for (auto t : targets) before << flag(layer.get(), t, key);
        const auto control = flag(layer.get(), {Ref::Node, "Control"}, "CHECKED");
        QSignalSpy dirty(layer.get(), &SWMMModelLayer::modelEdited);
        QString error; QVERIFY2(inheritedproperties::apply(layer.get(), edits(targets, "userflag:" + key, value), &undo, &error), qPrintable(error));
        QCOMPARE(dirty.count(), 1); QCOMPARE(undo.count(), 1);
        const auto actual = flag(layer.get(), targets.first(), key); QVERIFY(actual.isValid());
        for (auto t : targets) QCOMPARE(flag(layer.get(), t, key), actual);
        QVERIFY(inheritedproperties::apply(layer.get(), edits(targets, "userflag:" + key, value), &undo)); QCOMPARE(undo.count(), 1);
        undo.undo(); for (int i = 0; i < targets.size(); ++i) QCOMPARE(flag(layer.get(), targets[i], key), before[i]);
        undo.redo(); QCOMPARE(flag(layer.get(), {Ref::Node, "Control"}, "CHECKED"), control);
        const auto path = QDir(folder()).filePath(QString("flag-%1-%2.inp").arg(key).arg(si));
        QCOMPARE(swmm_model_write(layer->engine(), path.toUtf8().constData()), SWMM_OK);
        SWMMModelLayer reopened(path); QList<QString> warnings, errors; QVERIFY(reopened.loadModel(warnings, errors));
        for (auto t : targets) QCOMPARE(flag(&reopened, t, key), actual);
        QVERIFY(inheritedproperties::apply(layer.get(), edits(targets, "userflag:" + key, key == "CHECKED" ? QVariant(-1) : QVariant("")), &undo));
        for (auto t : targets) QVERIFY(!flag(layer.get(), t, key).isValid());
        undo.undo(); for (auto t : targets) QCOMPARE(flag(layer.get(), t, key), actual);
    }
    void configuredSpecies_data() { QTest::addColumn<QString>("key"); QTest::newRow("bulk") << QString("A"); QTest::newRow("wall") << QString("W"); }
    void configuredSpecies() {
        QFETCH(QString, key);
        auto layer = fixture(false, false, true); QVERIFY(layer); QUndoStack undo;
        SWMMAttributeTableModel source; source.setSource(layer.get(), SWMMModelLayer::CatJunctions);
        auto index = field(source, "J1", "initq:" + key); QVERIFY(index.isValid());
        QVERIFY(source.columnSpecs()[index.column()].tooltip.contains(key == "W" ? "wall" : "bulk"));
        QString error; QVERIFY2(source.applyScalarEdits({{SWMMModelLayer::CatJunctions, {"J1", "J2"}}, {SWMMModelLayer::CatConduits, {"J1"}}}, {{"initq:" + key, 2.25}}, &undo, &error), qPrintable(error));
        for (auto t : mixed) QCOMPARE(quality(layer->engine(), t, key), QVariant(2.25));
        undo.undo(); for (auto t : mixed) QVERIFY(!quality(layer->engine(), t, key).isValid());
        undo.redo(); auto path = QDir(folder()).filePath("species-" + key + ".inp");
        QCOMPARE(swmm_model_write(layer->engine(), path.toUtf8().constData()), SWMM_OK);
        SWMMModelLayer reopened(path); QList<QString> warnings, errors; QVERIFY2(reopened.loadModel(warnings, errors), qPrintable(errors.join(' ')));
        for (auto t : mixed) QCOMPARE(quality(reopened.engine(), t, key), QVariant(2.25));
    }
    void definitionChangeDuringTableEditAndHeatAliases() {
        auto layer = fixture(); QVERIFY(layer); auto e = layer->engine(); QUndoStack undo;
        SWMMAttributeTableModel source; source.setSource(layer.get(), SWMMModelLayer::CatJunctions); source.setUndoStack(&undo);
        auto index = field(source, "J1", "userflag:CHECKED"); QVERIFY(index.isValid());
        QCOMPARE(swmm_userflag_define(e, "CHECKED", 3, "Changed outside the view"), SWMM_OK);
        QVERIFY(!source.setData(index, 0)); QCOMPARE(flag(layer.get(), {Ref::Node, "J1"}, "CHECKED").toString(), QString("NO"));
        QCOMPARE(undo.count(), 0);
        QCOMPARE(swmm_heat_set_element_override(e, 1, SWMM_HEAT_SCOPE_NODE, "j1", .2), SWMM_OK);
        QCOMPARE(heat(e, {Ref::Node, "J1"}, 1), QVariant(.2));
        QVERIFY(inheritedproperties::apply(layer.get(), edits({{Ref::Node, "J1"}}, "heat.shade", .5), &undo));
        QCOMPARE(heat(e, {Ref::Node, "J1"}, 1), QVariant(.5)); undo.undo(); QCOMPARE(heat(e, {Ref::Node, "J1"}, 1), QVariant(.2));
    }
    void noOpEmptyAndInvalidBatch() {
        auto layer = fixture(); QVERIFY(layer); auto e = layer->engine(); QUndoStack undo;
        const auto old = flag(layer.get(), {Ref::Node, "J1"}, "FACTOR");
        const auto q = quality(e, {Ref::Node, "J1"}, "P1");
        for (auto pair : {qMakePair(QString("userflag:FACTOR"), QVariant("nan")), qMakePair(QString("userflag:FACTOR"), QVariant("3.14junk")),
                          qMakePair(QString("userflag:COUNT"), QVariant("2147483648")), qMakePair(QString("userflag:CHECKED"), QVariant(1.5)),
                          qMakePair(QString("initq:P1"), QVariant(-1)), qMakePair(QString("heat.shade"), QVariant(1.5))}) {
            QVector<inheritedproperties::Edit> batch{{{Ref::Node, "J1"}, "initq:P1", 5}, {{Ref::Node, "J2"}, pair.first, pair.second}};
            QVERIFY(!inheritedproperties::apply(layer.get(), batch, &undo));
            QCOMPARE(quality(e, {Ref::Node, "J1"}, "P1"), q); QCOMPARE(undo.count(), 0);
        }
        QVERIFY(!inheritedproperties::apply(layer.get(), {{{Ref::Node, "J1"}, "userflag:FACTOR", ".5"}, {{Ref::Node, "Missing"}, "userflag:FACTOR", ".5"}}, &undo));
        QCOMPARE(flag(layer.get(), {Ref::Node, "J1"}, "FACTOR"), old);
        QVERIFY(inheritedproperties::apply(layer.get(), {{{Ref::Node, "J2"}, "initq:P1", ""}}, &undo)); QCOMPARE(undo.count(), 0);
        QCOMPARE(swmm_userflag_value_set(e, "NODE", "J1", "LABEL", ""), SWMM_OK);
        SWMMAggregateSelection selection(layer.get(), SWMMModelLayer::CatJunctions, {"J1", "J2"}, &undo, nullptr);
        const int column = selection.source()->columnKeys().indexOf("userflag:LABEL"); QVERIFY(column >= 0);
        QVERIFY(selection.field(column).mixed); QVERIFY(selection.setValue(column, ""));
        QVERIFY(!flag(layer.get(), {Ref::Node, "J1"}, "LABEL").isValid()); QCOMPARE(undo.count(), 1);
        undo.undo(); QVERIFY(flag(layer.get(), {Ref::Node, "J1"}, "LABEL").isValid());
        QCOMPARE(flag(layer.get(), {Ref::Node, "J1"}, "LABEL").toString(), QString());
    }
    void sidecarOwnershipAndProcessSwitches() {
        auto layer = fixture(false, true); QVERIFY(layer); auto e = layer->engine(); QUndoStack undo;
        SWMMAttributeTableModel source; source.setSource(layer.get(), SWMMModelLayer::CatJunctions);
        QVERIFY(!(field(source, "J2", "initq:P1").flags() & Qt::ItemIsEditable));
        const auto a = quality(e, {Ref::Node, "J1"}, "P1"), b = quality(e, {Ref::Node, "J2"}, "P1");
        QVERIFY(!source.applyScalarEdits({"J1", "J2"}, {{"initq:P1", 5}}, &undo, nullptr));
        QVERIFY(!inheritedproperties::apply(layer.get(), edits(mixed, "initq:P1", 5), &undo));
        QCOMPARE(quality(e, {Ref::Node, "J1"}, "P1"), a); QCOMPARE(quality(e, {Ref::Node, "J2"}, "P1"), b); QCOMPARE(undo.count(), 0);
        QFile file(QDir(folder()).filePath("initial-seeds.csv")); QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("scope,element,constituent,value\nNODE,J2,P1,12\n"));
        SWMMAggregateSelection scope(layer.get(), SWMMModelLayer::CatJunctions, {"J1", "J2"}, &undo, nullptr);
        const int age = scope.source()->columnKeys().indexOf("initq:__WATER_AGE__"); QVERIFY(age >= 0);
        QCOMPARE(swmm_options_set(e, "WATER_AGE", "NO"), SWMM_OK); emit layer->optionsChanged({"WATER_AGE"});
        QCOMPARE(scope.field(age).editable, 0); QVERIFY(!scope.setValue(age, 5));
        QVERIFY(!inheritedproperties::apply(layer.get(), edits({{Ref::Node, "J1"}}, "initq:__WATER_AGE__", 5), &undo));
        QVERIFY(!inheritedproperties::apply(layer.get(), edits({{Ref::Node, "J1"}}, "heat.bed_thickness", 5), &undo));
    }
    void typedNamesRenameReindexAndStale() {
        auto layer = fixture(); QVERIFY(layer); auto e = layer->engine(); QUndoStack undo;
        const auto q = quality(e, {Ref::Node, "J1"}, "P1"), f = flag(layer.get(), {Ref::Node, "J1"}, "FACTOR");
        QVERIFY(inheritedproperties::apply(layer.get(), {{{Ref::Node, "J1"}, "initq:P1", 6.25}, {{Ref::Node, "J1"}, "userflag:FACTOR", ".5"}, {{Ref::Link, "J1"}, "initq:P1", 7.25}}, &undo));
        QVERIFY(layer->applyNodeDelete("Unused"));
        QVERIFY(layer->applyRename("J1", "Renamed", SWMMModelLayer::kKindNode));
        undo.undo(); QCOMPARE(quality(e, {Ref::Node, "Renamed"}, "P1"), q); QCOMPARE(flag(layer.get(), {Ref::Node, "Renamed"}, "FACTOR"), f);
        QCOMPARE(quality(e, {Ref::Link, "J1"}, "P1"), QVariant(2.5)); undo.redo();
        QCOMPARE(swmm_init_quality_set(e, 1, swmm_link_index(e, "J1"), "P1", 99), SWMM_OK);
        undo.undo(); QCOMPARE(quality(e, {Ref::Node, "Renamed"}, "P1"), QVariant(6.25));
        undo.clear(); QVERIFY(inheritedproperties::apply(layer.get(), edits({{Ref::Node, "J2"}}, "initq:P1", 3), &undo));
        QVERIFY(layer->applyNodeDelete("J2")); QVERIFY(layer->applyNodeAdd("J2", 0, 50, 0));
        undo.undo(); QVERIFY(!quality(e, {Ref::Node, "J2"}, "P1").isValid());
    }
    void definitionsAndClosedEditor() {
        auto layer = fixture(); QVERIFY(layer); QUndoStack undo;
        auto *flags = layer->ensureUserFlagsModel();
        QVERIFY(inheritedproperties::apply(layer.get(), edits({{Ref::Node, "J1"}}, "userflag:COUNT", 5), &undo));
        SWMMAggregateSelection scope(layer.get(), SWMMModelLayer::CatJunctions, {"J1", "J2"}, &undo, nullptr);
        const int column = scope.source()->columnKeys().indexOf("userflag:COUNT");
        QVERIFY(flags->define("COUNT", openswmmvis::ui::UserFlagsModel::FlagType::String, "Changed"));
        QCOMPARE(scope.field(column).editable, 0); undo.undo(); QCOMPARE(flag(layer.get(), {Ref::Node, "J1"}, "COUNT").toString(), QString("5"));
        undo.clear(); QVERIFY(inheritedproperties::apply(layer.get(), edits({{Ref::Node, "J1"}}, "userflag:FACTOR", ".75"), &undo));
        auto *dialog = openInheritedPropertiesDialog(layer.get(), {{Ref::Node, "J1"}}, false, nullptr, &undo); QVERIFY(dialog);
        QPointer<QDialog> guard = dialog; layer.reset(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!guard || !guard->isVisible()); undo.undo();
    }
    void heatSiblingAndExternalChanges() {
        auto layer = fixture(); QVERIFY(layer); auto e = layer->engine(); QUndoStack undo;
        QString error;
        QVERIFY2(OpenSWMMVis::applyHeatPropertyEdits(layer.get(), {{1, SWMM_HEAT_SCOPE_NODE, "J1", .5}}, &undo, "Shade", &error), qPrintable(error));
        QCOMPARE(undo.count(), 1);
        QCOMPARE(swmm_heat_set_element_override(e, 0, SWMM_HEAT_SCOPE_LINK, "CC", .9), SWMM_OK);
        undo.undo(); QVERIFY(!heat(e, {Ref::Node, "J1"}, 1).isValid()); QCOMPARE(heat(e, {Ref::Link, "CC"}, 0), QVariant(.9));
        undo.redo(); QCOMPARE(heat(e, {Ref::Node, "J1"}, 1), QVariant(.5));
        QVERIFY(layer->applyRename("J1", "RenamedHeat", SWMMModelLayer::kKindNode));
        undo.undo(); QVERIFY(!heat(e, {Ref::Node, "RenamedHeat"}, 1).isValid()); QCOMPARE(heat(e, {Ref::Link, "CC"}, 0), QVariant(.9));
        undo.redo(); QCOMPARE(heat(e, {Ref::Node, "RenamedHeat"}, 1), QVariant(.5));
        QCOMPARE(swmm_heat_set_element_override(e, 1, SWMM_HEAT_SCOPE_NODE, "RenamedHeat", .7), SWMM_OK);
        undo.undo(); QCOMPARE(heat(e, {Ref::Node, "RenamedHeat"}, 1), QVariant(.7));
    }
    void propertiesChoicesAndUntouchedMixed() {
        auto layer = fixture(); QVERIFY(layer); MapCanvas canvas; layer->setEditCanvas(&canvas);
        PropertiesPanel panel; panel.setProject(layer.get());
        panel.showSelection(&canvas, {{Ref::Node, "J1"}, {Ref::Node, "J2"}}); panel.resize(1000, 1000); panel.show();
        auto *tree = panel.findChild<QTreeView *>(); QVERIFY(tree); tree->expandAll();
        auto index = ::property(tree->model(), "CHECKED"); QVERIFY(index.isValid()); QVERIFY(index.flags() & Qt::ItemIsEditable);
        tree->edit(index); auto *combo = tree->findChild<QComboBox *>(); QVERIFY(combo);
        const int choice = combo->findData(0); QVERIFY(choice >= 0); combo->setCurrentIndex(choice); QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, choice));
        QTRY_COMPARE(canvas.undoStack()->count(), 1);
        QCOMPARE(flag(layer.get(), {Ref::Node, "J2"}, "CHECKED").toString(), QString("NO"));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        index = ::property(tree->model(), "Init. P1"); QVERIFY(index.isValid()); tree->edit(index);
        auto *line = qobject_cast<QLineEdit *>(tree->indexWidget(index)); QVERIFY(line);
        line->setFocus(); QTest::keyClick(line, Qt::Key_Tab); QCOMPARE(canvas.undoStack()->count(), 1);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        tree->edit(index); line = qobject_cast<QLineEdit *>(tree->indexWidget(index)); QVERIFY(line);
        line->setFocus(); line->selectAll(); QTest::keyClicks(line, "8.25"); QTest::keyClick(line, Qt::Key_Escape);
        QCOMPARE(canvas.undoStack()->count(), 1); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        tree->edit(index); line = qobject_cast<QLineEdit *>(tree->indexWidget(index)); QVERIFY(line);
        line->setFocus(); line->selectAll(); QTest::keyClicks(line, "8.25"); QTest::keyClick(line, Qt::Key_Tab);
        QTRY_COMPARE(canvas.undoStack()->count(), 2);
        QCOMPARE(quality(layer->engine(), {Ref::Node, "J1"}, "P1"), QVariant(8.25)); QCOMPARE(quality(layer->engine(), {Ref::Node, "J2"}, "P1"), QVariant(8.25));
    }
    void mixedSpatialPropertiesScope()
    {
        auto layer = fixture(); QVERIFY(layer); MapCanvas canvas; layer->setEditCanvas(&canvas);
        PropertiesPanel panel; panel.setProject(layer.get());
        const QList<Ref> targets{{Ref::Node, "J1"}, {Ref::Link, "J1"}, {Ref::Subcatchment, "S1"}, {Ref::RainGage, "G1"}};
        panel.showSelection(&canvas, targets); panel.resize(1000, 1000); panel.show();
        auto *tree = panel.findChild<QTreeView *>(); QVERIFY(tree); tree->expandAll();
        auto index = ::property(tree->model(), "CHECKED"); QVERIFY(index.isValid()); QVERIFY(index.flags() & Qt::ItemIsEditable);
        tree->edit(index); auto *combo = tree->findChild<QComboBox *>(); QVERIFY(combo);
        int choice = combo->findData(1); combo->setCurrentIndex(choice); QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, choice));
        QTRY_COMPARE(canvas.undoStack()->count(), 1);
        for (auto t : targets) QCOMPARE(flag(layer.get(), t, "CHECKED").toString(), QString("YES"));
        QVERIFY(!flag(layer.get(), {Ref::Node, "J2"}, "CHECKED").isValid());
        canvas.undoStack()->undo(); QCOMPARE(flag(layer.get(), {Ref::Node, "J1"}, "CHECKED").toString(), QString("NO"));
    }
    void ownedDialogsCommitAndUndoAfterClosing() {
        auto layer = fixture(); QVERIFY(layer); MapCanvas canvas; layer->setEditCanvas(&canvas);
        SWMMJunctionPropertyAdapter adapter(layer->engine(), "J1"); adapter.setModelLayer(layer.get());
        InitialQualityEditButton button; button.setValue(adapter.initialQualityRef()); button.show();
        auto *open = button.findChild<QPushButton *>(); QVERIFY(open); open->click();
        auto *dialog = button.window()->findChild<QDialog *>("inheritedPropertiesDialog"); QVERIFY(dialog);
        auto *table = dialog->findChild<QTableView *>(); QVERIFY(table);
        auto *buttons = dialog->findChild<QDialogButtonBox *>(); QVERIFY(buttons); QCOMPARE(buttons->standardButtons(), QDialogButtonBox::StandardButtons(QDialogButtonBox::Close));
        int row = -1; for (int r = 0; r < table->model()->rowCount(); ++r) if (table->model()->index(r, 0).data().toString() == "Init. P1") row = r;
        QVERIFY(row >= 0); auto index = table->model()->index(row, 1); table->edit(index);
        auto *line = table->findChild<QLineEdit *>(); QVERIFY(line); line->setFocus(); line->selectAll(); QTest::keyClicks(line, "4.5"); QTest::keyClick(line, Qt::Key_Return);
        QTRY_COMPARE(canvas.undoStack()->count(), 1); QCOMPARE(quality(layer->engine(), {Ref::Node, "J1"}, "P1"), QVariant(4.5));
        dialog->close(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        canvas.undoStack()->undo(); QCOMPARE(quality(layer->engine(), {Ref::Node, "J1"}, "P1"), QVariant(0.0));
        dialog = openInheritedPropertiesDialog(layer.get(), mixed, false, button.window(), canvas.undoStack()); QVERIFY(dialog);
        table = dialog->findChild<QTableView *>(); QVERIFY(table);
        row = -1; for (int r = 0; r < table->model()->rowCount(); ++r) if (table->model()->index(r, 0).data().toString() == "CHECKED") row = r;
        QVERIFY(row >= 0); index = table->model()->index(row, 1); table->edit(index);
        auto *combo = table->findChild<QComboBox *>(); QVERIFY(combo); QTest::keyClick(combo, Qt::Key_Escape);
        QCOMPARE(quality(layer->engine(), {Ref::Node, "J1"}, "P1"), QVariant(0.0)); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        table->edit(index); combo = table->findChild<QComboBox *>(); QVERIFY(combo); int choice = combo->findData(0); combo->setCurrentIndex(choice); QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, choice));
        QTRY_COMPARE(canvas.undoStack()->count(), 1); for (auto t : mixed) QCOMPARE(flag(layer.get(), t, "CHECKED").toString(), QString("NO"));
        table->setCurrentIndex(index); dialog->findChild<QPushButton *>("clearInheritedProperty")->click();
        QTRY_COMPARE(canvas.undoStack()->count(), 2); for (auto t : mixed) QVERIFY(!flag(layer.get(), t, "CHECKED").isValid());
        dialog->close(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); canvas.undoStack()->undo();
        for (auto t : mixed) QCOMPARE(flag(layer.get(), t, "CHECKED").toString(), QString("NO"));
    }
    void flagRenderingAndSortedTable() {
        auto layer = fixture(); QVERIFY(layer); auto e = layer->engine(); QUndoStack undo;
        auto renderer = std::make_unique<OpenSWMM::Render::CategorizedRenderer>(); renderer->setClassifyAttribute("uf:CHECKED");
        OpenSWMM::Render::MarkerSymbolLayerSpec marker; OpenSWMM::Render::SymbolStyle red, green;
        marker.fillColor = Qt::red; red.layers << marker.toSymbolLayer(); marker.fillColor = Qt::green; green.layers << marker.toSymbolLayer();
        renderer->addCategory({QStringLiteral("false"), "NO", red}); renderer->setFallbackSymbol(green); layer->setKindRenderer(SWMMModelLayer::CatJunctions, std::move(renderer));
        const int index = swmm_node_index(e, "J1"), control = swmm_node_index(e, "Control");
        QCOMPARE(layer->featureColor(SWMMModelLayer::CatJunctions, index), QColor(Qt::red));
        SWMMAttributeTableModel source; source.setSource(layer.get(), SWMMModelLayer::CatJunctions); source.setUndoStack(&undo);
        QSortFilterProxyModel proxy; proxy.setSourceModel(&source); proxy.setFilterKeyColumn(0); proxy.setFilterRegularExpression("^J[12]$"); proxy.sort(0, Qt::DescendingOrder);
        auto cell = proxy.index(1, source.columnKeys().indexOf("userflag:CHECKED"));
        QCOMPARE(source.objectNameAt(proxy.mapToSource(cell).row()), QString("J1")); QVERIFY(proxy.setData(cell, 1));
        QCoreApplication::processEvents(); QCOMPARE(layer->featureColor(SWMMModelLayer::CatJunctions, index), QColor(Qt::green));
        QCOMPARE(layer->featureColor(SWMMModelLayer::CatJunctions, control), QColor(Qt::green));
        undo.undo(); QCoreApplication::processEvents(); QCOMPARE(layer->featureColor(SWMMModelLayer::CatJunctions, index), QColor(Qt::red));
    }
    void reviewFixture() {
        auto layer = fixture(false, false, true); QVERIFY(layer);
        QString error;
        QVERIFY2(inheritedproperties::apply(layer.get(), edits({{Ref::Node, "J1"}}, "heat.albedo", .2), nullptr, &error), qPrintable(error));
        const auto path = QDir(folder()).filePath("Inherited_Properties_Review_Ready.inp");
        QCOMPARE(swmm_model_write(layer->engine(), path.toUtf8().constData()), SWMM_OK);
        SWMMModelLayer reopened(path); QList<QString> warnings, errors;
        QVERIFY2(reopened.loadModel(warnings, errors), qPrintable(errors.join(' ')));
        QCOMPARE(heat(reopened.engine(), {Ref::Node, "J1"}, 0), QVariant(.2));
    }
};
QTEST_MAIN(TestInheritedPropertyBulk)
#include "test_inheritedpropertybulk.moc"
