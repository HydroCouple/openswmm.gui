#include "layers/nodestructureedits.h"
#include "layers/swmmmodellayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "map/mapcanvas.h"
#include "render/renderers/categorizedrenderer.h"
#include "render/markersymbollayer.h"
#include "map/mapundostack.h"
#include "mesh/inpmeshreader.h"
#include "ui/panels/propertiespanel.h"
#include "ui/panels/swmmattributetablemodel.h"
#include "ui/panels/swmmaggregateselection.h"
#include "ui/properties/swmmnodepropertyadapter.h"
#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_2d.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_edit.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_gw2d.h>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QSignalSpy>
#include <QTest>
#include <QTreeView>
#include <cmath>
#include <memory>
namespace
{
QString folder()
{
    return QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA"))
        .absoluteFilePath("../../../workplans/bulk-node-structures/review");
}
std::unique_ptr<SWMMModelLayer> fixture(bool si = false)
{
    QFile base(QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA")).filePath("mini_2d.inp"));
    if (!base.open(QIODevice::ReadOnly))
        return {};
    auto text = base.readAll();
    if (!si)
        text.replace("FLOW_UNITS       CMS", "FLOW_UNITS       CFS");
    text.replace("[JUNCTIONS]", "[JUNCTIONS]\nUnusedNode 12 3 0 0 0");
    text +=
        "\n[MAP]\nUNITS " + QByteArray(si ? "METERS" : "FEET") +
        "\n[JUNCTIONS]\nJ2 10 3 0 0 0\nJ3 10 3 0 0 0\nControlJ 10 3 0 0 0\n"
        "[STORAGE]\nS1 10 5 0 FUNCTIONAL 0 0 100 0 0\nS2 10 5 0 FUNCTIONAL 0 0 110 0 0\nS3 10 5 0 FUNCTIONAL 0 0 120 0 0\nControlS 10 5 0 FUNCTIONAL 0 0 130 0 0\n"
        "[OUTFALLS]\nO2 8 FREE NO\n[CONDUITS]\nA1 S1 J1 100 .013 0 0\nAC ControlS J3 100 .013 0 0\nJ1 J3 O1 100 .013 0 0\n"
        "[WEIRS]\nA2 J2 S2 TRANSVERSE .3 .61 NO 0 0 0\n[XSECTIONS]\nA1 CIRCULAR 1 0 0 0 1\nA2 RECT_OPEN 1 2 0 0 1\nAC CIRCULAR 1 0 0 0 1\nJ1 CIRCULAR 1 0 0 0 1\n"
        "[COORDINATES]\nJ2 50 0\nJ3 100 0\nControlJ 150 0\nS1 0 50\nS2 50 50\nS3 100 50\nControlS 150 50\nO2 150 -50\n";
    QDir().mkpath(folder());
    QString path = QDir(folder()).filePath(si ? "Node_Structures_SI.inp"
                                              : "Node_Structures_Properties_Review.inp");
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
    auto e = layer->engine();
    for (const auto &name : {"UnusedLid", "StackA", "StackB", "Short"}) {
        if (swmm_lid_add(e, name, 8) != SWMM_OK)
            return {};
        double rain = si ? 25.4 : 1;
        QList<SWMM_LidNodeLayer> stack{{0, {6 * rain, .1}},
                                       {1, {12 * rain, .45, .2, .08, 2 * rain, 10 * rain, 3}},
                                       {2, {6 * rain, .4, 100 * rain}},
                                       {3, {.5 * rain, 0}}};
        if (QByteArray(name) == "StackB") {
            stack[1].params[0] = 24 * rain;
            stack[2].params[0] = 12 * rain;
        }
        if (QByteArray(name) == "Short")
            stack.removeAt(2);
        if (swmm_lid_node_layers_set(e, swmm_lid_index(e, name), stack.constData(), stack.size()) !=
            SWMM_OK)
            return {};
    }
    for (const auto &name : {"S1", "S2", "ControlS"}) {
        int node = swmm_node_index(e, name);
        if (swmm_node_set_lid(e, node, swmm_lid_index(e, "StackA"),
                              QByteArray(name) == "S1" ? 12.1234567890123 : 67.2345678901234) !=
            SWMM_OK)
            return {};
        if (swmm_node_set_lid_bottom(e, node, QByteArray(name) == "S2" ? 1 : 0) != SWMM_OK)
            return {};
    }
    for (const auto &name : {"A1", "A2", "AC"})
        if (swmm_lid_node_outlet_set(e, swmm_link_index(e, name), QByteArray(name) == "A1" ? 2 : 3,
                                     QByteArray(name) == "AC" ? 0 : 1) != SWMM_OK)
            return {};
    for (const auto &name : {"J1", "J3", "ControlJ"}) {
        nodecoupling2d::State s;
        if (!layer->nodeCoupling2D(name, &s))
            return {};
        s.authored = true;
        s.type = QByteArray(name) == "J1" ? 2 : 1;
        s.params = {{"D", "1.23456789012345"},
                    {"AREA", QByteArray(name) == "J1" ? "2.3" : "4.5"},
                    {"CREST", "MAX"},
                    {"CW", ".45"}};
        if (!nodecoupling2d::write(e, s))
            return {};
    }
    if (swmm_model_write(e, path.toUtf8().constData()) != SWMM_OK)
        return {};
    return layer;
}
nodecoupling2d::State coupling(SWMMModelLayer *layer, const QString &node)
{
    nodecoupling2d::State s;
    layer->nodeCoupling2D(node, &s);
    return s;
}
QVariantMap lid(SWMM_Engine e, const QString &name)
{
    int node = swmm_node_index(e, name.toUtf8().constData());
    SWMM_StorageLidState s{};
    if (swmm_storage_lid_get(e, node, &s) != SWMM_OK)
        return {};
    QVariantMap out{
        {"control", s.control < 0 ? QString() : QString::fromUtf8(swmm_lid_id(e, s.control))},
        {"sat", s.saturation},
        {"bottom", s.bottom},
        {"depth", s.max_depth_ft}};
    QVariantMap anchors;
    if (s.control >= 0)
        for (int link = 0; link < swmm_link_count(e); ++link) {
            SWMM_StorageLidAnchor a{};
            int from = -1, to = -1;
            swmm_link_get_from_node(e, link, &from);
            swmm_link_get_to_node(e, link, &to);
            if (from != node && to != node)
                continue;
            if (swmm_storage_lid_anchor_get(e, link, &a) != SWMM_OK || !a.layer)
                continue;
            anchors[QString::fromUtf8(swmm_link_id(e, link))] =
                QVariantList{a.layer, a.top, a.offset_up_ft, a.offset_down_ft, a.crest_ft};
        }
    out["anchors"] = anchors;
    return out;
}
QVariantMap persistedLid(SWMM_Engine engine, const QString &node)
{
    auto state = lid(engine, node);
    auto anchors = state["anchors"].toMap();
    for (auto it = anchors.begin(); it != anchors.end(); ++it) {
        int type = -1;
        swmm_link_get_type(engine, swmm_link_index(engine, it.key().toUtf8().constData()), &type);
        if (type == SWMM_LINK_WEIR || type == SWMM_LINK_OUTLET) {
            // INP authors their crest; the two base-link offset caches are rebuilt on load.
            // In-session Undo above still compares every raw geometry member exactly.
            auto values = it.value().toList();
            values.removeAt(3);
            values.removeAt(2);
            it.value() = values;
        }
    }
    state["anchors"] = anchors;
    return state;
}
QModelIndex field(SWMMAttributeTableModel &model, const QString &setter, const QString &name)
{
    auto specs = model.columnSpecs();
    for (int c = 0; c < specs.size(); ++c)
        if (specs[c].setter == setter)
            return model.index(model.rowForName(name), c);
    return {};
}
QModelIndex findProperty(QAbstractItemModel *model, const QString &name,
                         const QModelIndex &parent = {})
{
    for (int row = 0; row < model->rowCount(parent); ++row) {
        auto index = model->index(row, 0, parent);
        if (index.data().toString() == name || index.data().toString().startsWith(name + " ("))
            return index.siblingAtColumn(1);
        auto child = findProperty(model, name, index);
        if (child.isValid())
            return child;
    }
    return {};
}
bool apply(SWMMAttributeTableModel &model, const QStringList &nodes, const QString &setter,
           QVariant value, QUndoStack *undo, QString *error = nullptr)
{
    const auto index = field(model, setter, nodes.first());
    if (!index.isValid())
        return false;
    if (setter == "node_lid_control") {
        auto r = index.data(Qt::EditRole).value<DataObjectRef>();
        r.currentName = value.toString();
        value = QVariant::fromValue(r);
    }
    return model.applyScalarEdits(nodes, {{model.columnSpecs()[index.column()].key, value}}, undo,
                                  error);
}
}
class TestNodeStructureBulk : public QObject
{
    Q_OBJECT
private slots:
    void couplingFields_data()
    {
        QTest::addColumn<QString>("setter");
        QTest::addColumn<QVariant>("value");
        for (const auto &key : nodecoupling2d::paramKeys()) {
            QVariant v = key == "CREST"               ? QVariant("MIN")
                         : key == "LAW"               ? QVariant("WEIR_ORIFICE")
                         : key == "COEFFS"            ? QVariant("RUBINATO2017_LOWER")
                         : key == "LID_OPEN_FRACTION" ? QVariant("0.75")
                                                      : QVariant("1.76543210987654");
            QTest::newRow(qPrintable(key)) << "node_2d_param_" + key << v;
        }
        QTest::newRow("type") << QString("node_2d_coupling_type") << QVariant(3);
        QTest::newRow("class default") << QString("node_2d_coupling_type") << QVariant(-1);
        QTest::newRow("disable") << QString("node_2d_coupleable") << QVariant(0);
    }
    void couplingFields()
    {
        QFETCH(QString, setter);
        QFETCH(QVariant, value);
        auto layer = fixture();
        QVERIFY(layer);
        QUndoStack undo;
        bool outfall = setter.endsWith("SPREAD") || setter.endsWith("TAILWATER_RELAX");
        QStringList names = outfall ? QStringList{"O1", "O2"} : QStringList{"J1", "J3"};
        SWMMAttributeTableModel table;
        table.setSource(layer.get(),
                        outfall ? SWMMModelLayer::CatOutfalls : SWMMModelLayer::CatJunctions);
        QList<nodecoupling2d::State> before;
        for (const auto &n : names)
            before << coupling(layer.get(), n);
        const auto control = coupling(layer.get(), "ControlJ");
        QSignalSpy dirty(layer.get(), &SWMMModelLayer::modelEdited),
            repaint(layer.get(), &SWMMModelLayer::repaintRequested);
        QString error;
        QVERIFY2(apply(table, names, setter, value, &undo, &error), qPrintable(error));
        QCOMPARE(undo.count(), 1);
        QCOMPARE(dirty.count(), 1);
        QVERIFY(repaint.count() > 0);
        QList<nodecoupling2d::State> after;
        for (int i = 0; i < names.size(); ++i) {
            auto expected = before[i];
            if (setter == "node_2d_coupling_type") {
                expected.params.clear();
                expected.authored = value.toInt() != -1;
                expected.type = value.toInt() == -1
                                    ? nodecoupling2d::defaultType(layer->engine(), names[i])
                                    : value.toInt();
            } else if (setter == "node_2d_coupleable") {
                expected.authored = true;
                expected.type = 0;
                expected.params.clear();
            } else {
                expected.authored = true;
                expected.params[setter.mid(14)] = value.toString();
            }
            after << coupling(layer.get(), names[i]);
            QCOMPARE(after.last(), expected);
        }
        QVERIFY(apply(table, names, setter, value, &undo));
        QCOMPARE(undo.count(), 1);
        undo.undo();
        for (int i = 0; i < names.size(); ++i)
            QCOMPARE(coupling(layer.get(), names[i]), before[i]);
        undo.redo();
        for (int i = 0; i < names.size(); ++i)
            QCOMPARE(coupling(layer.get(), names[i]), after[i]);
        QCOMPARE(coupling(layer.get(), "ControlJ"), control);
    }
    void storageFieldsAndPersistence_data()
    {
        QTest::addColumn<bool>("si");
        QTest::addColumn<QString>("setter");
        for (bool si : {false, true})
            for (auto key : {"node_lid_control", "node_lid_saturation", "node_lid_bottom"})
                QTest::newRow(qPrintable(QString(key) + QString::number(si))) << si << QString(key);
    }
    void storageFieldsAndPersistence()
    {
        QFETCH(bool, si);
        QFETCH(QString, setter);
        auto layer = fixture(si);
        QVERIFY(layer);
        auto e = layer->engine();
        QUndoStack undo;
        SWMMAttributeTableModel table;
        table.setSource(layer.get(), SWMMModelLayer::CatStorage);
        const auto a = lid(e, "S1"), b = lid(e, "S2"), control = lid(e, "ControlS");
        auto renderer = std::make_unique<OpenSWMM::Render::CategorizedRenderer>();
        renderer->setClassifyAttribute("maxDepth");
        OpenSWMM::Render::MarkerSymbolLayerSpec marker;
        marker.fillColor = Qt::red;
        OpenSWMM::Render::SymbolStyle red, green;
        red.layers.append(marker.toSymbolLayer());
        marker.fillColor = Qt::green;
        green.layers.append(marker.toSymbolLayer());
        renderer->addCategory(
            {layer->identifyByName("S1", SWMMModelLayer::kKindNode)["maxDepth"].toString(),
             "Original depth", red});
        renderer->setFallbackSymbol(green);
        layer->setKindRenderer(SWMMModelLayer::CatStorage, std::move(renderer));
        const int row = swmm_node_index(e, "S1"), controlRow = swmm_node_index(e, "ControlS");
        QCOMPARE(layer->featureColor(SWMMModelLayer::CatStorage, row), QColor(Qt::red));
        QVariant value = setter == "node_lid_control"      ? QVariant("StackB")
                         : setter == "node_lid_saturation" ? QVariant(37.1234567890123)
                                                           : QVariant(1);
        QString error;
        QVERIFY2(apply(table, {"S1", "S2"}, setter, value, &undo, &error), qPrintable(error));
        QCOMPARE(undo.count(), 1);
        QCoreApplication::processEvents();
        const QColor edited = setter == "node_lid_control" ? QColor(Qt::green) : QColor(Qt::red);
        QCOMPARE(layer->featureColor(SWMMModelLayer::CatStorage, row), edited);
        QCOMPARE(layer->featureColor(SWMMModelLayer::CatStorage, controlRow), QColor(Qt::red));
        auto afterA = lid(e, "S1"), afterB = lid(e, "S2");
        for (auto pair : {qMakePair(a, afterA), qMakePair(b, afterB)}) {
            auto expected = pair.first;
            if (setter == "node_lid_saturation")
                expected["sat"] = value;
            else if (setter == "node_lid_bottom")
                expected["bottom"] = 1;
            else {
                expected["control"] = "StackB";
                expected["depth"] = pair.second["depth"];
                expected["anchors"] = pair.second["anchors"];
                QVERIFY(pair.first["depth"] != pair.second["depth"]);
            }
            QCOMPARE(pair.second, expected);
        }
        if (setter == "node_lid_control") {
            const auto oldUp = a["anchors"].toMap()["A1"].toList();
            const auto newUp = afterA["anchors"].toMap()["A1"].toList();
            const auto oldCrest = b["anchors"].toMap()["A2"].toList();
            const auto newCrest = afterB["anchors"].toMap()["A2"].toList();
            QVERIFY(oldUp[2] != newUp[2]);
            QVERIFY(oldCrest[4] != newCrest[4]);
            QCOMPARE(newUp[2].toDouble(), 3.0);
            QCOMPARE(newCrest[4].toDouble(), 1.0);
        }
        QCOMPARE(lid(e, "ControlS"), control);
        undo.undo();
        QCOMPARE(lid(e, "S1"), a);
        QCOMPARE(lid(e, "S2"), b);
        QCoreApplication::processEvents();
        QCOMPARE(layer->featureColor(SWMMModelLayer::CatStorage, row), QColor(Qt::red));
        undo.redo();
        QVERIFY(apply(table, {"S1", "S2"}, setter, value, &undo));
        QCOMPARE(undo.count(), 1);
        QCoreApplication::processEvents();
        QCOMPARE(layer->featureColor(SWMMModelLayer::CatStorage, row), edited);
        QString path = QDir(folder()).filePath(QString("saved-%1-%2.inp").arg(setter).arg(si));
        QCOMPARE(swmm_model_write(e, path.toUtf8().constData()), SWMM_OK);
        SWMMModelLayer reopened(path);
        QList<QString> warnings, errors;
        QVERIFY2(reopened.loadModel(warnings, errors), qPrintable(errors.join(' ')));
        QCOMPARE(persistedLid(reopened.engine(), "S1"), persistedLid(e, "S1"));
        QCOMPARE(persistedLid(reopened.engine(), "S2"), persistedLid(e, "S2"));
        QCOMPARE(persistedLid(reopened.engine(), "ControlS"), persistedLid(e, "ControlS"));
    }
    void clearAssignAndInvalidLast()
    {
        auto layer = fixture();
        QVERIFY(layer);
        auto e = layer->engine();
        QUndoStack undo;
        SWMMAttributeTableModel table;
        table.setSource(layer.get(), SWMMModelLayer::CatStorage);
        const auto a = lid(e, "S1"), b = lid(e, "S2"), missing = lid(e, "S3");
        QString error;
        QVERIFY(!apply(table, {"S1", "S2"}, "node_lid_control", "Short", &undo, &error));
        QCOMPARE(lid(e, "S1"), a);
        QCOMPARE(lid(e, "S2"), b);
        QCOMPARE(undo.count(), 0);
        QVERIFY(!apply(table, {"S1", "S2"}, "node_lid_saturation", NAN, &undo));
        QVERIFY(!apply(table, {"S1", "S3"}, "node_lid_bottom", 1, &undo));
        QVERIFY(!apply(table, {"S1", "S2"}, "node_lid_bottom", 2, &undo));
        QVERIFY(apply(table, {"S1", "S2"}, "node_lid_control", "", &undo));
        QVERIFY(lid(e, "S1")["control"].toString().isEmpty());
        QVERIFY(lid(e, "S1")["anchors"].toMap().isEmpty());
        QCOMPARE(lid(e, "S1")["depth"], a["depth"]);
        undo.undo();
        QCOMPARE(lid(e, "S1"), a);
        QCOMPARE(lid(e, "S2"), b);
        undo.clear();
        QVERIFY(apply(table, {"S1", "S3"}, "node_lid_control", "StackB", &undo));
        QCOMPARE(lid(e, "S3")["sat"].toDouble(), 0.0);
        QCOMPARE(lid(e, "S3")["bottom"].toInt(), 0);
        undo.undo();
        QCOMPARE(lid(e, "S3"), missing);
        QCOMPARE(lid(e, "S1"), a);
    }
    void wholeLayerFacadeIsAtomicAndInherited()
    {
        auto layer = fixture();
        QVERIFY(layer);
        MapCanvas canvas;
        layer->setEditCanvas(&canvas);
        QVERIFY(!nodestructure::applyFields(layer.get(), {"J1", "J3"},
                                            {{"node_2d_coupling_type", 1.5}}, canvas.undoStack()));
        auto a = coupling(layer.get(), "J1"), b = coupling(layer.get(), "J3"), x = a, y = b;
        x.params["D"] = "9.2";
        y.params["COI"] = "nan";
        QString error;
        QVERIFY(!layer->pushNodeCoupling2DEdits({x, y}, "invalid", &error));
        QCOMPARE(coupling(layer.get(), "J1"), a);
        QCOMPARE(coupling(layer.get(), "J3"), b);
        QCOMPARE(canvas.undoStack()->count(), 0);
        x.params["D"] = "1.2000";
        QVERIFY(layer->pushNodeCoupling2DEdits({x}, "one"));
        QCOMPARE(coupling(layer.get(), "J1").params["D"], QString("1.2"));
        canvas.undoStack()->undo();
        QCOMPARE(coupling(layer.get(), "J1"), a);
        canvas.undoStack()->clear();
        SWMMJunctionPropertyAdapter adapter(layer->engine(), "J1");
        adapter.setModelLayer(layer.get());
        adapter.setCouplingType(SWMMNodePropertyAdapter::USE_CLASS_DEFAULT);
        QVERIFY(!coupling(layer.get(), "J1").authored);
        canvas.undoStack()->undo();
        QCOMPARE(coupling(layer.get(), "J1"), a);
        canvas.undoStack()->clear();
        SWMMStoragePropertyAdapter storage(layer->engine(), "S1");
        storage.setModelLayer(layer.get());
        auto old = lid(layer->engine(), "S1");
        storage.setLidInitialSaturation(25);
        QCOMPARE(canvas.undoStack()->count(), 1);
        canvas.undoStack()->undo();
        QCOMPARE(lid(layer->engine(), "S1"), old);
        SWMMAttributeTableModel table;
        table.setSource(layer.get(), SWMMModelLayer::CatJunctions);
        canvas.undoStack()->clear();
        QVERIFY(apply(table, {"J1", "J3"}, "node_2d_coupleable", 0, canvas.undoStack()));
        QVERIFY(apply(table, {"J1", "J3"}, "node_2d_coupleable", 1, canvas.undoStack()));
        QVERIFY(!coupling(layer.get(), "J1").authored);
        QVERIFY(!coupling(layer.get(), "J3").authored);
        auto col = field(table, "node_2d_param_D", "J2");
        QVERIFY(table.setData(col, ""));
        QVERIFY(!coupling(layer.get(), "J2").authored);
    }
    void propertiesCommitAndMixedSubtypeChoices()
    {
        auto layer = fixture();
        QVERIFY(layer);
        MapCanvas canvas;
        layer->setEditCanvas(&canvas);
        PropertiesPanel panel;
        panel.setProject(layer.get());
        panel.showSelection(&canvas, {{SWMMObjectRef::Node, "J1"}, {SWMMObjectRef::Node, "S2"}});
        panel.resize(1000, 1000);
        panel.show();
        auto *tree = panel.findChild<QTreeView *>();
        QVERIFY(tree);
        tree->expandAll();
        auto index = findProperty(tree->model(), "1D-2D Coupling Structure");
        QVERIFY(index.isValid());
        QVERIFY(index.flags() & Qt::ItemIsEditable);
        tree->edit(index);
        auto *combo = tree->findChild<QComboBox *>();
        QVERIFY(combo);
        int row = combo->findData(3);
        QVERIFY(row >= 0);
        combo->setCurrentIndex(row);
        QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, row));
        QTRY_COMPARE(canvas.undoStack()->count(), 1);
        QCOMPARE(coupling(layer.get(), "J1").type, 3);
        QCOMPARE(coupling(layer.get(), "S2").type, 3);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        auto diameter = findProperty(tree->model(), "2D Opening diameter");
        QVERIFY(diameter.isValid());
        tree->edit(diameter);
        auto *line = tree->findChild<QLineEdit *>();
        QVERIFY(line);
        line->setFocus();
        line->selectAll();
        QTest::keyClicks(line, "9.2");
        QTest::keyClick(line, Qt::Key_Escape);
        QCOMPARE(canvas.undoStack()->count(), 1);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        tree->edit(diameter);
        line = tree->findChild<QLineEdit *>();
        QVERIFY(line);
        line->setFocus();
        line->selectAll();
        QTest::keyClicks(line, "2.5");
        QTest::keyClick(line, Qt::Key_Tab);
        QTRY_COMPARE(canvas.undoStack()->count(), 2);
        QCOMPARE(coupling(layer.get(), "J1").params["D"], QString("2.5"));
        QCOMPARE(coupling(layer.get(), "S2").params["D"], QString("2.5"));
        panel.showSelection(&canvas, {{SWMMObjectRef::Node, "S1"}, {SWMMObjectRef::Node, "S2"}});
        tree->expandAll();
        auto bottom = findProperty(tree->model(), "LID Bottom Ownership");
        QVERIFY(bottom.isValid());
        tree->edit(bottom);
        combo = tree->findChild<QComboBox *>();
        QVERIFY(combo);
        row = combo->findData(1);
        combo->setCurrentIndex(row);
        QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, row));
        QTRY_COMPARE(canvas.undoStack()->count(), 3);
        QCOMPARE(lid(layer->engine(), "S1")["bottom"].toInt(), 1);
    }
    void identitiesStaleRecordsAndContacts()
    {
        auto layer = fixture();
        QVERIFY(layer);
        auto e = layer->engine();
        QUndoStack undo;
        SWMMAttributeTableModel table;
        table.setSource(layer.get(), SWMMModelLayer::CatStorage);
        auto a = lid(e, "S1"), b = lid(e, "S2");
        QVERIFY(apply(table, {"S1", "S2"}, "node_lid_saturation", 29, &undo));
        QCOMPARE(swmm_lid_delete(e, swmm_lid_index(e, "UnusedLid"), nullptr), SWMM_OK);
        emit layer->dataObjectsChanged();
        QVERIFY(layer->applyNodeDelete("UnusedNode"));
        QVERIFY(layer->applyRename("S1", "Renamed", SWMMModelLayer::kKindNode));
        QVERIFY(layer->applyRename("A1", "RenamedAnchor", SWMMModelLayer::kKindLink));
        auto anchors = a["anchors"].toMap();
        anchors["RenamedAnchor"] = anchors.take("A1");
        a["anchors"] = anchors;
        undo.undo();
        QCOMPARE(lid(e, "Renamed"), a);
        QCOMPARE(lid(e, "S2"), b);
        undo.redo();
        undo.clear();
        QVERIFY(apply(table, {"Renamed", "S2"}, "node_lid_saturation", 44, &undo));
        QCOMPARE(swmm_gw2d_node_add(e, "Renamed", -1, 0, 0, 10), SWMM_OK);
        undo.undo();
        QCOMPARE(lid(e, "Renamed")["sat"].toDouble(), 44.0);
        undo.clear();
        QVERIFY(apply(table, {"Renamed", "S2"}, "node_lid_saturation", 55, &undo));
        QVERIFY(layer->applyRename("Renamed", "WithBed", SWMMModelLayer::kKindNode));
        undo.undo();
        QCOMPARE(lid(e, "WithBed")["sat"].toDouble(), 44.0);
        undo.clear();
        QVERIFY(apply(table, {"WithBed", "S2"}, "node_lid_saturation", 66, &undo));
        QCOMPARE(swmm_node_set_lid(e, swmm_node_index(e, "S2"), swmm_lid_index(e, "StackA"), 77),
                 SWMM_OK);
        undo.undo();
        QCOMPARE(lid(e, "WithBed")["sat"].toDouble(), 66.0);
        undo.clear();
        QVERIFY(apply(table, {"WithBed", "S2"}, "node_lid_saturation", 88, &undo));
        QVERIFY(layer->applyNodeDelete("S2"));
        QVERIFY(layer->applyNodeAdd("S2", 2, 50, 50));
        undo.undo();
        QCOMPARE(lid(e, "WithBed")["sat"].toDouble(), 88.0);
        undo.clear();
        layer.reset();
        undo.undo();
    }
    void couplingPersistenceAndMapping_data()
    {
        QTest::addColumn<bool>("si");
        QTest::newRow("US") << false;
        QTest::newRow("SI") << true;
    }
    void couplingPersistenceAndMapping()
    {
        QFETCH(bool, si);
        auto layer = fixture(si);
        QVERIFY(layer);
        QUndoStack undo;
        auto e = layer->engine();
        double cd = 0, area = 0;
        int mapped = -1;
        QCOMPARE(swmm_2d_vertex_get_coupled_node(e, 0, &mapped), SWMM_OK);
        QCOMPARE(swmm_2d_get_vertex_coupling_cd(e, 0, &cd), SWMM_OK);
        QCOMPARE(swmm_2d_get_vertex_coupling_area(e, 0, &area), SWMM_OK);
        SWMMAttributeTableModel table;
        table.setSource(layer.get(), SWMMModelLayer::CatJunctions);
        QVERIFY(apply(table, {"J1", "J3"}, "node_2d_param_D", "0.123456789012345", &undo));
        QVERIFY(apply(table, {"J1", "J3"}, "node_2d_param_COEFFS", "DEFAULT", &undo));
        auto a = coupling(layer.get(), "J1"), b = coupling(layer.get(), "J3");
        QVERIFY(a.params.contains("COEFFS"));
        QString path = QDir(folder()).filePath(QString("coupling-saved-%1.inp").arg(si));
        QCOMPARE(swmm_model_write(e, path.toUtf8().constData()), SWMM_OK);
        SWMMModelLayer reopened(path);
        QList<QString> warnings, errors;
        QVERIFY2(reopened.loadModel(warnings, errors), qPrintable(errors.join(' ')));
        QCOMPARE(coupling(&reopened, "J1"), a);
        QCOMPARE(coupling(&reopened, "J3"), b);
        double actualCd = 0, actualArea = 0;
        int actualMapped = -1;
        QCOMPARE(swmm_2d_vertex_get_coupled_node(e, 0, &actualMapped), SWMM_OK);
        QCOMPARE(swmm_2d_get_vertex_coupling_cd(e, 0, &actualCd), SWMM_OK);
        QCOMPARE(swmm_2d_get_vertex_coupling_area(e, 0, &actualArea), SWMM_OK);
        QCOMPARE(actualMapped, mapped);
        QCOMPARE(actualCd, cd);
        QCOMPARE(actualArea, area);
        QVERIFY(apply(table, {"J1", "J3"}, "node_2d_param_COEFFS", "", &undo));
        QVERIFY(!coupling(layer.get(), "J1").params.contains("COEFFS"));
        undo.undo();
        QCOMPARE(coupling(layer.get(), "J1"), a);
    }
    void virtualDefaultsAndIncompatibleMixedBatch()
    {
        auto layer = fixture();
        QVERIFY(layer);
        QUndoStack undo;
        QVERIFY(layer->applyInsertVirtualJunction("C1", .5, "V1", "C1b"));
        auto before = coupling(layer.get(), "V1");
        QCOMPARE(before.type, 0);
        QVERIFY(!before.authored);
        SWMMAttributeTableModel table;
        table.setSource(layer.get(), SWMMModelLayer::CatJunctions);
        QVERIFY(apply(table, {"V1"}, "node_2d_coupleable", 1, &undo));
        QCOMPARE(coupling(layer.get(), "V1").type, 1);
        QVERIFY(coupling(layer.get(), "V1").authored);
        undo.undo();
        QCOMPARE(coupling(layer.get(), "V1"), before);
        undo.clear();
        SWMMAggregateSelection aggregate(
            layer.get(),
            {{SWMMModelLayer::CatJunctions, {"J1"}}, {SWMMModelLayer::CatOutfalls, {"O1"}}}, &undo,
            nullptr);
        auto specs = aggregate.source()->columnSpecs();
        int column = -1;
        for (int c = 0; c < specs.size(); ++c)
            if (specs[c].setter == "node_2d_coupling_type")
                column = c;
        QVERIFY(column >= 0);
        QCOMPARE(aggregate.field(column).editable, 2);
        QVERIFY(aggregate.editorData(column, openswmmvis::kDisabledEnumValuesRole)
                    .toList()
                    .contains(3));
        const auto j = coupling(layer.get(), "J1"), o = coupling(layer.get(), "O1");
        QVERIFY(!nodestructure::applyFields(layer.get(), {"J1", "O1"},
                                            {{"node_2d_coupling_type", 3}}, &undo));
        QCOMPARE(coupling(layer.get(), "J1"), j);
        QCOMPARE(coupling(layer.get(), "O1"), o);
        QCOMPARE(undo.count(), 0);
        QVERIFY(aggregate.setValue(column, -1));
        QVERIFY(!coupling(layer.get(), "J1").authored);
        QVERIFY(!coupling(layer.get(), "O1").authored);
    }
    void meshOwnershipNeedsReviewedBedsAndPreservesThem()
    {
        auto layer = fixture();
        QVERIFY(layer);
        auto e = layer->engine();
        QUndoStack undo;
        QCOMPARE(swmm_gw2d_row_add(e, SWMM_GW2D_SCOPE_GLOBAL, nullptr, 0, 36, 4, .45, .1, 2),
                 SWMM_OK);
        QCOMPARE(swmm_gw2d_node_add(e, "S1", 0, 0, 0, 10), SWMM_OK);
        QCOMPARE(swmm_gw2d_node_add(e, "S2", 1, 0, 0, 11), SWMM_OK);
        SWMMAttributeTableModel table;
        table.setSource(layer.get(), SWMMModelLayer::CatStorage);
        QString error;
        const auto a = lid(e, "S1"), b = lid(e, "S2");
        QVERIFY2(apply(table, {"S1", "S2"}, "node_lid_bottom", 2, &undo, &error),
                 qPrintable(error));
        QCOMPARE(lid(e, "S1")["bottom"].toInt(), 2);
        QCOMPARE(lid(e, "S2")["bottom"].toInt(), 2);
        int count = 0;
        QCOMPARE(swmm_gw2d_node_count(e, &count), SWMM_OK);
        QCOMPARE(count, 2);
        int cell = -1;
        double area = 0;
        QCOMPARE(swmm_gw2d_node_get(e, 1, nullptr, 0, &cell, nullptr, nullptr, &area), SWMM_OK);
        QCOMPARE(cell, 1);
        QCOMPARE(area, 11.0);
        undo.undo();
        QCOMPARE(lid(e, "S1"), a);
        QCOMPARE(lid(e, "S2"), b);
        undo.redo();
        QCOMPARE(swmm_gw2d_node_set_exchange(e, 1, 0), SWMM_OK);
        undo.undo();
        QCOMPARE(lid(e, "S1")["bottom"].toInt(), 2);
    }
    void singlePropertiesRefreshAndClosedFacade()
    {
        auto layer = fixture();
        QVERIFY(layer);
        MapCanvas canvas;
        layer->setEditCanvas(&canvas);
        PropertiesPanel panel;
        panel.setProject(layer.get());
        panel.showSelection(&canvas, {{SWMMObjectRef::Node, "S1"}});
        panel.show();
        auto *tree = panel.findChild<QTreeView *>();
        QVERIFY(tree);
        tree->expandAll();
        auto index = findProperty(tree->model(), "LID Initial Saturation (%)");
        QVERIFY(index.isValid());
        QSignalSpy edits(layer.get(), &SWMMModelLayer::modelEdited),
            forwarded(&panel, &PropertiesPanel::objectEdited);
        QVERIFY(tree->model()->setData(index, 35.0, Qt::EditRole));
        QCOMPARE(lid(layer->engine(), "S1")["sat"].toDouble(), 35.0);
        QCOMPARE(edits.count(), 1);
        QCOMPARE(forwarded.count(), 0);
        canvas.undoStack()->undo();
        QCOMPARE(index.data(Qt::EditRole).toDouble(), 12.1234567890123);
        SWMMStoragePropertyAdapter facade(layer->engine(), "S1");
        facade.setModelLayer(layer.get());
        facade.setLidInitialSaturation(40);
        QCOMPARE(canvas.undoStack()->count(), 1);
        layer.reset();
        facade.setLidInitialSaturation(90);
        facade.setCouplingType(SWMMNodePropertyAdapter::CURB);
        canvas.undoStack()->undo();
    }
    void meshGeometryInvalidatesReplay()
    {
        auto layer = fixture();
        QVERIFY(layer);
        auto parsed = mesh::InpMeshReader::read(layer->modelFilePath());
        QVERIFY(parsed.hasMesh);
        SWMM2DMeshLayer meshLayer(parsed.mesh, layer->modelFilePath());
        MapCanvas canvas;
        canvas.addLayer(&meshLayer, false);
        layer->setEditCanvas(&canvas);
        SWMMAttributeTableModel table;
        table.setSource(layer.get(), SWMMModelLayer::CatStorage);
        QVERIFY(apply(table, {"S1", "S2"}, "node_lid_saturation", 25, canvas.undoStack()));
        QVERIFY(meshLayer.applyMeshVertexZ(0, 123));
        canvas.undoStack()->undo();
        QCOMPARE(lid(layer->engine(), "S1")["sat"].toDouble(), 25.0);
    }
};
QTEST_MAIN(TestNodeStructureBulk)
#include "test_nodestructurebulk.moc"
