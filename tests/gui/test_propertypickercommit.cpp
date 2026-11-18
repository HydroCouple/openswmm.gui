// A reference picked in the single-object Properties grid must reach the engine
// (and the saved model) as soon as it is picked, without the user first moving
// to another row. Workplan: PROPERTY_PICKER_COMMIT_PLAN_2026-11-18.

#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "selection/selectionmanager.h"
#include "ui/panels/propertiespanel.h"
#include "ui/properties/dataobjectpickereditor.h"

#include "map/tools/maptoolidentify.h"

#include <openswmm/engine/openswmm_gages.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_subcatchments.h>
#include <openswmm/engine/openswmm_model.h>

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QTest>
#include <QTreeView>

#include <memory>

namespace
{
QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

QString outputDir()
{
    return QDir(dataDir()).filePath("property_picker_commit");
}

// typed_selection_fixture.inp with a second time series, written where it can be reviewed.
std::unique_ptr<SWMMModelLayer> openLayer(const QString &fileName)
{
    QFile original(QDir(dataDir()).filePath("typed_selection_fixture.inp"));
    if (!original.open(QIODevice::ReadOnly) || !QDir().mkpath(outputDir()))
        return {};
    auto text = original.readAll();
    text.replace("\n[TIMESERIES]", "\n[TIMESERIES]\nTS2 0:00 1.0\nTS2 0:60 0.0");
    text.replace("\n[SUBCATCHMENTS]", "\nG2 INTENSITY 1:00 1 TIMESERIES TS1\n[SUBCATCHMENTS]");
    const QString path = QDir(outputDir()).filePath(fileName);
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};
    out.write(text);
    out.close();
    auto layer = std::make_unique<SWMMModelLayer>(path, nullptr);
    QList<QString> warnings, errors;
    if (!layer->loadModel(warnings, errors))
        return {};
    return layer;
}

QModelIndex valueCell(const QAbstractItemModel *model, const QString &label, const QModelIndex &parent = {})
{
    for (int r = 0; r < model->rowCount(parent); ++r) {
        const auto name = model->index(r, 0, parent);
        if (name.data().toString() == label)
            return model->index(r, 1, parent);
        if (const auto found = valueCell(model, label, name); found.isValid())
            return found;
    }
    return {};
}

