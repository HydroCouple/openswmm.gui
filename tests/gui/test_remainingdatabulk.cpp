// SPDX-License-Identifier: GPL-3.0-or-later
#include "layers/swmmmodellayer.h"
#include "ui/panels/dataobjectaggregateselection.h"
#include "ui/panels/propertiespanel.h"
#include "ui/panels/featureaggregateselection.h"
#include "ui/properties/datarecordsref.h"
#include "layers/gisobjectref.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "pollutant/pollutantregistry.h"
#include "timeseries/timeseriesregistry.h"
#include "curve/curveregistry.h"
#include "core/swmmdatetime.h"
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_tables.h>
#include <openswmm/engine/openswmm_controls.h>
#include <openswmm/engine/openswmm_inflows.h>
#include <openswmm/engine/openswmm_quality.h>
#include <QTest>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include "ui/panels/swmmaggregateselection.h"
#include <openswmm/engine/openswmm_nodes.h>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTreeView>
#include <QTableView>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPlainTextEdit>
#include <QDialog>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
using DC = SWMMModelLayer;
using Model = DataObjectAttributeTableModel;
namespace
{
QString folder()
{
    return QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA"))
        .absoluteFilePath("../../../workplans/bulk-completion/review");
}
QStringList targets(const QString &prefix)
{
    return {prefix + "_A", prefix + "_B", prefix + "_C"};
}
std::unique_ptr<DC> fixture(bool si = false)
{
    QDir().mkpath(folder());
    QFile base(QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA"))
                   .filePath("data_object_table_fixture.inp"));
    if (!base.open(QIODevice::ReadOnly))
        return {};
    auto text = base.readAll();
    if (si)
        text.replace("CFS", "CMS");
    text +=
        "\n[RAINGAGES]\nG1 INTENSITY 0:15 1 TIMESERIES TS_A\nG2 INTENSITY 0:15 1 TIMESERIES TS_B\n[LANDUSES]\n";
    for (const auto &id : targets("LU") + QStringList{"LU_Control"})
        text += (id + " 1 .2 0\n").toUtf8();
    text += "[BUILDUP]\n";
    for (const auto &id : targets("LU") + QStringList{"LU_Control"})
        text += (id + " TSS POW 2 .3 .2 AREA\n").toUtf8();
    text += "[WASHOFF]\n";
    for (const auto &id : targets("LU") + QStringList{"LU_Control"})
        text += (id + " TSS EXP .2 .5 10 20\n").toUtf8();
    text += "[CURVES]\n";
    const QStringList types{"STORAGE", "DIVERSION", "RATING", "SHAPE", "CONTROL", "TIDAL",
                            "PUMP1",   "PUMP2",     "PUMP3",  "PUMP4", "PUMP5"};
    for (int type = 0; type < types.size(); ++type)
        for (const auto &id : targets(QString("C%1").arg(type + 1)) +
                                  QStringList{QString("C%1_Control").arg(type + 1)})
            text += (id + " " + types[type] + " 0 0\n" + id + " 1 2\n" + id + " 2 4\n").toUtf8();
    text += "[TIMESERIES]\n";
    for (const auto &id : targets("TS") + QStringList{"TS_Control"})
        text += (id + " 01/01/2026 0:00 1\n" + id + " 01/01/2026 1:00 2\n" + id +
                 " 01/01/2026 2:00 3\n")
                    .toUtf8();
    text += "[HYDROGRAPHS]\n";
    const QStringList months{"ALL", "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                             "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"},
        responses{"SHORT", "MEDIUM", "LONG"};
    for (const auto &id : targets("H") + QStringList{"H_Control"}) {
        text += (id + " G1\n").toUtf8();
        for (const auto &m : months)
            for (const auto &r : responses)
                text += (id + " " + m + " " + r + " .1 1 2 .5 .2 .1\n").toUtf8();
    }
    text += "[CONTROLS]\n";
    for (const auto &id : targets("R") + QStringList{"R_Control"})
        text += ("RULE " + id +
                 "\nIF NODE J1 DEPTH > 1\nTHEN CONDUIT C1 STATUS = CLOSED\nPRIORITY 1\n\n")
                    .toUtf8();
    const auto path = QDir(folder()).filePath(si ? "initial-si.inp" : "initial-us.inp");
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};
    out.write(text);
    out.close();
    auto layer = std::make_unique<DC>(path);
    QList<QString> w, e;
    if (!layer->loadModel(w, e)) {
        qWarning() << e;
        return {};
    }
    for (const auto &id : targets("H") + QStringList{"H_Control"})
        for (int r = 0; r < 3; ++r)
            if (swmm_rdii_decay_set(layer->engine(), id.toUtf8(), r, .2, .1, .3, 10, .02, -1, 0, 0,
                                    1) != SWMM_OK)
                return {};
    return layer;
}
QString token(DC *l, const QString &name = "TSS")
{
    auto *r =
        qobject_cast<openswmmvis::pollutant::PollutantRegistry *>(l->ensurePollutantRegistry());
    return QString::number(reinterpret_cast<quintptr>(r->findByName(name)), 16);
}
QVariantMap stable(DC *l, DC::DataCategory cat, const QString &name)
{
    auto r = Model::dataRecord(l, cat, name);
    r.remove("_catalog");
    QVariantMap out;
    for (auto i = r.cbegin(); i != r.cend(); ++i) {
        auto key = i.key();
        for (auto *p :
             qobject_cast<openswmmvis::pollutant::PollutantRegistry *>(l->ensurePollutantRegistry())
                 ->providers())
            key.replace(QString::number(reinterpret_cast<quintptr>(p), 16), p->name());
        out[key] = i.value();
    }
    if (cat == DC::DataControls)
        out["body"] = out["body"].toString().trimmed();
    return out;
}
QModelIndex findProperty(QAbstractItemModel *m, const QString &label,
                         const QModelIndex &parent = {})
{
    for (int i = 0; i < m->rowCount(parent); ++i) {
        auto x = m->index(i, 0, parent);
        if (x.data().toString() == label)
            return x.siblingAtColumn(1);
        auto c = findProperty(m, label, x);
        if (c.isValid())
            return c;
    }
    return {};
}
}
class TestRemainingDataBulk : public QObject
{
    Q_OBJECT
private slots:
    void allFields_data()
    {
        QTest::addColumn<int>("category");
        QTest::addColumn<QString>("prefix");
        QTest::addColumn<QString>("key");
        QTest::addColumn<QVariant>("value");
        QTest::addColumn<bool>("si");
        for (bool si : {false, true}) {
            auto add = [&](DC::DataCategory c, QString prefix, QString key, QVariant v) {
                QTest::newRow(qPrintable(QString("%1-%2-%3-%4").arg(c).arg(prefix, key).arg(si)))
                    << int(c) << prefix << key << v << si;
            };
            for (int t = 1; t <= 11; ++t)
                add(DC::DataCurves, QString("C%1").arg(t), "point/1", 4.75);
            add(DC::DataHydrographs, "H", "gage", QString("G2"));
            for (int month = -1; month < 12; ++month)
                for (int response = 0; response < 3; ++response)
                    for (int i = 0; i < 6; ++i)
                        add(DC::DataHydrographs, "H",
                            QString("rtk/%1/%2/%3").arg(month).arg(response).arg(i),
                            i == 0   ? .25
                            : i == 1 ? 2.
                            : i == 2 ? 3.
                            : i == 3 ? .8
                            : i == 4 ? .3
                                     : .05);
            for (int response = 0; response < 3; ++response) {
                for (int i = 0; i < 8; ++i)
                    add(DC::DataHydrographs, "H", QString("decay/%1/%2").arg(response).arg(i),
                        i == 3   ? 12.
                        : i == 5 ? -2.
                        : i == 6 ? 1.
                                 : .04);
                add(DC::DataHydrographs, "H", QString("decay/%1/snow").arg(response), 1);
            }
            for (const auto &key :
                 QStringList{"buildup/p/TSS/0", "buildup/p/TSS/1", "buildup/p/TSS/2",
                             "buildup/p/TSS/normalizer", "washoff/p/TSS/0", "washoff/p/TSS/1",
                             "washoff/p/TSS/2", "washoff/p/TSS/3"})
                add(DC::DataLandUses, "LU", key,
                    key.endsWith("normalizer") ? QVariant(1)
                    : key.startsWith("washoff/") && (key.endsWith("/2") || key.endsWith("/3"))
                        ? QVariant(35.)
                        : QVariant(.75));
        }
    }
    void allFields()
    {
        QFETCH(int, category);
        QFETCH(QString, prefix);
        QFETCH(QString, key);
        QFETCH(QVariant, value);
        QFETCH(bool, si);
        auto l = fixture(si);
        QVERIFY(l);
        auto cat = DC::DataCategory(category);
        key.replace("p/TSS/", "p/" + token(l.get()) + "/");
        const auto ids = targets(prefix);
        QList<QVariantMap> before;
        for (const auto &id : ids)
            before << Model::dataRecord(l.get(), cat, id);
        auto control = stable(l.get(), cat, prefix + "_Control");
        QUndoStack undo;
        QString error;
        QSignalSpy dirty(l.get(), &DC::modelEdited);
        QVERIFY2(Model::applyValues(l.get(), cat, ids, key, value, &undo, &error),
                 qPrintable(error));
        QCOMPARE(undo.count(), 1);
        QCOMPARE(dirty.count(), 1);
        for (int n = 0; n < ids.size(); ++n) {
            auto expected = before[n];
            expected[key] = value;
            QCOMPARE(Model::dataRecord(l.get(), cat, ids[n]), expected);
        }
        QCOMPARE(stable(l.get(), cat, prefix + "_Control"), control);
        QVERIFY(Model::applyValues(l.get(), cat, ids, key, value, &undo));
        QCOMPARE(undo.count(), 1);
        undo.undo();
        for (int n = 0; n < ids.size(); ++n)
            QCOMPARE(Model::dataRecord(l.get(), cat, ids[n]), before[n]);
        undo.redo();
        const auto path = QDir(folder()).filePath(QString("saved-%1-%2-%3.inp")
                                                      .arg(category)
                                                      .arg(QString(key).replace('/', '-'))
                                                      .arg(si));
        QCOMPARE(swmm_model_write(l->engine(), path.toUtf8()), SWMM_OK);
        DC reopened(path);
        QList<QString> w, e;
        QVERIFY2(reopened.loadModel(w, e), qPrintable(e.join(' ')));
        for (const auto &id : ids)
            QCOMPARE(stable(&reopened, cat, id), stable(l.get(), cat, id));
    }
    void keysSourceTransitionsAndIndependentUndo()
    {
        auto l = fixture();
        QVERIFY(l);
        QUndoStack undo;
        auto ids = targets("C3");
        auto before = Model::dataRecord(l.get(), DC::DataCurves, ids[0]);
        QVERIFY(Model::applyValues(l.get(), DC::DataCurves, ids, "_pointAdd", QVariantList{1.5, 3.},
                                   &undo));
        QVERIFY(Model::applyValues(l.get(), DC::DataCurves, {ids[0]}, "point/0", .5, nullptr));
        undo.undo();
        QCOMPARE(Model::dataRecord(l.get(), DC::DataCurves, ids[0])["point/0"].toDouble(), .5);
        QVERIFY(!Model::dataRecord(l.get(), DC::DataCurves, ids[0]).contains("point/1.5"));
        undo.redo();
        QVERIFY(
            !Model::applyValues(l.get(), DC::DataCurves, {"C3_A", "C1_A"}, "point/1", 3., &undo));
        const auto first = Model::dataRecord(l.get(), DC::DataCurves, ids[0]);
        QVERIFY(Model::applyValues(l.get(), DC::DataCurves, {ids[2]}, "_pointRemove",
                                   QVariantList{1.5}, nullptr));
        QVERIFY(!Model::applyValues(l.get(), DC::DataCurves, ids, "point/1.5", 4., &undo));
        QCOMPARE(Model::dataRecord(l.get(), DC::DataCurves, ids[0]), first);
        QFile csv(QDir(folder()).filePath("samples.csv"));
        QVERIFY(csv.open(QIODevice::WriteOnly | QIODevice::Truncate));
        csv.write("Date,Flow,Stage\n01/01/2026 00:00,1,10\n01/01/2026 01:00,2,20\n");
        csv.close();
        const auto binding = csv.fileName() + ":Stage";
        const auto ts = targets("TS");
        QList<QVariantMap> old;
        for (const auto &id : ts)
            old << Model::dataRecord(l.get(), DC::DataTimeSeries, id);
        QString error;
        QVERIFY2(Model::applyValues(l.get(), DC::DataTimeSeries, ts, "fileToken", binding, &undo,
                                    &error),
                 qPrintable(error));
        for (const auto &id : ts) {
            auto r = Model::dataRecord(l.get(), DC::DataTimeSeries, id);
            QCOMPARE(r["fileToken"].toString(), binding);
            QCOMPARE(r["relativeCount"].toInt(), 0);
            QVERIFY(r.values().contains(QVariant(20.)));
        }
        QVERIFY(!Model::applyValues(l.get(), DC::DataTimeSeries, ts, "fileToken",
                                    csv.fileName() + ":Missing", &undo));
        undo.undo();
        for (int n = 0; n < ts.size(); ++n)
            QCOMPARE(Model::dataRecord(l.get(), DC::DataTimeSeries, ts[n]), old[n]);
        undo.redo();
        QVERIFY(Model::applyValues(l.get(), DC::DataTimeSeries, ts, "fileToken", QString(), &undo));
        QVERIFY(Model::applyValues(l.get(), DC::DataTimeSeries, ts, "timeMode", 1, &undo));
        const auto path = QDir(folder()).filePath("relative-series.inp");
        QCOMPARE(swmm_model_write(l->engine(), path.toUtf8()), SWMM_OK);
        DC reopened(path);
        QList<QString> w, e;
        QVERIFY2(reopened.loadModel(w, e), qPrintable(e.join(' ')));
        QCOMPARE(stable(&reopened, DC::DataTimeSeries, ts[0]),
                 stable(l.get(), DC::DataTimeSeries, ts[0]));
    }
    void rulesAndExplicitRows()
    {
        auto l = fixture();
        QVERIFY(l);
        QUndoStack undo;
        QString error;
        auto ids = targets("R");
        auto control = stable(l.get(), DC::DataControls, "R_Control");
        QVERIFY2(
            Model::applyValues(l.get(), DC::DataControls, ids, "body",
                               "IF NODE J2 DEPTH > 2\nTHEN CONDUIT C1 STATUS = OPEN\nPRIORITY 2\n",
                               &undo, &error),
            qPrintable(error));
        for (const auto &id : ids) {
            auto body = Model::dataRecord(l.get(), DC::DataControls, id)["body"].toString();
            QVERIFY(body.startsWith("RULE " + id + "\n"));
            QVERIFY(body.contains("NODE J2"));
        }
        auto before = stable(l.get(), DC::DataControls, ids[0]);
        QVERIFY(!Model::applyValues(l.get(), DC::DataControls, ids, "body",
                                    "IF NODE Missing DEPTH > 0\nTHEN CONDUIT C1 STATUS = CLOSED",
                                    &undo));
        QCOMPARE(stable(l.get(), DC::DataControls, ids[0]), before);
        QCOMPARE(stable(l.get(), DC::DataControls, "R_Control"), control);
        undo.undo();
        undo.redo();
        auto h = targets("H");
        const auto initial = Model::dataRecord(l.get(), DC::DataHydrographs, h[0]);
        QVERIFY(Model::applyValues(l.get(), DC::DataHydrographs, h, "decay/1/present", 0, &undo));
        QVERIFY(!Model::applyValues(l.get(), DC::DataHydrographs, h, "decay/1/0", .5, &undo));
        undo.undo();
        QCOMPARE(Model::dataRecord(l.get(), DC::DataHydrographs, h[0]), initial);
        QVERIFY(Model::applyValues(l.get(), DC::DataHydrographs, h, "rtk/2/2/present", 0, &undo));
        QVERIFY(Model::applyValues(l.get(), DC::DataHydrographs, h, "rtk/2/2/present", 1, &undo));
        QCOMPARE(Model::dataRecord(l.get(), DC::DataHydrographs, h[0])["rtk/2/2/2"].toDouble(), 1.);
        auto lu = targets("LU");
        auto p = "buildup/p/" + token(l.get()) + "/";
        QVERIFY(Model::applyValues(l.get(), DC::DataLandUses, lu, p + "series", "TS_A", &undo));
        QCOMPARE(Model::dataRecord(l.get(), DC::DataLandUses, lu[0])[p + "type"].toInt(), 4);
        const auto saved = QDir(folder()).filePath("external-buildup.inp");
        QCOMPARE(swmm_model_write(l->engine(), saved.toUtf8()), SWMM_OK);
        DC reopened(saved);
        QList<QString> w, e;
        QVERIFY2(reopened.loadModel(w, e), qPrintable(e.join(' ')));
        QCOMPARE(stable(&reopened, DC::DataLandUses, lu[0]),
                 stable(l.get(), DC::DataLandUses, lu[0]));
    }
    void widgetsAndStaleIdentities()
    {
        auto l = fixture();
        QVERIFY(l);
        MapCanvas canvas;
        l->setEditCanvas(&canvas);
        PropertiesPanel panel;
        panel.setProject(l.get());
        panel.showSelection(&canvas,
                            {{SWMMObjectRef::Curve, "C3_A"}, {SWMMObjectRef::Curve, "C3_B"}});
        panel.show();
        auto *tree = panel.findChild<QTreeView *>();
        QVERIFY(tree);
        const auto navigation = findProperty(tree->model(), "Detailed properties");
        QVERIFY(navigation.isValid());
        auto ref = navigation.data(Qt::EditRole).value<DataRecordsRef>();
        QCOMPARE(ref.names.size(), 2);
        openDataRecords(ref, &panel);
        QCoreApplication::processEvents();
        auto *dialog = panel.findChild<QDialog *>("DataRecordsDialog");
        QVERIFY(dialog && dialog->isVisible());
        auto *table = dialog->findChild<QTableView *>("dataRecordsTable");
        QVERIFY(table);
        auto cell = table->model()->index(1, 1);
        table->edit(cell);
        auto *spin = table->findChild<QDoubleSpinBox *>();
        QVERIFY(spin);
        spin->setFocus();
        spin->selectAll();
        QTest::keyClicks(spin, "6.25");
        QTest::keyClick(spin, Qt::Key_Return);
        QTRY_COMPARE(Model::dataRecord(l.get(), DC::DataCurves, "C3_A")["point/1"].toDouble(),
                     6.25);
        QCOMPARE(canvas.undoStack()->count(), 1);
        QTRY_COMPARE(table->model()->index(1, 1).data(Qt::DisplayRole).toDouble(), 6.25);
        QVERIFY(dialog->grab().save(QDir(folder()).filePath("detailed-properties.png")));
        dialog->close();
        canvas.undoStack()->undo();
        QCOMPARE(Model::dataRecord(l.get(), DC::DataCurves, "C3_B")["point/1"].toDouble(), 2.);
        DataObjectAggregateSelection scope(l.get(), DC::DataCurves, {"C3_A"}, canvas.undoStack());
        auto *reg = qobject_cast<openswmmvis::curve::CurveRegistry *>(l->ensureCurveRegistry());
        reg->remove(reg->findByName("C3_A"));
        QVERIFY(reg->create("C3_A", openswmmvis::curve::CurveType::Rating));
        QVERIFY(!scope.active());
        QVERIFY(!scope.editorData(3).isValid());
        const auto path = QDir(folder()).filePath("All_Object_Bulk_Review.inp");
        QCOMPARE(swmm_model_write(l->engine(), path.toUtf8()), SWMM_OK);
    }
    void acceptanceOptionalFeatureChoiceCanBeCleared()
    {
        using namespace openswmmvis::feature;
        const auto gpkg = QDir(folder()).filePath("acceptance-choice.gpkg");
        QFile::remove(gpkg);
        Schema schema;
        FieldDef choice;
        choice.name = "choice";
        choice.choiceSource = ChoiceSource::Fixed;
        choice.choices = {{"a", "First"}, {"b", "Second"}};
        schema.append(choice);
        QString error;
        auto *layer = FeatureLayer::create(gpkg, "Choices", GeometryType::Point, ZPolicy{}, schema,
                                           QString(), FeatureLayerRole::General, &error, nullptr);
        QVERIFY2(layer, qPrintable(error));
        MapCanvas canvas;
        canvas.addLayer(layer, false);
        QVector<qint64> ids;
        for (int i = 0; i < 3; ++i) {
            Feature f;
            f.geometry = FeatureGeometry(GeometryType::Point);
            Part p;
            p.exterior.pts << QPointF(i, 0);
            f.geometry.parts() << p;
            f.attributes = {{"choice", i == 1 ? "b" : "a"}};
            ids << layer->addFeature(f);
        }
        layer->setEditing(true);
        PropertiesPanel panel;
        panel.showSelection(&canvas, {GisObjectRef::feature(layer->layerId(), ids[0]),
                                      GisObjectRef::feature(layer->layerId(), ids[1])});
        panel.show();
        auto *tree = panel.findChild<QTreeView *>();
        QVERIFY(tree);
        auto cell = findProperty(tree->model(), "choice");
        QVERIFY(cell.isValid());
        tree->edit(cell);
        auto *combo = tree->findChild<QComboBox *>();
        QVERIFY(combo);
        const int none = combo->findData(QString());
        QVERIFY(none >= 0);
        combo->setCurrentIndex(none);
        QVERIFY(QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, none)));
        QCOMPARE(canvas.undoStack()->count(), 1);
        Feature f;
        QVERIFY(layer->feature(ids[0], f));
        QVERIFY(f.attributes["choice"].toString().isEmpty());
        QVERIFY(layer->feature(ids[2], f));
        QCOMPARE(f.attributes["choice"].toString(), QString("a"));
        canvas.undoStack()->undo();
        QVERIFY(layer->feature(ids[1], f));
        QCOMPARE(f.attributes["choice"].toString(), QString("b"));
        canvas.undoStack()->redo();
        QVERIFY(!applyFeatureAttributeValues(layer, ids.mid(0, 2), "choice", QString("unknown"),
                                             &canvas, &error));
        QCOMPARE(canvas.undoStack()->count(), 1);
    }
    void acceptanceBoundSeriesUnchangedBindingIsNoOp()
    {
        auto layer = fixture();
        QVERIFY(layer);
        QUndoStack undo;
        QString error;
        QFile file(QDir(folder()).filePath("acceptance-binding.csv"));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("Date,Flow\n01/01/2026 00:00,1\n01/01/2026 01:00,2\n");
        file.close();
        const auto ids = targets("TS");
        const auto binding = file.fileName() + ":Flow";
        QVERIFY2(Model::applyValues(layer.get(), DC::DataTimeSeries, ids, "fileToken", binding,
                                    &undo, &error),
                 qPrintable(error));
        const auto before = Model::dataRecord(layer.get(), DC::DataTimeSeries, ids.first());
        // A source can disappear while the accepted samples remain usable. Leaving
        // an unchanged binding must neither reread it nor turn a no-op into an error.
        QVERIFY(file.remove());
        QVERIFY2(Model::applyValues(layer.get(), DC::DataTimeSeries, ids, "fileToken", binding,
                                    &undo, &error),
                 qPrintable(error));
        QCOMPARE(undo.count(), 1);
        auto after = Model::dataRecord(layer.get(), DC::DataTimeSeries, ids.first());
        auto expected = before;
        expected["_fileStamp"] = after["_fileStamp"];
        QCOMPARE(after, expected);
    }
    void acceptanceRetainedMixedTimeModeIsNoOp()
    {
        auto layer = fixture();
        QVERIFY(layer);
        QUndoStack undo;
        QString error;
        for (const auto &name : targets("TS")) {
            auto record = Model::dataRecord(layer.get(), DC::DataTimeSeries, name);
            QVector<double> x, y;
            for (auto it = record.cbegin(); it != record.cend(); ++it)
                if (it.key().startsWith("point/")) {
                    x << it.key().mid(6).toDouble();
                    y << it.value().toDouble();
                }
            const auto index = swmm_table_index(layer->engine(), name.toUtf8());
            QCOMPARE(swmm_table_replace_points(layer->engine(), index, x.data(), y.data(), x.size(),
                                               1, x.first(), ""),
                     SWMM_OK);
        }
        const auto before = Model::dataRecord(layer.get(), DC::DataTimeSeries, "TS_A");
        QVERIFY2(Model::applyValues(layer.get(), DC::DataTimeSeries, targets("TS"), "timeMode", 2,
                                    &undo, &error),
                 qPrintable(error));
        QCOMPARE(undo.count(), 0);
        QCOMPARE(Model::dataRecord(layer.get(), DC::DataTimeSeries, "TS_A"), before);
    }
    void acceptanceSelectedPointUsesExactKey_data()
    {
        QTest::addColumn<double>("precise");
        QTest::newRow("full-double-precision") << .123456789012345;
        QTest::newRow("small-coordinate") << 1.e-23;
    }
    void acceptanceSelectedPointUsesExactKey()
    {
        auto layer = fixture();
        QVERIFY(layer);
        QUndoStack undo;
        QString error;
        QFETCH(double, precise);
        QVERIFY(Model::applyValues(layer.get(), DC::DataCurves, targets("C3"), "_pointAdd",
                                   QVariantList{precise, 8.}, nullptr));
        QWidget host;
        openDataRecords(dataRecordsRef(layer.get(), DC::DataCurves, targets("C3"), &undo), &host);
        auto *dialog = host.findChild<QDialog *>("DataRecordsDialog");
        QVERIFY(dialog);
        auto *table = dialog->findChild<QTableView *>("dataRecordsTable");
        QVERIFY(table);
        QModelIndex point;
        for (int row = 0; row < table->model()->rowCount(); ++row)
            if (table->model()->index(row, 1).data().toDouble() == 8.)
                point = table->model()->index(row, 1);
        QVERIFY(point.isValid());
        table->setCurrentIndex(point);
        QCoreApplication::processEvents();
        auto *x = dialog->findChild<QLineEdit *>("dataPointX");
        QVERIFY(x);
        QCOMPARE(x->text().toDouble(), precise);
        QPushButton *remove = nullptr;
        for (auto *b : dialog->findChildren<QPushButton *>())
            if (b->text() == "Remove point")
                remove = b;
        QVERIFY(remove);
        remove->click();
        const auto key = "point/" + QString::number(precise, 'g', 17);
        QTRY_VERIFY(!Model::dataRecord(layer.get(), DC::DataCurves, "C3_A").contains(key));
        QCOMPARE(undo.count(), 1);
        undo.undo();
        QCOMPARE(Model::dataRecord(layer.get(), DC::DataCurves, "C3_C")[key].toDouble(), 8.);
        QTRY_COMPARE(table->model()->rowCount(), 4);
        bool restored = false;
        for (int row = 0; row < table->model()->rowCount(); ++row)
            restored |= table->model()->index(row, 1).data().toDouble() == 8.;
        QVERIFY(restored);
        const auto saved = QDir(folder()).filePath("acceptance-precise-curve.inp");
        QCOMPARE(swmm_model_write(layer->engine(), saved.toUtf8()), SWMM_OK);
        DC reopened(saved);
        QList<QString> w, e;
        QVERIFY2(reopened.loadModel(w, e), qPrintable(e.join(' ')));
        QCOMPARE(stable(&reopened, DC::DataCurves, "C3_C"),
                 stable(layer.get(), DC::DataCurves, "C3_C"));
        QVERIFY(dialog->grab().save(QDir(folder()).filePath("acceptance-precise-point.png")));
    }
    void acceptanceDisabledPointExplainsOwnership()
    {
        auto layer = fixture();
        QVERIFY(layer);
        QUndoStack undo;
        QVERIFY(Model::applyValues(layer.get(), DC::DataCurves, {"C3_C"}, "_pointRemove",
                                   QVariantList{1.}, nullptr));
        QWidget host;
        openDataRecords(dataRecordsRef(layer.get(), DC::DataCurves, targets("C3"), &undo), &host);
        auto *table = host.findChild<QTableView *>("dataRecordsTable");
        QVERIFY(table);
        const auto cell = table->model()->index(1, 1);
        QVERIFY(!(cell.flags() & Qt::ItemIsEditable));
        QVERIFY2(cell.data(Qt::ToolTipRole).toString().contains("missing", Qt::CaseInsensitive),
                 qPrintable(cell.data(Qt::ToolTipRole).toString()));
    }
    void removedReferenceDoesNotRedirectHistory()
    {
        auto layer = fixture();
        QVERIFY(layer);
        QUndoStack undo;
        QVERIFY(Model::applyValues(layer.get(), DC::DataHydrographs, targets("H"), "gage",
                                   QString("G2"), &undo));
        QVERIFY(layer->applyGageDelete("G1"));
        undo.undo();
        for (const auto &name : targets("H"))
            QCOMPARE(Model::dataRecord(layer.get(), DC::DataHydrographs, name)["gage"].toString(),
                     QString("G2"));
    }
    void ruleWidgetImmediateUndoAndEscape()
    {
        auto layer = fixture();
        QVERIFY(layer);
        MapCanvas canvas;
        layer->setEditCanvas(&canvas);
        QWidget host;
        const auto ref =
            dataRecordsRef(layer.get(), DC::DataControls, targets("R"), canvas.undoStack());
        openDataRecords(ref, &host);
        auto *dialog = host.findChild<QDialog *>("DataRecordsDialog");
        QVERIFY(dialog);
        auto *text = dialog->findChild<QPlainTextEdit *>("dataRuleBody");
        QVERIFY(text);
        const auto original = text->toPlainText();
        text->setFocus();
        text->setPlainText("IF NODE J2 DEPTH > 3\nTHEN CONDUIT C1 STATUS = OPEN\n");
        QTest::keyClick(text, Qt::Key_Return, Qt::ControlModifier);
        QTRY_COMPARE(canvas.undoStack()->count(), 1);
        QVERIFY(Model::dataRecord(layer.get(), DC::DataControls, "R_B")["body"].toString().contains(
            "NODE J2"));
        canvas.undoStack()->undo();
        QTRY_COMPARE(text->toPlainText(), original);
        text->setPlainText("unfinished");
        QTest::keyClick(text, Qt::Key_Escape);
        QCOMPARE(text->toPlainText(), original);
        QCOMPARE(canvas.undoStack()->count(), 1);
    }
    void reviewInventory()
    {
        auto layer = fixture();
        QVERIFY(layer);
        QJsonArray inventory;
        for (int c = 0; c < int(DC::NumDataCategories); ++c) {
            const auto category = DC::DataCategory(c);
            QVERIFY(Model::supportsCategory(category));
            Model source;
            source.setSource(layer.get(), category);
            QStringList names;
            for (int row = 0; row < source.rowCount(); ++row)
                names << source.objectNameAt(row);
            QJsonArray fields;
            for (int col = 0; col < source.columnCount(); ++col) {
                const auto spec = source.columnSpecs()[col];
                fields << QJsonObject{{"key", spec.key},
                                      {"label", spec.label},
                                      {"editor", int(spec.editor)},
                                      {"reason", spec.tooltip}};
            }
            QJsonArray records;
            for (const auto &field : Model::recordFields(layer.get(), category, names))
                records << QJsonObject{{"key", field.spec.key},
                                       {"label", field.spec.label},
                                       {"editable", field.editable},
                                       {"reason", field.reason}};
            inventory << QJsonObject{{"category", c},
                                     {"objects", names.size()},
                                     {"fields", fields},
                                     {"keyed_fields", records}};
        }
        QFile out(QDir(folder()).filePath("data-field-inventory.json"));
        QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
        out.write(QJsonDocument(inventory).toJson());
        out.close();
        QCOMPARE(swmm_model_write(layer->engine(),
                                  QDir(folder()).filePath("All_Object_Bulk_Review.inp").toUtf8()),
                 SWMM_OK);
    }
    void largeSelectionEvidence()
    {
        QJsonArray timings;
        for (bool bellinge : {false, true}) {
            std::unique_ptr<DC> layer;
            if (bellinge) {
                const auto src =
                    QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA"))
                        .absoluteFilePath(
                            "../../../examples/bellinge_2d/BellingeSWMM_v021_nopervious.inp");
                QFile f(src);
                QVERIFY(f.open(QIODevice::ReadOnly));
                auto input = f.readAll();
                const auto start = input.indexOf("[2D_NODE_COUPLING]");
                QVERIFY(start > 0);
                input.truncate(start);
                const auto path = QDir(folder()).filePath("Bellinge_1D_Bulk_Review.inp");
                QFile output(path);
                QVERIFY(output.open(QIODevice::WriteOnly | QIODevice::Truncate));
                output.write(input);
                output.close();
                layer = std::make_unique<DC>(path);
                QList<QString> w, e;
                QVERIFY2(layer->loadModel(w, e), qPrintable(e.join(' ')));
            } else {
                layer = fixture();
                QVERIFY(layer);
                DC::BulkEdit bulk(layer.get());
                for (int i = 0; i < 5000; ++i) {
                    int index = -1;
                    QVERIFY(layer->applyNodeAdd(QString("Perf%1").arg(i), SWMM_NODE_JUNCTION, i, 0,
                                                &index));
                }
            }
            MapCanvas canvas;
            layer->setEditCanvas(&canvas);
            PropertiesPanel panel;
            panel.setProject(layer.get());
            QSet<SWMMObjectRef> refs;
            QStringList ids;
            for (int row = 0; row < layer->categoryCount(DC::CatJunctions); ++row) {
                auto id = layer->objectNameAt(DC::CatJunctions, row);
                ids << id;
                refs.insert({SWMMObjectRef::Node, id});
            }
            QVERIFY(ids.size() > (bellinge ? 100 : 1000));
            QElapsedTimer timer;
            timer.start();
            panel.showSelection(&canvas, refs.values());
            auto *tree = panel.findChild<QTreeView *>();
            QVERIFY(tree);
            auto cell = findProperty(tree->model(), "Tag");
            QVERIFY(cell.isValid());
            const auto originalValue = cell.data(Qt::EditRole);
            const auto selectionMs = timer.elapsed();
            QSignalSpy dirty(layer.get(), &DC::modelEdited);
            timer.restart();
            QVERIFY(tree->model()->setData(cell, QString("bulk-performance"), Qt::EditRole));
            const auto editMs = timer.elapsed();
            QCOMPARE(canvas.undoStack()->count(), 1);
            QCOMPARE(dirty.count(), 1);
            for (const auto &id : ids) {
                char tag[128]{};
                QCOMPARE(swmm_node_get_tag(layer->engine(),
                                           swmm_node_index(layer->engine(), id.toUtf8()), tag,
                                           sizeof(tag)),
                         SWMM_OK);
                QCOMPARE(QString::fromUtf8(tag), QString("bulk-performance"));
            }
            timer.restart();
            QCoreApplication::processEvents();
            const auto refreshMs = timer.elapsed();
            timer.restart();
            canvas.undoStack()->undo();
            const auto undoMs = timer.elapsed();
            QCoreApplication::processEvents();
            QCOMPARE(findProperty(tree->model(), "Tag").data(Qt::EditRole), originalValue);
            for (const auto &id : ids) {
                char tag[128]{};
                QCOMPARE(swmm_node_get_tag(layer->engine(),
                                           swmm_node_index(layer->engine(), id.toUtf8()), tag,
                                           sizeof(tag)),
                         SWMM_OK);
                QVERIFY(QString::fromUtf8(tag) != "bulk-performance");
            }
            timings << QJsonObject{{"network", bellinge ? "Bellinge 1D" : "Synthetic"},
                                   {"selected", ids.size()},
                                   {"selection_ms", selectionMs},
                                   {"edit_ms", editMs},
                                   {"refresh_ms", refreshMs},
                                   {"undo_ms", undoMs},
                                   {"undo_steps", 1}};
        }
        QFile evidence(QDir(folder()).filePath("performance.json"));
        QVERIFY(evidence.open(QIODevice::WriteOnly | QIODevice::Truncate));
        evidence.write(QJsonDocument(timings).toJson());
    }
    void featureSessionPropertiesAndBatch()
    {
        using namespace openswmmvis::feature;
        QDir().mkpath(folder());
        const auto gpkg = QDir(folder()).filePath("feature-bulk.gpkg");
        QFile::remove(gpkg);
        Schema schema;
        FieldDef amount;
        amount.name = "amount";
        amount.type = FieldType::Real;
        schema.append(amount);
        FieldDef label;
        label.name = "label";
        schema.append(label);
        QString error;
        auto *layer = FeatureLayer::create(gpkg, "Bulk", GeometryType::Point, ZPolicy{}, schema,
                                           QString(), FeatureLayerRole::General, &error, nullptr);
        QVERIFY2(layer, qPrintable(error));
        MapCanvas canvas;
        canvas.addLayer(layer, false);
        QVector<qint64> ids;
        for (int i = 0; i < 4; ++i) {
            Feature f;
            f.geometry = FeatureGeometry(GeometryType::Point);
            Part part;
            part.exterior.pts << QPointF(i, 0);
            f.geometry.parts() << part;
            f.attributes = {{"amount", double(i + 1)}, {"label", QString::number(i)}};
            auto id = layer->addFeature(f, &error);
            QVERIFY2(id != kInvalidFeatureId, qPrintable(error));
            ids << id;
        }
        QVERIFY(!applyFeatureAttributeValues(layer, ids.mid(0, 3), "amount", 8., &canvas, &error));
        layer->setEditing(true);
        QSignalSpy changed(layer, &FeatureLayer::featuresChanged);
        QVERIFY2(applyFeatureAttributeValues(layer, ids.mid(0, 3), "amount", 8., &canvas, &error),
                 qPrintable(error));
        QCOMPARE(canvas.undoStack()->count(), 1);
        QCOMPARE(changed.count(), 1);
        Feature f;
        QVERIFY(layer->feature(ids[3], f));
        QCOMPARE(f.attributes["amount"].toDouble(), 4.);
        canvas.undoStack()->undo();
        QVERIFY(layer->feature(ids[0], f));
        QCOMPARE(f.attributes["amount"].toDouble(), 1.);
        canvas.undoStack()->redo();
        QVERIFY(!applyFeatureAttributeValues(layer, ids + QVector<qint64>{999999}, "amount", 9.,
                                             &canvas, &error));
        QCOMPARE(canvas.undoStack()->count(), 1);
        PropertiesPanel panel;
        panel.showSelection(&canvas, {GisObjectRef::feature(layer->layerId(), ids[0]),
                                      GisObjectRef::feature(layer->layerId(), ids[1])});
        auto *tree = panel.findChild<QTreeView *>();
        QVERIFY(tree);
        auto value = findProperty(tree->model(), "amount");
        QVERIFY(value.isValid());
        QVERIFY(tree->model()->setData(value, 12., Qt::EditRole));
        QVERIFY(layer->feature(ids[0], f));
        QCOMPARE(f.attributes["amount"].toDouble(), 12.);
        layer->setEditing(false);
        QVERIFY(!(findProperty(tree->model(), "amount").flags() & Qt::ItemIsEditable));
        canvas.undoStack()->undo();
        QVERIFY(layer->feature(ids[0], f));
        QCOMPARE(f.attributes["amount"].toDouble(), 8.);
    }
};
QTEST_MAIN(TestRemainingDataBulk)
#include "test_remainingdatabulk.moc"
