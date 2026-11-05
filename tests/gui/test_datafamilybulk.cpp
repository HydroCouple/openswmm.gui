#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "ui/panels/dataobjectaggregateselection.h"
#include "ui/panels/propertiespanel.h"
#include "ui/panels/objectbrowserpanel.h"
#include "ui/panels/swmmobjecttreemodel.h"
#include "selection/selectionmanager.h"
#include "ui/properties/swmmpollutantpropertyadapter.h"
#include "ui/properties/dataobjectref.h"
#include "pollutant/pollutantregistry.h"
#include "pollutant/pollutantprovider.h"
#include "curve/curveregistry.h"
#include "curve/curveprovider.h"
#include "pattern/patternregistry.h"
#include "pattern/patternprovider.h"
#include <openswmm/engine/openswmm_infrastructure.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_pollutants.h>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QTextStream>
#include <QTreeView>
#include <QTest>
#include <memory>
using DC = SWMMModelLayer;
namespace
{
QString folder()
{
    return QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA"))
        .absoluteFilePath("../../../workplans/bulk-data-families/review");
}
std::unique_ptr<DC> fixture(bool si = false)
{
    QDir().mkpath(folder());
    QFile source(QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA"))
                     .filePath("data_object_table_fixture.inp"));
    if (!source.open(QIODevice::ReadOnly))
        return {};
    auto text = source.readAll();
    if (si)
        text.replace("CFS", "CMS");
    const QByteArray additions =
        "\n[POLLUTANTS]\nP2 MG/L 0 0 0 .1 NO * 0 0 0\nShared MG/L 0 0 0 .2 NO * 0 0 0\nControl MG/L 0 0 0 .9 NO * 0 0 0\n"
        "[LANDUSES]\nShared 14 .25 0\nControl 21 .9 0\n"
        "[AQUIFERS]\nShared .5 .1 .3 .2 12 15 .35 14 .002 0 3.5 .4\nControl .5 .1 .3 .3 12 15 .35 14 .002 0 3.5 .4\n"
        "[STREETS]\nShared 25 .5 3 .017 0 0 1 0 0 0\nControl 40 .75 4 .04 0 0 2 0 0 0\n"
        "[INLETS]\nG2 GRATE 2 2 GENERIC .6 .5\nShared GRATE 3 3 GENERIC .7 .6\nG3 GRATE 4 4 GENERIC .8 .7\nControl GRATE 5 5 GENERIC .9 .8\n"
        "C2 CURB 4 .6 VERTICAL\nC3 CURB 5 .7 VERTICAL\nCCtrl CURB 6 .8 VERTICAL\n"
        "S1 SLOTTED 1 .1\nS2 SLOTTED 2 .2\nS3 SLOTTED 3 .3\nSCtrl SLOTTED 4 .4\n"
        "[PATTERNS]\nMonthB MONTHLY 1 1 1 1 1 1 1 1 1 1 1 1\nBad DAILY 1 1 1 1 1 1 1\n"
        "[TIMESERIES]\nCap 0:00 1\nCap 1:00 1\n[CURVES]\nCap DIVERSION 0 0\nCap 10 5\nCapB RATING 0 0\nCapB 2 1\nWrong STORAGE 0 1\nWrong 2 3\n";
    // Keep one section per family: repeated definition sections can reset
    // earlier native records, erasing the deliberately different starting values.
    QByteArray section, rows;
    auto appendSection = [&] {
        if (section.isEmpty())
            return;
        const auto marker = section + '\n';
        const auto position = text.indexOf(marker);
        if (position >= 0)
            text.insert(position + marker.size(), rows);
        else
            text += '\n' + marker + rows;
    };
    for (const auto &line : additions.split('\n')) {
        if (line.startsWith('[')) {
            appendSection();
            section = line;
            rows.clear();
        } else if (!section.isEmpty())
            rows += line + '\n';
    }
    appendSection();
    const auto path =
        QDir(folder()).filePath(si ? "Data_Families_SI.inp" : "Data_Families_Review.inp");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};
    file.write(text);
    file.close();
    auto layer = std::make_unique<DC>(path);
    QList<QString> warnings, errors;
    if (!layer->loadModel(warnings, errors)) {
        qWarning() << errors;
        return {};
    }
    return layer;
}
int column(const DataObjectAttributeTableModel &model, const QString &key)
{
    const auto fields = model.columnSpecs();
    for (int c = 0; c < fields.size(); ++c)
        if (fields[c].key == key)
            return c;
    return -1;
}
QStringList targets(DC::DataCategory cat, const QString &key = {})
{
    switch (cat) {
    case DC::DataPollutants:
        return {"TSS", "P2", "Shared"};
    case DC::DataLandUses:
        return {"Residential", "Commercial", "Shared"};
    case DC::DataAquifers:
        return {"AQ1", "AQ2", "Shared"};
    case DC::DataStreets:
        return {"ST1", "ST2", "Shared"};
    case DC::DataInlets:
        if (key.startsWith("curb") || key == "throat")
            return {"CURB1", "C2", "C3"};
        if (key.startsWith("slot"))
            return {"S1", "S2", "S3"};
        return {"G2", "Shared", "G3"};
    default:
        return {};
    }
}
QVariantMap state(DC *layer, DC::DataCategory cat, const QString &name)
{
    DataObjectAttributeTableModel model;
    model.setSource(layer, cat);
    model.refreshObject(name);
    QVariantMap result;
    const int row = model.rowForName(name);
    const auto fields = model.columnSpecs();
    for (int c = 1; c < fields.size(); ++c)
        if(fields[c].key!="records")result[fields[c].key] = model.index(row, c).data(Qt::EditRole);
    if (cat == DC::DataInlets) {
        SWMM_InletDesign d{};
        swmm_inlet_get_design(layer->engine(),
                              swmm_inlet_index(layer->engine(), name.toUtf8().constData()), &d);
        result = {{"type", d.type},
                  {"grateLength", d.grate_length},
                  {"grateWidth", d.grate_width},
                  {"grateType", d.grate_type},
                  {"openArea", d.open_area},
                  {"splashVeloc", d.splash_veloc},
                  {"curbLength", d.curb_length},
                  {"curbHeight", d.curb_height},
                  {"throat", d.throat},
                  {"slotLength", d.slot_length},
                  {"slotWidth", d.slot_width},
                  {"curveId", QString::fromUtf8(d.curve_id)},
                  {"curveKind", d.curve_kind}};
    }
    return result;
}
QModelIndex property(QAbstractItemModel *model, const QString &label,
                     const QModelIndex &parent = {})
{
    for (int r = 0; r < model->rowCount(parent); ++r) {
        const auto i = model->index(r, 0, parent);
        if (i.data().toString() == label)
            return i.siblingAtColumn(1);
        const auto child = property(model, label, i);
        if (child.isValid())
            return child;
    }
    return {};
}
void fieldsData()
{
    QTest::addColumn<int>("category");
    QTest::addColumn<QString>("key");
    QTest::addColumn<QVariant>("value");
    QTest::addColumn<bool>("si");
    auto add = [](DC::DataCategory cat, const char *key, const QVariant &value) {
        for (bool si : {false, true})
            QTest::newRow(qPrintable(QString("%1-%2-%3").arg(int(cat)).arg(key).arg(si)))
                << int(cat) << QString(key) << value << si;
    };
    for (auto key : {"rainConc", "gwConc", "rdiiConc", "initConc", "kDecay", "mwt", "coFraction"})
        add(DC::DataPollutants, key, 1.25);
    add(DC::DataPollutants, "snowOnly", 1);
    add(DC::DataPollutants, "coPollutant", "Control");
    add(DC::DataLandUses, "sweepInterval", 4.5);
    add(DC::DataLandUses, "sweepRemoval", .75);
    for (auto key : {"porosity", "wiltingPoint", "fieldCapacity", "upperEvapFrac", "upperMoisture"})
        add(DC::DataAquifers, key, .5);
    for (auto key : {"conductivity", "conductSlope", "tensionSlope", "lowerEvapDepth",
                     "lowerLossCoeff", "bottomElev", "waterTableElev"})
        add(DC::DataAquifers, key, 1.25);
    add(DC::DataAquifers, "evapPattern", "MonthB");
    for (auto key : {"crownWidth", "curbHeight", "crossSlope", "roadRoughness", "gutterDepression",
                     "gutterWidth", "backingWidth", "backingSlope", "backingRoughness"})
        add(DC::DataStreets, key, 1.25);
    add(DC::DataStreets, "sides", 2);
    for (auto key : {"grateLength", "grateWidth", "splashVeloc", "curbLength", "curbHeight",
                     "slotLength", "slotWidth"})
        add(DC::DataInlets, key, 1.25);
    add(DC::DataInlets, "openArea", .5);
    add(DC::DataInlets, "grateType", 0);
    add(DC::DataInlets, "throat", 0);
    add(DC::DataInlets, "type", SWMM_INLET_DROP_GRATE);
}
}
class TestDataFamilyBulk : public QObject
{
    Q_OBJECT
private slots:
    void everyFieldAndPersistence_data() { fieldsData(); }
    void everyFieldAndPersistence()
    {
        QFETCH(int, category);
        QFETCH(QString, key);
        QFETCH(QVariant, value);
        QFETCH(bool, si);
        auto layer = fixture(si);
        QVERIFY(layer);
        const auto cat = DC::DataCategory(category);
        QUndoStack undo;
        const auto names = targets(cat, key);
        QList<QVariantMap> before;
        for (const auto &name : names)
            before << state(layer.get(), cat, name);
        const auto control = state(layer.get(), cat, "Control");
        QSignalSpy dirty(layer.get(), &DC::modelEdited);
        QString error;
        QVERIFY2(DataObjectAttributeTableModel::applyValues(layer.get(), cat, names, key, value,
                                                            &undo, &error),
                 qPrintable(error));
        QCOMPARE(undo.count(), 1);
        QCOMPARE(dirty.count(), 1);
        QList<QVariantMap> after;
        int n = 0;
        for (const auto &name : names) {
            auto expected = before[n++];
            expected[key] = value;
            after << state(layer.get(), cat, name);
            QCOMPARE(after.last(), expected);
        }
        QCOMPARE(state(layer.get(), cat, "Control"), control);
        QVERIFY(
            DataObjectAttributeTableModel::applyValues(layer.get(), cat, names, key, value, &undo));
        QCOMPARE(undo.count(), 1);
        QCOMPARE(dirty.count(), 1);
        undo.undo();
        n = 0;
        for (const auto &name : names)
            QCOMPARE(state(layer.get(), cat, name), before[n++]);
        undo.redo();
        const auto path =
            QDir(folder()).filePath(QString("saved-%1-%2-%3.inp").arg(category).arg(key).arg(si));
        QCOMPARE(swmm_model_write(layer->engine(), path.toUtf8().constData()), SWMM_OK);
        DC reopened(path);
        QList<QString> warnings, errors;
        QVERIFY2(reopened.loadModel(warnings, errors), qPrintable(errors.join(' ')));
        n = 0;
        for (const auto &name : names) {
            auto persisted = after[n++];
            if (cat == DC::DataInlets && key == "grateType") {
                // INP omits Generic-only fields for a standard grate. They
                // remain untouched in the live record and its Undo snapshot.
                persisted["openArea"] = 0.0;
                persisted["splashVeloc"] = 0.0;
            }
            QCOMPARE(state(&reopened, cat, name), persisted);
        }
        QCOMPARE(state(&reopened, cat, "Control"), control);
    }
    void propertiesKeyboardScopesAndIndependentUndo()
    {
        auto layer = fixture();
        QVERIFY(layer);
        MapCanvas canvas;
        layer->setEditCanvas(&canvas);
        PropertiesPanel panel;
        panel.setProject(layer.get());
        panel.showSelection(&canvas, {{SWMMObjectRef::Pollutant, "TSS"},
                                      {SWMMObjectRef::Pollutant, "P2"},
                                      {SWMMObjectRef::Pollutant, "Shared"}});
        panel.resize(1000, 1000);
        panel.show();
        auto *tree = panel.findChild<QTreeView *>();
        QVERIFY(tree);
        tree->expandAll();
        auto cell = ::property(tree->model(), "Decay Coeff. (1/days)");
        QVERIFY(cell.isValid());
        QVERIFY(cell.flags() & Qt::ItemIsEditable);
        for (auto key : {Qt::Key_Tab, Qt::Key_Escape}) {
            tree->edit(cell);
            auto *spin = tree->findChild<QDoubleSpinBox *>();
            QVERIFY(spin);
            spin->setFocus();
            if (key == Qt::Key_Escape) {
                spin->selectAll();
                QTest::keyClicks(spin, ".7");
            }
            QTest::keyClick(spin, key);
            QCOMPARE(canvas.undoStack()->count(), 0);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
        tree->edit(cell);
        auto *spin = tree->findChild<QDoubleSpinBox *>();
        QVERIFY(spin);
        spin->setFocus();
        spin->selectAll();
        QTest::keyClicks(spin, ".375");
        QTest::keyClick(spin, Qt::Key_Return);
        QTRY_COMPARE(canvas.undoStack()->count(), 1);
        for (auto name : {"TSS", "P2", "Shared"})
            QCOMPARE(state(layer.get(), DC::DataPollutants, name).value("kDecay").toDouble(), .375);
        DataObjectAttributeTableModel table;
        table.setSource(layer.get(), DC::DataPollutants);
        QVERIFY(
            table.setData(table.index(table.rowForName("TSS"), column(table, "rainConc")), 2.5));
        canvas.undoStack()->undo();
        QCOMPARE(state(layer.get(), DC::DataPollutants, "TSS").value("rainConc").toDouble(), 2.5);
        QCOMPARE(state(layer.get(), DC::DataPollutants, "TSS").value("kDecay").toDouble(), 0.0);
        panel.showSelection(&canvas, {{SWMMObjectRef::Pollutant, "Shared"},
                                      {SWMMObjectRef::Street, "Shared"},
                                      {SWMMObjectRef::Node, "J1"}});
        auto *scope = panel.findChild<QComboBox *>("propertiesSelectionScope");
        QVERIFY(scope);
        QCOMPARE(scope->count(), 4);
        int streets = -1;
        for (int i = 1; i < scope->count(); ++i)
            if (scope->itemText(i).startsWith("Streets"))
                streets = i;
        QVERIFY(streets > 0);
        scope->setCurrentIndex(streets);
        QVERIFY(::property(tree->model(), "Road Roughness").isValid());
    }
    void objectBrowserSelectionReachesProperties()
    {
        auto layer = fixture();
        QVERIFY(layer);
        MapCanvas canvas;
        layer->setEditCanvas(&canvas);
        SelectionManager selection;
        ObjectBrowserPanel browser;
        PropertiesPanel panel;
        panel.setProject(layer.get());
        browser.setProject(layer.get(), &selection, &canvas);
        connect(
            &selection, &SelectionManager::selectionChanged, &panel,
            [&](const QSet<SWMMObjectRef> &current, const QSet<SWMMObjectRef> &,
                const QSet<SWMMObjectRef> &) { panel.showSelection(&canvas, current.values()); });
        auto *tree = browser.findChild<QTreeView *>();
        QVERIFY(tree);
        QItemSelection picks;
        std::function<void(const QModelIndex &)> collect = [&](const QModelIndex &parent) {
            for (int row = 0; row < tree->model()->rowCount(parent); ++row) {
                const auto index = tree->model()->index(row, 0, parent);
                const auto ref =
                    index.data(SWMMObjectTreeModel::RoleObjectRef).value<SWMMObjectRef>();
                if (ref.objectType == SWMMObjectRef::Pollutant &&
                    targets(DC::DataPollutants).contains(ref.name))
                    picks.select(index, index);
                collect(index);
            }
        };
        collect({});
        QCOMPARE(picks.size(), 3);
        tree->selectionModel()->select(picks, QItemSelectionModel::ClearAndSelect |
                                                  QItemSelectionModel::Rows);
        QCOMPARE(selection.selection().size(), 3);
        auto *properties = panel.findChild<QTreeView *>();
        QVERIFY(properties);
        const auto decay = ::property(properties->model(), "Decay Coeff. (1/days)");
        QVERIFY(decay.isValid());
        QVERIFY(properties->model()->setData(decay, .625));
        QCOMPARE(canvas.undoStack()->count(), 1);
        for (const auto &name : targets(DC::DataPollutants))
            QCOMPARE(state(layer.get(), DC::DataPollutants, name).value("kDecay").toDouble(), .625);
        QCOMPARE(state(layer.get(), DC::DataPollutants, "Control").value("kDecay").toDouble(), .9);
    }
    void strictFailureNoopAndMixedUnits()
    {
        auto layer = fixture();
        QVERIFY(layer);
        QUndoStack undo;
        QString error;
        QVERIFY(DataObjectAttributeTableModel::applyValues(layer.get(), DC::DataPollutants,
                                                           {"TSS", "P2"}, "kDecay", .5, &undo));
        undo.undo();
        QVERIFY(undo.canRedo());
        const auto before = state(layer.get(), DC::DataPollutants, "TSS");
        QSignalSpy dirty(layer.get(), &DC::modelEdited);
        QVERIFY(!DataObjectAttributeTableModel::applyValues(
            layer.get(), DC::DataPollutants, {"TSS", "Missing"}, "kDecay", .3, &undo, &error));
        QCOMPARE(state(layer.get(), DC::DataPollutants, "TSS"), before);
        QCOMPARE(dirty.count(), 0);
        QVERIFY(undo.canRedo());
        QVERIFY(!DataObjectAttributeTableModel::applyValues(
            layer.get(), DC::DataPollutants, {"TSS", "Lead"}, "rainConc", 1, &undo, &error));
        QVERIFY(error.contains("units"));
        DataObjectAggregateSelection mixed(layer.get(), DC::DataPollutants, {"TSS", "Lead"}, &undo);
        QCOMPARE(mixed.field(column(*mixed.source(), "rainConc")).editable, 0);
        DataObjectAttributeTableModel table;
        table.setSource(layer.get(), DC::DataPollutants);
        table.setUndoStack(&undo);
        QVERIFY(!table.setDataForRows(column(table, "kDecay"), {table.rowForName("TSS"), -1}, .3,
                                      &error));
        QVERIFY(undo.canRedo());
        QVERIFY(
            table.setDataForRows(column(table, "kDecay"), {table.rowForName("TSS")}, 0, &error));
        QVERIFY(undo.canRedo());
        QCOMPARE(dirty.count(), 0);
        for (auto value : {QVariant("nan"), QVariant(-1), QVariant("junk")})
            QVERIFY(!DataObjectAttributeTableModel::applyValues(layer.get(), DC::DataStreets,
                                                                targets(DC::DataStreets),
                                                                "roadRoughness", value, &undo));
        const auto g = state(layer.get(), DC::DataInlets, "G2");
        QVERIFY(!DataObjectAttributeTableModel::applyValues(layer.get(), DC::DataInlets,
                                                            {"G2", "Shared", "CURB1"}, "type",
                                                            SWMM_INLET_DROP_GRATE, &undo, &error));
        QCOMPARE(state(layer.get(), DC::DataInlets, "G2"), g);
        QCOMPARE(dirty.count(), 0);
    }
    void choicesSubsetsAndSourceUnits()
    {
        auto layer = fixture();
        auto si = fixture(true);
        QVERIFY(layer && si);
        MapCanvas canvas;
        layer->setEditCanvas(&canvas);
        PropertiesPanel panel;
        panel.setProject(layer.get());
        panel.showSelection(&canvas, {{SWMMObjectRef::Pollutant, "TSS"},
                                      {SWMMObjectRef::Pollutant, "P2"},
                                      {SWMMObjectRef::Pollutant, "Lead"}});
        panel.resize(1000, 1000);
        panel.show();
        auto *tree = panel.findChild<QTreeView *>();
        auto *scope = panel.findChild<QComboBox *>("propertiesSelectionScope");
        QVERIFY(tree && scope);
        int all = -1, mg = -1;
        for (int i = 1; i < scope->count(); ++i) {
            if (scope->itemText(i).startsWith("Pollutants ("))
                all = i;
            if (scope->itemText(i).contains("MG/L"))
                mg = i;
        }
        QVERIFY(all > 0 && mg > 0);
        scope->setCurrentIndex(all);
        QVERIFY(!(::property(tree->model(), "Rain Concen.").flags() & Qt::ItemIsEditable));
        auto snow = ::property(tree->model(), "Snow Only");
        QVERIFY(snow.isValid());
        tree->edit(snow);
        auto *combo = tree->findChild<QComboBox *>();
        QVERIFY(combo);
        const int yes = combo->findData(1);
        QVERIFY(yes >= 0);
        combo->setCurrentIndex(yes);
        QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, yes));
        QTRY_COMPARE(canvas.undoStack()->count(), 1);
        for (const auto &name : {"TSS", "P2", "Lead"})
            QCOMPARE(state(layer.get(), DC::DataPollutants, name).value("snowOnly").toInt(), 1);
        QCOMPARE(state(layer.get(), DC::DataPollutants, "Control").value("snowOnly").toInt(), 0);
        scope->setCurrentIndex(mg);
        QVERIFY(::property(tree->model(), "Rain Concen.").flags() & Qt::ItemIsEditable);
        panel.showSelection(&canvas, {{SWMMObjectRef::Inlet, "G2"},
                                      {SWMMObjectRef::Inlet, "GRATE1"},
                                      {SWMMObjectRef::Inlet, "CURB1"}});
        int generic = -1;
        for (int i = 1; i < scope->count(); ++i)
            if (scope->itemText(i).contains("Generic grates"))
                generic = i;
        QVERIFY(generic > 0);
        scope->setCurrentIndex(generic);
        QVERIFY(::property(tree->model(), "Grate Open Fraction").flags() & Qt::ItemIsEditable);
        DataObjectAttributeTableModel usTable, siTable;
        usTable.setSource(layer.get(), DC::DataStreets);
        siTable.setSource(si.get(), DC::DataStreets);
        QVERIFY(usTable.headerData(column(usTable, "crownWidth"), Qt::Horizontal)
                    .toString()
                    .endsWith("(ft)"));
        QVERIFY(siTable.headerData(column(siTable, "crownWidth"), Qt::Horizontal)
                    .toString()
                    .endsWith("(m)"));
    }
    void referenceRenameAndStaleSelection()
    {
        auto layer = fixture();
        QVERIFY(layer);
        QUndoStack undo;
        auto *patterns =
            qobject_cast<openswmmvis::pattern::PatternRegistry *>(layer->ensurePatternRegistry());
        QVERIFY(patterns);
        QVERIFY(DataObjectAttributeTableModel::applyValues(
            layer.get(), DC::DataAquifers, {"AQ1", "AQ2"}, "evapPattern", "MonthB", &undo));
        QVERIFY(patterns->rename(patterns->findByName("MonthB"), "RenamedMonth"));
        undo.undo();
        QCOMPARE(state(layer.get(), DC::DataAquifers, "AQ1").value("evapPattern").toString(),
                 QString());
        undo.redo();
        QCOMPARE(state(layer.get(), DC::DataAquifers, "AQ1").value("evapPattern").toString(),
                 QString("RenamedMonth"));
        DataObjectAggregateSelection selection(layer.get(), DC::DataPollutants, {"TSS", "P2"},
                                               &undo);
        const int decay = column(*selection.source(), "kDecay");
        QVERIFY(selection.stage(decay, .8));
        QCOMPARE(swmm_pollutant_set_kdecay(layer->engine(),
                                           swmm_pollutant_index(layer->engine(), "P2"), .6),
                 SWMM_OK);
        QString error;
        QVERIFY(!selection.apply(&error));
        QVERIFY(error.contains("changed"));
        QCOMPARE(state(layer.get(), DC::DataPollutants, "TSS").value("kDecay").toDouble(), 0.0);
        selection.discard();
        QCOMPARE(swmm_options_set(layer->engine(), "FLOW_UNITS", "CMS"), SWMM_OK);
        emit layer->optionsChanged({"FLOW_UNITS"});
        QVERIFY(!selection.setValue(decay, .8));
    }
    void referencesDesignsAndComments()
    {
        auto layer = fixture();
        QVERIFY(layer);
        auto e = layer->engine();
        QUndoStack undo;
        QString error;
        const auto before = state(layer.get(), DC::DataInlets, "G2");
        const QByteArray comment(3000, 'x');
        QCOMPARE(swmm_inlet_set_comment(e, swmm_inlet_index(e, "G2"), comment.constData()),
                 SWMM_OK);
        QVERIFY2(DataObjectAttributeTableModel::applyValues(layer.get(), DC::DataInlets,
                                                            {"G2", "Shared"}, "curveId", "Cap",
                                                            &undo, &error),
                 qPrintable(error));
        auto custom = state(layer.get(), DC::DataInlets, "G2");
        QCOMPARE(custom.value("type").toInt(), SWMM_INLET_CUSTOM);
        QCOMPARE(custom.value("curveKind").toInt(), SWMM_INLET_CURVE_DIVERSION);
        QByteArray restored(4096, 0);
        QCOMPARE(
            swmm_inlet_get_comment(e, swmm_inlet_index(e, "G2"), restored.data(), restored.size()),
            SWMM_OK);
        QCOMPARE(QByteArray(restored.constData()), comment);
        QVERIFY(!DataObjectAttributeTableModel::applyValues(
            layer.get(), DC::DataInlets, {"G2", "Shared"}, "curveId", "Wrong", &undo, &error));
        QVERIFY(!DataObjectAttributeTableModel::applyValues(
            layer.get(), DC::DataInlets, {"G2", "Shared"}, "curveId", "", &undo, &error));
        QCOMPARE(undo.count(), 1);
        undo.undo();
        QCOMPARE(state(layer.get(), DC::DataInlets, "G2"), before);
        undo.redo();
        QVERIFY(DataObjectAttributeTableModel::applyValues(
            layer.get(), DC::DataInlets, {"G2", "Shared"}, "curveId", "CapB", &undo));
        QCOMPARE(state(layer.get(), DC::DataInlets, "G2").value("curveKind").toInt(),
                 SWMM_INLET_CURVE_RATING);
        const auto path = QDir(folder()).filePath("custom-curve.inp");
        QCOMPARE(swmm_model_write(e, path.toUtf8().constData()), SWMM_OK);
        DC reopened(path);
        QList<QString> warnings, errors;
        QVERIFY(reopened.loadModel(warnings, errors));
        QCOMPARE(state(&reopened, DC::DataInlets, "G2"), state(layer.get(), DC::DataInlets, "G2"));
        QVERIFY(!DataObjectAttributeTableModel::applyValues(
            layer.get(), DC::DataAquifers, {"AQ1", "AQ2"}, "evapPattern", "Bad", &undo, &error));
        auto other = fixture();
        QVERIFY(other);
        DataObjectRef wrong;
        wrong.layer = other.get();
        wrong.engine = other->engine();
        wrong.kind = DataObjectRef::Pattern;
        wrong.currentName = "MonthB";
        QVERIFY(!DataObjectAttributeTableModel::applyValues(
            layer.get(), DC::DataAquifers, {"AQ1"}, "evapPattern", QVariant::fromValue(wrong),
            &undo, &error));
    }
    void identityLifetimeAndReplay()
    {
        auto layer = fixture();
        QVERIFY(layer);
        QUndoStack undo;
        auto *reg = qobject_cast<openswmmvis::pollutant::PollutantRegistry *>(
            layer->ensurePollutantRegistry());
        QVERIFY(reg);
        QVERIFY(DataObjectAttributeTableModel::applyValues(layer.get(), DC::DataPollutants,
                                                           {"TSS", "P2"}, "kDecay", .5, &undo));
        auto *p = reg->findByName("TSS");
        QVERIFY(reg->rename(p, "Renamed"));
        undo.undo();
        QCOMPARE(state(layer.get(), DC::DataPollutants, "Renamed").value("kDecay").toDouble(), 0.0);
        undo.redo();
        QCOMPARE(swmm_pollutant_set_kdecay(layer->engine(),
                                           swmm_pollutant_index(layer->engine(), "P2"), .7),
                 SWMM_OK);
        undo.undo();
        QCOMPARE(state(layer.get(), DC::DataPollutants, "Renamed").value("kDecay").toDouble(), .5);
        undo.clear();
        QVERIFY(DataObjectAttributeTableModel::applyValues(layer.get(), DC::DataPollutants, {"P2"},
                                                           "kDecay", .6, &undo));
        reg->remove(reg->findByName("P2"));
        auto *fresh = reg->create("P2");
        QVERIFY(fresh);
        reg->saveToEngine(layer->engine(), "P2");
        undo.undo();
        QCOMPARE(state(layer.get(), DC::DataPollutants, "P2").value("kDecay").toDouble(), 0.0);
        undo.clear();
        QVERIFY(DataObjectAttributeTableModel::applyValues(layer.get(), DC::DataStreets, {"ST1"},
                                                           "roadRoughness", .03, &undo));
        layer.reset();
        undo.undo();
    }
    void ownedSingleSortedAndProjectSwitch()
    {
        auto layer = fixture();
        QVERIFY(layer);
        MapCanvas canvas;
        layer->setEditCanvas(&canvas);
        PropertiesPanel panel;
        panel.setProject(layer.get());
        panel.showDataObject(layer.get(), SWMMObjectRef::Pollutant, "TSS");
        auto *adapter = panel.findChild<SWMMPollutantPropertyAdapter *>();
        QVERIFY(adapter);
        adapter->setKDecay(.25);
        QCOMPARE(canvas.undoStack()->count(), 1);
        DataObjectAttributeTableModel source;
        source.setSource(layer.get(), DC::DataPollutants);
        source.setUndoStack(canvas.undoStack());
        QSortFilterProxyModel proxy;
        proxy.setSourceModel(&source);
        proxy.setFilterKeyColumn(0);
        proxy.setFilterRegularExpression("^(P2|Shared)$");
        proxy.sort(0, Qt::DescendingOrder);
        const auto cell = proxy.index(0, column(source, "kDecay"));
        const auto name = source.objectNameAt(proxy.mapToSource(cell).row());
        QVERIFY(proxy.setData(cell, .4));
        QCOMPARE(state(layer.get(), DC::DataPollutants, name).value("kDecay").toDouble(), .4);
        panel.resize(1000, 1000);
        panel.show();
        auto *tree = panel.findChild<QTreeView *>();
        tree->expandAll();
        auto field = ::property(tree->model(), "Decay Coeff. (1/days)");
        QVERIFY(field.isValid());
        tree->edit(field);
        QPointer<QDoubleSpinBox> draft = tree->findChild<QDoubleSpinBox *>();
        QVERIFY(draft);
        draft->setValue(.9);
        auto other = fixture(true);
        QVERIFY(other);
        panel.setProject(other.get());
        if (draft)
            QTest::keyClick(draft, Qt::Key_Return);
        QCOMPARE(state(layer.get(), DC::DataPollutants, "TSS").value("kDecay").toDouble(), .25);
    }
    void reviewAndInventory()
    {
        auto layer = fixture();
        QVERIFY(layer);
        QFile file(QDir(folder()).filePath("../DATA_FIELDS.md"));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QTextStream out(&file);
        out << "# Registry-backed data field inventory\n\n| Category | Key | Label | Disposition |\n|---|---|---|---|\n";
        for (auto cat : {DC::DataPollutants, DC::DataLandUses, DC::DataAquifers, DC::DataStreets,
                         DC::DataInlets}) {
            DataObjectAttributeTableModel model;
            model.setSource(layer.get(), cat);
            for (const auto &spec : model.columnSpecs())
                out << "| " << int(cat) << " | " << spec.key << " | " << spec.label << " | "
                    << (spec.key == "Name"    ? "Individual identity"
                        : spec.key == "units" ? "Existing units read-only"
                        : spec.key == "curveKind"
                            ? "Derived from curve"
                            : "Immediate bulk; applicability and validation apply")
                    << " |\n";
        }
        const auto path = QDir(folder()).filePath("Data_Families_Review_Ready.inp");
        QCOMPARE(swmm_model_write(layer->engine(), path.toUtf8().constData()), SWMM_OK);
    }
};
QTEST_MAIN(TestDataFamilyBulk)
#include "test_datafamilybulk.moc"
