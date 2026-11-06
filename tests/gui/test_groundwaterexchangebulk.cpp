#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "selection/selectionmanager.h"
#include "ui/dialogs/groundwaterexchangedialog.h"
#include "ui/panels/attributetablepanel.h"
#include "ui/panels/groundwaterexchangetablemodel.h"
#include "ui/panels/propertiespanel.h"
#include "ui/properties/subcatchcompoundeditbutton.h"
#include "ui/properties/swmmsubcatchpropertyadapter.h"
#include "ui/widgets/gwfexpressionedit.h"
#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_edit.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_subcatchments.h>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTableView>
#include <QTest>
#include <QTextDocument>
#include <QTimer>
#include <QTreeView>
#include <cmath>
#include <memory>

namespace
{
using Model = GroundwaterExchangeTableModel;
using Expression = openswmmvis::ui::GwfExpressionEdit;
QString folder()
{
    return QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA"))
        .absoluteFilePath("../../../workplans/bulk-groundwater-exchange/review");
}
std::unique_ptr<SWMMModelLayer> fixture(bool si = false)
{
    QFile file(
        QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA")).filePath("gw_exchange_fixture.inp"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QString text = QString::fromUtf8(file.readAll());
    if (si)
        text.replace(QRegularExpression("FLOW_UNITS\\s+CFS"), "FLOW_UNITS CMS");
    // Insert an earlier aquifer definition to exercise name-based replay after reindex.
    text.replace("[AQUIFERS]", "[AQUIFERS]\nUnused .5 .15 .3 5 10 15 .35 0 .002 0 10 .3");
    text.replace(
        QRegularExpression("(^S1\\s+AQ1\\s+J1[^\\r\\n]*)", QRegularExpression::MultilineOption),
        "\\1 -99 8 .21");
    text +=
        "\n[JUNCTIONS]\nJ2 90 5 0 0 0\nUnusedNode 85 5 0 0 0\n"
        "[AQUIFERS]\nAQ2 .55 .12 .32 6 11 16 .3 0 .003 -10 9 .25\n"
        "[SUBCATCHMENTS]\nS2 RG1 J1 4 40 400 1 0\nS3 RG1 J2 3 30 300 1 0\nControl RG1 J1 2 20 200 1 0\nNoAquifer RG1 J1 2 20 200 1 0\n"
        "[SUBAREAS]\nS2 .02 .2 .1 .2 25 OUTLET\nS3 .02 .2 .1 .2 25 OUTLET\nControl .02 .2 .1 .2 25 OUTLET\nNoAquifer .02 .2 .1 .2 25 OUTLET\n"
        "[INFILTRATION]\nS2 4 .5 4 7 0 HORTON\nS3 4 .5 4 7 0 HORTON\nControl 4 .5 4 7 0 HORTON\nNoAquifer 4 .5 4 7 0 HORTON\n"
        "[GROUNDWATER]\nS2 AQ2 O1 18 .002 1.2 .2 1.5 .02 1 -99 -8 7 .22\nS3 AQ1 J2 25 .003 1.3 .4 1.6 .03 2 12 * 11 .25\nControl AQ2 J1 30 .004 1.4 .6 1.7 .04 3 14 -7 12 .24\n"
        "[GWF]\nS2 LATERAL HGW + 0.2\nS2 DEEP KS * 0.2\nControl LATERAL HGW * 0.4\nControl DEEP KS * 0.4\n"
        "[POLYGONS]\nS2 500 800\nS2 900 800\nS2 900 1000\nS2 500 1000\nS3 1000 800\nS3 1400 800\nS3 1400 1000\nS3 1000 1000\nControl 1500 800\nControl 1900 800\nControl 1900 1000\nControl 1500 1000\n";
    QDir().mkpath(folder());
    const auto path = QDir(folder()).filePath(si ? "Groundwater_Exchange_SI.inp"
                                                 : "Groundwater_Exchange_Properties_Review.inp");
    QFile output(path);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};
    output.write(text.toUtf8());
    output.close();
    auto layer = std::make_unique<SWMMModelLayer>(path);
    QList<QString> warnings, errors;
    if (!layer->loadModel(warnings, errors)) {
        qWarning().noquote() << errors.join(' ');
        return {};
    }
    const auto e = layer->engine();
    if (swmm_subcatch_set_gw_params(e, swmm_subcatch_index(e, "S1"), 12.1234567890123,
                                    .000012345678901234, 1.1234567890123, 0, 1, 0, 0,
                                    -1e10) != SWMM_OK)
        return {};
    // Preserve the precision/sentinel setup in the model opened for human review.
    if (swmm_model_write(e, path.toUtf8().constData()) != SWMM_OK)
        return {};
    return layer;
}
const QStringList keys{"aquifer", "node", "surface", "a1",   "b1",      "a2",
                       "b2",      "a3",   "dsw",     "egwt", "lateral", "deep"};
QVariantMap state(SWMM_Engine e, const QString &name)
{
    QVariantMap result;
    const int sub = swmm_subcatch_index(e, name.toUtf8().constData());
    int aq = -1, node = -1;
    double v[8]{};
    if (sub < 0 || swmm_subcatch_get_aquifer(e, sub, &aq) != SWMM_OK ||
        swmm_subcatch_get_gw_node(e, sub, &node) != SWMM_OK ||
        swmm_subcatch_get_gw_params(e, sub, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6],
                                    &v[7]) != SWMM_OK)
        return {};
    result["aquifer"] = aq < 0 ? QString() : QString::fromUtf8(swmm_aquifer_id(e, aq));
    result["node"] = node < 0 ? QString() : QString::fromUtf8(swmm_node_id(e, node));
    for (int i = 0; i < 8; ++i)
        result[keys[Model::Surface + i]] = v[i];
    for (int type = 0; type < 2; ++type) {
        QByteArray text(32768, '\0');
        if (swmm_subcatch_get_gwf_expression(e, sub, type, text.data(), text.size()) != SWMM_OK)
            return {};
        result[type == 0 ? "lateral" : "deep"] = QString::fromUtf8(text.constData());
    }
    return result;
}
SubcatchCompoundEditRef refFor(SWMMModelLayer *layer, QUndoStack *undo)
{
    SubcatchCompoundEditRef ref;
    ref.layer = layer;
    ref.engine = layer->engine();
    ref.subName = "S1";
    ref.subNames = {"S1", "S2", "S3"};
    ref.kind = SubcatchCompoundEditRef::Groundwater;
    ref.undoStack = undo;
    return ref;
}
QModelIndex propertyIndex(QAbstractItemModel *model, const QString &name,
                          const QModelIndex &parent = {})
{
    for (int row = 0; row < model->rowCount(parent); ++row) {
        const auto index = model->index(row, 0, parent);
        if (index.data().toString() == name)
            return index.siblingAtColumn(1);
        const auto child = propertyIndex(model, name, index);
        if (child.isValid())
            return child;
    }
    return {};
}
QMap<QString, QStringList> overrides(SWMM_Engine e, const QString &fileName)
{
    if (swmm_model_write(e, fileName.toUtf8().constData()) != SWMM_OK)
        return {};
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QMap<QString, QStringList> result;
    bool active = false;
    for (auto line : QString::fromUtf8(file.readAll()).split('\n')) {
        line = line.trimmed();
        if (line.startsWith('[')) {
            active = line == "[GROUNDWATER]";
            continue;
        }
        if (!active || line.startsWith(';') || line.isEmpty())
            continue;
        const auto tokens = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        result[tokens.first()] = tokens.mid(11);
    }
    return result;
}
}

