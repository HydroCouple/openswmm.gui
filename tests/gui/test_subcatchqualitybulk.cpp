#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "selection/selectionmanager.h"
#include "ui/dialogs/subcatchcompoundeditdialog.h"
#include "ui/panels/attributetablepanel.h"
#include "ui/panels/propertiespanel.h"
#include "ui/panels/subcatchqualitytablemodel.h"
#include "ui/panels/swmmaggregateselection.h"
#include "ui/properties/subcatchcompoundeditbutton.h"
#include "ui/properties/swmmsubcatchpropertyadapter.h"
#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_subcatchments.h>
#include <openswmm/engine/openswmm_quality.h>
#include <openswmm/engine/openswmm_pollutants.h>
#include <openswmm/engine/openswmm_edit.h>
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
#include <QTextStream>
#include <QTimer>
#include <QTreeView>
#include <cmath>
#include <memory>

namespace
{
using Kind = SubcatchCompoundEditRef::Kind;
QString auditDir()
{
    return QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA"))
        .absoluteFilePath("../../../workplans/bulk-subcatch-quality");
}
bool coverage(Kind kind)
{
    return kind == SubcatchCompoundEditRef::LandUse;
}
std::unique_ptr<SWMMModelLayer> fixture(bool si = false)
{
    QFile base(QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA"))
                   .filePath("typed_selection_fixture.inp"));
    if (!base.open(QIODevice::ReadOnly))
        return {};
    auto text = base.readAll();
    if (si)
        text.replace("FLOW_UNITS           CFS", "FLOW_UNITS           CMS");
    text +=
        "\n[SUBCATCHMENTS]\nS2 S1 J1 4 40 400 1 0\nS3 S1 J1 3 30 300 1 0\nControl S1 J1 2 20 200 1 0\n"
        "[SUBAREAS]\nS2 .02 .2 .1 .2 25 OUTLET\nS3 .02 .2 .1 .2 25 OUTLET\nControl .02 .2 .1 .2 25 OUTLET\n"
        "[INFILTRATION]\nS2 4 .5 4 7 0 HORTON\nS3 4 .5 4 7 0 HORTON\nControl 4 .5 4 7 0 HORTON\n"
        "[POLLUTANTS]\nUnused MG/L 0 0 0 0 NO * 0 0 0\nPA MG/L 0 0 0 0 NO * 0 0 0\nPB UG/L 0 0 0 0 NO * 0 0 0\n"
        "[LANDUSES]\nUnused 7 .5 0\nRes 7 .5 0\nCom 14 .3 0\n"
        "[COVERAGES]\nS1 Res 25.1234567890123\nS1 Com 60\nS2 Res 10\nS2 Com 30\nS3 Com 5\nControl Res 33\nControl Com 44\n"
        "[LOADINGS]\nS1 PA 1.23456789012345\nS1 PB 9.125\nS2 PA 2.345\nS2 PB 8.25\nS3 PB 7.75\nControl PA 17\nControl PB 6.5\n"
        "[POLYGONS]\nS2 -1500 1600\nS2 -1000 1600\nS2 -1000 2100\nS2 -1500 2100\n"
        "S3 -800 1600\nS3 -300 1600\nS3 -300 2100\nS3 -800 2100\n"
        "Control -100 1600\nControl 400 1600\nControl 400 2100\nControl -100 2100\n";
    QDir().mkpath(auditDir() + "/review");
    const auto path = QDir(auditDir())
                          .filePath(si ? "review/subcatch-quality-SI.inp"
                                       : "review/Subcatch_Quality_Properties_Review.inp");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};
    file.write(text);
    file.close();
    auto layer = std::make_unique<SWMMModelLayer>(path);
    QList<QString> warnings, errors;
    if (!layer->loadModel(warnings, errors)) {
        qWarning().noquote() << errors.join(' ');
        return {};
    }
    return layer;
}
SubcatchCompoundEditRef refFor(SWMMModelLayer *layer, Kind kind, QUndoStack *undo)
{
    SubcatchCompoundEditRef ref;
    ref.layer = layer;
    ref.engine = layer->engine();
    ref.kind = kind;
    ref.subName = "S1";
    ref.subNames = {"S1", "S2", "S3"};
    ref.undoStack = undo;
    return ref;
}
QVariantMap state(SWMM_Engine e, Kind kind, const QString &name)
{
    QVariantMap result;
    const int sub = swmm_subcatch_index(e, name.toUtf8().constData());
    const int count = coverage(kind) ? swmm_landuse_count(e) : swmm_pollutant_count(e);
    for (int i = 0; i < count; ++i) {
        double value = 0;
        const auto id = coverage(kind) ? swmm_landuse_id(e, i) : swmm_pollutant_id(e, i);
        const int status = coverage(kind) ? swmm_subcatch_get_coverage(e, sub, i, &value)
                                          : swmm_subcatch_get_initial_loading(e, sub, i, &value);
        if (status != SWMM_OK || !id)
            return {};
        result[QString::fromUtf8(id)] = value;
    }
    return result;
}
QModelIndex cell(SubcatchQualityTableModel *model, const QString &key)
{
    for (int row = 0; row < model->rowCount(); ++row)
        if (model->index(row, 0).data().toString() == key)
            return model->index(row, 1);
    return {};
}
QModelIndex qualityProperty(QAbstractItemModel *model, const QString &label,
                            const QModelIndex &parent = {})
{
    for (int row = 0; row < model->rowCount(parent); ++row) {
        const auto index = model->index(row, 0, parent);
        if (index.data().toString() == label)
            return index.siblingAtColumn(1);
        const auto child = qualityProperty(model, label, index);
        if (child.isValid())
            return child;
    }
    return {};
}
void writeAudit(SWMMModelLayer *layer)
{
    QFile file(QDir(auditDir()).filePath("SPATIAL_FIELDS.md"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    QTextStream out(&file);
    out << "# Runtime spatial field audit\n\nSchema snapshot from the US quality review fixture. Dynamic columns depend on the loaded definitions/options. Eligibility does not replace field-specific acceptance testing. Properties-only gaps and remaining group semantics are in SPATIAL_GAPS.md.\n\n";
    const QStringList categories{"Junctions", "Outfalls",      "Storage",   "Dividers",
                                 "Conduits",  "Pumps",         "Orifices",  "Weirs",
                                 "Outlets",   "Subcatchments", "Rain gages"};
    QMap<QString, int> counts;
    out << "| Category | Key | Label | Writer | Editor | Disposition | Reason |\n|---|---|---|---|---|---|---|\n";
    for (int c = 0; c < SWMMModelLayer::NumCategories; ++c) {
        SWMMAttributeTableModel model;
        model.setSource(layer, static_cast<SWMMModelLayer::Category>(c));
        for (const auto &spec : model.columnSpecs()) {
            QString disposition, reason;
            if (spec.key == "Name") {
                disposition = "Individual identity";
                reason = "Unique name; no shared-value assignment";
            } else if (spec.editor == openswmmvis::EditorKind::ReadOnly || spec.setter.isEmpty()) {
                disposition = "Read-only";
                reason = "Computed/source-owned or per-object identity";
            } else if (spec.setter == "node_groundwater_sources_ref") {
                disposition = "Owner navigation";
                reason = "Records belong to receiving subcatchments";
            } else if (SWMMAggregateSelection::supports(spec)) {
                const bool keyed = QStringList{"node_inflows_ref",     "node_dwf_ref",
                                               "node_rdii_ref",        "node_treatment_ref",
                                               "subcatch_landuse_ref", "subcatch_loadings_ref",
                                               "subcatch_groundwater_ref"}
                                       .contains(spec.setter);
                disposition = keyed ? "Keyed structured bulk" : "Inline bulk eligible";
                reason =
                    keyed
                        ? "Shared checked row editor"
                        : "Subtype/state/shape applicability still applies; verification is field-specific";
            } else {
                disposition = "Pending";
                reason = spec.setter.startsWith("node_2d_")
                             ? "Complete structure/inheritance record"
                         : spec.setter == "heat_property" || spec.setter == "initquality"
                             ? "Unset/inherited override semantics"
                             : "Needs typed choice, definition or structured-record semantics";
            }
            ++counts[disposition];
            auto clean = [](QString text) { return text.replace('|', "\\|").replace('\n', " "); };
            out << "| " << categories.value(c) << " | " << clean(spec.key) << " | "
                << clean(spec.label) << " | " << clean(spec.setter) << " | " << int(spec.editor)
                << " | " << disposition << " | " << reason << " |\n";
        }
    }
    out << "\nCounts are category-specific schema columns, including repeated common fields.\n\n";
    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
        out << "- " << it.key() << ": " << it.value() << '\n';
}
}

class TestSubcatchQualityBulk : public QObject
{
    Q_OBJECT
private slots:
    void propertiesCommitUndoAndPersistence_data()
    {
        QTest::addColumn<bool>("loading");
        QTest::addColumn<bool>("si");
        QTest::newRow("coverage-US") << false << false;
        QTest::newRow("coverage-SI") << false << true;
        QTest::newRow("loading-US") << true << false;
        QTest::newRow("loading-SI") << true << true;
    }
    void propertiesCommitUndoAndPersistence()
    {
        QFETCH(bool, loading);
        QFETCH(bool, si);
        const auto kind =
            loading ? SubcatchCompoundEditRef::Loadings : SubcatchCompoundEditRef::LandUse;
        auto layer = fixture(si);
        QVERIFY(layer);
        const auto e = layer->engine();
        char flowUnits[32]{};
        QCOMPARE(swmm_options_get(e, "FLOW_UNITS", flowUnits, sizeof(flowUnits)), SWMM_OK);
        QCOMPARE(QString::fromUtf8(flowUnits), si ? QString("CMS") : QString("CFS"));
        const QString key = loading ? "PA" : "Res",
                      label = loading ? "Initial Loadings" : "Land Use Coverage";
        QList<QVariantMap> before;
        for (auto name : {"S1", "S2", "S3"})
            before << state(e, kind, name);
        const auto control = state(e, kind, "Control");
        MapCanvas canvas;
        PropertiesPanel properties;
        properties.setProject(layer.get());
        properties.showSelection(&canvas, {{SWMMObjectRef::Subcatchment, "S1"},
                                           {SWMMObjectRef::Subcatchment, "S2"},
                                           {SWMMObjectRef::Subcatchment, "S3"}});
        properties.resize(700, 1000);
        properties.show();
        auto *tree = properties.findChild<QTreeView *>();
        tree->expandAll();
        const auto field = qualityProperty(tree->model(), label);
        QVERIFY(field.isValid());
        tree->edit(field);
        auto *button = qobject_cast<SubcatchCompoundEditButton *>(tree->indexWidget(field));
        QVERIFY(button);
        QCOMPARE(button->value().subNames.size(), 3);
        QSignalSpy dirty(layer.get(), &SWMMModelLayer::modelEdited);
        QTest::mouseClick(button->findChild<QPushButton *>(), Qt::LeftButton);
        auto *dialog = properties.findChild<SubcatchCompoundEditDialog *>();
        QVERIFY(dialog);
        QCOMPARE(dialog->findChild<QDialogButtonBox *>()->standardButtons(),
                 QDialogButtonBox::StandardButtons(QDialogButtonBox::Close));
        auto *table = dialog->findChild<QTableView *>("subcatchQualityTable");
        QVERIFY(table);
        auto *model = dynamic_cast<SubcatchQualityTableModel *>(table->model());
        QVERIFY(model);
        properties.showSelection(&canvas, {{SWMMObjectRef::Subcatchment, "Control"}});
        const auto target = cell(model, key);
        QVERIFY(target.isValid());
        QCOMPARE(target.data().toString(), QString("Mixed"));
        table->edit(target);
        auto *line = table->findChild<QLineEdit *>();
        QVERIFY(line);
        line->setFocus();
        QTest::keyClick(line, Qt::Key_Escape);
        QCOMPARE(canvas.undoStack()->count(), 0);
        QCOMPARE(dirty.count(), 0);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        table->edit(target);
        line = table->findChild<QLineEdit *>();
        QVERIFY(line);
        line->setFocus();
        QTest::keyClicks(line, "50");
        QTest::keyClick(line, si ? Qt::Key_Tab : Qt::Key_Return);
        QTRY_COMPARE(canvas.undoStack()->count(), 1);
        QList<QVariantMap> after;
        int n = 0;
        for (auto name : {"S1", "S2", "S3"}) {
            auto expected = before[n++];
            expected[key] = 50.0;
            after << state(e, kind, name);
            QCOMPARE(after.last(), expected);
        }
        QCOMPARE(state(e, kind, "Control"), control);
        if (!loading)
            QVERIFY(dialog->findChild<QLabel *>("subcatchQualityFeedback")->text().contains("S1"));
        dialog->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        canvas.undoStack()->undo();
        n = 0;
        for (auto name : {"S1", "S2", "S3"})
            QCOMPARE(state(e, kind, name), before[n++]);
        canvas.undoStack()->redo();
        const auto file =
            QDir(auditDir())
                .filePath(QString("edited-%1-%2.inp")
                              .arg(loading ? "loading" : "coverage", si ? "SI" : "US"));
        QCOMPARE(swmm_model_write(e, file.toUtf8().constData()), SWMM_OK);
        SWMMModelLayer reopened(file);
        QList<QString> warnings, errors;
        QVERIFY2(reopened.loadModel(warnings, errors), qPrintable(errors.join(' ')));
        n = 0;
        for (auto name : {"S1", "S2", "S3"})
            QCOMPARE(state(reopened.engine(), kind, name), after[n++]);
        QCOMPARE(state(reopened.engine(), kind, "Control"), control);
        if (!loading && !si)
            writeAudit(layer.get());
    }
    void validationZeroAndNativeReplay_data()
    {
        QTest::addColumn<bool>("loading");
        QTest::newRow("coverage") << false;
        QTest::newRow("loading") << true;
    }
    void validationZeroAndNativeReplay()
    {
        QFETCH(bool, loading);
        const auto kind =
            loading ? SubcatchCompoundEditRef::Loadings : SubcatchCompoundEditRef::LandUse;
        auto layer = fixture();
        QVERIFY(layer);
        const auto e = layer->engine();
        QUndoStack undo;
        auto ref = refFor(layer.get(), kind, &undo);
        SubcatchQualityTableModel model(ref);
        const QString key = loading ? "PA" : "Res";
        const auto original = state(e, kind, "S1");
        for (auto value : {QVariant("nan"), QVariant("inf"), QVariant(-1.0)}) {
            QVERIFY(!model.setData(cell(&model, key), value));
            QCOMPARE(undo.count(), 0);
            QCOMPARE(state(e, kind, "S1"), original);
        }
        if (!loading)
            QVERIFY(!model.setData(cell(&model, key), 101.0));
        QVERIFY(model.setData(cell(&model, key), 0.0));
        QCOMPARE(undo.count(), 1);
        QVERIFY(model.setData(cell(&model, key), 0.0));
        QCOMPARE(undo.count(), 1);
        undo.undo();
        QCOMPARE(state(e, kind, "S1"), original);
        undo.clear();
        ref.subNames = {"S1", "S2"};
        const int item = loading ? swmm_pollutant_index(e, "PA") : swmm_landuse_index(e, "Res");
        const double authored = loading ? -2.0 : 150.0;
        QCOMPARE(
            loading
                ? swmm_subcatch_set_initial_loading(e, swmm_subcatch_index(e, "S1"), item, authored)
                : swmm_subcatch_set_coverage(e, swmm_subcatch_index(e, "S1"), item, authored),
            SWMM_OK);
        SubcatchQualityTableModel legacy(ref);
        QVERIFY(legacy.setData(cell(&legacy, key), 20.0));
        undo.undo();
        QCOMPARE(state(e, kind, "S1").value(key).toDouble(), authored);
        undo.clear();
        QCOMPARE(loading
                     ? swmm_subcatch_set_initial_loading(e, swmm_subcatch_index(e, "S2"), item, NAN)
                     : swmm_subcatch_set_coverage(e, swmm_subcatch_index(e, "S2"), item, NAN),
                 SWMM_OK);
        QVERIFY(!legacy.setData(cell(&legacy, key), 25.0));
        QCOMPARE(undo.count(), 0);
        QCOMPARE(state(e, kind, "S1").value(key).toDouble(), authored);
    }
    void definitionReindexAndReuse_data() { validationZeroAndNativeReplay_data(); }
    void definitionReindexAndReuse();
    void targetAndModelLifetime();
    void tableSelectionAndNavigation_data() { validationZeroAndNativeReplay_data(); }
    void tableSelectionAndNavigation();
};

void TestSubcatchQualityBulk::definitionReindexAndReuse()
{
    QFETCH(bool, loading);
    const auto kind =
        loading ? SubcatchCompoundEditRef::Loadings : SubcatchCompoundEditRef::LandUse;
    auto layer = fixture();
    QVERIFY(layer);
    const auto e = layer->engine();
    QUndoStack undo;
    auto ref = refFor(layer.get(), kind, &undo);
    ref.subNames = {"S1", "S2"};
    SubcatchQualityTableModel model(ref);
    const QString key = loading ? "PA" : "Res";
    auto before = state(e, kind, "S1");
    before.remove("Unused");
    QVERIFY(model.setData(cell(&model, key), 8.125));
    QCOMPARE(undo.count(), 1);
    QCOMPARE(loading ? swmm_pollutant_delete(e, swmm_pollutant_index(e, "Unused"), nullptr)
                     : swmm_landuse_delete(e, swmm_landuse_index(e, "Unused"), nullptr),
             SWMM_OK);
    emit layer->dataObjectsChanged();
    undo.undo();
    QCOMPARE(state(e, kind, "S1"), before);
    undo.redo();
    QCOMPARE(
        loading
            ? swmm_pollutant_delete(e, swmm_pollutant_index(e, key.toUtf8().constData()), nullptr)
            : swmm_landuse_delete(e, swmm_landuse_index(e, key.toUtf8().constData()), nullptr),
        SWMM_OK);
    emit layer->dataObjectsChanged();
    QCOMPARE(loading ? swmm_pollutant_add(e, key.toUtf8().constData(), 0)
                     : swmm_landuse_add(e, key.toUtf8().constData()),
             SWMM_OK);
    emit layer->dataObjectsChanged();
    const auto fresh = state(e, kind, "S1");
    undo.undo();
    QCOMPARE(state(e, kind, "S1"), fresh);
    QCOMPARE(state(e, kind, "S1").value(key).toDouble(), 0.0);
}
void TestSubcatchQualityBulk::targetAndModelLifetime()
{
    auto layer = fixture();
    QVERIFY(layer);
    const auto e = layer->engine();
    QUndoStack undo;
    auto ref = refFor(layer.get(), SubcatchCompoundEditRef::LandUse, &undo);
    ref.subNames = {"S1", "s1", "S2"};
    SubcatchQualityTableModel model(ref);
    QCOMPARE(model.targetNames().size(), 2);
    const auto before = state(e, ref.kind, "S1"), control = state(e, ref.kind, "Control");
    QVERIFY(model.setData(cell(&model, "Res"), 30.0));
    QVERIFY(layer->applyRename("S1", "Renamed", SWMMModelLayer::kKindCatch));
    undo.undo();
    QCOMPARE(state(e, ref.kind, "Renamed"), before);
    undo.redo();
    const auto edited = state(e, ref.kind, "Renamed");
    QVERIFY(layer->applySubcatchDelete("S2"));
    QVERIFY(layer->applySubcatchAdd("S2", {{0, 0}, {10, 0}, {10, 10}, {0, 0}}));
    undo.undo();
    QCOMPARE(state(e, ref.kind, "Renamed"), edited);
    QCOMPARE(state(e, ref.kind, "S2").value("Res").toDouble(), 0.0);
    QVERIFY(!model.setData(cell(&model, "Res"), 40.0));
    QCOMPARE(state(e, ref.kind, "Control"), control);
    undo.clear();
    ref.subName = "Renamed";
    ref.subNames = {"Renamed"};
    SubcatchCompoundEditDialog dialog(ref);
    dialog.show();
    auto *table = dialog.findChild<QTableView *>("subcatchQualityTable");
    QVERIFY(table);
    auto *active = dynamic_cast<SubcatchQualityTableModel *>(table->model());
    QVERIFY(active);
    table->edit(cell(active, "Res"));
    QPointer<QLineEdit> line = table->findChild<QLineEdit *>();
    QVERIFY(line);
    line->setText("12");
    line->setModified(true);
    layer.reset();
    if (line)
        QMetaObject::invokeMethod(line, "editingFinished");
}
void TestSubcatchQualityBulk::tableSelectionAndNavigation()
{
    QFETCH(bool, loading);
    const auto kind =
        loading ? SubcatchCompoundEditRef::Loadings : SubcatchCompoundEditRef::LandUse;
    const QString key = loading ? "PA" : "Res",
                  label = loading ? "Initial Loadings" : "Land Use Coverage";
    auto layer = fixture();
    QVERIFY(layer);
    const auto e = layer->engine();
    const auto before = state(e, kind, "S1"), other = state(e, kind, "S2"),
               control = state(e, kind, "Control");
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
                if (view->model()->headerData(col, Qt::Horizontal).toString() == label)
                    clicked = view->model()->index(row, col);
    QVERIFY(clicked.isValid());
    QSignalSpy dirty(layer.get(), &SWMMModelLayer::modelEdited);
    QVERIFY(view->model()->setData(clicked, clicked.data(Qt::EditRole), Qt::EditRole));
    QCOMPARE(dirty.count(), 0);
    SWMMSubcatchPropertyAdapter facade(e, "S1");
    facade.setModelLayer(layer.get());
    QSignalSpy changed(&facade, &SWMMSubcatchPropertyAdapter::changed);
    if (loading)
        facade.setLoadingsRef(facade.loadingsRef());
    else
        facade.setLandUseRef(facade.landUseRef());
    QCOMPARE(changed.count(), 0);
    view->scrollTo(clicked);
    QCoreApplication::processEvents();
    bool opened = false;
    QTimer::singleShot(0, &panel, [&] {
        auto *menu = panel.findChild<QMenu *>(QString(), Qt::FindDirectChildrenOnly);
        if (!menu)
            return;
        for (auto *action : menu->actions())
            if (action->text().startsWith("Edit " + label + " for 2")) {
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
    auto *dialog = panel.findChild<SubcatchCompoundEditDialog *>();
    QVERIFY(dialog);
    auto *table = dialog->findChild<QTableView *>("subcatchQualityTable");
    QVERIFY(table);
    auto *model = dynamic_cast<SubcatchQualityTableModel *>(table->model());
    QVERIFY(model);
    table->edit(cell(model, key));
    auto *line = table->findChild<QLineEdit *>();
    QVERIFY(line);
    line->setFocus();
    line->selectAll();
    QTest::keyClicks(line, "12.125");
    QTest::keyClick(line, Qt::Key_Tab);
    QTRY_COMPARE(canvas.undoStack()->count(), 1);
    auto expected = before;
    expected[key] = 12.125;
    QCOMPARE(state(e, kind, "S1"), expected);
    expected = other;
    expected[key] = 12.125;
    QCOMPARE(state(e, kind, "S2"), expected);
    QCOMPARE(state(e, kind, "Control"), control);
    dialog->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    canvas.undoStack()->undo();
    QCOMPARE(state(e, kind, "S1"), before);
    QCOMPARE(state(e, kind, "S2"), other);
}

QTEST_MAIN(TestSubcatchQualityBulk)
#include "test_subcatchqualitybulk.moc"
