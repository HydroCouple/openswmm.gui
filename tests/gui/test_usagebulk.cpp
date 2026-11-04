#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "selection/selectionmanager.h"
#include "ui/dialogs/linkcompoundeditdialog.h"
#include "ui/dialogs/subcatchcompoundeditdialog.h"
#include "ui/panels/attributetablepanel.h"
#include "ui/panels/propertiespanel.h"
#include "ui/panels/usagetablemodel.h"
#include "ui/properties/linkcompoundeditbutton.h"
#include "ui/properties/subcatchcompoundeditbutton.h"
#include "ui/properties/usageeditbutton.h"
#include "ui/properties/swmmnodepropertyadapter.h"
#include <openswmm/engine/openswmm_edit.h>
#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_subcatchments.h>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableView>
#include <QTest>
#include <QTimer>
#include <QTreeView>
#include <cmath>
#include <memory>
namespace
{
QString folder()
{
    return QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA"))
        .absoluteFilePath("../../../workplans/bulk-lid-inlets/review");
}
std::unique_ptr<SWMMModelLayer> fixture(bool si = false)
{
    QFile source(
        QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA")).filePath("street_inlet_local.inp"));
    if (!source.open(QIODevice::ReadOnly))
        return {};
    auto text = source.readAll();
    if (si)
        text.replace("FLOW_UNITS           CFS", "FLOW_UNITS           CMS");
    text.replace("[JUNCTIONS]", "[JUNCTIONS]\nUnusedNode 80 5 0 0 0");
    text.replace("[INLETS]", "[INLETS]\nUnusedInlet GRATE 2 2 P_BAR-50");
    text +=
        "\n[JUNCTIONS]\nJ3 100 1 0 0 0\nJ4 99 1 0 0 0\nJ5 100 1 0 0 0\nJ6 99 1 0 0 0\nCAP2 85 6 0 0 0\n"
        "[CONDUITS]\nC2 J3 J4 400 .016 0 0\nControlL J5 J6 400 .016 0 0\n"
        "[XSECTIONS]\nC2 STREET ST1 0 0 0 1\nControlL STREET ST1 0 0 0 1\n"
        "[INLETS]\nCURB2 CURB 3 .3 VERTICAL\nDROP DROP_GRATE 2 2 P_BAR-50\n"
        "[RAINGAGES]\nRGage INTENSITY 0:15 1 TIMESERIES TS1\n[TIMESERIES]\nTS1 01/01/2026 00:00 0\nTS1 00:15 0\n"
        "[SUBCATCHMENTS]\nS1 RGage J1 5 40 400 1 0\nS2 RGage J3 4 30 300 1 0\nS3 RGage J5 3 20 200 1 0\nControlS RGage J1 3 20 200 1 0\nDup RGage J1 3 20 200 1 0\n"
        "[SUBAREAS]\nS1 .02 .2 .1 .2 25 OUTLET\nS2 .02 .2 .1 .2 25 OUTLET\nS3 .02 .2 .1 .2 25 OUTLET\nControlS .02 .2 .1 .2 25 OUTLET\nDup .02 .2 .1 .2 25 OUTLET\n"
        "[INFILTRATION]\nS1 4 .5 4 7 0\nS2 4 .5 4 7 0\nS3 4 .5 4 7 0\nControlS 4 .5 4 7 0\nDup 4 .5 4 7 0\n"
        "[LID_CONTROLS]\nUnusedLid RG\nRG RG\nBC BC\n"
        "[LID_USAGE]\nS1 RG 2 10.1234567890123 1.234567890123 25.1234567890123 40 0 \"reports/S1 old.txt\" SEWER 13\nS1 BC 1 7 2 35 10 1 * * 12\n"
        "S2 RG 4 20.1234567890123 2.345678901234 45 50 1 \"reports/S2 old.txt\" ControlS 15\nS2 BC 2 8 3 55 20 0 * * 11\nS3 BC 3 9 4 65 30 0 * * 10\n"
        "ControlS RG 5 30 3 60 25 1 * SEWER 5\nDup RG 1 11 1 10 20 0 * * 3\nDup RG 2 22 2 20 30 1 * SEWER 4\nDup BC 3 33 3 30 40 0 * * 5\n"
        "[COORDINATES]\nJ3 0 200\nJ4 400 200\nJ5 0 400\nJ6 400 400\nCAP2 100 -200\n"
        "[POLYGONS]\nS1 0 700\nS1 300 700\nS1 300 900\nS1 0 900\nS2 400 700\nS2 700 700\nS2 700 900\nS2 400 900\nS3 800 700\nS3 1100 700\nS3 1100 900\nS3 800 900\nControlS 1200 700\nControlS 1500 700\nControlS 1500 900\nControlS 1200 900\nDup 1600 700\nDup 1900 700\nDup 1900 900\nDup 1600 900\n";
    QDir().mkpath(folder());
    QString path =
        QDir(folder()).filePath(si ? "LID_Inlets_SI.inp" : "LID_Inlets_Properties_Review.inp");
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};
    out.write(text);
    out.close();
    auto layer = std::make_unique<SWMMModelLayer>(path);
    QList<QString> warnings, errors;
    if (!layer->loadModel(warnings, errors)) {
        qWarning().noquote() << errors.join(' ');
        return {};
    }
    for (auto pair :
         {qMakePair(QString("C1"), QString("IJ1")), qMakePair(QString("C2"), QString("IJ2")),
          qMakePair(QString("ControlL"), QString("IJC"))})
        if (!layer->applyInsertInletJunction(pair.first, .5, pair.second, pair.first + "b",
                                             "GRATE1", "SEWER", nullptr, nullptr, nullptr))
            return {};
    auto e = layer->engine();
    for (auto target : QList<UsageTarget>{{UsageTarget::Link, "C1"},
                                          {UsageTarget::Link, "C2"},
                                          {UsageTarget::Link, "ControlL"},
                                          {UsageTarget::Node, "IJ1"},
                                          {UsageTarget::Node, "IJ2"},
                                          {UsageTarget::Node, "IJC"}}) {
        bool second = target.name == "C2" || target.name == "IJ2";
        SWMM_InletUsage u{};
        u.host_kind =
            target.kind == UsageTarget::Node ? SWMM_INLET_HOST_NODE : SWMM_INLET_HOST_LINK;
        u.host_idx = target.kind == UsageTarget::Node
                         ? swmm_node_index(e, target.name.toUtf8().constData())
                         : swmm_link_index(e, target.name.toUtf8().constData());
        u.design_idx = swmm_inlet_index(e, second ? "CURB2" : "GRATE1");
        u.capture_node_idx = swmm_node_index(e, second ? "CAP2" : "SEWER");
        u.num_inlets = second ? 3 : 2;
        u.pct_clogged = second ? 24.75 : 22.2;
        u.flow_limit = second ? 2.5 : 1.25;
        u.local_depress = second ? .2 : .1;
        u.local_width = second ? .8 : .5;
        u.placement = second ? SWMM_INLET_ON_SAG : SWMM_INLET_AUTOMATIC;
        if (!layer->applySetInletUsage(u))
            return {};
    }
    if (swmm_model_write(e, path.toUtf8().constData()) != SWMM_OK)
        return {};
    return layer;
}
UsageEditRef refFor(SWMMModelLayer *layer, QUndoStack *undo, int family)
{
    UsageEditRef r;
    r.layer = layer;
    r.engine = layer->engine();
    r.undoStack = undo;
    r.family = family == 0 ? UsageEditRef::Lid : UsageEditRef::Inlet;
    if (family == 0)
        r.targets = {{UsageTarget::Subcatchment, "S1"},
                     {UsageTarget::Subcatchment, "S2"},
                     {UsageTarget::Subcatchment, "S3"}};
    else if (family == 1)
        r.targets = {{UsageTarget::Link, "C1"}, {UsageTarget::Link, "C2"}};
    else
        r.targets = {{UsageTarget::Node, "IJ1"}, {UsageTarget::Node, "IJ2"}};
    return r;
}
QList<QVariantMap> lids(SWMM_Engine e, const QString &name)
{
    QList<QVariantMap> out;
    int sub = swmm_subcatch_index(e, name.toUtf8().constData());
    for (int row = 0; row < swmm_lid_usage_count(e); ++row) {
        SWMM_LidUsageRecord r{};
        if (swmm_lid_usage_get_record(e, row, &r) != SWMM_OK)
            return {};
        if (r.subcatch_idx != sub)
            continue;
        out << QVariantMap{{"control", QString::fromUtf8(swmm_lid_id(e, r.control_idx))},
                           {"units", r.number},
                           {"area", r.area},
                           {"width", r.width},
                           {"sat", r.init_sat},
                           {"imp", r.from_imperv},
                           {"toPerv", r.to_perv},
                           {"perv", r.from_perv},
                           {"drain", QString::fromUtf8(r.drain_to)},
                           {"report", QString::fromUtf8(r.report_file)},
                           {"absolute", QString::fromUtf8(r.report_file_resolved)}};
    }
    return out;
}
QVariantMap inlet(SWMM_Engine e, UsageTarget::Kind kind, const QString &name)
{
    int host = kind == UsageTarget::Node ? swmm_node_index(e, name.toUtf8().constData())
                                         : swmm_link_index(e, name.toUtf8().constData());
    int row = kind == UsageTarget::Node ? swmm_inlet_usage_find_node(e, host)
                                        : swmm_inlet_usage_find_link(e, host);
    if (row < 0)
        return {};
    SWMM_InletUsage r{};
    if (swmm_inlet_usage_get(e, row, &r) != SWMM_OK)
        return {};
    return {{"design", QString::fromUtf8(swmm_inlet_id(e, r.design_idx))},
            {"capture", QString::fromUtf8(swmm_node_id(e, r.capture_node_idx))},
            {"units", r.num_inlets},
            {"clog", r.pct_clogged},
            {"limit", r.flow_limit},
            {"height", r.local_depress},
            {"width", r.local_width},
            {"placement", r.placement}};
}
int rowFor(UsageTableModel *model, const QString &key, int occurrence = 0)
{
    for (int row = 0; row < model->rowCount(); ++row)
        if (model->rowKey(row) == key && occurrence-- == 0)
            return row;
    return -1;
}
QModelIndex propertyIndex(QAbstractItemModel *model, const QString &name,
                          const QModelIndex &parent = {})
{
    for (int row = 0; row < model->rowCount(parent); ++row) {
        auto index = model->index(row, 0, parent);
        if (index.data().toString() == name)
            return index.siblingAtColumn(1);
        auto child = propertyIndex(model, name, index);
        if (child.isValid())
            return child;
    }
    return {};
}
QTableView *usageTable(QWidget *owner)
{
    return owner->findChild<QTableView *>("usageTable");
}
QVariant proposal(int family, int column)
{
    if (family == 0) {
        if (column == 9)
            return "CAP2";
        if (column == 10)
            return "reports/new report.txt";
        if (column == 7)
            return 1;
        if (column == 2)
            return 7;
        return 12.5;
    }
    if (column == 2)
        return "CURB2";
    if (column == 3)
        return "CAP2";
    if (column == 4)
        return 7;
    if (column == 9)
        return int(SWMM_INLET_ON_GRADE);
    return 6.25;
}
QString fieldKey(int family, int column)
{
    return family == 0 ? QStringList{"",    "",       "units", "area",  "width", "sat",
                                     "imp", "toPerv", "perv",  "drain", "report"}
                             .value(column)
                       : QStringList{"",     "",      "design", "capture", "units",
                                     "clog", "limit", "height", "width",   "placement"}
                             .value(column);
}
QList<QVariantMap> comparable(QList<QVariantMap> records)
{
    for (auto &r : records)
        r.remove("report");
    return records;
}
}
class TestUsageBulk : public QObject
{
    Q_OBJECT
private slots:
    void eachFieldPreservesSiblings_data()
    {
        QTest::addColumn<int>("family");
        QTest::addColumn<int>("column");
        for (int family = 0; family < 3; ++family)
            for (int col = 2; col < (family == 0 ? 11 : 10); ++col)
                QTest::newRow(
                    QString("%1-%2").arg(family).arg(fieldKey(family, col)).toUtf8().constData())
                    << family << col;
    }
    void eachFieldPreservesSiblings()
    {
        QFETCH(int, family);
        QFETCH(int, column);
        auto layer = fixture();
        QVERIFY(layer);
        auto e = layer->engine();
        QUndoStack undo;
        auto ref = refFor(layer.get(), &undo, family);
        if (family == 0)
            ref.targets.removeLast();
        UsageTableModel model(ref);
        int row = family == 0 ? rowFor(&model, "RG") : 0;
        QVERIFY(row >= 0);
        auto index = model.index(row, column);
        QVERIFY(model.flags(index) & Qt::ItemIsEditable);
        auto value = proposal(family, column);
        if (family == 0) {
            auto a = lids(e, "S1"), b = lids(e, "S2"), control = lids(e, "ControlS");
            QVERIFY2(model.setData(index, value), qPrintable(model.lastError()));
            for (auto name : {"S1", "S2"}) {
                auto expected = QString(name) == "S1" ? a : b;
                expected[0][fieldKey(family, column)] = value;
                if (column == 10)
                    expected[0]["absolute"] = QDir(folder()).absoluteFilePath(value.toString());
                QCOMPARE(lids(e, name), expected);
            }
            QCOMPARE(undo.count(), 1);
            QVERIFY(model.setData(index, value));
            QCOMPARE(undo.count(), 1);
            undo.undo();
            QCOMPARE(lids(e, "S1"), a);
            QCOMPARE(lids(e, "S2"), b);
            undo.redo();
            QCOMPARE(lids(e, "ControlS"), control);
        } else {
            auto kind = family == 1 ? UsageTarget::Link : UsageTarget::Node;
            auto a = inlet(e, kind, ref.targets[0].name), b = inlet(e, kind, ref.targets[1].name),
                 control = inlet(e, UsageTarget::Node, "IJC");
            QVERIFY2(model.setData(index, value), qPrintable(model.lastError()));
            for (int n = 0; n < 2; ++n) {
                auto expected = n == 0 ? a : b;
                expected[fieldKey(family, column)] = value;
                QCOMPARE(inlet(e, kind, ref.targets[n].name), expected);
            }
            QCOMPARE(undo.count(), 1);
            QVERIFY(model.setData(index, value));
            QCOMPARE(undo.count(), 1);
            undo.undo();
            QCOMPARE(inlet(e, kind, ref.targets[0].name), a);
            QCOMPARE(inlet(e, kind, ref.targets[1].name), b);
            undo.redo();
            QCOMPARE(inlet(e, UsageTarget::Node, "IJC"), control);
        }
    }
    void propertiesWidgetsAndPersistence_data()
    {
        QTest::addColumn<int>("family");
        QTest::addColumn<bool>("si");
        for (int family = 0; family < 3; ++family) {
            QTest::newRow(QString("%1-US").arg(family).toUtf8().constData()) << family << false;
            QTest::newRow(QString("%1-SI").arg(family).toUtf8().constData()) << family << true;
        }
    }
    void propertiesWidgetsAndPersistence()
    {
        QFETCH(int, family);
        QFETCH(bool, si);
        auto layer = fixture(si);
        QVERIFY(layer);
        auto e = layer->engine();
        char units[32]{};
        QCOMPARE(swmm_options_get(e, "FLOW_UNITS", units, 32), SWMM_OK);
        QCOMPARE(QString::fromUtf8(units), si ? QString("CMS") : QString("CFS"));
        MapCanvas canvas;
        layer->setEditCanvas(&canvas);
        PropertiesPanel panel;
        panel.setProject(layer.get());
        QList<SWMMObjectRef> selection;
        auto ref = refFor(layer.get(), canvas.undoStack(), family);
        for (auto t : ref.targets)
            selection << SWMMObjectRef{t.kind == UsageTarget::Subcatchment
                                           ? SWMMObjectRef::Subcatchment
                                       : t.kind == UsageTarget::Link ? SWMMObjectRef::Link
                                                                     : SWMMObjectRef::Node,
                                       t.name};
        if (family == 2)
            selection << SWMMObjectRef{SWMMObjectRef::Node, "SEWER"};
        panel.showSelection(&canvas, selection);
        panel.resize(900, 1100);
        panel.show();
        if (family == 2) {
            auto *scope = panel.findChild<QComboBox *>("propertiesSelectionScope");
            QVERIFY(scope);
            int choice = -1;
            for (int i = 0; i < scope->count(); ++i)
                if (scope->itemText(i).startsWith("Inlet junctions"))
                    choice = i;
            QVERIFY(choice > 0);
            scope->setCurrentIndex(choice);
        }
        auto *tree = panel.findChild<QTreeView *>();
        QVERIFY(tree);
        tree->expandAll();
        const auto field = propertyIndex(tree->model(), family == 0   ? "LID Usage"
                                                        : family == 1 ? "Inlet Usage"
                                                                      : "Inlet Assignment");
        QVERIFY(field.isValid());
        tree->edit(field);
        QWidget *button = tree->indexWidget(field);
        QVERIFY(button);
        QSignalSpy dirty(layer.get(), &SWMMModelLayer::modelEdited);
        QTest::mouseClick(button->findChild<QPushButton *>(), Qt::LeftButton);
        auto *table = usageTable(&panel);
        QVERIFY(table);
        auto *model = dynamic_cast<UsageTableModel *>(table->model());
        QVERIFY(model);
        QCOMPARE(model->targetNames().size(), ref.targets.size());
        QCOMPARE(table->window()->findChild<QDialogButtonBox *>()->standardButtons(),
                 QDialogButtonBox::StandardButtons(QDialogButtonBox::Close));
        QList<QList<QVariantMap>> oldLids;
        QList<QVariantMap> oldInlets;
        for (const auto &t : ref.targets)
            if (family == 0)
                oldLids << lids(e, t.name);
            else
                oldInlets << inlet(e, t.kind, t.name);
        auto controlLids = lids(e, "ControlS");
        auto controlInlet = inlet(e, UsageTarget::Node, "IJC");
        panel.showSelection(&canvas, {{SWMMObjectRef::Subcatchment, "ControlS"}});
        int row = family == 0 ? rowFor(model, "RG") : 0;
        int col = family == 0 ? 3 : 4;
        auto target = model->index(row, col);
        table->edit(target);
        auto *line = table->findChild<QLineEdit *>();
        QVERIFY(line);
        line->setFocus();
        QTest::keyClicks(line, "999");
        QTest::keyClick(line, Qt::Key_Escape);
        QCOMPARE(canvas.undoStack()->count(), 0);
        QCOMPARE(dirty.count(), 0);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        table->edit(target);
        line = table->findChild<QLineEdit *>();
        QVERIFY(line);
        line->setFocus();
        line->selectAll();
        QTest::keyClicks(line, family == 0 ? "75.125" : "7");
        QTest::keyClick(line, si ? Qt::Key_Tab : Qt::Key_Return);
        QTRY_COMPARE(canvas.undoStack()->count(), 1);
        QList<QList<QVariantMap>> newLids;
        QList<QVariantMap> newInlets;
        for (int n = 0; n < ref.targets.size(); ++n) {
            const auto &t = ref.targets[n];
            if (family == 0) {
                auto expected = oldLids[n];
                int at = -1;
                for (int i = 0; i < expected.size(); ++i)
                    if (expected[i]["control"] == "RG")
                        at = i;
                if (at < 0) {
                    expected << QVariantMap{
                        {"control", "RG"},     {"units", 1},           {"area", 75.125},
                        {"width", 0.0},        {"sat", 0.0},           {"imp", 0.0},
                        {"toPerv", 0},         {"perv", 0.0},          {"drain", QString()},
                        {"report", QString()}, {"absolute", QString()}};
                } else
                    expected[at]["area"] = 75.125;
                newLids << lids(e, t.name);
                QCOMPARE(newLids.last(), expected);
            } else {
                auto expected = oldInlets[n];
                expected["units"] = 7;
                newInlets << inlet(e, t.kind, t.name);
                QCOMPARE(newInlets.last(), expected);
            }
        }
        QCOMPARE(lids(e, "ControlS"), controlLids);
        QCOMPARE(inlet(e, UsageTarget::Node, "IJC"), controlInlet);
        auto *dialog = qobject_cast<QDialog *>(table->window());
        QVERIFY(dialog);
        dialog->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        canvas.undoStack()->undo();
        for (int n = 0; n < ref.targets.size(); ++n)
            if (family == 0)
                QCOMPARE(lids(e, ref.targets[n].name), oldLids[n]);
            else
                QCOMPARE(inlet(e, ref.targets[n].kind, ref.targets[n].name), oldInlets[n]);
        canvas.undoStack()->redo();
        QString directory = QDir(folder()).filePath(QString("saved/%1-%2").arg(family).arg(si));
        QDir().mkpath(directory);
        QString path = QDir(directory).filePath("edited.inp");
        QCOMPARE(swmm_model_write(e, path.toUtf8().constData()), SWMM_OK);
        SWMMModelLayer reopened(path);
        QList<QString> warnings, errors;
        QVERIFY2(reopened.loadModel(warnings, errors), qPrintable(errors.join(' ')));
        for (int n = 0; n < ref.targets.size(); ++n)
            if (family == 0)
                QCOMPARE(comparable(lids(reopened.engine(), ref.targets[n].name)),
                         comparable(newLids[n]));
            else
                QCOMPARE(inlet(reopened.engine(), ref.targets[n].kind, ref.targets[n].name),
                         newInlets[n]);
    }
    void duplicateAndMissingLidPlacements()
    {
        auto layer = fixture();
        QVERIFY(layer);
        auto e = layer->engine();
        QUndoStack undo;
        auto ref = refFor(layer.get(), &undo, 0);
        ref.targets = {{UsageTarget::Subcatchment, "S1"}, {UsageTarget::Subcatchment, "Dup"}};
        UsageTableModel bulk(ref);
        int rg = rowFor(&bulk, "RG");
        QVERIFY(rg >= 0);
        QVERIFY(!(bulk.flags(bulk.index(rg, 3)) & Qt::ItemIsEditable));
        QVERIFY(!bulk.setData(bulk.index(rg, 3), 50));
        QCOMPARE(undo.count(), 0);
        ref.targets = {{UsageTarget::Subcatchment, "Dup"}};
        UsageTableModel single(ref);
        int first = rowFor(&single, "RG", 0), second = rowFor(&single, "RG", 1);
        QVERIFY(first >= 0 && second >= 0);
        auto before = lids(e, "Dup");
        QVERIFY(single.setData(single.index(second, 4), 5.5));
        auto expected = before;
        expected[1]["width"] = 5.5;
        QCOMPARE(lids(e, "Dup"), expected);
        QCOMPARE(undo.count(), 1);
        QVERIFY(single.removeAssignment(first));
        auto removed = expected;
        removed.removeFirst();
        QCOMPARE(lids(e, "Dup"), removed);
        undo.undo();
        QCOMPARE(lids(e, "Dup"), expected);
        undo.undo();
        QCOMPARE(lids(e, "Dup"), before);
    }
    void inletCreationRemovalAndInlineFacade()
    {
        auto layer = fixture();
        QVERIFY(layer);
        auto e = layer->engine();
        MapCanvas canvas;
        layer->setEditCanvas(&canvas);
        QVERIFY(layer->applyRemoveInletUsage(SWMM_INLET_HOST_NODE, swmm_node_index(e, "IJ2")));
        auto ref = refFor(layer.get(), canvas.undoStack(), 2);
        UsageTableModel model(ref);
        auto first = inlet(e, UsageTarget::Node, "IJ1"),
             control = inlet(e, UsageTarget::Node, "IJC");
        QVERIFY(!model.setData(model.index(0, 4), 5));
        QVERIFY(model.assignMissingInlets("CURB2", "CAP2"));
        QCOMPARE(canvas.undoStack()->count(), 1);
        QCOMPARE(inlet(e, UsageTarget::Node, "IJ1"), first);
        auto created = inlet(e, UsageTarget::Node, "IJ2");
        QCOMPARE(created.value("design").toString(), QString("CURB2"));
        QCOMPARE(created.value("units").toInt(), 1);
        QCOMPARE(created.value("clog").toDouble(), 0.0);
        canvas.undoStack()->undo();
        QVERIFY(inlet(e, UsageTarget::Node, "IJ2").isEmpty());
        canvas.undoStack()->redo();
        QVERIFY(model.removeAssignment(0));
        QVERIFY(inlet(e, UsageTarget::Node, "IJ1").isEmpty());
        QVERIFY(inlet(e, UsageTarget::Node, "IJ2").isEmpty());
        int flag = 0;
        QCOMPARE(swmm_node_is_inlet(e, swmm_node_index(e, "IJ1"), &flag), SWMM_OK);
        QCOMPARE(flag, 1);
        canvas.undoStack()->undo();
        QCOMPARE(inlet(e, UsageTarget::Node, "IJ1"), first);
        QCOMPARE(inlet(e, UsageTarget::Node, "IJ2"), created);
        canvas.undoStack()->clear();
        SWMMInletJunctionPropertyAdapter facade(e, "IJ1");
        facade.setModelLayer(layer.get());
        facade.setNumInlets(7);
        QCOMPARE(canvas.undoStack()->count(), 1);
        auto expected = first;
        expected["units"] = 7;
        QCOMPARE(inlet(e, UsageTarget::Node, "IJ1"), expected);
        canvas.undoStack()->undo();
        QCOMPARE(inlet(e, UsageTarget::Node, "IJ1"), first);
        QCOMPARE(inlet(e, UsageTarget::Node, "IJC"), control);
    }
    void invalidBatchHasNoPartialWrites()
    {
        auto layer = fixture();
        QVERIFY(layer);
        auto e = layer->engine();
        QUndoStack undo;
        auto ref = refFor(layer.get(), &undo, 1);
        ref.targets << UsageTarget{UsageTarget::Link, "D1"};
        UsageTableModel model(ref);
        auto before = inlet(e, UsageTarget::Link, "C1");
        QVERIFY(!model.assignMissingInlets("GRATE1", "SEWER"));
        QCOMPARE(undo.count(), 0);
        QCOMPARE(inlet(e, UsageTarget::Link, "C1"), before);
        ref = refFor(layer.get(), &undo, 1);
        UsageTableModel valid(ref);
        for (auto v : QVariantList{QString("nan"), QString("inf"), -1.0})
            QVERIFY(!valid.setData(valid.index(0, 6), v));
        QVERIFY(!valid.setData(valid.index(0, 4), 1.5));
        QVERIFY(!valid.setData(valid.index(0, 3), "IJ1"));
        QVERIFY(!valid.setData(valid.index(0, 2), "DROP"));
        QCOMPARE(undo.count(), 0);
        QCOMPARE(inlet(e, UsageTarget::Link, "C1"), before);
        auto lr = refFor(layer.get(), &undo, 0);
        UsageTableModel lid(lr);
        int row = rowFor(&lid, "RG");
        for (auto v : QVariantList{0.0, -1.0, QString("nan")})
            QVERIFY(!lid.setData(lid.index(row, 3), v));
        QVERIFY(!lid.setData(lid.index(row, 6), 101));
        QVERIFY(!lid.setData(lid.index(row, 9), "S2"));
        QCOMPARE(undo.count(), 0);
    }
    void tableEntryPoints_data()
    {
        QTest::addColumn<int>("family");
        for (int family = 0; family < 3; ++family)
            QTest::newRow(QString::number(family).toUtf8().constData()) << family;
    }
    void tableEntryPoints()
    {
        QFETCH(int, family);
        auto layer = fixture();
        QVERIFY(layer);
        MapCanvas canvas;
        SelectionManager selection;
        AttributeTablePanel panel;
        panel.setProject(layer.get(), &selection, &canvas);
        panel.showModelCategory(layer.get(), family == 0   ? SWMMModelLayer::CatSubcatchments
                                             : family == 1 ? SWMMModelLayer::CatConduits
                                                           : SWMMModelLayer::CatJunctions);
        panel.resize(1600, 700);
        panel.show();
        auto ref = refFor(layer.get(), canvas.undoStack(), family);
        QList<SWMMObjectRef> selected;
        for (const auto &t : ref.targets)
            selected << SWMMObjectRef{family == 0   ? SWMMObjectRef::Subcatchment
                                      : family == 1 ? SWMMObjectRef::Link
                                                    : SWMMObjectRef::Node,
                                      t.name};
        if (family == 2)
            selected << SWMMObjectRef{SWMMObjectRef::Node, "SEWER"};
        selection.select(QSet<SWMMObjectRef>(selected.begin(), selected.end()),
                         SelectionManager::Replace);
        QCoreApplication::processEvents();
        auto *view = panel.findChild<QTableView *>();
        QVERIFY(view);
        QModelIndex cell;
        const QString label = family == 0   ? "LID Usage"
                              : family == 1 ? "Inlet Usage"
                                            : "Inlet Assignment";
        for (int row = 0; row < view->model()->rowCount(); ++row)
            if (view->model()->index(row, 0).data().toString() == ref.targets[0].name)
                for (int col = 0; col < view->model()->columnCount(); ++col)
                    if (view->model()->headerData(col, Qt::Horizontal).toString() == label)
                        cell = view->model()->index(row, col);
        QVERIFY(cell.isValid());
        QSignalSpy edited(layer.get(), &SWMMModelLayer::modelEdited);
        QVERIFY(view->model()->setData(cell, cell.data(Qt::EditRole), Qt::EditRole));
        QCOMPARE(edited.count(), 0);
        view->scrollTo(cell);
        QCoreApplication::processEvents();
        bool opened = false;
        QTimer::singleShot(0, &panel, [&] {
            auto *menu = panel.findChild<QMenu *>(QString(), Qt::FindDirectChildrenOnly);
            if (!menu)
                return;
            for (auto *action : menu->actions())
                if ((family == 0 && action->text().startsWith("Edit LID Usage for 3")) ||
                    (family == 1 && action->text().startsWith("Edit Inlet Usage for 2")) ||
                    (family == 2 && action->text().contains("2 inlet junctions") &&
                     action->text().contains("1 excluded"))) {
                    action->trigger();
                    opened = true;
                    break;
                }
            menu->close();
        });
        QVERIFY(QMetaObject::invokeMethod(&panel, "onContextMenuRequested",
                                          Q_ARG(QPoint, view->visualRect(cell).center())));
        QVERIFY(opened);
        auto *table = usageTable(&panel);
        QVERIFY(table);
        auto *model = dynamic_cast<UsageTableModel *>(table->model());
        QVERIFY(model);
        QCOMPARE(model->targetNames().size(), ref.targets.size());
        // Choices commit on activation without another button or a focus change.
        const auto target =
            model->index(family == 0 ? rowFor(model, "RG") : 0, family == 0 ? 9 : 3);
        if (family == 0) {
            // The missing placement must be explicitly created by a positive area first.
            QVERIFY(model->setData(model->index(rowFor(model, "RG"), 3), 17.5));
            canvas.undoStack()->clear();
        }
        table->edit(target);
        auto *combo = table->findChild<QComboBox *>();
        QVERIFY(combo);
        combo->setCurrentIndex(combo->findData("CAP2"));
        QVERIFY(combo->currentIndex() >= 0);
        QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, combo->currentIndex()));
        QTRY_COMPARE(canvas.undoStack()->count(), 1);
        for (const auto &t : ref.targets)
            if (family == 0)
                QCOMPARE(lids(layer->engine(), t.name)[0]["control"].toString() == "RG"
                             ? lids(layer->engine(), t.name)[0]["drain"].toString()
                             : lids(layer->engine(), t.name)[1]["drain"].toString(),
                         QString("CAP2"));
            else
                QCOMPARE(inlet(layer->engine(), t.kind, t.name)["capture"].toString(),
                         QString("CAP2"));
    }
    void twoChoiceCreationAndConnectorRefresh()
    {
        auto layer = fixture();
        QVERIFY(layer);
        MapCanvas canvas;
        layer->setEditCanvas(&canvas);
        auto e = layer->engine();
        QVERIFY(layer->applyRemoveInletUsage(SWMM_INLET_HOST_NODE, swmm_node_index(e, "IJ2")));
        const auto old = inlet(e, UsageTarget::Node, "IJ1");
        const int oldLines = layer->inletConnectors().size();
        QWidget owner;
        auto *dialog = openUsageEditor(refFor(layer.get(), canvas.undoStack(), 2), &owner);
        auto *table = usageTable(dialog);
        QVERIFY(table);
        QTest::mouseClick(dialog->findChild<QPushButton *>("addMissingInletAssignments"),
                          Qt::LeftButton);
        auto *form = dialog->findChild<QDialog *>("newInletAssignments");
        QVERIFY(form);
        QCOMPARE(form->findChild<QDialogButtonBox *>()->standardButtons(),
                 QDialogButtonBox::StandardButtons(QDialogButtonBox::Close));
        auto *design = form->findChild<QComboBox *>("newInletDesign");
        auto *capture = form->findChild<QComboBox *>("newInletCapture");
        QVERIFY(design && capture);
        design->setCurrentIndex(design->findData("GRATE1"));
        QMetaObject::invokeMethod(design, "activated", Q_ARG(int, design->currentIndex()));
        QCOMPARE(canvas.undoStack()->count(), 0);
        QVERIFY(inlet(e, UsageTarget::Node, "IJ2").isEmpty());
        capture->setCurrentIndex(capture->findData("SEWER"));
        QMetaObject::invokeMethod(capture, "activated", Q_ARG(int, capture->currentIndex()));
        QTRY_COMPARE(canvas.undoStack()->count(), 1);
        QCOMPARE(inlet(e, UsageTarget::Node, "IJ1"), old);
        QCOMPARE(layer->inletConnectors().size(), oldLines + 1);
        auto endpoints = [&] {
            QList<QPointF> out;
            for (const auto &line : layer->inletConnectors())
                out << line.capture;
            return out;
        };
        auto before = endpoints();
        QSignalSpy repaint(layer.get(), &SWMMModelLayer::repaintRequested);
        auto *model = dynamic_cast<UsageTableModel *>(table->model());
        QVERIFY(model->setData(model->index(0, 3), "CAP2"));
        QVERIFY(repaint.count() > 0);
        QVERIFY(endpoints() != before);
        canvas.undoStack()->undo();
        QCOMPARE(endpoints(), before);
        canvas.undoStack()->redo();
        QVERIFY(endpoints() != before);
        // Parent teardown must discard a still-modified text editor.
        table->edit(model->index(0, 4));
        auto *line = table->findChild<QLineEdit *>();
        QVERIFY(line);
        line->setText("99");
        line->setModified(true);
        const auto stable = inlet(e, UsageTarget::Node, "IJ1");
        delete dialog;
        QCOMPARE(inlet(e, UsageTarget::Node, "IJ1"), stable);
    }
    void navigationButtonsIgnoreClosedModels()
    {
        auto layer = fixture();
        QVERIFY(layer);
        QWidget owner;
        LinkCompoundEditButton link(&owner);
        SubcatchCompoundEditButton subcatch(&owner);
        UsageEditButton inletButton(&owner);
        LinkCompoundEditRef lr;
        lr.layer = layer.get();
        lr.engine = layer->engine();
        lr.linkName = "C1";
        lr.kind = LinkCompoundEditRef::InletUsage;
        link.setValue(lr);
        SubcatchCompoundEditRef sr;
        sr.layer = layer.get();
        sr.engine = layer->engine();
        sr.subName = "S1";
        sr.kind = SubcatchCompoundEditRef::LidUsage;
        subcatch.setValue(sr);
        inletButton.setValue(refFor(layer.get(), nullptr, 2));
        layer.reset();
        for (auto *button : owner.findChildren<QPushButton *>())
            QTest::mouseClick(button, Qt::LeftButton);
        QVERIFY(owner.findChildren<QDialog *>().isEmpty());
    }
    void staleSourcesAndReferenceReuse()
    {
        auto layer = fixture();
        QVERIFY(layer);
        auto e = layer->engine();
        QUndoStack undo;
        auto ref = refFor(layer.get(), &undo, 2);
        UsageTableModel model(ref);
        QVERIFY(model.setData(model.index(0, 4), 7));
        SWMM_InletUsage external{};
        QCOMPARE(swmm_inlet_usage_get(e, swmm_inlet_usage_find_node(e, swmm_node_index(e, "IJ2")),
                                      &external),
                 SWMM_OK);
        external.local_width = 9.25;
        QCOMPARE(swmm_inlet_usage_set(e, &external, nullptr), SWMM_OK);
        auto first = inlet(e, UsageTarget::Node, "IJ1"),
             second = inlet(e, UsageTarget::Node, "IJ2");
        undo.undo();
        QCOMPARE(inlet(e, UsageTarget::Node, "IJ1"), first);
        QCOMPARE(inlet(e, UsageTarget::Node, "IJ2"), second);
        undo.clear();
        QVERIFY(model.setData(model.index(0, 4), 8));
        QVERIFY(layer->applyNodeDelete("SEWER"));
        QVERIFY(layer->applyNodeAdd("SEWER", 0, 200, -100));
        const auto afterDelete = inlet(e, UsageTarget::Node, "IJ2");
        undo.undo();
        QCOMPARE(inlet(e, UsageTarget::Node, "IJ2"), afterDelete);
        QVERIFY(!model.setData(model.index(0, 4), 9));
        undo.clear();
        // An unreadable last owner must reject before changing an earlier owner.
        auto lr = refFor(layer.get(), &undo, 0);
        lr.targets.removeLast();
        UsageTableModel lidsModel(lr);
        auto original = lids(e, "S1");
        int owner = swmm_subcatch_index(e, "S2");
        for (int row = 0; row < swmm_lid_usage_count(e); ++row) {
            SWMM_LidUsageRecord r{};
            QCOMPARE(swmm_lid_usage_get_record(e, row, &r), SWMM_OK);
            if (r.subcatch_idx == owner) {
                QCOMPARE(swmm_lid_usage_add(e, owner, r.control_idx, r.number, NAN, r.width,
                                            r.init_sat, r.from_imperv),
                         SWMM_OK);
                break;
            }
        }
        QVERIFY(!lidsModel.setData(lidsModel.index(rowFor(&lidsModel, "RG"), 3), 50));
        QCOMPARE(lids(e, "S1"), original);
        QCOMPARE(undo.count(), 0);
    }
    void typedIdentityAndLifecycle()
    {
        auto layer = fixture();
        QVERIFY(layer);
        auto e = layer->engine();
        QUndoStack undo;
        auto ref = refFor(layer.get(), &undo, 2);
        UsageTableModel model(ref);
        auto before = inlet(e, UsageTarget::Node, "IJ1");
        QVERIFY(model.setData(model.index(0, 4), 7));
        QCOMPARE(swmm_inlet_delete(e, swmm_inlet_index(e, "UnusedInlet"), nullptr), SWMM_OK);
        emit layer->dataObjectsChanged();
        QVERIFY(layer->applyNodeDelete("UnusedNode"));
        QVERIFY(layer->applyRename("IJ1", "Renamed", SWMMModelLayer::kKindNode));
        QVERIFY(layer->applyRename("SEWER", "Receiver", SWMMModelLayer::kKindNode));
        undo.undo();
        before["capture"] = "Receiver";
        QCOMPARE(inlet(e, UsageTarget::Node, "Renamed"), before);
        undo.redo();
        undo.clear();
        auto lr = refFor(layer.get(), &undo, 0);
        lr.targets.removeLast();
        UsageTableModel lid(lr);
        auto old = lids(e, "S1");
        QVERIFY(lid.setData(lid.index(rowFor(&lid, "RG"), 3), 44));
        QCOMPARE(swmm_lid_delete(e, swmm_lid_index(e, "UnusedLid"), nullptr), SWMM_OK);
        emit layer->dataObjectsChanged();
        QVERIFY(layer->applyRename("S1", "Catchment", SWMMModelLayer::kKindCatch));
        undo.undo();
        QCOMPARE(lids(e, "Catchment"), old);
        undo.redo();
        auto edited = lids(e, "Catchment");
        QVERIFY(layer->applySubcatchDelete("S2"));
        QVERIFY(layer->applySubcatchAdd("S2", {{0, 0}, {10, 0}, {10, 10}, {0, 0}}));
        undo.undo();
        QCOMPARE(lids(e, "Catchment"), edited);
        undo.clear();
        QWidget parent;
        auto active = refFor(layer.get(), &undo, 0);
        active.targets = {{UsageTarget::Subcatchment, "Catchment"}};
        auto *dialog = openUsageEditor(active, &parent);
        auto *table = usageTable(dialog);
        QVERIFY(table);
        table->edit(table->model()->index(0, 3));
        QPointer<QLineEdit> line = table->findChild<QLineEdit *>();
        QVERIFY(line);
        line->setText("123");
        line->setModified(true);
        layer.reset();
        if (line)
            QMetaObject::invokeMethod(line, "editingFinished");
    }
};
QTEST_MAIN(TestUsageBulk)
#include "test_usagebulk.moc"