class TestGroundwaterExchangeBulk : public QObject
{
    Q_OBJECT
private slots:
    void propertiesWidgetsAndPersistence_data()
    {
        QTest::addColumn<bool>("si");
        QTest::newRow("US") << false;
        QTest::newRow("SI") << true;
    }
    void propertiesWidgetsAndPersistence()
    {
        QFETCH(bool, si);
        auto layer = fixture(si);
        QVERIFY(layer);
        const auto e = layer->engine();
        char flow[32]{};
        QCOMPARE(swmm_options_get(e, "FLOW_UNITS", flow, sizeof(flow)), SWMM_OK);
        QCOMPARE(QString::fromUtf8(flow), si ? QString("CMS") : QString("CFS"));
        QList<QVariantMap> before;
        for (auto name : {"S1", "S2", "S3"})
            before << state(e, name);
        const auto control = state(e, "Control");
        const auto originalOverrides =
            overrides(e, QDir(folder()).filePath("before-overrides.inp"));
        QVERIFY(!originalOverrides.value("S1").isEmpty());
        MapCanvas canvas;
        PropertiesPanel panel;
        panel.setProject(layer.get());
        panel.showSelection(&canvas, {{SWMMObjectRef::Subcatchment, "S1"},
                                      {SWMMObjectRef::Subcatchment, "S2"},
                                      {SWMMObjectRef::Subcatchment, "S3"}});
        panel.resize(800, 1100);
        panel.show();
        auto *tree = panel.findChild<QTreeView *>();
        QVERIFY(tree);
        tree->expandAll();
        const auto field = propertyIndex(tree->model(), "Groundwater");
        QVERIFY(field.isValid());
        tree->edit(field);
        auto *button = qobject_cast<SubcatchCompoundEditButton *>(tree->indexWidget(field));
        QVERIFY(button);
        QCOMPARE(button->value().subNames.size(), 3);
        QSignalSpy dirty(layer.get(), &SWMMModelLayer::modelEdited);
        QTest::mouseClick(button->findChild<QPushButton *>(), Qt::LeftButton);
        auto *dialog = panel.findChild<GroundwaterExchangeDialog *>();
        QVERIFY(dialog);
        QCOMPARE(dialog->findChild<QDialogButtonBox *>()->standardButtons(),
                 QDialogButtonBox::StandardButtons(QDialogButtonBox::Close));
        auto *table = dialog->findChild<QTableView *>("groundwaterExchangeTable");
        QVERIFY(table);
        auto *model = dynamic_cast<Model *>(table->model());
        QVERIFY(model);
        panel.showSelection(&canvas, {{SWMMObjectRef::Subcatchment, "Control"}});
        const auto numeric = model->index(Model::A1, 1);
        QCOMPARE(numeric.data().toString(), QString("Mixed"));
        table->edit(numeric);
        auto *line = table->findChild<QLineEdit *>();
        QVERIFY(line);
        line->setFocus();
        QTest::keyClicks(line, "999");
        QTest::keyClick(line, Qt::Key_Escape);
        QCOMPARE(canvas.undoStack()->count(), 0);
        QCOMPARE(dirty.count(), 0);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        table->edit(numeric);
        line = table->findChild<QLineEdit *>();
        QVERIFY(line);
        line->setFocus();
        QTest::keyClicks(line, "0.125");
        QTest::keyClick(line, si ? Qt::Key_Tab : Qt::Key_Return);
        QTRY_COMPARE(canvas.undoStack()->count(), 1);
        for (int n = 0; n < 3; ++n) {
            auto expected = before[n];
            expected["a1"] = .125;
            QCOMPARE(state(e, QString("S%1").arg(n + 1)), expected);
        }
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        table->edit(model->index(Model::Lateral, 1));
        auto *expression = table->findChild<Expression *>();
        QVERIFY(expression);
        expression->setExpression("HGW + 99");
        expression->document()->setModified(true);
        QTest::keyClick(expression, Qt::Key_Escape);
        QCOMPARE(canvas.undoStack()->count(), 1);
        QCOMPARE(state(e, "S1").value("lateral"), before.first().value("lateral"));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        table->edit(model->index(Model::Lateral, 1));
        expression = table->findChild<Expression *>();
        QVERIFY(expression);
        expression->setFocus();
        expression->selectAll();
        QTest::keyClicks(expression, "0.2 * HGW + 0");
        QTest::keyClick(expression, si ? Qt::Key_Tab : Qt::Key_Return);
        QTRY_COMPARE(canvas.undoStack()->count(), 2);
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        table->edit(model->index(Model::Node, 1));
        auto *combo = table->findChild<QComboBox *>();
        QVERIFY(combo);
        combo->setCurrentIndex(combo->findData("J2"));
        QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, combo->currentIndex()));
        QTRY_COMPARE(canvas.undoStack()->count(), 3);
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        table->edit(model->index(Model::Egwt, 1));
        line = table->findChild<QLineEdit *>();
        QVERIFY(line);
        line->setFocus();
        line->selectAll();
        QTest::keyClicks(line, "*");
        QTest::keyClick(line, Qt::Key_Tab);
        QTRY_COMPARE(canvas.undoStack()->count(), 4);
        QList<QVariantMap> after;
        for (int n = 0; n < 3; ++n) {
            auto expected = before[n];
            expected["a1"] = .125;
            expected["lateral"] = "0.2 * HGW + 0";
            expected["node"] = "J2";
            expected["egwt"] = -1e10;
            after << state(e, QString("S%1").arg(n + 1));
            QCOMPARE(after.last(), expected);
        }
        QCOMPARE(state(e, "Control"), control);
        dialog->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        for (int i = 0; i < 4; ++i)
            canvas.undoStack()->undo();
        for (int n = 0; n < 3; ++n)
            QCOMPARE(state(e, QString("S%1").arg(n + 1)), before[n]);
        for (int i = 0; i < 4; ++i)
            canvas.undoStack()->redo();
        const auto saved = QDir(folder()).filePath(si ? "edited-SI.inp" : "edited-US.inp");
        QCOMPARE(overrides(e, saved), originalOverrides);
        SWMMModelLayer reopened(saved);
        QList<QString> warnings, errors;
        QVERIFY2(reopened.loadModel(warnings, errors), qPrintable(errors.join(' ')));
        for (int n = 0; n < 3; ++n)
            QCOMPARE(state(reopened.engine(), QString("S%1").arg(n + 1)), after[n]);
        QCOMPARE(state(reopened.engine(), "Control"), control);
    }
    void everyFieldPreservesSiblings_data()
    {
        QTest::addColumn<int>("row");
        for (int row = Model::Node; row < Model::Rows; ++row)
            QTest::newRow(keys[row].toUtf8().constData()) << row;
    }
    void everyFieldPreservesSiblings()
    {
        QFETCH(int, row);
        auto layer = fixture();
        QVERIFY(layer);
        auto e = layer->engine();
        QUndoStack undo;
        Model model(refFor(layer.get(), &undo));
        QList<QVariantMap> before;
        for (auto name : {"S1", "S2", "S3"})
            before << state(e, name);
        const auto control = state(e, "Control");
        const QVariant proposed = row == Model::Node      ? QVariant("J2")
                                  : row >= Model::Lateral ? QVariant("HGW * 0.75")
                                                          : QVariant(-99.1234567890123);
        QVERIFY2(model.setData(model.index(row, 1), proposed), qPrintable(model.lastError()));
        QCOMPARE(undo.count(), 1);
        for (int n = 0; n < 3; ++n) {
            auto expected = before[n];
            expected[keys[row]] = proposed;
            QCOMPARE(state(e, QString("S%1").arg(n + 1)), expected);
        }
        QVERIFY(model.setData(model.index(row, 1), proposed));
        QCOMPARE(undo.count(), 1);
        undo.undo();
        for (int n = 0; n < 3; ++n)
            QCOMPARE(state(e, QString("S%1").arg(n + 1)), before[n]);
        undo.redo();
        QCOMPARE(state(e, "Control"), control);
    }
    void validationAndIncompleteRecords()
    {
        auto layer = fixture();
        QVERIFY(layer);
        auto e = layer->engine();
        QUndoStack undo;
        auto ref = refFor(layer.get(), &undo);
        Model model(ref);
        const auto before = state(e, "S1");
        for (auto bad : {"nan", "inf", "-inf", ""})
            QVERIFY(!model.setData(model.index(Model::A1, 1), bad));
        for (auto bad : {"NOT_A_GW_VARIABLE", "min(HGW)", "HGW $ 2", "HGW\n+1"})
            QVERIFY2(!model.setData(model.index(Model::Lateral, 1), bad), bad);
        QVERIFY(!model.setData(model.index(Model::Node, 1), "Unknown"));
        QVERIFY(!model.setData(model.index(Model::Node, 1), ""));
        QVERIFY(!model.setData(model.index(Model::Aquifer, 1), "AQ2"));
        QCOMPARE(undo.count(), 0);
        QCOMPARE(state(e, "S1"), before);
        QVERIFY(model.setData(model.index(Model::Deep, 1), ""));
        QCOMPARE(undo.count(), 1);
        undo.undo();
        undo.clear();
        ref.subNames = {"S1", "NoAquifer"};
        Model missingAquifer(ref);
        QVERIFY(!missingAquifer.setData(missingAquifer.index(Model::Node, 1), "J1"));
        QCOMPARE(undo.count(), 0);
        const int no = swmm_subcatch_index(e, "NoAquifer");
        QCOMPARE(swmm_subcatch_set_aquifer(e, no, swmm_aquifer_index(e, "AQ1")), SWMM_OK);
        Model noReceiver(ref);
        QVERIFY(!(noReceiver.flags(noReceiver.index(Model::A1, 1)) & Qt::ItemIsEditable));
        QVERIFY(noReceiver.setData(noReceiver.index(Model::Node, 1), "J2"));
        QCOMPARE(undo.count(), 1);
        undo.undo();
        QCOMPARE(state(e, "NoAquifer").value("node").toString(), QString());
        undo.clear();
        // An invalid last target must fail before changing an earlier target.
        const int sub = swmm_subcatch_index(e, "S3");
        QCOMPARE(swmm_subcatch_set_gw_params(e, sub, 25, NAN, 1.3, .4, 1.6, .03, 2, 12), SWMM_OK);
        QVERIFY(!model.setData(model.index(Model::A1, 1), 2.0));
        QCOMPARE(undo.count(), 0);
        QCOMPARE(state(e, "S1"), before);
    }
    void aliasesRenamesAndReferenceReindex()
    {
        auto layer = fixture();
        QVERIFY(layer);
        auto e = layer->engine();
        QUndoStack undo;
        auto ref = refFor(layer.get(), &undo);
        ref.subNames = {"S1", "s1", "S2"};
        Model model(ref);
        QCOMPARE(model.targetNames().size(), 2);
        const auto before = state(e, "S1");
        QVERIFY(model.setData(model.index(Model::A1, 1), .5));
        QCOMPARE(swmm_aquifer_delete(e, swmm_aquifer_index(e, "Unused"), nullptr), SWMM_OK);
        emit layer->dataObjectsChanged();
        QVERIFY(layer->applyNodeDelete("UnusedNode"));
        QVERIFY(layer->applyRename("S1", "Renamed", SWMMModelLayer::kKindCatch));
        QCOMPARE(state(e, "Renamed").value("lateral"), before.value("lateral"));
        QVERIFY(layer->applyRename("J1", "Receiver", SWMMModelLayer::kKindNode));
        undo.undo();
        auto expected = before;
        expected["node"] = "Receiver";
        QCOMPARE(state(e, "Renamed"), expected);
        undo.redo();
        QCOMPARE(state(e, "Renamed").value("a1").toDouble(), .5);
    }
    void deletedContextCannotRebind_data()
    {
        QTest::addColumn<QString>("kind");
        for (auto kind : {"target", "node", "aquifer", "stale"})
            QTest::newRow(kind) << QString(kind);
    }
    void deletedContextCannotRebind()
    {
        QFETCH(QString, kind);
        auto layer = fixture();
        QVERIFY(layer);
        auto e = layer->engine();
        QUndoStack undo;
        Model model(refFor(layer.get(), &undo));
        QVERIFY(model.setData(model.index(Model::A1, 1), .5));
        if (kind == "target") {
            QVERIFY(layer->applySubcatchDelete("S2"));
            QVERIFY(layer->applySubcatchAdd("S2", {{0, 0}, {10, 0}, {10, 10}, {0, 0}}));
            QCOMPARE(state(e, "S2").value("lateral").toString(), QString());
        } else if (kind == "node") {
            QVERIFY(layer->applyNodeDelete("O1"));
            QVERIFY(layer->applyNodeAdd("O1", 0, 10, 10));
        } else if (kind == "aquifer") {
            QCOMPARE(swmm_aquifer_delete(e, swmm_aquifer_index(e, "AQ2"), nullptr), SWMM_OK);
            emit layer->dataObjectsChanged();
            QCOMPARE(swmm_aquifer_add(e, "AQ2"), SWMM_OK);
            emit layer->dataObjectsChanged();
        } else {
            QCOMPARE(swmm_subcatch_set_gwf_expression(e, swmm_subcatch_index(e, "S3"),
                                                      SWMM_GWF_DEEP, "HGW + 99"),
                     SWMM_OK);
        }
        const auto first = state(e, "S1"), last = state(e, "S3");
        undo.undo();
        QCOMPARE(state(e, "S1"), first);
        QCOMPARE(state(e, "S3"), last);
    }
    void activeExpressionAndLongNativeReplay()
    {
        auto layer = fixture();
        QVERIFY(layer);
        auto e = layer->engine();
        QUndoStack undo;
        auto ref = refFor(layer.get(), &undo);
        const QString original = " HGW" + QString(" + 0").repeated(500) + " ";
        QCOMPARE(
            swmm_subcatch_set_gwf_expression(e, 0, SWMM_GWF_DEEP, original.toUtf8().constData()),
            SWMM_OK);
        Model model(ref);
        QVERIFY(model.setData(model.index(Model::Deep, 1), "HGW * .1"));
        undo.undo();
        QCOMPARE(state(e, "S1").value("deep").toString(), original);
        undo.clear();
        GroundwaterExchangeDialog dialog(ref);
        dialog.show();
        auto *table = dialog.findChild<QTableView *>("groundwaterExchangeTable");
        QVERIFY(table);
        table->edit(table->model()->index(Model::Deep, 1));
        QPointer<Expression> edit = table->findChild<Expression *>();
        QVERIFY(edit);
        edit->setExpression("HGW+1");
        edit->document()->setModified(true);
        layer.reset();
        if (edit) {
            edit->validateNow();
            QMetaObject::invokeMethod(edit, "editingFinished");
        }
        QTest::qWait(300);
    }
    void singleOwnerFindsProjectUndo()
    {
        auto layer = fixture();
        QVERIFY(layer);
        const auto before = state(layer->engine(), "S1");
        QWidget root;
        MapCanvas canvas(&root);
        canvas.addLayer(layer.get(), false);
        QWidget floating(&root, Qt::Window);
        auto ref = refFor(layer.get(), nullptr);
        ref.subNames.clear();
        GroundwaterExchangeDialog dialog(ref, &floating);
        auto *table = dialog.findChild<QTableView *>("groundwaterExchangeTable");
        QVERIFY(table);
        QVERIFY(table->model()->setData(table->model()->index(Model::A1, 1), .25));
        QCOMPARE(canvas.undoStack()->count(), 1);
        canvas.undoStack()->undo();
        QCOMPARE(state(layer->engine(), "S1"), before);
        Model unbound(ref);
        QVERIFY(!unbound.setData(unbound.index(Model::A1, 1), .5));
        QVERIFY(unbound.summary().contains("Undo"));
    }
    void parentDestructionDiscardsActiveExpression()
    {
        auto layer = fixture();
        QVERIFY(layer);
        QUndoStack undo;
        const auto before = state(layer->engine(), "S1");
        auto parent = std::make_unique<PropertiesPanel>();
        parent->setProject(layer.get());
        auto *dialog = new GroundwaterExchangeDialog(refFor(layer.get(), &undo), parent.get());
        dialog->show();
        auto *table = dialog->findChild<QTableView *>("groundwaterExchangeTable");
        QVERIFY(table);
        table->edit(table->model()->index(Model::Lateral, 1));
        auto *edit = table->findChild<Expression *>();
        QVERIFY(edit);
        edit->setFocus();
        edit->selectAll();
        QTest::keyClicks(edit, "HGW");
        parent.reset();
        QCOMPARE(undo.count(), 0);
        QCOMPARE(state(layer->engine(), "S1"), before);
    }
    void tableRouteAndOwnedNavigation()
    {
        auto layer = fixture();
        QVERIFY(layer);
        auto e = layer->engine();
        const auto before = state(e, "S1"), other = state(e, "S2"), control = state(e, "Control");
        MapCanvas canvas;
        SelectionManager selection;
        AttributeTablePanel panel;
        panel.setProject(layer.get(), &selection, &canvas);
        panel.showModelCategory(layer.get(), SWMMModelLayer::CatSubcatchments);
        panel.resize(1500, 650);
        panel.show();
        selection.select({{SWMMObjectRef::Subcatchment, "S1"}, {SWMMObjectRef::Subcatchment, "S2"}},
                         SelectionManager::Replace);
        QCoreApplication::processEvents();
        auto *view = panel.findChild<QTableView *>();
        QVERIFY(view);
        QModelIndex clicked;
        for (int row = 0; row < view->model()->rowCount(); ++row)
            if (view->model()->index(row, 0).data().toString() == "S1")
                for (int col = 0; col < view->model()->columnCount(); ++col)
                    if (view->model()->headerData(col, Qt::Horizontal).toString() == "Groundwater")
                        clicked = view->model()->index(row, col);
        QVERIFY(clicked.isValid());
        QSignalSpy dirty(layer.get(), &SWMMModelLayer::modelEdited);
        QVERIFY(view->model()->setData(clicked, clicked.data(Qt::EditRole), Qt::EditRole));
        QCOMPARE(dirty.count(), 0);
        SWMMSubcatchPropertyAdapter facade(e, "S1");
        facade.setModelLayer(layer.get());
        QSignalSpy changed(&facade, &SWMMSubcatchPropertyAdapter::changed);
        facade.setGroundwaterRef(facade.groundwaterRef());
        QCOMPARE(changed.count(), 0);
        view->scrollTo(clicked);
        QCoreApplication::processEvents();
        bool opened = false;
        QTimer::singleShot(0, &panel, [&] {
            auto *menu = panel.findChild<QMenu *>(QString(), Qt::FindDirectChildrenOnly);
            if (!menu)
                return;
            for (auto *action : menu->actions())
                if (action->text().startsWith("Edit Groundwater for 2")) {
                    action->trigger();
                    opened = true;
                    break;
                }
            menu->close();
        });
        QVERIFY(QMetaObject::invokeMethod(&panel, "onContextMenuRequested",
                                          Q_ARG(QPoint, view->visualRect(clicked).center())));
        QVERIFY(opened);
        QCOMPARE(dirty.count(), 0);
        auto *dialog = panel.findChild<GroundwaterExchangeDialog *>();
        QVERIFY(dialog);
        auto *table = dialog->findChild<QTableView *>("groundwaterExchangeTable");
        QVERIFY(table);
        table->edit(table->model()->index(Model::Node, 1));
        auto *combo = table->findChild<QComboBox *>();
        QVERIFY(combo);
        combo->setCurrentIndex(combo->findData("J2"));
        QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, combo->currentIndex()));
        QTRY_COMPARE(canvas.undoStack()->count(), 1);
        auto expected = before;
        expected["node"] = "J2";
        QCOMPARE(state(e, "S1"), expected);
        expected = other;
        expected["node"] = "J2";
        QCOMPARE(state(e, "S2"), expected);
        QCOMPARE(state(e, "Control"), control);
        dialog->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        canvas.undoStack()->undo();
        QCOMPARE(state(e, "S1"), before);
        QCOMPARE(state(e, "S2"), other);
        // The owned single-object route uses the same writer and supplied stack.
        auto single = refFor(layer.get(), canvas.undoStack());
        single.subNames.clear();
        GroundwaterExchangeDialog owned(single);
        QCOMPARE(owned.findChild<QDialogButtonBox *>()->standardButtons(),
                 QDialogButtonBox::StandardButtons(QDialogButtonBox::Close));
        auto *singleTable = owned.findChild<QTableView *>("groundwaterExchangeTable");
        QVERIFY(singleTable);
        QVERIFY(singleTable->model()->setData(singleTable->model()->index(Model::A1, 1), .75));
        QCOMPARE(state(e, "S1").value("a1").toDouble(), .75);
        QCOMPARE(state(e, "S2"), other);
        canvas.undoStack()->undo();
        QCOMPARE(state(e, "S1"), before);
    }
};
QTEST_MAIN(TestGroundwaterExchangeBulk)
#include "test_groundwaterexchangebulk.moc"