QString gageSeries(SWMM_Engine engine, const char *gage)
{
    char buf[256] = {};
    swmm_gage_get_timeseries(engine, swmm_gage_index(engine, gage), buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

// Open the editor of the row labeled @p label and return its combo box; the row stays current.
QComboBox *openCombo(PropertiesPanel &properties, const QString &label)
{
    properties.resize(700, 1000);
    properties.show();
    auto *tree = properties.findChild<QTreeView *>();
    tree->expandAll();
    const auto field = valueCell(tree->model(), label);
    if (!field.isValid())
        return nullptr;
    tree->setCurrentIndex(field);
    if (!tree->indexWidget(field))
        tree->edit(field);
    auto *editor = tree->indexWidget(field);
    if (!editor)
        return nullptr;
    auto *combo = qobject_cast<QComboBox *>(editor);
    return combo ? combo : editor->findChild<QComboBox *>();
}

void showIdentified(PropertiesPanel &properties, SWMMModelLayer *layer, int objectType, const QString &name)
{
    IdentifyResult result;
    result.layerName = layer->name();
    result.features = {layer->identifyByName(name, SWMMModelLayer::kindMaskForObjectType(objectType))};
    properties.showIdentifyResults({result});
}
} // namespace

class TestPropertyPickerCommit : public QObject
{
    Q_OBJECT
private slots:
    void rainGageSeriesPickReachesEngineAndSavedModel_data()
    {
        QTest::addColumn<bool>("undoable");
        QTest::newRow("with-edit-canvas") << true;
        QTest::newRow("without-edit-canvas") << false;
    }

    void rainGageSeriesPickReachesEngineAndSavedModel()
    {
        QFETCH(bool, undoable);
        const QString tag = QString::fromLatin1(QTest::currentDataTag());
        auto layer = openLayer("gage-series-" + tag + "-before.inp");
        QVERIFY(layer);
        const auto engine = layer->engine();
        QCOMPARE(gageSeries(engine, "S1"), QStringLiteral("TS1"));

        MapCanvas canvas;
        if (undoable)
            layer->setEditCanvas(&canvas);
        PropertiesPanel properties;
        properties.setProject(layer.get());
        properties.showDataObject(layer.get(), int(SWMMObjectRef::RainGage), "S1");
        properties.resize(700, 1000);
        properties.show();
        auto *tree = properties.findChild<QTreeView *>();
        QVERIFY(tree);
        tree->expandAll();
        const auto field = valueCell(tree->model(), "Series Name");
        QVERIFY(field.isValid());
        tree->setCurrentIndex(field);
        tree->edit(field);
        auto *editor = tree->indexWidget(field);
        QVERIFY(editor);
        auto *picker = qobject_cast<DataObjectPickerEditor *>(editor);
        if (!picker)
            picker = editor->findChild<DataObjectPickerEditor *>();
        QVERIFY(picker);
        auto *combo = picker->findChild<QComboBox *>();
        QVERIFY(combo);
        const int choice = combo->findText("TS2");
        QVERIFY(choice >= 0);
        combo->setCurrentIndex(choice); // the user picks; the row is not left

        QTRY_COMPARE(gageSeries(engine, "S1"), QStringLiteral("TS2"));
        if (undoable)
            QCOMPARE(canvas.undoStack()->count(), 1);

        const QString saved = QDir(outputDir()).filePath("gage-series-" + tag + "-saved.inp");
        QCOMPARE(swmm_model_write(engine, saved.toUtf8().constData()), SWMM_OK);
        QFile file(saved);
        QVERIFY(file.open(QIODevice::ReadOnly));
        bool found = false;
        for (const auto &line : file.readAll().split('\n'))
            if (line.startsWith("S1 ") && line.contains("TIMESERIES"))
                found = line.simplified().endsWith("TIMESERIES TS2");
        QVERIFY2(found, "saved [RAINGAGES] row for S1 does not name TS2");
    }

    void refusedRainGageSeriesPickIsExplainedAndReverted()
    {
        auto layer = openLayer("gage-series-refused-before.inp");
        QVERIFY(layer);
        const auto engine = layer->engine();
        MapCanvas canvas;
        layer->setEditCanvas(&canvas);
        PropertiesPanel properties;
        properties.setProject(layer.get());
        properties.showDataObject(layer.get(), int(SWMMObjectRef::RainGage), "S1");
        auto *combo = openCombo(properties, "Series Name");
        QVERIFY(combo);
        auto *picker = properties.findChild<DataObjectPickerEditor *>();
        QVERIFY(picker);
        picker->applyPickedName("NoSuchSeries"); // a name the checked write path refuses
        QCOMPARE(gageSeries(engine, "S1"), QStringLiteral("TS1"));
        QCOMPARE(canvas.undoStack()->count(), 0);
        auto *note = properties.findChild<QLabel *>("meshAggregateNote");
        QVERIFY(note);
        QVERIFY(note->isVisible());
        QVERIFY(!note->text().isEmpty());
        QCOMPARE(combo->currentText(), QStringLiteral("TS1"));
    }

    void rainGageIntervalPickReachesEngine()
    {
        auto layer = openLayer("gage-interval-before.inp");
        QVERIFY(layer);
        const auto engine = layer->engine();
        MapCanvas canvas;
        layer->setEditCanvas(&canvas);
        PropertiesPanel properties;
        properties.setProject(layer.get());
        properties.showDataObject(layer.get(), int(SWMMObjectRef::RainGage), "S1");
        auto *combo = openCombo(properties, "Recording Interval (s)");
        QVERIFY(combo);
        const int choice = combo->findText("0:15");
        QVERIFY(choice >= 0);
        combo->setCurrentIndex(choice);
        emit combo->activated(choice);
        double seconds = 0;
        QTRY_VERIFY(swmm_gage_get_rain_interval(engine, swmm_gage_index(engine, "S1"), &seconds) == SWMM_OK &&
                    seconds == 900.0);
    }

    void subcatchmentRainGagePickReachesEngine()
    {
        auto layer = openLayer("subcatch-gage-before.inp");
        QVERIFY(layer);
        const auto engine = layer->engine();
        MapCanvas canvas;
        layer->setEditCanvas(&canvas);
        PropertiesPanel properties;
        properties.setProject(layer.get());
        showIdentified(properties, layer.get(), int(SWMMObjectRef::Subcatchment), "S1");
        auto *combo = openCombo(properties, "Rain Gage");
        QVERIFY(combo);
        const int choice = combo->findText("G2");
        QVERIFY(choice >= 0);
        combo->setCurrentIndex(choice);
        int gage = -1;
        QTRY_VERIFY(swmm_subcatch_get_gage(engine, swmm_subcatch_index(engine, "S1"), &gage) == SWMM_OK &&
                    gage == swmm_gage_index(engine, "G2"));
    }

    void conduitCulvertCodePickReachesEngine()
    {
        auto layer = openLayer("culvert-code-before.inp");
        QVERIFY(layer);
        const auto engine = layer->engine();
        MapCanvas canvas;
        layer->setEditCanvas(&canvas);
        PropertiesPanel properties;
        properties.setProject(layer.get());
        showIdentified(properties, layer.get(), int(SWMMObjectRef::Link), "C1");
        auto *combo = openCombo(properties, "Culvert Code");
        QVERIFY(combo);
        QVERIFY(combo->count() > 2);
        const int before = combo->currentIndex();
        const int choice = before == 2 ? 1 : 2;
        int codeBefore = -1, code = -1;
        QCOMPARE(swmm_link_get_culvert_code(engine, swmm_link_index(engine, "C1"), &codeBefore), SWMM_OK);
        combo->setCurrentIndex(choice);
        emit combo->activated(choice);
        QTRY_VERIFY(swmm_link_get_culvert_code(engine, swmm_link_index(engine, "C1"), &code) == SWMM_OK &&
                    code != codeBefore);
    }
};

QTEST_MAIN(TestPropertyPickerCommit)
#include "test_propertypickercommit.moc"
