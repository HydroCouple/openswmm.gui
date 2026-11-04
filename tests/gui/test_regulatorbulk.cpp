#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "render/renderers/categorizedrenderer.h"
#include "render/linesymbollayer.h"
#include "ui/panels/propertiespanel.h"
#include "ui/panels/swmmaggregateselection.h"
#include "ui/panels/swmmattributetablemodel.h"
#include "ui/properties/swmmlinkpropertyadapter.h"
#include "ui/dialogs/linkcompoundeditdialog.h"
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_tables.h>
#include <openswmm/engine/openswmm_edit.h>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QListWidget>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QTest>
#include <QTreeView>
#include <memory>
namespace {
using Category = SWMMModelLayer::Category;
QString folder()
{
    return QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA"))
        .absoluteFilePath("../../../workplans/bulk-regulators/review");
}
std::unique_ptr<SWMMModelLayer> fixture(bool si = false)
{
    QDir().mkpath(folder());
    QString path = QDir(folder()).filePath(si ? "Regulator_SI.inp" : "Regulator_Properties_Review.inp");
    QByteArray text =
        "[OPTIONS]\nFLOW_UNITS CFS\nFLOW_ROUTING DYNWAVE\nSTART_DATE 01/01/2026\nEND_DATE 01/01/2026\nEND_TIME 01:00\n"
        "[JUNCTIONS]\nJ1 10 10\nJ2 9 10\nW1 10 10\n[OUTFALLS]\nO1 8 FREE NO\n"
        "[CONDUITS]\nUnused J1 J2 100 .013 0 0\n"
        "[WEIRS]\nW1 J1 J2 TRANSVERSE .2 .61 NO 0 0\nW2 J1 J2 V-NOTCH .3 .62 NO 0 0\n"
        "W3 J1 J2 TRAPEZOIDAL .4 .63 YES 1 .5\nWC J1 J2 TRANSVERSE .2 .64 NO 0 0\n"
        "[ORIFICES]\nR1 J2 O1 SIDE .1 .6 NO 0\nR2 J2 O1 BOTTOM .2 .7 YES 1\nRC J2 O1 SIDE .1 .6 NO 0\n"
        "[OUTLETS]\nT1 J2 O1 .1 FUNCTIONAL/HEAD 2 1.2 NO\nT2 J2 O1 .2 FUNCTIONAL/DEPTH 3 1.3 YES\nTC J2 O1 .1 FUNCTIONAL/HEAD 4 1.4 NO\n"
        "[XSECTIONS]\nUnused CIRCULAR 1 0 0 0 1\n"
        "W1 RECT_OPEN 2 3 0 0 1\nW2 TRIANGULAR 4 5 0 0 1\nW3 TRAPEZOIDAL 6 7 1 2 1\nWC RECT_OPEN 2 3 0 0 1\n"
        "R1 CIRCULAR 1 0 0 0 1\nR2 RECT_CLOSED 2 3 0 0 1\nRC CIRCULAR 1 0 0 0 1\n"
        "[CURVES]\nUnusedCurve RATING 0 0\nUnusedCurve 10 1\nRatingA RATING 0 0\nRatingA 10 20\nRatingB RATING 0 0\nRatingB 10 30\nOther STORAGE 0 100\nOther 10 200\n"
        "[TIMESERIES]\nRatingA 0:00 5\nRatingA 1:00 6\n"
        "[COORDINATES]\nJ1 0 0\nJ2 100 0\nW1 50 60\nO1 200 0\n"
        "[VERTICES]\nW1 30 10\nW2 30 20\nW3 30 30\nWC 30 40\nR1 150 10\nR2 150 20\nRC 150 30\nT1 130 -10\nT2 130 -20\nTC 130 -30\n";
    if (si) text.replace("FLOW_UNITS CFS", "FLOW_UNITS CMS");
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    out.write(text); out.close();
    auto layer = std::make_unique<SWMMModelLayer>(path);
    QList<QString> warnings, errors;
    if (!layer->loadModel(warnings, errors)) { qWarning() << errors; return {}; }
    return layer;
}
QModelIndex field(SWMMAttributeTableModel &model, const QString &name, const QString &setter)
{
    auto specs = model.columnSpecs();
    for (int c = 0; c < specs.size(); ++c)
        if (specs[c].setter == setter) return model.index(model.rowForName(name), c);
    return {};
}
bool apply(SWMMAttributeTableModel &model, const QStringList &names, const QString &setter,
           QVariant value, QUndoStack *undo, QString *error = nullptr)
{
    auto index = field(model, names.first(), setter);
    if (!index.isValid()) return false;
    if (setter == "link_pump_curve_ref") {
        auto ref = index.data(Qt::EditRole).value<DataObjectRef>();
        ref.currentName = value.toString(); value = QVariant::fromValue(ref);
    }
    return model.applyScalarEdits(names, {{model.columnSpecs()[index.column()].key, value}}, undo, error);
}
QVariantList section(SWMM_Engine e, const char *name)
{
    int link = swmm_link_index(e, name), shape = -1, type = -1, kind = -1, gate = -1;
    double g[4]{}, offset = 0, cd = 0;
    swmm_link_get_type(e, link, &kind);
    if (kind == SWMM_LINK_WEIR) swmm_link_get_weir_type(e, link, &type);
    else swmm_link_get_orifice_type(e, link, &type);
    swmm_link_get_xsect(e, link, &shape, &g[0], &g[1], &g[2], &g[3]);
    swmm_link_get_offset_up(e, link, &offset);
    swmm_link_get_discharge_coeff(e, link, &cd);
    swmm_link_get_flap_gate(e, link, &gate);
    return {type, shape, g[0], g[1], g[2], g[3], offset, cd, gate};
}
QVariantList outlet(SWMM_Engine e, const char *name, bool persisted = false)
{
    int link = swmm_link_index(e, name), type = -1, curve = -1, gate = -1;
    double coeff = 0, expon = 0, offset = 0;
    swmm_link_get_outlet_rating_type(e, link, &type);
    swmm_link_get_pump_curve(e, link, &curve);
    swmm_link_get_discharge_coeff(e, link, &coeff);
    swmm_link_get_outlet_expon(e, link, &expon);
    swmm_link_get_offset_up(e, link, &offset);
    swmm_link_get_flap_gate(e, link, &gate);
    QString id = curve < 0 ? QString() : QString::fromUtf8(swmm_table_id(e, curve));
    if (persisted) { if (type >= 2) coeff = expon = 0; else id.clear(); }
    return {type, id, coeff, expon, offset, gate};
}
QModelIndex findProperty(QAbstractItemModel *model, const QString &label, const QModelIndex &parent = {})
{
    for (int r = 0; r < model->rowCount(parent); ++r) {
        auto index = model->index(r, 0, parent);
        if (index.data().toString() == label || index.data().toString().startsWith(label + " ("))
            return index.siblingAtColumn(1);
        auto child = findProperty(model, label, index);
        if (child.isValid()) return child;
    }
    return {};
}
}
class TestRegulatorBulk : public QObject {
    Q_OBJECT
private slots:
    void typesAndShapes_data()
    {
        QTest::addColumn<bool>("si"); QTest::addColumn<int>("category");
        QTest::addColumn<QString>("setter"); QTest::addColumn<int>("value");
        for (bool si : {false, true}) {
            for (int type = 0; type < 5; ++type)
                QTest::newRow(qPrintable(QString("weir-%1-%2").arg(si).arg(type)))
                    << si << int(SWMMModelLayer::CatWeirs) << QString("link_weir_type") << type;
            for (int type = 0; type < 2; ++type)
                QTest::newRow(qPrintable(QString("orifice-%1-%2").arg(si).arg(type)))
                    << si << int(SWMMModelLayer::CatOrifices) << QString("link_orifice_type") << type;
            for (int shape : {int(SWMM_XSECT_RECT_OPEN), int(SWMM_XSECT_TRIANGULAR), int(SWMM_XSECT_TRAPEZOIDAL)})
                QTest::newRow(qPrintable(QString("weir-shape-%1-%2").arg(si).arg(shape)))
                    << si << int(SWMMModelLayer::CatWeirs) << QString("link_xsect_shape") << shape;
            for (int shape : {int(SWMM_XSECT_CIRCULAR), int(SWMM_XSECT_RECT_CLOSED)})
                QTest::newRow(qPrintable(QString("orifice-shape-%1-%2").arg(si).arg(shape)))
                    << si << int(SWMMModelLayer::CatOrifices) << QString("link_xsect_shape") << shape;
        }
    }
    void typesAndShapes()
    {
        QFETCH(bool, si); QFETCH(int, category); QFETCH(QString, setter); QFETCH(int, value);
        auto layer = fixture(si); QVERIFY(layer); auto e = layer->engine();
        const bool weir = category == SWMMModelLayer::CatWeirs;
        QStringList names = weir ? QStringList{"W1", "W2", "W3"} : QStringList{"R1", "R2"};
        const char *control = weir ? "WC" : "RC";
        QList<QVariantList> before; for (auto name : names) before << section(e, name.toUtf8().constData());
        auto untouched = section(e, control);
        QUndoStack undo; SWMMAttributeTableModel model; model.setSource(layer.get(), Category(category));
        QString error;
        QVERIFY2(apply(model, names, setter, value, &undo, &error), qPrintable(error));
        QCOMPARE(undo.count(), 1);
        QList<QVariantList> after;
        for (int i = 0; i < names.size(); ++i) {
            auto actual = section(e, names[i].toUtf8().constData()); after << actual;
            for (int j = 6; j < actual.size(); ++j) QCOMPARE(actual[j], before[i][j]);
            if (setter == "link_weir_type") {
                QCOMPARE(actual[0].toInt(), value);
                QCOMPARE(actual[1].toInt(), value == 2 ? int(SWMM_XSECT_TRIANGULAR) : value == 3 ? int(SWMM_XSECT_TRAPEZOIDAL) : int(SWMM_XSECT_RECT_OPEN));
            } else if (setter == "link_orifice_type") {
                QCOMPARE(actual[0].toInt(), value); for (int j = 1; j < 6; ++j) QCOMPARE(actual[j], before[i][j]);
            } else QCOMPARE(actual[1].toInt(), value);
        }
        QCOMPARE(section(e, control), untouched);
        QVERIFY(apply(model, names, setter, value, &undo)); QCOMPARE(undo.count(), 1);
        undo.undo(); for (int i = 0; i < names.size(); ++i) QCOMPARE(section(e, names[i].toUtf8().constData()), before[i]);
        undo.redo(); for (int i = 0; i < names.size(); ++i) QCOMPARE(section(e, names[i].toUtf8().constData()), after[i]);
        QString path = QDir(folder()).filePath(QString("saved-%1-%2-%3.inp").arg(setter).arg(value).arg(si));
        QCOMPARE(swmm_model_write(e, path.toUtf8().constData()), SWMM_OK);
        SWMMModelLayer reopened(path); QList<QString> warnings, errors; QVERIFY(reopened.loadModel(warnings, errors));
        for (auto name : names) QCOMPARE(section(reopened.engine(), name.toUtf8().constData()), section(e, name.toUtf8().constData()));
    }
    void dimensionsAndSortedTable()
    {
        auto layer = fixture(); QVERIFY(layer); QUndoStack undo;
        SWMMAttributeTableModel model; model.setSource(layer.get(), SWMMModelLayer::CatWeirs); model.setUndoStack(&undo);
        auto e = layer->engine(); const auto a = section(e, "W1"), b = section(e, "W2");
        QVERIFY(!apply(model, {"W1", "W2"}, "link_xsect_geom1", 2.5, &undo));
        QVERIFY(apply(model, {"W1", "W2", "W3"}, "link_xsect_shape", int(SWMM_XSECT_RECT_OPEN), &undo));
        for (int n = 1; n <= 4; ++n) {
            QString setter = QString("link_xsect_geom%1").arg(n);
            QVERIFY(apply(model, {"W1", "W2", "W3"}, setter, 2.25 + n, &undo));
            for (auto name : {"W1", "W2", "W3"}) QCOMPARE(section(e, name)[n + 1].toDouble(), 2.25 + n);
        }
        QSortFilterProxyModel proxy; proxy.setSourceModel(&model); proxy.setFilterKeyColumn(0);
        proxy.setFilterRegularExpression("W[12]"); proxy.sort(0, Qt::DescendingOrder);
        auto index = proxy.index(0, field(model, "W2", "link_weir_type").column());
        QCOMPARE(model.objectNameAt(proxy.mapToSource(index).row()), QString("W2"));
        QVERIFY(proxy.setData(index, 2)); QCOMPARE(section(e, "W2")[0].toInt(), 2); QCOMPARE(section(e, "W1")[0].toInt(), 0);
        while (undo.canUndo()) undo.undo(); QCOMPARE(section(e, "W1"), a); QCOMPARE(section(e, "W2"), b);
    }
    void outletRecords_data()
    {
        QTest::addColumn<bool>("si"); QTest::addColumn<QString>("setter"); QTest::addColumn<QVariant>("value");
        for (bool si : {false, true}) {
            for (int type = 0; type < 4; ++type)
                QTest::newRow(qPrintable(QString("rating-%1-%2").arg(si).arg(type))) << si << QString("link_outlet_rating_type") << QVariant(type);
            QTest::newRow(si ? "curve-SI" : "curve-US") << si << QString("link_pump_curve_ref") << QVariant("RatingB");
            QTest::newRow(si ? "clear-SI" : "clear-US") << si << QString("link_pump_curve_ref") << QVariant("");
            QTest::newRow(si ? "coeff-SI" : "coeff-US") << si << QString("link_discharge_coeff") << QVariant(8.25);
            QTest::newRow(si ? "expon-SI" : "expon-US") << si << QString("link_outlet_expon") << QVariant(2.25);
        }
    }
    void outletRecords()
    {
        QFETCH(bool, si); QFETCH(QString, setter); QFETCH(QVariant, value);
        auto layer = fixture(si); QVERIFY(layer); auto e = layer->engine();
        QUndoStack undo; SWMMAttributeTableModel model; model.setSource(layer.get(), SWMMModelLayer::CatOutlets);
        QVERIFY(apply(model, {"T1", "T2"}, "link_pump_curve_ref", "RatingA", &undo));
        if (setter == "link_discharge_coeff" || setter == "link_outlet_expon")
            QVERIFY(apply(model, {"T1", "T2"}, "link_outlet_rating_type", 1, &undo));
        undo.clear(); auto a = outlet(e, "T1"), b = outlet(e, "T2"), c = outlet(e, "TC");
        QString error; QVERIFY2(apply(model, {"T1", "T2"}, setter, value, &undo, &error), qPrintable(error));
        QCOMPARE(undo.count(), 1); auto x = outlet(e, "T1"), y = outlet(e, "T2");
        for (auto pair : {qMakePair(a, x), qMakePair(b, y)}) {
            auto expected = pair.first;
            if (setter == "link_outlet_rating_type") expected[0] = value;
            if (setter == "link_pump_curve_ref") { expected[0] = expected[0].toInt() % 2 + (value.toString().isEmpty() ? 0 : 2); expected[1] = value; }
            if (setter == "link_discharge_coeff") expected[2] = value;
            if (setter == "link_outlet_expon") expected[3] = value;
            QCOMPARE(pair.second, expected);
        }
        QCOMPARE(outlet(e, "TC"), c);
        undo.undo(); QCOMPARE(outlet(e, "T1"), a); QCOMPARE(outlet(e, "T2"), b);
        undo.redo(); QCOMPARE(outlet(e, "T1"), x); QCOMPARE(outlet(e, "T2"), y);
        QString path = QDir(folder()).filePath(QString("outlet-%1-%2-%3.inp").arg(setter).arg(value.toString()).arg(si));
        QCOMPARE(swmm_model_write(e, path.toUtf8().constData()), SWMM_OK);
        SWMMModelLayer reopened(path); QList<QString> warnings, errors; QVERIFY(reopened.loadModel(warnings, errors));
        for (auto name : {"T1", "T2", "TC"}) QCOMPARE(outlet(reopened.engine(), name, true), outlet(e, name, true));
    }
    void rejectionAndTypedCurves()
    {
        auto layer = fixture(); QVERIFY(layer); auto e = layer->engine(); QUndoStack undo;
        SWMMAttributeTableModel model; model.setSource(layer.get(), SWMMModelLayer::CatOutlets);
        const auto a = outlet(e, "T1"), b = outlet(e, "T2");
        QVERIFY(!apply(model, {"T1", "T2"}, "link_outlet_rating_type", 2, &undo));
        QVERIFY(!apply(model, {"T1", "T2"}, "link_pump_curve_ref", "Other", &undo));
        QVERIFY(!apply(model, {"T1", "T2"}, "link_pump_curve_ref", "Missing", &undo));
        QVERIFY(!apply(model, {"T1", "Missing"}, "link_outlet_rating_type", 1, &undo));
        QCOMPARE(outlet(e, "T1"), a); QCOMPARE(outlet(e, "T2"), b); QCOMPARE(undo.count(), 0);
        QVERIFY(apply(model, {"T1", "T2"}, "link_pump_curve_ref", "RatingA", &undo));
        QCOMPARE(outlet(e, "T1")[1].toString(), QString("RatingA"));
        int curve = -1, type = -1; swmm_link_get_pump_curve(e, swmm_link_index(e, "T1"), &curve);
        QCOMPARE(swmm_table_get_type(e, curve, &type), SWMM_OK); QCOMPARE(type, 3);
        QVERIFY(!(field(model, "T1", "link_discharge_coeff").flags() & Qt::ItemIsEditable));
        QVERIFY(!apply(model, {"T1", "T2"}, "link_discharge_coeff", 10, &undo));
        undo.undo(); QCOMPARE(outlet(e, "T1"), a);
        model.setSource(layer.get(), SWMMModelLayer::CatOrifices);
        const auto r = section(e, "R1"), s = section(e, "R2");
        QVERIFY(!apply(model, {"R1", "R2"}, "link_xsect_shape", int(SWMM_XSECT_TRIANGULAR), &undo));
        QVERIFY(!apply(model, {"R1", "R2"}, "link_xsect_geom1", 0, &undo));
        QCOMPARE(section(e, "R1"), r); QCOMPARE(section(e, "R2"), s);
    }
    void immediatePropertiesAndPalette()
    {
        auto layer = fixture(); QVERIFY(layer); auto e = layer->engine();
        MapCanvas canvas; layer->setEditCanvas(&canvas);
        PropertiesPanel panel; panel.setProject(layer.get());
        panel.showSelection(&canvas, {{SWMMObjectRef::Link, "W1"}, {SWMMObjectRef::Link, "W2"}, {SWMMObjectRef::Link, "W3"}});
        panel.resize(1000, 1000); panel.show(); auto *tree = panel.findChild<QTreeView *>(); QVERIFY(tree); tree->expandAll();
        auto index = findProperty(tree->model(), "Type"); QVERIFY(index.isValid()); tree->edit(index);
        auto *combo = tree->findChild<QComboBox *>(); QVERIFY(combo); int row = combo->findData(3); QVERIFY(row >= 0);
        combo->setCurrentIndex(row); QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, row));
        QTRY_COMPARE(canvas.undoStack()->count(), 1);
        for (auto name : {"W1", "W2", "W3"}) QCOMPARE(section(e, name)[0].toInt(), 3);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        index = findProperty(tree->model(), "Geom 1"); QVERIFY(index.isValid()); tree->edit(index);
        auto *spin = qobject_cast<QDoubleSpinBox *>(tree->indexWidget(index)); QVERIFY(spin);
        spin->setFocus(); spin->selectAll(); QTest::keyClicks(spin, "9"); QTest::keyClick(spin, Qt::Key_Escape);
        QCOMPARE(canvas.undoStack()->count(), 1); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        tree->edit(index); spin = qobject_cast<QDoubleSpinBox *>(tree->indexWidget(index)); QVERIFY(spin);
        spin->setFocus(); spin->selectAll(); QTest::keyClicks(spin, "2.5"); QTest::keyClick(spin, Qt::Key_Tab);
        QTRY_COMPARE(canvas.undoStack()->count(), 2);
        for (auto name : {"W1", "W2", "W3"}) QCOMPARE(section(e, name)[2].toDouble(), 2.5);
        panel.showSelection(&canvas, {{SWMMObjectRef::Link, "W1"}}); tree->expandAll();
        QSignalSpy dirty(layer.get(), &SWMMModelLayer::modelEdited), forwarded(&panel, &PropertiesPanel::objectEdited);
        index = findProperty(tree->model(), "Type"); QVERIFY(index.isValid());
        QVERIFY(tree->model()->setData(index, 2, Qt::EditRole));
        QCOMPARE(section(e, "W1")[0].toInt(), 2); QCOMPARE(dirty.count(), 1); QCOMPARE(forwarded.count(), 0);
        auto before = section(e, "W1");
        {
            SWMMWeirPropertyAdapter adapter(e, "W1"); adapter.setModelLayer(layer.get());
            LinkCompoundEditDialog dialog(adapter.xsectionRef()); dialog.show();
            auto *spin = dialog.findChild<QDoubleSpinBox *>("xsectGeom1"); QVERIFY(spin);
            auto *edit = spin->findChild<QLineEdit *>(); QVERIFY(edit); edit->setFocus(); edit->selectAll();
            QTest::keyClicks(edit, "9"); QTest::keyClick(edit, Qt::Key_Escape); QCOMPARE(section(e, "W1"), before);
            edit->selectAll(); QTest::keyClicks(edit, "4.5"); QTest::keyClick(edit, Qt::Key_Return);
            QCOMPARE(section(e, "W1")[2].toDouble(), 4.5);
            auto *list = dialog.findChild<QListWidget *>(); QVERIFY(list);
            for (int i = 0; i < list->count(); ++i)
                if (list->item(i)->data(Qt::UserRole).toInt() == SWMM_XSECT_RECT_OPEN) list->setCurrentRow(i);
            QCOMPARE(section(e, "W1")[0].toInt(), 0);
            canvas.undoStack()->undo(); QCOMPARE(section(e, "W1")[1].toInt(), int(SWMM_XSECT_TRIANGULAR));
            dialog.close();
        }
        canvas.undoStack()->undo(); QCOMPARE(section(e, "W1"), before);
    }
    void namedCurveReplayAndLayerFacade()
    {
        auto layer = fixture(); QVERIFY(layer); auto e = layer->engine();
        MapCanvas canvas; layer->setEditCanvas(&canvas); auto *undo = canvas.undoStack();
        SWMMAttributeTableModel model; model.setSource(layer.get(), SWMMModelLayer::CatOutlets);
        QVERIFY(apply(model, {"T1", "T2"}, "link_pump_curve_ref", "RatingA", undo));
        const auto a = outlet(e, "T1"), b = outlet(e, "T2");
        QVERIFY(apply(model, {"T1", "T2"}, "link_pump_curve_ref", "RatingB", undo));
        QCOMPARE(swmm_table_delete(e, swmm_table_index(e, "UnusedCurve"), nullptr), SWMM_OK);
        undo->undo(); QCOMPARE(outlet(e, "T1"), a); QCOMPARE(outlet(e, "T2"), b);
        undo->redo(); const auto x = outlet(e, "T1"), y = outlet(e, "T2");
        int rating = -1;
        for (int i = 0; i < swmm_table_count(e); ++i) {
            int type = -1; swmm_table_get_type(e, i, &type);
            if (type == 3 && QString::fromUtf8(swmm_table_id(e, i)) == "RatingA") rating = i;
        }
        QVERIFY(rating >= 0); QCOMPARE(swmm_table_delete(e, rating, nullptr), SWMM_OK);
        undo->undo(); QCOMPARE(outlet(e, "T1"), x); QCOMPARE(outlet(e, "T2"), y);
        undo->clear(); auto before = section(e, "R2");
        QVERIFY(layer->applyLinkXsect(swmm_link_index(e, "R2"), SWMM_XSECT_CIRCULAR, 4.5, 0, 0, 0));
        QCOMPARE(section(e, "R2")[1].toInt(), int(SWMM_XSECT_CIRCULAR)); QCOMPARE(undo->count(), 1);
        undo->undo(); QCOMPARE(section(e, "R2"), before);
        QVERIFY(!layer->applyLinkXsect(swmm_link_index(e, "R2"), SWMM_XSECT_RECT_OPEN, 4.5, 1, 0, 0));
        QCOMPARE(section(e, "R2"), before);
    }
    void outletSinglePropertiesAndUndoFlags()
    {
        auto layer = fixture(); QVERIFY(layer); auto e = layer->engine();
        MapCanvas canvas; layer->setEditCanvas(&canvas);
        PropertiesPanel panel; panel.setProject(layer.get());
        panel.showSelection(&canvas, {{SWMMObjectRef::Link, "T1"}}); panel.show();
        auto *tree = panel.findChild<QTreeView *>(); QVERIFY(tree); tree->expandAll();
        auto coefficient = findProperty(tree->model(), "Coefficient");
        auto curve = findProperty(tree->model(), "Tabular Curve");
        QVERIFY(coefficient.isValid()); QVERIFY(curve.isValid());
        QVERIFY(coefficient.flags() & Qt::ItemIsEditable);
        SWMMOutletPropertyAdapter adapter(e, "T1"); adapter.setModelLayer(layer.get());
        auto ref = adapter.pumpCurveRef(); ref.currentName = "RatingA";
        QSignalSpy dirty(layer.get(), &SWMMModelLayer::modelEdited), forwarded(&panel, &PropertiesPanel::objectEdited);
        QVERIFY(tree->model()->setData(curve, QVariant::fromValue(ref), Qt::EditRole));
        QCOMPARE(outlet(e, "T1")[0].toInt(), 2); QCOMPARE(dirty.count(), 1); QCOMPARE(forwarded.count(), 0);
        QVERIFY(!(coefficient.flags() & Qt::ItemIsEditable));
        canvas.undoStack()->undo(); QVERIFY(coefficient.flags() & Qt::ItemIsEditable);
        QCOMPARE(outlet(e, "T1")[0].toInt(), 0);
        canvas.undoStack()->redo(); QVERIFY(!(coefficient.flags() & Qt::ItemIsEditable));
    }
    void identitiesStaleAndClosed()
    {
        auto layer = fixture(); QVERIFY(layer); auto e = layer->engine();
        MapCanvas canvas; layer->setEditCanvas(&canvas); auto *undo = canvas.undoStack();
        SWMMAttributeTableModel model; model.setSource(layer.get(), SWMMModelLayer::CatWeirs);
        auto a = section(e, "W1"), b = section(e, "W2");
        QVERIFY(apply(model, {"W1", "W2"}, "link_weir_type", 3, undo));
        QVERIFY(layer->applyLinkDelete("Unused"));
        QVERIFY(layer->applyRename("W1", "Renamed", SWMMModelLayer::kKindLink));
        undo->undo(); QCOMPARE(section(e, "Renamed"), a); QCOMPARE(section(e, "W2"), b);
        undo->redo(); auto x = section(e, "Renamed");
        QCOMPARE(swmm_link_set_orifice_type(e, swmm_link_index(e, "R1"), 1), SWMM_OK);
        undo->undo(); QCOMPARE(section(e, "Renamed"), a);
        undo->redo(); QCOMPARE(swmm_link_set_weir_type(e, swmm_link_index(e, "W2"), 1), SWMM_OK);
        undo->undo(); QCOMPARE(section(e, "Renamed"), x); // Last target stale: first stays untouched.
        undo->clear(); QVERIFY(apply(model, {"Renamed"}, "link_weir_type", 2, undo));
        QVERIFY(layer->applyLinkDelete("Renamed"));
        QVERIFY(layer->applyLinkAdd("Renamed", SWMM_LINK_WEIR, "J1", "J2", {}));
        auto reused = section(e, "Renamed"); undo->undo(); QCOMPARE(section(e, "Renamed"), reused);
        SWMMOrificePropertyAdapter facade(e, "R1"); facade.setModelLayer(layer.get()); facade.setOrificeType(SWMMLinkPropertyAdapter::SIDE);
        layer.reset(); facade.setOrificeType(SWMMLinkPropertyAdapter::BOTTOM); undo->undo();
    }
    void singleLinkIgnoresSameNamedNodeRename()
    {
        auto layer = fixture(); QVERIFY(layer); MapCanvas canvas; layer->setEditCanvas(&canvas);
        PropertiesPanel panel; panel.setProject(layer.get());
        panel.showSelection(&canvas, {{SWMMObjectRef::Link, "W1"}}); panel.show();
        QVERIFY(layer->applyRename("W1", "RenamedNode", SWMMModelLayer::kKindNode));
        auto *tree = panel.findChild<QTreeView *>(); QVERIFY(tree); tree->expandAll();
        auto index = findProperty(tree->model(), "Type"); QVERIFY(index.isValid());
        QVERIFY(tree->model()->setData(index, 2, Qt::EditRole));
        QCOMPARE(section(layer->engine(), "W1")[0].toInt(), 2);
        QCOMPARE(canvas.undoStack()->count(), 1);
        canvas.undoStack()->undo(); QCOMPARE(section(layer->engine(), "W1")[0].toInt(), 0);
    }
    void dialogInvalidatesConvertedOrRemovedTarget()
    {
        auto layer = fixture(); QVERIFY(layer);
        SWMMWeirPropertyAdapter adapter(layer->engine(), "W1"); adapter.setModelLayer(layer.get());
        LinkCompoundEditDialog dialog(adapter.xsectionRef()); dialog.show();
        QVERIFY(layer->applyRename("W1", "Renamed", SWMMModelLayer::kKindLink));
        QVERIFY(dialog.isEnabled());
        QVERIFY(layer->applyLinkConvert("Renamed", SWMM_LINK_ORIFICE));
        QVERIFY(!dialog.isEnabled());
        SWMMOrificePropertyAdapter orifice(layer->engine(), "R1"); orifice.setModelLayer(layer.get());
        LinkCompoundEditDialog removed(orifice.xsectionRef()); removed.show();
        QVERIFY(layer->applyLinkDelete("R1")); QVERIFY(!removed.isEnabled());
        QVERIFY(layer->applyLinkAdd("R1", SWMM_LINK_ORIFICE, "J2", "O1", {}));
        auto before = section(layer->engine(), "R1");
        auto *spin = removed.findChild<QDoubleSpinBox *>("xsectGeom1"); QVERIFY(spin);
        spin->setValue(9); QCOMPARE(section(layer->engine(), "R1"), before);
    }
    void renderingAndMixedChoices()
    {
        auto layer = fixture(); QVERIFY(layer); auto e = layer->engine(); QUndoStack undo;
        SWMMAttributeTableModel model; model.setSource(layer.get(), SWMMModelLayer::CatWeirs);
        auto renderer = std::make_unique<OpenSWMM::Render::CategorizedRenderer>();
        renderer->setClassifyAttribute("maxDepth");
        OpenSWMM::Render::LineSymbolLayerSpec line;
        OpenSWMM::Render::SymbolStyle red, green; line.color = Qt::red; red.layers << line.toSymbolLayer(); line.color = Qt::green; green.layers << line.toSymbolLayer();
        renderer->addCategory({layer->identifyByName("W1", SWMMModelLayer::kKindLink)["maxDepth"].toString(), "Original", red}); renderer->setFallbackSymbol(green);
        layer->setKindRenderer(SWMMModelLayer::CatWeirs, std::move(renderer));
        int row = swmm_link_index(e, "W1"), control = swmm_link_index(e, "WC");
        QCOMPARE(layer->featureColor(SWMMModelLayer::CatWeirs, row), QColor(Qt::red));
        QVERIFY(apply(model, {"W1"}, "link_xsect_geom1", 3.25, &undo));
        QCoreApplication::processEvents(); QCOMPARE(layer->featureColor(SWMMModelLayer::CatWeirs, row), QColor(Qt::green));
        QCOMPARE(layer->featureColor(SWMMModelLayer::CatWeirs, control), QColor(Qt::red));
        undo.undo(); QCoreApplication::processEvents(); QCOMPARE(layer->featureColor(SWMMModelLayer::CatWeirs, row), QColor(Qt::red));
        undo.redo(); QCoreApplication::processEvents(); QCOMPARE(layer->featureColor(SWMMModelLayer::CatWeirs, row), QColor(Qt::green));
        SWMMAggregateSelection scope(layer.get(), {{SWMMModelLayer::CatOrifices, {"R1"}}, {SWMMModelLayer::CatWeirs, {"W1"}}}, &undo, nullptr);
        int column = field(*scope.source(), scope.source()->objectNameAt(0), "link_xsect_shape").column();
        QVERIFY(column >= 0); QCOMPARE(scope.field(column).editable, 2);
        auto disabled = scope.editorData(column, openswmmvis::kDisabledEnumValuesRole).toList();
        const auto choices = scope.source()->columnSpecs()[column].enumValues;
        for (const auto &choice : choices) QVERIFY(disabled.contains(choice.toList()[1]));
    }
};
QTEST_MAIN(TestRegulatorBulk)
#include "test_regulatorbulk.moc"
