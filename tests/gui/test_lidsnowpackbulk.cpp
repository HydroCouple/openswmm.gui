#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "lid/lidcontrolregistry.h"
#include "pollutant/pollutantregistry.h"
#include <openswmm/engine/openswmm_pollutants.h>
#include <openswmm/engine/openswmm_reactions.h>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include "snowpack/snowpackregistry.h"
#include "ui/dialogs/lidcontroleditordialog.h"
#include "ui/dialogs/snowpackeditordialog.h"
#include "ui/editors/dataobjectdraftsession.h"
#include "ui/editors/comprehensiveeditorregistry.h"
#include "ui/panels/dataobjectaggregateselection.h"
#include "ui/panels/propertiespanel.h"
#include "ui/panels/objectbrowserpanel.h"
#include "ui/panels/swmmobjecttreemodel.h"
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_infrastructure.h>
#include <openswmm/engine/openswmm_subcatchments.h>
#include <openswmm/engine/openswmm_snow.h>
#include <openswmm/engine/openswmm_edit.h>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QTreeView>
#include <QTest>
#include <memory>
using DC = SWMMModelLayer;
using Model = DataObjectAttributeTableModel;
namespace {
const QStringList lidKeys[]{
    {"surfStorage", "surfRoughness", "surfSlope"},
    {"soilThick", "soilPorosity", "soilFc", "soilWp", "soilKsat", "soilKslope"},
    {"storThick", "storVoidFrac", "storKsat"}, {"drainCoeff", "drainExpon", "drainOffset"},
    {"paveThick", "paveVoidRatio", "paveImpervious", "paveKsat", "paveClogFactor", "paveRegenDays"},
    {"matThick", "matVoidFrac", "matRoughness"}};
QString snowKey(int i) { return QString("snow/%1").arg(i); }
QString folder() { return QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA")).absoluteFilePath("../../../workplans/bulk-lid-completion/review"); }
QStringList targets(const QString &prefix) { return {prefix + "_A", prefix + "_B", prefix + "_C"}; }
std::unique_ptr<DC> fixture(bool si = false)
{
    QDir().mkpath(folder());
    QFile base(QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA")).filePath("data_object_table_fixture.inp"));
    if (!base.open(QIODevice::ReadOnly)) return {};
    auto text = base.readAll(); if (si) text.replace("CFS", "CMS");
    text += "\n[RAINGAGES]\nG1 INTENSITY 0:15 1 TIMESERIES Rain\n[TIMESERIES]\nRain 0:00 0\nRain 24:00 0\n"
            "[SUBCATCHMENTS]\nSC1 G1 J1 1 50 100 1 0\nSC2 G1 J2 2 40 120 2 0\n"
            "[SUBAREAS]\nSC1 .01 .1 .05 .05 25 OUTLET 100\nSC2 .01 .1 .05 .05 25 OUTLET 100\n"
            "[INFILTRATION]\nSC1 3 .5 7\nSC2 3 .5 7\n[LID_CONTROLS]\n";
    const QStringList codes{"BC", "RG", "GR", "IT", "PP", "RB", "RD", "VS"};
    for (int type = 0; type < 8; ++type) {
        auto ids = targets(codes[type]); ids << codes[type] + "_Control";
        for (int n = 0; n < ids.size(); ++n) {
            const auto id = ids[n];
            auto row = [&](const QString &kind, const QList<double> &values) {
                text += (id + " " + kind).toUtf8();
                for (double v : values) text += ' ' + QByteArray::number(v, 'g', 17);
                text += '\n';
            };
            text += (id + " " + codes[type] + '\n').toUtf8();
            if (type != 5) row("SURFACE", {2+n*.2, .2+n*.01, .1+n*.01, 1+n*.1, 6.+n});
            if (type == 0 || type == 1 || type == 2 || type == 4)
                row("SOIL", {8.+n, .5+n*.01, .25+n*.01, .1+n*.01, .3+n*.1, 10.+n, 3.+n});
            if (type == 0 || type == 3 || type == 4 || type == 5) {
                row("STORAGE", {10.+n, .75+n*.1, .2+n*.1, 20.+n, 0});
                row("DRAIN", {.4+n*.1, .5+n*.1, 1+n*.1, 2.+n, 3.+n, 1.+n});
            }
            if (type == 4) row("PAVEMENT", {5.+n, .4+n*.01, .2+n*.01, .5+n*.1, 40.+n, 7.+n, .3+n*.01});
            if (type == 2) row("DRAINMAT", {1.+n, .4+n*.01, .1+n*.01});
        }
    }
    text += "[SNOWPACKS]\n";
    for (const auto &prefix : {QString("DD"), QString("UEB"), QString("Inherited")}) {
        auto ids = targets(prefix); ids << prefix + "_Control";
        for (int n = 0; n < ids.size(); ++n) {
            for (int surface = 0; surface < 3; ++surface) {
                text += (ids[n] + " " + QStringList{"PLOWABLE", "IMPERVIOUS", "PERVIOUS"}[surface]).toUtf8();
                const QList<double> values{.01+n*.001, .1+n*.01, si ? 0. : 32., .05+n*.01, 1+n*.1, .01+n*.01, surface == 0 ? .3+n*.01 : .5+n*.1};
                for (double v : values) text += ' ' + QByteArray::number(v, 'g', 17);
                text += '\n';
            }
            text += (ids[n] + " REMOVAL 1 .1 .1 .1 .1 .1 SC1\n").toUtf8();
        }
    }
    // UEB override rows belong to the existing SNOWPACKS section.
    for (const auto &prefix : {QString("DD"), QString("UEB")}) {
        auto ids = targets(prefix); ids << prefix + "_Control";
        for (int n = 0; n < ids.size(); ++n)
            text += (ids[n] + QString(" UEB %1 %2 %3 %4 %5 MODEL %6\n").arg(.2+n*.01, 0, 'g', 17).arg(1+n*.1, 0, 'g', 17)
                     .arg(2.+n).arg(.12+n*.01, 0, 'g', 17).arg(10.+n).arg(prefix == "DD" ? "DEGREE_DAY" : "UEB")).toUtf8();
    }
    const auto path = QDir(folder()).filePath(si ? "initial-si.inp" : "initial-us.inp");
    QFile file(path); if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    file.write(text); file.close();
    auto layer = std::make_unique<DC>(path); QList<QString> warnings, errors;
    if (!layer->loadModel(warnings, errors)) { qWarning() << errors; return {}; }
    for (const auto &id : targets("NODE")) {
        if (swmm_lid_add(layer->engine(), id.toUtf8().constData(), 8) != SWMM_OK) return {};
        SWMM_LidNodeLayer rows[]{{0, {2, .2}}, {1, {10, .5, .25, .1, .5, 10, 3}}, {2, {8, .4, .1}}, {3, {.05, 0}}};
        if (swmm_lid_node_configure(layer->engine(), swmm_lid_index(layer->engine(), id.toUtf8().constData()), rows, 4, nullptr, 0) != SWMM_OK) return {};
    }
    return layer;
}
int column(const Model &model, const QString &key)
{
    const auto specs = model.columnSpecs();
    for (int c = 0; c < specs.size(); ++c) if (specs[c].key == key) return c;
    return -1;
}
QVariantMap state(DC *layer, DC::DataCategory cat, const QString &name)
{
    Model model; model.setSource(layer, cat); model.refreshObject(name);
    QVariantMap result;
    const int row = model.rowForName(name);
    for (int c = 1; c < model.columnCount(); ++c) {
        const auto key = model.columnSpecs()[c].key;
        if (cat == DC::DataLIDControls && key == "nodeLayers") continue;
        auto value = model.index(row, c).data(Qt::EditRole);
        if (cat == DC::DataSnowpacks && key.startsWith("snow/"))
            value = static_cast<openswmmvis::snowpack::SnowpackProvider *>(model.providerAt(row))->param(key.mid(5).toInt());
        if (cat == DC::DataLIDControls && !value.isValid()) continue;
        auto stableKey = key;
        if (cat == DC::DataLIDControls)
            if (auto *reg = qobject_cast<openswmmvis::pollutant::PollutantRegistry *>(layer->ensurePollutantRegistry()))
                for (auto *p : reg->providers()) stableKey.replace(QString::number(reinterpret_cast<quintptr>(p), 16), p->name());
        result[stableKey] = value;
    }
    return result;
}
QMap<QString, QStringList> lidRows(DC *layer, const QString &name)
{
    const auto path = QDir(folder()).filePath("native-members.inp");
    if (swmm_model_write(layer->engine(), path.toUtf8().constData()) != SWMM_OK) return {};
    QFile f(path); if (!f.open(QIODevice::ReadOnly)) return {};
    QMap<QString, QStringList> result;
    for (const auto &line : f.readAll().split('\n')) {
        const auto words = QString::fromUtf8(line).simplified().split(' ');
        if (words.size() > 2 && words[0] == name) result[words[1]] = words.mid(2);
    }
    return result;
}
QModelIndex findProperty(QAbstractItemModel *model, const QString &label, const QModelIndex &parent = {})
{
    for (int r = 0; r < model->rowCount(parent); ++r) {
        const auto i = model->index(r, 0, parent);
        if (i.data().toString() == label || i.data().toString().startsWith(label + " (")) return i.siblingAtColumn(1);
        const auto child = findProperty(model, label, i); if (child.isValid()) return child;
    }
    return {};
}
}
class TestLidSnowpackBulk : public QObject
{
    Q_OBJECT
private slots:
    void fields_data()
    {
        QTest::addColumn<int>("category"); QTest::addColumn<QString>("prefix");
        QTest::addColumn<QString>("key"); QTest::addColumn<QVariant>("value"); QTest::addColumn<bool>("si");
        for (bool si : {false, true}) {
            for (int group = 0; group < 6; ++group)
                for (int i = 0; i < lidKeys[group].size(); ++i) {
                    double value = 1.25;
                    const auto key = lidKeys[group][i];
                    if (key == "soilPorosity") value = .65;
                    if (key == "soilFc") value = .3;
                    if (key == "soilWp") value = .05;
                    if (key == "paveImpervious" || key == "matVoidFrac") value = .55;
                    QTest::newRow(qPrintable(QString("lid-%1-%2").arg(key).arg(si)))
                        << int(DC::DataLIDControls) << QString(group == 4 ? "PP" : group == 5 ? "GR" : "BC") << key << QVariant(value) << si;
                }
            for (const auto &key : QStringList{"surfVegetation", "surfSideSlope", "soilSuction", "storClogging", "drainDelay", "drainOpen", "drainClose"})
                QTest::newRow(qPrintable(QString("lid-extra-%1-%2").arg(key).arg(si)))
                    << int(DC::DataLIDControls) << QString("BC") << key << QVariant(key == "surfVegetation" ? .4 : 1.25) << si;
            for (int i = 0; i < 32; ++i) {
                double value = .025;
                if (i < 21) value = i % 7 == 0 ? .015 : i % 7 == 1 ? .15 : i % 7 == 2 ? -1.25 : i % 7 == 3 ? .25 : i == 6 ? .5 : 2.25;
                if (i == 21) value = 2.25;
                if (i == 27) value = .45;
                if (i == 28) value = 1.5;
                if (i == 29) value = -5;
                if (i == 30) value = .25;
                if (i == 31) value = -100;
                QTest::newRow(qPrintable(QString("snow-%1-%2").arg(i).arg(si)))
                    << int(DC::DataSnowpacks) << QString(i >= 27 ? "UEB" : "DD") << snowKey(i) << QVariant(value) << si;
            }
            QTest::newRow(qPrintable(QString("model-%1").arg(si))) << int(DC::DataSnowpacks) << QString("DD") << QString("modelKind") << QVariant(1) << si;
            QTest::newRow(qPrintable(QString("destination-%1").arg(si))) << int(DC::DataSnowpacks) << QString("DD") << QString("removalSubcatch") << QVariant("SC2") << si;
        }
    }
    void fields()
    {
        QFETCH(int, category); QFETCH(QString, prefix); QFETCH(QString, key); QFETCH(QVariant, value); QFETCH(bool, si);
        auto layer = fixture(si); QVERIFY(layer); const auto cat = DC::DataCategory(category); const auto ids = targets(prefix);
        QList<QVariantMap> before; for (const auto &id : ids) before << state(layer.get(), cat, id);
        const auto control = state(layer.get(), cat, prefix + "_Control");
        const auto hidden = cat == DC::DataLIDControls ? lidRows(layer.get(), ids.first()) : QMap<QString, QStringList>{};
        QUndoStack undo; QSignalSpy dirty(layer.get(), &DC::modelEdited); QString error;
        QVERIFY2(Model::applyValues(layer.get(), cat, ids, key, value, &undo, &error), qPrintable(error));
        QCOMPARE(undo.count(), 1); QCOMPARE(dirty.count(), 1);
        for (int n = 0; n < ids.size(); ++n) {
            auto expected = before[n]; expected[key] = value;
            if (key == "modelKind") expected["effectiveModel"] = 1;
            QCOMPARE(state(layer.get(), cat, ids[n]), expected);
        }
        QCOMPARE(state(layer.get(), cat, prefix + "_Control"), control);
        if (cat == DC::DataLIDControls) {
            const auto afterRows = lidRows(layer.get(), ids.first());
            for (auto it = hidden.cbegin(); it != hidden.cend(); ++it) {
                auto expected = it.value();
                int position = -1;
                if (it.key() == "SURFACE") position = QStringList{"surfStorage", "surfVegetation", "surfRoughness", "surfSlope", "surfSideSlope"}.indexOf(key);
                if (it.key() == "SOIL") position = (lidKeys[1] + QStringList{"soilSuction"}).indexOf(key);
                if (it.key() == "STORAGE") position = (lidKeys[2] + QStringList{"storClogging"}).indexOf(key);
                if (it.key() == "DRAIN") position = (lidKeys[3] + QStringList{"drainDelay", "drainOpen", "drainClose"}).indexOf(key);
                if (it.key() == "PAVEMENT") position = lidKeys[4].indexOf(key);
                if (it.key() == "DRAINMAT") position = lidKeys[5].indexOf(key);
                if (position >= 0) expected[position] = afterRows[it.key()].value(position);
                QCOMPARE(afterRows[it.key()], expected);
            }
        }
        QVERIFY(Model::applyValues(layer.get(), cat, ids, key, value, &undo)); QCOMPARE(dirty.count(), 1); QCOMPARE(undo.count(), 1);
        undo.undo(); for (int n = 0; n < ids.size(); ++n) QCOMPARE(state(layer.get(), cat, ids[n]), before[n]);
        undo.redo();
        const auto path = QDir(folder()).filePath(QString("saved-%1-%2-%3.inp").arg(category).arg(QString(key).replace('/', '-')).arg(si));
        QCOMPARE(swmm_model_write(layer->engine(), path.toUtf8().constData()), SWMM_OK);
        DC reopened(path); QList<QString> warnings, errors; QVERIFY2(reopened.loadModel(warnings, errors), qPrintable(errors.join(' ')));
        for (const auto &id : ids) QCOMPARE(state(&reopened, cat, id), state(layer.get(), cat, id));
        QCOMPARE(state(&reopened, cat, prefix + "_Control"), control);
    }
    void nodeFields_data()
    {
        QTest::addColumn<QString>("key"); QTest::addColumn<QVariant>("value"); QTest::addColumn<bool>("si");
        for (bool si : {false, true}) {
            for (int kind = 0; kind < 4; ++kind) {
                const int count[]{2,7,3,2};
                for (int i = 0; i < count[kind]; ++i) {
                    const QString key = QString("node/%1/1/p/%2").arg(kind).arg(i);
                    const double v = kind == 0 && i == 1 ? .3 : kind == 1 && i == 1 ? .6 : kind == 1 && i == 2 ? .3 : kind == 1 && i == 3 ? .06 : kind == 2 && i == 1 ? .55 : .8;
                    QTest::newRow(qPrintable(key + QString::number(si))) << key << QVariant(v) << si;
                }
                if (kind == 1 || kind == 2) {
                    const double values[]{.03,1.25,1.8,.6,.0002};
                    for (int i = 0; i < 5; ++i) {
                        const auto key = QString("node/%1/1/r/%2").arg(kind).arg(i);
                        QTest::newRow(qPrintable(key + QString::number(si))) << key << QVariant(values[i]) << si;
                    }
                }
            }
            const QList<QPair<QString,QVariant>> other{{"flowModel",0}, {"flowCells",12}, {"flowAtol",2.e-7}, {"flowRtol",2.e-5}, {"flowMaxStep",20.},
                {"node/1/1/t/TSS/present",0}, {"node/1/1/t/TSS/removal",35.}, {"node/1/1/t/TSS/decay",1.25}, {"node/1/1/t/TSS/expression","R = 0.35"}};
            for (const auto &field : other)
                QTest::newRow(qPrintable(field.first + QString::number(si))) << field.first << field.second << si;
        }
    }
    void nodeFields()
    {
        QFETCH(QString,key); QFETCH(QVariant,value); QFETCH(bool,si);
        auto layer = fixture(si); QVERIFY(layer); const auto ids = targets("NODE");
        auto *polls = qobject_cast<openswmmvis::pollutant::PollutantRegistry *>(layer->ensurePollutantRegistry());
        const auto token = QString::number(reinterpret_cast<quintptr>(polls->findByName("TSS")),16);
        key.replace("/TSS/", "/" + token + "/"); const auto treatment = "node/1/1/t/" + token + "/";
        for (const auto &id : ids) QVERIFY(Model::editLidProfile(layer.get(),id,"materials",0,0,nullptr));
        QVERIFY(Model::applyValues(layer.get(),DC::DataLIDControls,ids,"flowModel",1,nullptr));
        QVERIFY(Model::applyValues(layer.get(),DC::DataLIDControls,ids,treatment+"present",1,nullptr));
        QList<QVariantMap> before; for (const auto &id : ids) before << Model::lidProfile(layer.get(),id);
        const auto control = Model::lidProfile(layer.get(),"BC_Control");
        QUndoStack undo; QSignalSpy dirty(layer.get(),&DC::modelEdited); QString error;
        QVERIFY2(Model::applyValues(layer.get(),DC::DataLIDControls,ids,key,value,&undo,&error),qPrintable(error));
        QCOMPARE(undo.count(),1); QCOMPARE(dirty.count(),1);
        for (int i = 0; i < ids.size(); ++i) {
            auto expected = before[i]; expected[key] = value;
            if (key == "flowModel") { expected["flowCells"] = 8; expected["flowAtol"] = 1.e-7; expected["flowRtol"] = 1.e-5; expected["flowMaxStep"] = 30.; }
            if (key.endsWith("/present")) { expected[treatment+"removal"] = 0.; expected[treatment+"decay"] = 0.; expected[treatment+"expression"] = QString(); }
            QCOMPARE(Model::lidProfile(layer.get(),ids[i]),expected);
        }
        QCOMPARE(Model::lidProfile(layer.get(),"BC_Control"),control);
        QVERIFY(Model::applyValues(layer.get(),DC::DataLIDControls,ids,key,value,&undo)); QCOMPARE(undo.count(),1);
        undo.undo(); for (int i = 0; i < ids.size(); ++i) QCOMPARE(Model::lidProfile(layer.get(),ids[i]),before[i]);
        undo.redo();
        const auto path = QDir(folder()).filePath(QString("node-%1-%2.inp").arg(QString(key).replace('/','-')).arg(si));
        QCOMPARE(swmm_model_write(layer->engine(),path.toUtf8()),SWMM_OK);
        DC reopened(path); QList<QString>warnings,errors; QVERIFY2(reopened.loadModel(warnings,errors),qPrintable(errors.join(' ')));
        for (const auto &id : ids) QCOMPARE(state(&reopened,DC::DataLIDControls,id),state(layer.get(),DC::DataLIDControls,id));
    }
    void nodeDependentReplayDefinitionsAndAssignedGeometry()
    {
        const auto path=QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA")).filePath("lid_treatment_editor.inp");
        DC layer(path); QList<QString>warnings,errors; QVERIFY(layer.loadModel(warnings,errors));
        MapCanvas canvas; layer.setEditCanvas(&canvas); QSignalSpy changed(&layer,&DC::attributeChanged);
        SWMM_StorageLidState before{},after{}; QCOMPARE(swmm_storage_lid_get(layer.engine(),0,&before),SWMM_OK);
        QString detail; QVERIFY2(Model::applyValues(&layer,DC::DataLIDControls,{"Stack"},"node/1/1/p/0",24.,canvas.undoStack(),&detail),qPrintable(detail));
        QCOMPARE(swmm_storage_lid_get(layer.engine(),0,&after),SWMM_OK); QVERIFY(after.max_depth_ft>before.max_depth_ft);
        QStringList names; for(const auto &args:changed) names<<args[0].toString(); QVERIFY(names.contains("S")); QVERIFY(names.contains("D")); QVERIFY(names.contains("Stack"));
        canvas.undoStack()->undo(); QCOMPARE(swmm_storage_lid_get(layer.engine(),0,&after),SWMM_OK); QCOMPARE(after.max_depth_ft,before.max_depth_ft);
        auto *polls=qobject_cast<openswmmvis::pollutant::PollutantRegistry *>(layer.ensurePollutantRegistry());
        auto *poll=polls->findByName("TSS"); const auto prefix="node/1/1/t/"+QString::number(reinterpret_cast<quintptr>(poll),16)+"/";
        const auto record=Model::lidProfile(&layer,"Stack");
        QVERIFY(!Model::applyValues(&layer,DC::DataLIDControls,{"Stack"},prefix+"expression","R = undefined_token",canvas.undoStack()));
        QVERIFY(!Model::applyValues(&layer,DC::DataLIDControls,{"Stack"},prefix+"expression","R = .2\nR = .4",canvas.undoStack()));
        QCOMPARE(Model::lidProfile(&layer,"Stack"),record);
        QVERIFY(Model::applyValues(&layer,DC::DataLIDControls,{"Stack"},prefix+"present",0,canvas.undoStack()));
        QVERIFY(Model::applyValues(&layer,DC::DataLIDControls,{"Stack"},"node/1/1/p/6",4.,nullptr));
        canvas.undoStack()->undo(); QCOMPARE(Model::lidProfile(&layer,"Stack")[prefix+"expression"],record[prefix+"expression"]);
        QCOMPARE(Model::lidProfile(&layer,"Stack")["node/1/1/p/6"].toDouble(),4.);
        DataObjectAggregateSelection scope(&layer,DC::DataLIDControls,{"Stack"},canvas.undoStack());
        QVERIFY(scope.stage(column(*scope.source(),prefix+"removal"),25.));
        QCOMPARE(swmm_pollutant_set_units(layer.engine(),0,1),SWMM_OK); QString error; QVERIFY(!scope.apply(&error));
        QVERIFY(!scope.active());
    }
    void reactionRemovalDispositionAndStaleDefinitions()
    {
        auto layer=fixture(); QVERIFY(layer); auto e=layer->engine(); QUndoStack undo;
        QCOMPARE(swmm_reaction_species_add(e,"Tracer",0,"mg/L",1.e-6,1.e-3),SWMM_OK);
        QCOMPARE(swmm_reaction_species_add(e,"Wall",1,"mg/m2",1.e-6,1.e-3),SWMM_OK);
        QCOMPARE(swmm_reaction_species_add(e,"TSS",0,"mg/L",1.e-6,1.e-3),SWMM_ERR_BADPARAM);
        Model source; source.setSource(layer.get(),DC::DataLIDControls); const auto row=source.rowForName("BC_A");
        QVERIFY(source.index(row,column(source,"removal/s/Tracer")).flags()&Qt::ItemIsEditable);
        QVERIFY(!(source.index(row,column(source,"removal/s/Wall")).flags()&Qt::ItemIsEditable));
        QVERIFY(Model::applyValues(layer.get(),DC::DataLIDControls,targets("BC"),"removal/s/Tracer",30.,&undo));
        double value=-1; QCOMPARE(swmm_lid_get_removal(e,swmm_lid_index(e,"BC_A"),swmm_pollutant_count(e),&value),SWMM_OK); QCOMPARE(value,30.);
        // Adding a new definition invalidates historical catalog assumptions instead of redirecting a row.
        QCOMPARE(swmm_reaction_species_add(e,"Later",0,"mg/L",1.e-6,1.e-3),SWMM_OK); undo.undo();
        QCOMPARE(undo.count(),0); QCOMPARE(swmm_lid_get_removal(e,swmm_lid_index(e,"BC_A"),swmm_pollutant_count(e),&value),SWMM_OK); QCOMPARE(value,30.);
    }
    void occurrenceMatchingValidationAndSchemaGuards()
    {
        auto layer=fixture(); QVERIFY(layer); QUndoStack undo; const auto ids=targets("NODE"); QString error;
        QVERIFY2(Model::editLidProfile(layer.get(),ids[0],"insert",2,1,nullptr,&error),qPrintable(error));
        QVERIFY(Model::editLidProfile(layer.get(),ids[1],"insert",3,1,nullptr));
        const auto before=Model::lidProfile(layer.get(),ids[0]);
        QVERIFY(!Model::applyValues(layer.get(),DC::DataLIDControls,ids,"node/1/2/p/0",17.,&undo,&error));
        QCOMPARE(Model::lidProfile(layer.get(),ids[0]),before); QCOMPARE(undo.count(),0);
        QVERIFY(Model::applyValues(layer.get(),DC::DataLIDControls,ids.mid(0,2),"node/1/2/p/0",17.,&undo));
        for(const auto &id:ids.mid(0,2)) QCOMPARE(Model::lidProfile(layer.get(),id)["node/1/2/p/0"].toDouble(),17.);
        QCOMPARE(Model::lidProfile(layer.get(),ids[0])["node/1/1/p/0"],before["node/1/1/p/0"]);
        DataObjectAggregateSelection scope(layer.get(),DC::DataLIDControls,ids.mid(0,2),&undo);
        const int col=column(*scope.source(),"node/1/2/p/0"); QVERIFY(scope.stage(col,18.));
        QVERIFY(Model::editLidProfile(layer.get(),ids[0],"up",2,0,nullptr));
        QVERIFY(!scope.apply(&error)); QVERIFY(!scope.active());
        const auto current=Model::lidProfile(layer.get(),ids[1]);
        for(const auto &field:QList<QPair<QString,QVariant>>{{"node/1/1/p/1",.1},{"node/1/1/p/0",-1.},{"flowModel",1}})
            QVERIFY(!Model::applyValues(layer.get(),DC::DataLIDControls,{ids[1]},field.first,field.second,&undo));
        QCOMPARE(Model::lidProfile(layer.get(),ids[1]),current);
        QUndoStack history;
        const auto original=Model::lidProfile(layer.get(),ids[2]);
        QVERIFY(Model::applyValues(layer.get(),DC::DataLIDControls,{ids[2]},"node/1/1/p/4",.123,&history));
        QVERIFY(Model::editLidProfile(layer.get(),ids[2],"insert",2,1,&history));
        history.undo(); history.undo(); QCOMPARE(Model::lidProfile(layer.get(),ids[2]),original);
        history.redo(); history.redo(); QCOMPARE(Model::lidProfile(layer.get(),ids[2])["_kinds"].toList().size(),5);
    }
    void typeRemovalAndTypedPollutantReplay()
    {
        auto layer=fixture(); QVERIFY(layer); QUndoStack undo; const auto ids=targets("BC");
        auto *polls=qobject_cast<openswmmvis::pollutant::PollutantRegistry *>(layer->ensurePollutantRegistry());
        auto *p=polls->findByName("TSS"); const auto key="removal/p/"+QString::number(reinterpret_cast<quintptr>(p),16);
        const auto before=Model::lidProfile(layer.get(),ids[0]);
        QVERIFY(Model::applyValues(layer.get(),DC::DataLIDControls,ids,key,35.,&undo));
        QVERIFY(polls->rename(p,"Solids")); undo.undo();
        double removal=-1; QCOMPARE(swmm_lid_get_removal(layer->engine(),swmm_lid_index(layer->engine(),"BC_A"),swmm_pollutant_index(layer->engine(),"Solids"),&removal),SWMM_OK); QCOMPARE(removal,0.);
        undo.redo(); QCOMPARE(Model::lidProfile(layer.get(),ids[0])[key].toDouble(),35.);
        QVERIFY(Model::applyValues(layer.get(),DC::DataLIDControls,ids,"type",4,&undo));
        auto expected=before; expected[key]=35.; expected["type"]=4;
        QCOMPARE(Model::lidProfile(layer.get(),ids[0]),expected);
        undo.undo(); QCOMPARE(Model::lidProfile(layer.get(),ids[0])["type"].toInt(),0);
        const auto saved=QDir(folder()).filePath("renamed-removals.inp");
        QCOMPARE(swmm_model_write(layer->engine(),saved.toUtf8()),SWMM_OK);
        DC restored(saved); QList<QString>warnings,errors; QVERIFY(restored.loadModel(warnings,errors));
        QCOMPARE(state(&restored,DC::DataLIDControls,ids[0]),state(layer.get(),DC::DataLIDControls,ids[0]));
        polls->remove(p); QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        auto *replacement=polls->create("Solids"); QVERIFY(replacement); undo.undo();
        QCOMPARE(Model::lidProfile(layer.get(),ids[0])["type"].toInt(),0);
        QVERIFY(undo.count()<2);
        QUndoStack removalHistory;
        QVERIFY(Model::applyValues(layer.get(),DC::DataLIDControls,{"BC_B"},"surfStorage",3.,&removalHistory));
        auto *controls=qobject_cast<openswmmvis::lid::LidControlRegistry *>(layer->ensureLidControlRegistry());
        DataObjectAggregateSelection held(layer.get(),DC::DataLIDControls,{"BC_B"},&removalHistory);
        QVERIFY(held.stage(column(*held.source(),"surfStorage"),4.));
        Model stale; stale.setSource(layer.get(),DC::DataLIDControls);
        const auto staleCell=stale.index(stale.rowForName("BC_B"),column(stale,"surfStorage"));
        controls->remove(controls->findByName("BC_B"));
        QVERIFY(controls->create("BC_B")); QVERIFY(Model::initializeLidDesign(layer.get(),"BC_B"));
        QVERIFY(Model::applyValues(layer.get(),DC::DataLIDControls,{"BC_B"},"surfStorage",3.,nullptr));
        QVERIFY(!held.apply(nullptr)); QVERIFY(!held.active());
        QVERIFY(!stale.setData(staleCell,4.));
        // Deferred QObject destruction must not let old history target this reused native name.
        removalHistory.undo(); QCOMPARE(removalHistory.count(),0);
        QCOMPARE(Model::lidProfile(layer.get(),"BC_B")["surfStorage"].toDouble(),3.);
    }
    void ownedEditorDraftStructureAndRendering()
    {
        // Apply/Revert plan Phase 7: the project-owned editor holds layer operations and
        // field edits in a draft; Apply writes them as one undo step.
        auto layer=fixture(); QVERIFY(layer); MapCanvas canvas; layer->setEditCanvas(&canvas);
        auto *reg=qobject_cast<openswmmvis::lid::LidControlRegistry *>(layer->ensureLidControlRegistry());
        openswmmvis::ui::LidControlEditorDialog editor(reg,layer.get()); editor.openForLidControl("NODE_A"); QCoreApplication::processEvents();
        auto *session=editor.draftSession(); QVERIFY(session); QVERIFY(editor.findChild<QPushButton *>("draftApply"));
        auto *layers=editor.findChild<QListWidget *>("lidLiveLayerOrder"); QVERIFY(layers); QCOMPARE(layers->count(),4);
        const double alpha=Model::lidProfile(layer.get(),"NODE_A")["node/1/1/r/1"].toDouble();
        auto *materials=editor.findChild<QPushButton *>("lidInitializeMaterials"); QVERIFY(materials); materials->click(); QCoreApplication::processEvents();
        QVERIFY(session->hasDraft()); QCOMPARE(Model::lidProfile(layer.get(),"NODE_A")["node/1/1/r/1"].toDouble(),alpha); QCOMPARE(canvas.undoStack()->count(),0);
        auto *flow=editor.findChild<QComboBox *>("lidField:flowModel"); QVERIFY(flow); flow->setCurrentIndex(flow->findData(1));
        QTRY_VERIFY(editor.findChild<QSpinBox *>("lidField:flowCells"));
        QVERIFY(!Model::applyValues(layer.get(),DC::DataLIDControls,{"NODE_A"},"flowCells",1.5,nullptr));
        auto *physical=editor.findChild<QDoubleSpinBox *>("lidField:node/1/1/p/0"); QVERIFY(physical);
        layers->setCurrentRow(1); QCoreApplication::processEvents(); // MEDIA 1: its tab shows
        const auto image=editor.grab().toImage();
        physical->setFocus(); physical->selectAll();
        QTest::keyClicks(physical,"15"); QTest::keyClick(physical,Qt::Key_Return);
        QCoreApplication::processEvents(); QVERIFY(editor.grab().toImage()!=image);
        QCOMPARE(Model::lidProfile(layer.get(),"NODE_A")["node/1/1/p/0"].toDouble(),10.);
        QVERIFY2(session->apply(),qPrintable(session->message()));
        QCOMPARE(Model::lidProfile(layer.get(),"NODE_A")["node/1/1/p/0"].toDouble(),15.); QCOMPARE(Model::lidProfile(layer.get(),"NODE_A")["node/1/1/r/1"].toDouble(),1.);
        QCOMPARE(Model::lidProfile(layer.get(),"NODE_A")["flowModel"].toInt(),1); QCOMPARE(canvas.undoStack()->count(),1);
        canvas.undoStack()->undo(); QCoreApplication::processEvents();
        QCOMPARE(Model::lidProfile(layer.get(),"NODE_A")["node/1/1/p/0"].toDouble(),10.); QCOMPARE(Model::lidProfile(layer.get(),"NODE_A")["flowModel"].toInt(),0);
        const auto beforeInsert=Model::lidProfile(layer.get(),"NODE_A");
        QVERIFY(Model::editLidProfile(layer.get(),"NODE_A","insert",2,1,canvas.undoStack()));
        QCOMPARE(Model::lidProfile(layer.get(),"NODE_A")["_kinds"].toList().size(),5);
        canvas.undoStack()->undo(); QCOMPARE(Model::lidProfile(layer.get(),"NODE_A"),beforeInsert);
        QCoreApplication::processEvents();
        QVERIFY(editor.grab().save(QDir(folder()).filePath("draft-lid-editor.png")));
        const auto review=QDir(folder()).filePath("LID_Complete_Properties_Review.inp");
        QCOMPARE(swmm_model_write(layer->engine(),review.toUtf8()),SWMM_OK);
        editor.close(); QCOMPARE(Model::lidProfile(layer.get(),"NODE_A")["node/1/1/p/0"].toDouble(),10.);
    }
    void validationAndIndependentReplay()
    {
        auto layer = fixture(); QVERIFY(layer); QUndoStack undo; QString error;
        const auto before = state(layer.get(), DC::DataLIDControls, "BC_A");
        for (const auto key : {"soilPorosity", "soilFc", "soilWp"})
            QVERIFY(!Model::applyValues(layer.get(), DC::DataLIDControls, targets("BC"), key, QString(key) == "soilWp" ? .5 : .1, &undo, &error));
        for (auto value : {QVariant(-1), QVariant("nan"), QVariant("junk")})
            QVERIFY(!Model::applyValues(layer.get(), DC::DataLIDControls, targets("BC"), "surfStorage", value, &undo));
        QVERIFY(!Model::applyValues(layer.get(), DC::DataLIDControls, {"BC_A", "RB_A"}, "soilKsat", .8, &undo));
        QVERIFY(!Model::applyValues(layer.get(), DC::DataLIDControls, targets("NODE"), "surfStorage", 1, &undo));
        QCOMPARE(state(layer.get(), DC::DataLIDControls, "BC_A"), before); QCOMPARE(undo.count(), 0);
        QVERIFY(Model::applyValues(layer.get(), DC::DataLIDControls, targets("BC"), "soilKsat", .8, &undo));
        QCOMPARE(swmm_lid_set_soil(layer->engine(), swmm_lid_index(layer->engine(), "BC_A"), 8, .5, .25, .1, .8, 99), SWMM_OK);
        undo.undo(); QCOMPARE(state(layer.get(), DC::DataLIDControls, "BC_A").value("soilKslope").toDouble(), 99.0);
        QCOMPARE(state(layer.get(), DC::DataLIDControls, "BC_A").value("soilKsat").toDouble(), .3);
        QVERIFY(undo.canRedo());
        auto *lidReg = qobject_cast<openswmmvis::lid::LidControlRegistry *>(layer->ensureLidControlRegistry());
        lidReg->findByName("BC_B")->setSurfStorage(99);
        DataObjectAggregateSelection draft(layer.get(), DC::DataLIDControls, {"BC_A", "BC_B"}, &undo);
        QVERIFY(!draft.setValue(column(*draft.source(), "surfStorage"), 5));
        QCOMPARE(lidReg->findByName("BC_B")->surfStorage(), 99.0);
        QVERIFY(lidReg->findByName("BC_B")->dirty());
        QVERIFY(!Model::applyValues(layer.get(), DC::DataSnowpacks, targets("DD"), snowKey(22), .9, &undo));
        QVERIFY(!Model::applyValues(layer.get(), DC::DataSnowpacks, targets("DD"), snowKey(0), .9, &undo));
        QVERIFY(!Model::applyValues(layer.get(), DC::DataSnowpacks, targets("DD"), "removalSubcatch", "", &undo));
        QVERIFY(undo.canRedo());
        QVERIFY(!Model::applyValues(layer.get(), DC::DataSnowpacks, targets("DD"), snowKey(27), .2, &undo));
    }
    void inheritanceDestinationAndLifetime()
    {
        auto layer = fixture(); QVERIFY(layer); auto e = layer->engine(); QUndoStack undo;
        QCOMPARE(swmm_snow_set_model(e, 1), SWMM_OK); emit layer->optionsChanged({"SNOW_MODEL"});
        const auto before = state(layer.get(), DC::DataSnowpacks, "Inherited_A");
        QVERIFY(Model::applyValues(layer.get(), DC::DataSnowpacks, targets("Inherited"), snowKey(27), .45, &undo));
        QCOMPARE(state(layer.get(), DC::DataSnowpacks, "Inherited_A").value("modelKind").toInt(), 1);
        undo.undo(); QCOMPARE(state(layer.get(), DC::DataSnowpacks, "Inherited_A"), before); undo.redo();
        QVERIFY(Model::applyValues(layer.get(), DC::DataSnowpacks, targets("Inherited"), "modelKind", -1, &undo));
        QCOMPARE(state(layer.get(), DC::DataSnowpacks, "Inherited_A").value(snowKey(27)).toDouble(), 0.0);
        undo.undo(); QCOMPARE(state(layer.get(), DC::DataSnowpacks, "Inherited_A").value(snowKey(27)).toDouble(), .45);
        undo.clear();
        QVERIFY(Model::applyValues(layer.get(), DC::DataSnowpacks, targets("DD"), "removalSubcatch", "SC2", &undo));
        QVERIFY(layer->applyRename("SC2", "RenamedCatch", DC::kKindCatch));
        undo.undo(); QCOMPARE(state(layer.get(), DC::DataSnowpacks, "DD_A").value("removalSubcatch").toString(), QString("SC1"));
        undo.redo(); QCOMPARE(state(layer.get(), DC::DataSnowpacks, "DD_A").value("removalSubcatch").toString(), QString("RenamedCatch"));
        DataObjectAggregateSelection selection(layer.get(), DC::DataSnowpacks, targets("DD"), &undo);
        const int col = column(*selection.source(), snowKey(21)); QVERIFY(selection.stage(col, 3));
        auto *reg = qobject_cast<openswmmvis::snowpack::SnowpackRegistry *>(layer->ensureSnowpackRegistry());
        reg->remove(reg->findByName("DD_B")); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QCoreApplication::processEvents();
        QVERIFY(reg->create("DD_B")); QCoreApplication::processEvents(); QVERIFY(!selection.apply(nullptr));
        DataObjectAggregateSelection units(layer.get(), DC::DataSnowpacks, {"DD_A", "DD_C"}, &undo);
        QCOMPARE(swmm_snow_set_model(e, 0), SWMM_OK); emit layer->optionsChanged({"SNOW_MODEL"});
        QVERIFY(!units.setValue(column(*units.source(), snowKey(21)), 4));
    }
    void widgetsEditorsAndBrowser()
    {
        auto layer = fixture(); QVERIFY(layer); MapCanvas canvas; layer->setEditCanvas(&canvas);
        PropertiesPanel panel; panel.setProject(layer.get()); panel.resize(1000, 1000); panel.show();
        panel.showSelection(&canvas, {{SWMMObjectRef::LIDControl, "BC_A"}, {SWMMObjectRef::LIDControl, "BC_B"}, {SWMMObjectRef::LIDControl, "RB_A"}});
        auto *tree = panel.findChild<QTreeView *>(); auto *scope = panel.findChild<QComboBox *>("propertiesSelectionScope"); QVERIFY(tree && scope);
        int bc = -1; for (int i = 1; i < scope->count(); ++i) if (scope->itemText(i).contains("Bio-Retention")) bc = i;
        QVERIFY(bc > 0); scope->setCurrentIndex(bc);
        auto cell = findProperty(tree->model(), "Soil — Conductivity"); QVERIFY(cell.isValid());
        openswmmvis::ui::LidControlEditorDialog lidEditor(qobject_cast<openswmmvis::lid::LidControlRegistry *>(layer->ensureLidControlRegistry()), layer.get());
        lidEditor.openForLidControl("BC_A");
        for (auto key : {Qt::Key_Tab, Qt::Key_Escape}) {
            tree->edit(cell); auto *spin = tree->findChild<QDoubleSpinBox *>(); QVERIFY(spin); spin->setFocus();
            if (key == Qt::Key_Escape) { spin->selectAll(); QTest::keyClicks(spin, ".9"); }
            QTest::keyClick(spin, key); QCOMPARE(canvas.undoStack()->count(), 0); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
        tree->edit(cell); auto *spin = tree->findChild<QDoubleSpinBox *>(); QVERIFY(spin); spin->setFocus(); spin->selectAll(); QTest::keyClicks(spin, ".75"); QTest::keyClick(spin, Qt::Key_Return);
        QTRY_COMPARE(canvas.undoStack()->count(), 1); QCOMPARE(lidEditor.currentProvider()->soilKsat(), .75);
        auto *snowReg = qobject_cast<openswmmvis::snowpack::SnowpackRegistry *>(layer->ensureSnowpackRegistry());
        openswmmvis::ui::SnowpackEditorDialog snowEditor(snowReg, layer.get()); snowEditor.openForSnowpack("DD_A");
        panel.showSelection(&canvas, {{SWMMObjectRef::Snowpack, "DD_A"}, {SWMMObjectRef::Snowpack, "DD_B"}});
        cell = findProperty(tree->model(), "Removal — Threshold Depth"); QVERIFY(cell.isValid());
        QVERIFY(tree->model()->setData(cell, 2.5)); QCOMPARE(snowEditor.findChild<QDoubleSpinBox *>("snowParameter21")->value(), 2.5);
        auto *depth = snowEditor.findChild<QDoubleSpinBox *>("snowParameter21"); depth->setValue(3.5);
        // The editor holds the edit until Apply: the model still has 2.5 (Apply/Revert plan D1).
        QCOMPARE(state(layer.get(), DC::DataSnowpacks, "DD_A").value(snowKey(21)).toDouble(), 2.5);
        QCOMPARE(canvas.undoStack()->count(), 2); QVERIFY(snowEditor.draftSession()->apply());
        QCOMPARE(state(layer.get(), DC::DataSnowpacks, "DD_A").value(snowKey(21)).toDouble(), 3.5);
        QCOMPARE(canvas.undoStack()->count(), 3); canvas.undoStack()->undo(); QCOMPARE(depth->value(), 2.5);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        cell = findProperty(tree->model(), "Snow Model"); QVERIFY(cell.isValid()); tree->edit(cell);
        auto *choice = tree->findChild<QComboBox *>(); QVERIFY(choice);
        const int ueb = choice->findData(1); QVERIFY(ueb >= 0); choice->setCurrentIndex(ueb);
        QMetaObject::invokeMethod(choice, "activated", Q_ARG(int, ueb));
        QTRY_COMPARE(canvas.undoStack()->count(), 3);
        QCOMPARE(state(layer.get(), DC::DataSnowpacks, "DD_A").value("modelKind").toInt(), 1);
        QCOMPARE(snowEditor.findChild<QComboBox *>("snowModelKind")->currentData().toInt(), 1);
        QVERIFY(findProperty(tree->model(), "UEB — Forest Fraction").flags() & Qt::ItemIsEditable);
        snowReg->findByName("DD_Control")->setParam(21, 999); // Close must not flush an unrelated draft.
        snowEditor.accept();
        double a,b,c,d,e,f; QCOMPARE(swmm_snowpack_get_removal(layer->engine(), swmm_snowpack_index(layer->engine(), "DD_Control"), &a,&b,&c,&d,&e,&f), SWMM_OK); QCOMPARE(a, 1.0);
        SelectionManager selection; ObjectBrowserPanel browser; browser.setProject(layer.get(), &selection, &canvas);
        connect(&selection, &SelectionManager::selectionChanged, &panel, [&](const QSet<SWMMObjectRef> &current, const QSet<SWMMObjectRef> &, const QSet<SWMMObjectRef> &) { panel.showSelection(&canvas, current.values()); });
        auto *objects = browser.findChild<QTreeView *>(); QVERIFY(objects); QItemSelection picks;
        std::function<void(const QModelIndex &)> collect = [&](const QModelIndex &parent) {
            for (int r=0; r<objects->model()->rowCount(parent); ++r) {
                const auto index=objects->model()->index(r,0,parent); const auto ref=index.data(SWMMObjectTreeModel::RoleObjectRef).value<SWMMObjectRef>();
                if (ref.objectType==SWMMObjectRef::Snowpack && targets("UEB").contains(ref.name)) picks.select(index,index); collect(index);
            }
        }; collect({}); QCOMPARE(picks.size(), 3);
        objects->selectionModel()->select(picks,QItemSelectionModel::ClearAndSelect|QItemSelectionModel::Rows);
        cell=findProperty(tree->model(),"UEB — Forest Fraction"); QVERIFY(cell.isValid()); QVERIFY(tree->model()->setData(cell,.6));
        for (const auto &name:targets("UEB")) QCOMPARE(state(layer.get(),DC::DataSnowpacks,name).value(snowKey(27)).toDouble(),.6);
    }
    void sortedRowsAndReview()
    {
        auto layer=fixture(); QVERIFY(layer); QUndoStack undo; Model model; model.setSource(layer.get(), DC::DataLIDControls); model.setUndoStack(&undo);
        QSortFilterProxyModel proxy; proxy.setSourceModel(&model); proxy.setFilterKeyColumn(0); proxy.setFilterRegularExpression("^PP_[ABC]$"); proxy.sort(0,Qt::DescendingOrder);
        QList<int> rows; for(int i=0;i<proxy.rowCount();++i) rows << proxy.mapToSource(proxy.index(i,0)).row(); QCOMPARE(rows.size(),3);
        QVERIFY(model.setDataForRows(column(model,"paveKsat"),rows,.875)); QCOMPARE(undo.count(),1);
        QVERIFY(!model.setDataForRows(column(model,"paveKsat"),rows+QList<int>{-1},.5));
        const auto path=QDir(folder()).filePath("LID_Snowpack_Properties_Review.inp"); QCOMPARE(swmm_model_write(layer->engine(),path.toUtf8().constData()),SWMM_OK);
    }
    void launchedEditorCloseAndNodePreservation()
    {
        auto layer = fixture(); QVERIFY(layer); MapCanvas canvas; layer->setEditCanvas(&canvas);
        const auto node = lidRows(layer.get(), "NODE_A"); QVERIFY(!node.isEmpty());
        QVERIFY(Model::applyValues(layer.get(), DC::DataLIDControls, {"BC_A", "RG_A", "PP_A"}, "soilKsat", .875, canvas.undoStack()));
        QCOMPARE(lidRows(layer.get(), "NODE_A"), node);
        Model model; model.setSource(layer.get(), DC::DataLIDControls);
        const int nodeRow = model.rowForName("NODE_A");
        QCOMPARE(model.index(nodeRow, column(model, "nodeLayers")).data(Qt::EditRole).toInt(), 4);
        QVERIFY(!(model.index(nodeRow, column(model, "type")).flags() & Qt::ItemIsEditable));
        QVERIFY(model.index(nodeRow, column(model, "node/1/1/p/0")).flags() & Qt::ItemIsEditable);
        QWidget host;
        QVERIFY(ComprehensiveEditorRegistry::instance().openForObject(DC::DataSnowpacks, layer.get(), canvas.undoStack(), "DD_A", &host));
        auto *editor = host.findChild<openswmmvis::ui::SnowpackEditorDialog *>(); QVERIFY(editor);
        auto *reg = qobject_cast<openswmmvis::snowpack::SnowpackRegistry *>(layer->ensureSnowpackRegistry());
        reg->findByName("DD_Control")->setParam(21, 999);
        editor->close(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        double a,b,c,d,e,f;
        QCOMPARE(swmm_snowpack_get_removal(layer->engine(), swmm_snowpack_index(layer->engine(), "DD_Control"), &a,&b,&c,&d,&e,&f), SWMM_OK);
        QCOMPARE(a, 1.0);
    }
    void destinationDeletionStaleAndClosedContexts()
    {
        auto layer = fixture(); QVERIFY(layer); QUndoStack undo;
        QVERIFY(Model::applyValues(layer.get(), DC::DataSnowpacks, targets("DD"), "removalSubcatch", "sc2", &undo));
        const auto before = state(layer.get(), DC::DataSnowpacks, "DD_A");
        QCOMPARE(before.value("removalSubcatch").toString(), QString("SC2"));
        QCOMPARE(swmm_subcatch_delete(layer->engine(), swmm_subcatch_index(layer->engine(), "SC1"), nullptr), SWMM_OK);
        emit layer->geometryChanged();
        QCOMPARE(swmm_subcatch_add(layer->engine(), "SC1"), SWMM_OK);
        undo.undo(); QCOMPARE(state(layer.get(), DC::DataSnowpacks, "DD_A"), before);
        DataObjectAggregateSelection selection(layer.get(), DC::DataSnowpacks, targets("DD"), &undo);
        const int col = column(*selection.source(), snowKey(21)); QVERIFY(selection.stage(col, 4));
        const int pack = swmm_snowpack_index(layer->engine(), "DD_B");
        QCOMPARE(swmm_snowpack_set_removal(layer->engine(), pack, 3, .1, .1, .1, .1, .1), SWMM_OK);
        QString error; QVERIFY(!selection.apply(&error)); QVERIFY(error.contains("changed"));
        QCOMPARE(state(layer.get(), DC::DataSnowpacks, "DD_A").value(snowKey(21)).toDouble(), 1.0);
        openswmmvis::ui::SnowpackEditorDialog editor(qobject_cast<openswmmvis::snowpack::SnowpackRegistry *>(layer->ensureSnowpackRegistry()), layer.get());
        emit layer->engineAboutToClose(); QVERIFY(!selection.setValue(col, 5)); QVERIFY(!editor.isEnabled());
    }
};
QTEST_MAIN(TestLidSnowpackBulk)
#include "test_lidsnowpackbulk.moc"
