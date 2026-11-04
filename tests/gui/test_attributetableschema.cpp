/**
 * @file      test_attributetableschema.cpp
 * @brief     Attribute Table column-schema contract.
 *
 * Guards three properties the schema has to keep:
 *
 *   1. The conduit cross-section block (XSection compound cell, inline
 *      Geom 1-4, Barrels) is present, carries the right delegates, and
 *      stays EDITABLE for the tested circular/irregular shape cases. It used to blank + grey each geom
 *      the live shape didn't consume, so a CIRCULAR conduit showed three
 *      permanently empty uneditable columns — which reads as broken
 *      editors, and threw away a width typed before a shape switch.
 *      Only the picker-owned index slots (IRREGULAR / STREET geom1,
 *      CUSTOM geom2) stay locked.
 *
 *   2. Post-run "dynamics" statistics form a contiguous suffix of every
 *      category's schema — never interleaved with the editable inputs.
 *
 *   3. The per-category attribute coverage that the engine C API exposes
 *      is actually surfaced (conduit Slope, subcatchment Tag, storage
 *      exfiltration triple, outfall RouteTo).
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "layers/swmmmodellayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "map/swmmlayeritem.h"
#include "map/swmmlayerqsgrenderer.h"
#include "mesh/meshobjectref.h"
#include "mesh/meshresult.h"
#include "plugins/extensionattributes.h"
#include "selection/selectionmanager.h"
#include "ui/panels/attributetablepanel.h"
#include "ui/panels/dataobjectattributetablemodel.h"
#include "ui/panels/meshattributetablemodel.h"
#include "ui/panels/attributedelegates.h"
#include "ui/panels/propertiespanel.h"
#include "ui/panels/swmmaggregateselection.h"
#include "ui/panels/swmmattributetablemodel.h"
#include "ui/properties/swmmlinkpropertyadapter.h"
#include "ui/properties/linkendpointcombobox.h"
#include "ui/properties/attributegroups.h"
#ifdef HAVE_QPROPERTYMODEL
#include "ui/properties/groupedpropertymodel.h"
#include "ui/properties/swmmnodepropertyadapter.h"
#include <qpropertymodel.h>
#include <QAbstractItemModelTester>
#endif
#include "ui/properties/xsectshapegeom.h"
#include "ui/properties/storageshapegeom.h"
#include "ui/toolbars/mesheditingtoolbar.h"
#include "street/streetprovider.h"
#include "street/streetregistry.h"

#include "ui/properties/nodecompoundeditref.h"
#include "ui/properties/nodecompoundeditbutton.h"
#include "ui/panels/nodecompoundtablemodel.h"
#include "ui/dialogs/nodecompoundeditdialog.h"
#include "ui/widgets/treatmentexpressionedit.h"
#include <openswmm/engine/openswmm_quality.h>
#include "ui/properties/swmmsubcatchpropertyadapter.h"
#include "ui/properties/dataobjectref.h"
#include "ui/properties/dataobjectpickereditor.h"
#include <openswmm/engine/openswmm_tables.h>
#include "ui/dialogs/snowpackeditordialog.h"
#include "snowpack/snowpackregistry.h"
#include <QTimer>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QStyleFactory>
#include <QStyle>
#include <QPushButton>

#include <openswmm/engine/openswmm_gages.h>
#include <openswmm/engine/openswmm_inflows.h>
#include <openswmm/engine/openswmm_infrastructure.h>
#include <openswmm/engine/openswmm_initial_quality.h> // Initial-quality UI round
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_edit.h>
#include <openswmm/engine/openswmm_xsect.h>
#include <openswmm/engine/openswmm_model.h> // swmm_options_set
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_pollutants.h> // Initial-quality UI round
#include <openswmm/engine/openswmm_subcatchments.h>

#include <algorithm> // std::reverse — ordering-override pin

#include <QApplication>
#include <QComboBox>
#include <QCompleter>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTreeView>
#include <QUndoStack>
#include <QDir>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QLocale>
#include <QObject>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QTest>

#include <memory>

using openswmmvis::ColumnSpec;
using openswmmvis::EditorKind;

namespace
{

/**
 *
 * @brief Locate the test fixture/evidence base without creating it.
 * @return SWMMVIS_GUI_TEST_DATA verbatim when set, otherwise the relative dot path;
 * caller resolves filenames and creates any required evidence directories.
 *
 */
QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

/**
 *
 * @brief Load typed_selection_fixture.inp into a test-owned editing model layer.
 * @return Owning unique_ptr on successful load, null on refusal; local warnings/errors
 * are collected but not surfaced. Layer destruction releases its engine; does not copy
 * or write the fixture. Callers retain it while borrowed model/adapter refs are used.
 *
 */
std::unique_ptr<SWMMModelLayer> openLayer()
{
    auto layer = std::make_unique<SWMMModelLayer>(
        QDir(dataDir()).filePath(QStringLiteral("typed_selection_fixture.inp")), nullptr);
    QList<QString> warnings, errors;
    if (!layer->loadModel(warnings, errors))
        return nullptr;
    return layer;
}

/**
 *
 * @brief Find the first exact schema-key match.
 * @param specs Borrowed ordered column specifications.
 * @param key Borrowed exact case-sensitive key, without trimming or label/header aliasing.
 * @return Zero-based column or minus one; does not mutate/rebuild the schema.
 *
 */
int colFor(const QList<ColumnSpec> &specs, const QString &key)
{
    for (int i = 0; i < specs.size(); ++i)
        if (specs[i].key == key)
            return i;
    return -1;
}

/**
 *
 * @brief Read the native entity id for a category family and engine index.
 * @param eng Borrowed valid editing engine for supported categories.
 * @param cat SWMM spatial category, mapped to node/link/subcatchment/gage id getter.
 * @param idx Native engine index, not a displayed/category row; getter handles bounds.
 * @return Borrowed engine-owned C string or null for unsupported/missing entity.
 * Caller retains engine lifetime and copies before invalidating/rebuilding its storage.
 *
 */
const char *engineIdFor(SWMM_Engine eng, SWMMModelLayer::Category cat, int idx)
{
    switch (cat) {
    case SWMMModelLayer::CatJunctions:
    case SWMMModelLayer::CatOutfalls:
    case SWMMModelLayer::CatStorage:
    case SWMMModelLayer::CatDividers:
        return swmm_node_id(eng, idx);
    case SWMMModelLayer::CatConduits:
    case SWMMModelLayer::CatPumps:
    case SWMMModelLayer::CatOrifices:
    case SWMMModelLayer::CatWeirs:
    case SWMMModelLayer::CatOutlets:
        return swmm_link_id(eng, idx);
    case SWMMModelLayer::CatSubcatchments:
        return swmm_subcatch_id(eng, idx);
    case SWMMModelLayer::CatRainGages:
        return swmm_gage_id(eng, idx);
    default:
        return nullptr;
    }
}

/**
 *
 * @brief Enumerate the eleven spatial categories exercised by the schema invariants.
 * @return Owned list of four node, five link, subcatchment and rain-gage categories;
 * nonspatial data categories are outside this helper's coverage.
 *
 */
QList<SWMMModelLayer::Category> allCategories()
{
    return {
        SWMMModelLayer::CatJunctions,     SWMMModelLayer::CatOutfalls,  SWMMModelLayer::CatStorage,
        SWMMModelLayer::CatDividers,      SWMMModelLayer::CatConduits,  SWMMModelLayer::CatPumps,
        SWMMModelLayer::CatOrifices,      SWMMModelLayer::CatWeirs,     SWMMModelLayer::CatOutlets,
        SWMMModelLayer::CatSubcatchments, SWMMModelLayer::CatRainGages,
    };
}

/**
 *
 * @brief Classify a schema entry using its native statistic tag convention.
 * @param s Borrowed column spec; tests its setter string for case-sensitive _stat_.
 * @return True for that substring, false otherwise; does not inspect editor kind,
 * label, runtime availability or actual engine getter implementation.
 *
 */
bool isDynamics(const ColumnSpec &s)
{
    return s.setter.contains(QStringLiteral("_stat_"));
}

bool applyBulkCell(AttributeTablePanel &panel, QTableView *view, const QModelIndex &clicked,
                   bool prompt = false, double value = 0.0, bool typeValue = false,
                   bool cancel = false)
{
    if (!clicked.isValid())
        return false;
    panel.resize(1200, 600);
    panel.show();
    view->scrollTo(clicked);
    QCoreApplication::processEvents();
    const QPoint pos = view->visualRect(clicked).center();
    if (!view->viewport()->rect().contains(pos))
        return false;
    bool applied = false;
    QTimer::singleShot(0, &panel, [&] {
        auto *menu = panel.findChild<QMenu *>(QString(), Qt::FindDirectChildrenOnly);
        if (!menu)
            return;
        for (auto *action : menu->actions()) {
            if (action->text().startsWith(prompt ? "Apply \"" : "Apply this ")) {
                if (prompt) {
                    QTimer::singleShot(0, &panel, [&] {
                        if (auto *input = qobject_cast<QInputDialog *>(
                                QApplication::activeModalWidget())) {
                            if (typeValue) {
                                input->setStyle(panel.style());
                                for (auto *widget : input->findChildren<QWidget *>())
                                    widget->setStyle(panel.style());
                            }
                            if (cancel) {
                                input->reject();
                            } else if (typeValue) {
                                auto *spin = input->findChild<QDoubleSpinBox *>();
                                auto *line = spin ? spin->findChild<QLineEdit *>() : nullptr;
                                auto *buttons = input->findChild<QDialogButtonBox *>();
                                auto *ok = buttons ? buttons->button(QDialogButtonBox::Ok) : nullptr;
                                if (!line || !ok) { input->reject(); return; }
                                line->setFocus();
                                line->selectAll();
                                QTest::keyClicks(line, QString::number(value));
                                QTest::mouseClick(ok, Qt::LeftButton);
                            } else {
                                input->setDoubleValue(value);
                                input->accept();
                            }
                        }
                    });
                }
                if (typeValue) {
                    QObject::connect(action, &QAction::triggered, &panel, [&] { applied = true; });
                    menu->setActiveAction(action);
                    QTest::keyClick(menu, Qt::Key_Return);
                } else {
                    action->trigger();
                    applied = true;
                }
                break;
            }
        }
        menu->close();
    });
    return QMetaObject::invokeMethod(&panel, "onContextMenuRequested", Q_ARG(QPoint, pos)) &&
           applied;
}

// A reviewable fixture with three conduit targets, two different pumps and extra endpoints.
std::unique_ptr<SWMMModelLayer> openBulkNetworkLayer(bool si = false)
{
    QFile original(QDir(dataDir()).filePath("typed_selection_fixture.inp"));
    if (!original.open(QIODevice::ReadOnly)) return {};
    auto text = original.readAll();
    if (si) text.replace("FLOW_UNITS           CFS", "FLOW_UNITS           CMS");
    text.replace("\n[OUTFALLS]", "\nJ2 98 10 0 0 0\nJ3 97 10 0 0 0\n[OUTFALLS]");
    text.replace("\n[XSECTIONS]", "\nC2 J2 O1 1000 .014 0 0 0 0\n[XSECTIONS]");
    text.replace("\n[REPORT]", "\nC2 CIRCULAR 1 0 0 0 1\n[REPORT]");
    text.replace("\n[VERTICES]", "\nJ2 500 500\nJ3 1500 500\n[VERTICES]");
    text += "\n[PUMPS]\nP1 J1 O1 PC ON 0 0\nP2 J2 O1 PC2 ON 0 0\n"
            "[CURVES]\nPC PUMP3 0 10\nPC 10 0\nPC2 PUMP3 0 20\nPC2 10 0\nPC3 PUMP3 0 30\nPC3 10 0\n";
    const QString dir = QDir(dataDir()).filePath("multi_selection");
    if (!QDir().mkpath(dir)) return {};
    const QString path = dir + "/network-bulk-checkpoint.inp";
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    out.write(text);
    out.close();
    auto layer = std::make_unique<SWMMModelLayer>(path, nullptr);
    QList<QString> warnings, errors;
    if (!layer->loadModel(warnings, errors)) return {};
    return layer;
}

QModelIndex bulkProperty(PropertiesPanel &panel, const QString &label)
{
    const auto *model = panel.findChild<QTreeView *>()->model();
    for (int g = 0; g < model->rowCount(); ++g) {
        const auto group = model->index(g, 0);
        for (int r = 0; r < model->rowCount(group); ++r) {
            const auto name = model->index(r, 0, group).data().toString();
            if (name == label || name.startsWith(label + " (")) // Unit suffix follows the project.
                return model->index(r, 1, group);
        }
    }
    return {};
}

QString bulkLinkTag(SWMMModelLayer *layer, SWMMModelLayer::Category category, const QString &name)
{
    SWMMAttributeTableModel table;
    table.setSource(layer, category);
    return table.index(table.rowForName(name), colFor(table.columnSpecs(), "Tag")).data().toString();
}

QVariantList conduitSection(SWMM_Engine engine, const char *name)
{
    const int link = swmm_link_index(engine, name);
    int shape = -1, barrels = 0, culvert = 0;
    double g[4], up = 0, down = 0, roughness = 0;
    if (swmm_link_get_xsect(engine, link, &shape, &g[0], &g[1], &g[2], &g[3]) != SWMM_OK ||
        swmm_link_get_barrels(engine, link, &barrels) != SWMM_OK ||
        swmm_link_get_offset_up(engine, link, &up) != SWMM_OK ||
        swmm_link_get_offset_dn(engine, link, &down) != SWMM_OK ||
        swmm_link_get_roughness(engine, link, &roughness) != SWMM_OK ||
        swmm_link_get_culvert_code(engine, link, &culvert) != SWMM_OK)
        return {};
    return {shape, g[0], g[1], g[2], g[3], barrels, up, down, roughness, culvert};
}

std::unique_ptr<SWMMModelLayer> openBulkNodeLayer(bool si = false)
{
    auto base = openBulkNetworkLayer(si);
    if (!base) return {};
    QFile input(base->modelFilePath());
    if (!input.open(QIODevice::ReadOnly)) return {};
    auto text = input.readAll();
    text.replace("\n[CONDUITS]", "\nO2 89 FIXED 91 YES S1\nO3 88 TIDAL Tide NO\nOC 87 NORMAL NO\n[CONDUITS]");
    text.replace("\n[VERTICES]", "\nO2 2500 0\nO3 2500 500\nOC 2500 1000\n"
                 "ST1 0 1000\nST2 750 1000\nST3 1500 1000\nSTControl 1500 1500\n[VERTICES]");
    text += "Unused STORAGE 0 1\nUnused 10 1\nArea1 STORAGE 0 10\nArea1 10 20\n"
            "Area2 STORAGE 0 30\nArea2 10 50\nTide TIDAL 0 90\nTide 24 91\n"
            "Tide2 TIDAL 0 92\nTide2 24 93\n"
            "[TIMESERIES]\nStage1 0:00 92\nStage1 2:00 93\nStage2 0:00 94\nStage2 2:00 95\n"
            "[STORAGE]\nST1 80 10 .5 FUNCTIONAL 3 1 20 0 .1 2 .03 .2\n"
            "ST2 81 11 .75 TABULAR Area1 0 .2 3 .04 .3\n"
            "ST3 82 12 1 PYRAMIDAL 3 4 .5 0 .3 4 .05 .4\n"
            "STControl 83 13 0 FUNCTIONAL 0 0 100 0 0\n";
    const auto path = QDir(dataDir()).filePath(si ? "multi_selection/node-records-SI.inp"
                                                : "multi_selection/node-records-US.inp");
    QFile output(path);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    output.write(text);
    output.close();
    auto layer = std::make_unique<SWMMModelLayer>(path);
    QList<QString> warnings, errors;
    if (!layer->loadModel(warnings, errors)) return {};
    return layer;
}

QVariantList outfallState(SWMM_Engine engine, const char *name)
{
    const int node = swmm_node_index(engine, name);
    int type = -1, gate = 0, route = -1;
    double stage = 0, invert = 0;
    if (swmm_node_get_outfall_type(engine, node, &type) != SWMM_OK ||
        swmm_node_get_outfall_flap_gate(engine, node, &gate) != SWMM_OK ||
        swmm_node_get_outfall_route_to(engine, node, &route) != SWMM_OK ||
        swmm_node_get_invert_elev(engine, node, &invert) != SWMM_OK) return {};
    QVariant parameter;
    if (type == 2) {
        if (swmm_node_get_outfall_param(engine, node, &stage) != SWMM_OK) return {};
        parameter = stage;
    } else if (type >= 3) {
        int index = -1;
        if ((type == 3 ? swmm_node_get_outfall_tidal(engine, node, &index)
                       : swmm_node_get_outfall_timeseries(engine, node, &index)) != SWMM_OK) return {};
        const char *id = swmm_table_id(engine, index);
        if (!id) return {};
        parameter = QString::fromUtf8(id);
    }
    const char *routeId = route < 0 ? nullptr : swmm_subcatch_id(engine, route);
    return {type, parameter, gate, routeId ? QString::fromUtf8(routeId) : QString(), invert};
}

QVariantList storageState(SWMM_Engine engine, const char *name)
{
    const int node = swmm_node_index(engine, name);
    int shape = -1;
    double values[3], invert = 0, depth = 0, seep = 0, exfil[3];
    if (swmm_node_get_storage_shape(engine, node, &shape) != SWMM_OK ||
        swmm_node_get_invert_elev(engine, node, &invert) != SWMM_OK ||
        swmm_node_get_max_depth(engine, node, &depth) != SWMM_OK ||
        swmm_node_get_storage_seep_rate(engine, node, &seep) != SWMM_OK ||
        swmm_node_get_exfil_params(engine, node, &exfil[0], &exfil[1], &exfil[2]) != SWMM_OK) return {};
    QVariant geometry;
    if (shape == openswmmvis::kStorageTabularId) {
        int index = -1;
        if (swmm_node_get_storage_curve(engine, node, &index) != SWMM_OK) return {};
        const char *id = swmm_table_id(engine, index);
        if (!id) return {};
        geometry = QString::fromUtf8(id);
    } else {
        const int rc = shape == openswmmvis::kStorageFunctionalId
            ? swmm_node_get_storage_functional(engine, node, &values[0], &values[1], &values[2])
            : swmm_node_get_storage_geometry(engine, node, &values[0], &values[1], &values[2]);
        if (rc != SWMM_OK) return {};
        geometry = QVariantList{values[0], values[1], values[2]};
    }
    return {shape, geometry, invert, depth, seep, exfil[0], exfil[1], exfil[2]};
}

std::unique_ptr<SWMMModelLayer> openBulkRunoffLayer(bool si = false)
{
    auto base = openBulkNetworkLayer(si);
    if (!base) return {};
    const auto dir = QDir(dataDir()).filePath("multi_selection/runoff_files");
    if (!QDir().mkpath(dir)) return {};
    for (const auto &file : {QString("one.csv"), QString("two.csv")}) {
        QFile csv(dir + "/" + file);
        if (!csv.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
        csv.write(file == "one.csv" ? "DateTime,A,B\n2026-01-01 00:00,1,2\n2026-01-01 01:00,0,0\n"
                                    : "DateTime,B,C\n2026-01-01 00:00,2,3\n2026-01-01 01:00,0,0\n");
    }
    QFile source(base->modelFilePath());
    if (!source.open(QIODevice::ReadOnly)) return {};
    auto text = source.readAll();
    text.replace("\n[XSECTIONS]", "\nVL1 D1 O1 100 .013 0 0 0 0\nVL2 D2 O1 100 .013 0 0 0 0\nVL3 D3 O1 100 .013 0 0 0 0\nVLC DC O1 100 .013 0 0 0 0\n[XSECTIONS]");
    text.replace("\n[REPORT]", "\nVL1 CIRCULAR 1 0 0 0 1\nVL2 CIRCULAR 1 0 0 0 1\nVL3 CIRCULAR 1 0 0 0 1\nVLC CIRCULAR 1 0 0 0 1\n[REPORT]");
    text.replace("\n[VERTICES]", "\nD1 0 500\nD2 500 1000\nD3 1000 1000\nDC 1500 1000\n[VERTICES]");
    text += "Div1 DIVERSION 0 0\nDiv1 10 5\nDiv2 DIVERSION 0 0\nDiv2 10 7\n"
            "[DIVIDERS]\nD1 95 VL1 CUTOFF 2.5 9 .1 .2 3\nD2 96 VL2 TABULAR Div1 10 .2 .3 4\n"
            "D3 97 VL3 WEIR 3 1.5 1.25 11 .3 .4 5\nDC 98 VLC OVERFLOW 8 0 0 0\n"
            "[RAINGAGES]\nG2 INTENSITY 1:00 1 TIMESERIES TS1\nGC INTENSITY 1:00 1 TIMESERIES TS1\n";
    text += "G3 INTENSITY 1:00 1 FILE \"" + (dir + "/one.csv:B").toUtf8() + "\"\n";
    text += "[SUBCATCHMENTS]\nH2 G2 J1 4 40 400 1 0\nH3 S1 J3 3 30 300 1 0\nHC S1 O1 2 20 200 1 0\nX1 S1 O1 1 10 100 1 0\n"
            "[SUBAREAS]\nH2 .02 .2 .1 .2 25 OUTLET\nH3 .03 .3 .1 .2 25 OUTLET\nHC .04 .4 .1 .2 25 OUTLET\nX1 .02 .2 .1 .2 25 OUTLET\n"
            "[INFILTRATION]\nH2 3.5 .4 5 4 .6 MODIFIED_HORTON\nH3 3 .8 .2 GREEN_AMPT\nHC 75 0 7 CURVE_NUMBER\nX1 4 .5 4 7 0 HORTON\n";
    const auto path = QDir(dataDir()).filePath(si ? "multi_selection/runoff-SI.inp" : "multi_selection/runoff-US.inp");
    QFile output(path);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    output.write(text); output.close();
    auto layer = std::make_unique<SWMMModelLayer>(path);
    QList<QString> warnings, errors;
    if (!layer->loadModel(warnings, errors)) return {};
    return layer;
}

QVariantList dividerState(SWMM_Engine engine, const char *name)
{
    const int node = swmm_node_index(engine, name);
    SWMM_DividerParams record{};
    double depth = 0, invert = 0;
    if (swmm_node_get_divider_params(engine, node, &record) != SWMM_OK ||
        swmm_node_get_max_depth(engine, node, &depth) != SWMM_OK ||
        swmm_node_get_invert_elev(engine, node, &invert) != SWMM_OK) return {};
    const char *link = swmm_link_id(engine, record.diversion_link);
    const char *curve = record.curve < 0 ? nullptr : swmm_table_id(engine, record.curve);
    return {record.method, link ? QString::fromUtf8(link) : QString(), curve ? QString::fromUtf8(curve) : QString(),
            record.cutoff_flow, record.weir_height, record.weir_coefficient, depth, invert};
}

QVariantList infilState(SWMM_Engine engine, const char *name)
{
    const int sub = swmm_subcatch_index(engine, name);
    int method = -1;
    double p[4];
    if (swmm_subcatch_get_infil_model(engine, sub, &method) != SWMM_OK ||
        swmm_subcatch_get_infil_horton(engine, sub, &p[0], &p[1], &p[2], &p[3]) != SWMM_OK) return {};
    return {method, p[0], p[1], p[2], p[3]};
}

QVariantList rainSourceState(SWMM_Engine engine, const char *name)
{
    const int gage = swmm_gage_index(engine, name);
    int source = -1, format = -1;
    char series[4096]{}, path[4096]{}, absolute[4096]{}, column[4096]{}, station[4096]{};
    if (swmm_gage_get_data_source(engine, gage, &source) != SWMM_OK ||
        swmm_gage_get_file_format(engine, gage, &format) != SWMM_OK ||
        swmm_gage_get_timeseries(engine, gage, series, sizeof(series)) != SWMM_OK ||
        swmm_file_path_get(engine, SWMM_FILE_RAINGAGE_DATA, name, absolute, sizeof(absolute), path, sizeof(path)) != SWMM_OK ||
        swmm_gage_get_file_column(engine, gage, column, sizeof(column)) != SWMM_OK ||
        swmm_gage_get_station_id(engine, gage, station, sizeof(station)) != SWMM_OK) return {};
    return {source, source == 0 ? QString::fromUtf8(series) : QString(), QString::fromUtf8(absolute[0] ? absolute : path), format,
            QString::fromUtf8(column), QString::fromUtf8(station)};
}

} // namespace

std::unique_ptr<SWMMModelLayer> openBulkCompoundLayer(bool si = false)
{
    auto layer = openBulkNetworkLayer(si);
    if (!layer)
        return {};
    const auto e = layer->engine();
    for (auto name : {"PA", "PB"})
        if (swmm_pollutant_add(e, name, 0) != SWMM_OK)
            return {};
    if (swmm_timeseries_add(e, "OtherRain") != SWMM_OK)
        return {};
    for (int p = 0; p < 4; ++p) {
        const auto name = QString("Pattern%1").arg(p).toUtf8();
        if (swmm_pattern_add(e, name.constData(), p) != SWMM_OK)
            return {};
        const double factors[24] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
        if (swmm_pattern_set_factors(e, swmm_pattern_index(e, name.constData()), factors,
                                     p == 0   ? 12
                                     : p == 1 ? 7
                                              : 24) != SWMM_OK)
            return {};
    }
    for (auto group : {"UH1", "UH2"}) {
        if (swmm_hydrograph_add_gage(e, group, "S1") != SWMM_OK ||
            swmm_hydrograph_add(e, group, -1, 0, .1, 1, 1, 0, 0, 0) != SWMM_OK)
            return {};
    }
    int n = 0;
    for (auto name : {"J1", "J2", "O1", "X1"}) {
        const int node = swmm_node_index(e, name);
        ++n;
        if (n != 3) {
            if (swmm_ext_inflow_add(e, node, "FLOW", "TS1", "FLOW", n + .25, n + .5, n * 2,
                                    "Pattern2") != SWMM_OK ||
                swmm_dwf_add(e, node, "FLOW", n * 3, "Pattern0", "Pattern1", "Pattern2",
                             "Pattern3") != SWMM_OK ||
                swmm_rdii_add(e, node, n == 2 ? "UH2" : "UH1", n * 4) != SWMM_OK ||
                swmm_treatment_set(e, node, swmm_pollutant_index(e, "PA"),
                                   n == 1 ? "R = 0.1" : "R = 0.2") != SWMM_OK)
                return {};
        }
        if (swmm_ext_inflow_add(e, node, "PB", "", "CONCEN", 1, 1, n + .125, "") != SWMM_OK ||
            swmm_dwf_add(e, node, "PB", n + .375, "", "", "", "") != SWMM_OK ||
            swmm_treatment_set(e, node, swmm_pollutant_index(e, "PB"), "R = 0.31") != SWMM_OK)
            return {};
    }
    const auto file = QDir(dataDir()).filePath(si ? "multi_selection/node-compounds-SI.inp"
                                                  : "multi_selection/node-compounds-US.inp");
    if (swmm_model_write(e, file.toUtf8().constData()) != SWMM_OK)
        return {};
    auto saved = std::make_unique<SWMMModelLayer>(file);
    QList<QString> warnings, errors;
    if (!saved->loadModel(warnings, errors))
        return {};
    return saved;
}

QVariantList compoundState(SWMM_Engine e, NodeCompoundEditRef::Kind kind, const char *name)
{
    const int node = swmm_node_index(e, name);
    QMap<QString, QVariantList> rows;
    if (kind == NodeCompoundEditRef::Treatment) {
        for (int p = 0; p < swmm_pollutant_count(e); ++p) {
            char expression[8192]{};
            if (swmm_treatment_get(e, node, p, expression, sizeof(expression)) != SWMM_OK)
                return {};
            if (expression[0])
                rows[QString::fromUtf8(swmm_pollutant_id(e, p))] = {QString::fromUtf8(expression)};
        }
    } else {
        const int count = kind == NodeCompoundEditRef::Inflows ? swmm_ext_inflow_count(e)
                          : kind == NodeCompoundEditRef::Dwf   ? swmm_dwf_count(e)
                                                               : swmm_rdii_count(e);
        for (int i = 0; i < count; ++i) {
            int owner = -1;
            char key[4096]{}, a[4096]{}, b[4096]{}, c[4096]{}, d[4096]{};
            double x = 0, y = 0, z = 0;
            QVariantList values;
            if (kind == NodeCompoundEditRef::Inflows) {
                if (swmm_ext_inflow_get(e, i, &owner, key, sizeof(key), a, sizeof(a), b, sizeof(b),
                                        &x, &y, &z, c, sizeof(c)) != SWMM_OK)
                    return {};
                values = {QString::fromUtf8(a), QString::fromUtf8(b), x, y, z,
                          QString::fromUtf8(c)};
            } else if (kind == NodeCompoundEditRef::Dwf) {
                if (swmm_dwf_get(e, i, &owner, key, sizeof(key), &x, a, sizeof(a), b, sizeof(b), c,
                                 sizeof(c), d, sizeof(d)) != SWMM_OK)
                    return {};
                values = {x, QString::fromUtf8(a), QString::fromUtf8(b), QString::fromUtf8(c),
                          QString::fromUtf8(d)};
            } else {
                if (swmm_rdii_get(e, i, &owner, key, sizeof(key), &x) != SWMM_OK)
                    return {};
                values = {x};
            }
            if (owner == node)
                rows[QString::fromUtf8(key)] = values;
        }
    }
    QVariantList result;
    for (auto it = rows.cbegin(); it != rows.cend(); ++it)
        result << QVariant(QVariantList{it.key(), it.value()});
    return result;
}

QModelIndex compoundCell(NodeCompoundTableModel *model, const QString &key, const QString &field)
{
    for (int row = 0; row < model->rowCount(); ++row)
        if (model->index(row, 0).data(Qt::EditRole).toString() == key)
            for (int col = 2; col < model->columnCount(); ++col)
                if (model->fieldKey(col) == field)
                    return model->index(row, col);
    return {};
}

class TestAttributeTableSchema : public QObject
{
    Q_OBJECT

private slots:
    void bulkNodeCompoundFields_data()
    {
        QTest::addColumn<int>("kind");
        QTest::addColumn<QString>("label");
        QTest::addColumn<QString>("field");
        QTest::addColumn<bool>("si");
        for (bool si : {false, true}) {
            const auto suffix = si ? "-SI" : "-US";
            QTest::newRow(qPrintable(QString("inflow") + suffix))
                << int(NodeCompoundEditRef::Inflows) << QString("External Inflows")
                << QString("baseline") << si;
            QTest::newRow(qPrintable(QString("dwf") + suffix))
                << int(NodeCompoundEditRef::Dwf) << QString("Dry Weather Flow")
                << QString("baseline") << si;
            QTest::newRow(qPrintable(QString("rdii") + suffix))
                << int(NodeCompoundEditRef::Rdii) << QString("RDII") << QString("group") << si;
            QTest::newRow(qPrintable(QString("treatment") + suffix))
                << int(NodeCompoundEditRef::Treatment) << QString("Pollutant Treatment")
                << QString("expression") << si;
        }
    }
    void bulkNodeCompoundFields()
    {
        QFETCH(int, kind);
        QFETCH(QString, label);
        QFETCH(QString, field);
        QFETCH(bool, si);
        const auto page = static_cast<NodeCompoundEditRef::Kind>(kind);
        auto layer = openBulkCompoundLayer(si);
        QVERIFY(layer);
        const auto engine = layer->engine();
        const char *names[] = {"J1", "J2", "O1"};
        QList<QVariantList> before;
        for (auto name : names)
            before << compoundState(engine, page, name);
        const auto control = compoundState(engine, page, "X1");
        MapCanvas canvas;
        PropertiesPanel properties;
        properties.setProject(layer.get());
        properties.showSelection(&canvas, {{SWMMObjectRef::Node, "J1"},
                                           {SWMMObjectRef::Node, "J2"},
                                           {SWMMObjectRef::Node, "O1"}});
        properties.resize(700, 1000);
        properties.show();
        auto *tree = properties.findChild<QTreeView *>();
        tree->expandAll();
        const auto property = bulkProperty(properties, label);
        QVERIFY(property.isValid());
        tree->edit(property);
        auto *button = qobject_cast<NodeCompoundEditButton *>(tree->indexWidget(property));
        QVERIFY(button);
        QCOMPARE(button->value().nodeNames.size(), 3);
        QTest::mouseClick(button->findChild<QPushButton *>(), Qt::LeftButton);
        auto *dialog = properties.findChild<NodeCompoundEditDialog *>();
        QTRY_VERIFY(dialog);
        auto *table = dialog->findChild<QTableView *>("nodeCompoundTable");
        QVERIFY(table);
        auto *model = dynamic_cast<NodeCompoundTableModel *>(table->model());
        QVERIFY(model);
        // A modeless editor retains its captured scope when the Properties selection changes.
        properties.showSelection(&canvas,
                                 {{SWMMObjectRef::Node, "X1"}, {SWMMObjectRef::Node, "J3"}});
        QSignalSpy notifications(layer.get(), &SWMMModelLayer::attributeChanged);
        const QString key = page == NodeCompoundEditRef::Treatment ? "PA"
                            : page == NodeCompoundEditRef::Rdii    ? "Assignment"
                                                                   : "FLOW";
        const auto cell = compoundCell(model, key, field);
        QVERIFY(cell.isValid());
        QCOMPARE(cell.data().toString(), QString("Mixed"));
        table->edit(cell);
        if (field == "group") {
            auto *combo = table->findChild<QComboBox *>();
            QVERIFY(combo);
            combo->setCurrentIndex(combo->findData("UH2"));
            QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, combo->currentIndex()));
        } else if (field == "expression") {
            auto *text = table->findChild<openswmmvis::ui::TreatmentExpressionEdit *>();
            QVERIFY(text);
            text->setFocus();
            QTest::keyClicks(text, "R = 0.45");
            QTest::keyClick(text, Qt::Key_Return);
        } else {
            auto *line = table->findChild<QLineEdit *>();
            QVERIFY(line);
            line->setFocus();
            QTest::keyClick(line, Qt::Key_Escape);
            QCOMPARE(canvas.undoStack()->count(), 0);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            table->edit(cell);
            line = table->findChild<QLineEdit *>();
            QVERIFY(line);
            line->setFocus();
            QTest::keyClicks(line, "2.125");
            QTest::keyClick(line, si ? Qt::Key_Tab : Qt::Key_Return);
        }
        QTRY_COMPARE(canvas.undoStack()->count(), 1);
        QCOMPARE(notifications.count(), 3);
        QList<QVariantList> after;
        for (auto name : names)
            after << compoundState(engine, page, name);
        QVERIFY(after != before);
        QCOMPARE(compoundState(engine, page, "X1"), control);
        const auto entry = [](const QVariantList &state, const QString &key) {
            for (const auto &value : state) {
                const auto row = value.toList();
                if (row[0].toString() == key)
                    return row[1].toList();
            }
            return QVariantList();
        };
        for (int n = 0; n < 3; ++n) {
            if (page == NodeCompoundEditRef::Inflows) {
                auto expected = entry(before[n], "FLOW");
                if (expected.isEmpty())
                    expected = {QString(), QString("FLOW"), 1.0, 1.0, 0.0, QString()};
                expected[4] = 2.125;
                QCOMPARE(entry(after[n], "FLOW"), expected);
            } else if (page == NodeCompoundEditRef::Dwf) {
                auto expected = entry(before[n], "FLOW");
                if (expected.isEmpty())
                    expected = {0.0, QString(), QString(), QString(), QString()};
                expected[0] = 2.125;
                QCOMPARE(entry(after[n], "FLOW"), expected);
            } else if (page == NodeCompoundEditRef::Rdii) {
                const double area =
                    before[n].isEmpty() ? 0 : before[n].first().toList()[1].toList()[0].toDouble();
                QCOMPARE(after[n],
                         QVariantList{QVariant(QVariantList{QString("UH2"), QVariantList{area}})});
            } else
                QCOMPARE(entry(after[n], "PA"), QVariantList{QString("R = 0.45")});
        }
        // Unrelated PB rows stay exactly as authored, including on the newly assigned node.
        if (page != NodeCompoundEditRef::Rdii)
            for (int n = 0; n < 3; ++n)
                QCOMPARE(after[n].last(), before[n].last());
        dialog->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        canvas.undoStack()->undo();
        for (int n = 0; n < 3; ++n)
            QCOMPARE(compoundState(engine, page, names[n]), before[n]);
        canvas.undoStack()->redo();
        for (int n = 0; n < 3; ++n)
            QCOMPARE(compoundState(engine, page, names[n]), after[n]);
        const auto file = QDir(dataDir()).filePath(
            QString("multi_selection/compound-%1-%2.inp").arg(kind).arg(si ? "SI" : "US"));
        QCOMPARE(swmm_model_write(engine, file.toUtf8().constData()), SWMM_OK);
        SWMMModelLayer reopened(file);
        QList<QString> warnings, errors;
        QVERIFY2(reopened.loadModel(warnings, errors), qPrintable(errors.join(' ')));
        for (int n = 0; n < 3; ++n)
            QCOMPARE(compoundState(reopened.engine(), page, names[n]), after[n]);
    }

    void compoundFieldsPreserveSiblingsAndRejectInvalidBatches()
    {
        auto layer = openBulkCompoundLayer();
        QVERIFY(layer);
        const auto engine = layer->engine();
        QUndoStack undo;
        NodeCompoundEditRef ref;
        ref.layer = layer.get();
        ref.engine = engine;
        ref.nodeName = "J1";
        ref.nodeNames = {"J1", "J2"};
        ref.undoStack = &undo;
        for (auto kind : {NodeCompoundEditRef::Inflows, NodeCompoundEditRef::Dwf,
                          NodeCompoundEditRef::Rdii, NodeCompoundEditRef::Treatment}) {
            ref.kind = kind;
            NodeCompoundTableModel model(ref);
            const auto a = compoundState(engine, kind, "J1"), b = compoundState(engine, kind, "J2");
            const QString key = kind == NodeCompoundEditRef::Treatment ? "PA"
                                : kind == NodeCompoundEditRef::Rdii    ? "Assignment"
                                                                       : "FLOW";
            const QString field = kind == NodeCompoundEditRef::Inflows ? "scale"
                                  : kind == NodeCompoundEditRef::Dwf   ? "p2"
                                  : kind == NodeCompoundEditRef::Rdii  ? "area"
                                                                       : "expression";
            const auto cell = compoundCell(&model, key, field);
            QVERIFY(cell.isValid());
            const QVariant value = kind == NodeCompoundEditRef::Dwf         ? QVariant("Pattern2")
                                   : kind == NodeCompoundEditRef::Treatment ? QVariant("R = 0.55")
                                                                            : QVariant(7.125);
            QVERIFY2(model.setData(cell, value), qPrintable(model.lastError()));
            const int count = undo.count();
            QVERIFY(model.setData(cell, value));
            QCOMPARE(undo.count(), count);
            QVERIFY(model.removeAssignment(cell.row()));
            undo.undo();
            undo.undo();
            QCOMPARE(compoundState(engine, kind, "J1"), a);
            QCOMPARE(compoundState(engine, kind, "J2"), b);
            undo.clear();
            const QVariant invalid = kind == NodeCompoundEditRef::Dwf         ? QVariant("Pattern0")
                                     : kind == NodeCompoundEditRef::Treatment ? QVariant("R = ???")
                                                                              : QVariant(-1.0);
            QVERIFY(!model.setData(
                compoundCell(&model, key,
                             kind == NodeCompoundEditRef::Inflows ? "baseline" : field),
                invalid));
            QCOMPARE(undo.count(), 0);
            QCOMPARE(compoundState(engine, kind, "J1"), a);
            QCOMPARE(compoundState(engine, kind, "J2"), b);
        }
        // Proposal ranges must not prevent Undo from restoring an authored native value.
        ref.kind = NodeCompoundEditRef::Inflows;
        ref.nodeNames = {"J1"};
        int flow = -1;
        for (int i = 0; i < swmm_ext_inflow_count(engine); ++i) {
            int node = -1;
            char key[128]{}, series[128]{}, type[128]{}, pattern[128]{};
            double multiplier = 0, scale = 0, baseline = 0;
            QCOMPARE(swmm_ext_inflow_get(engine, i, &node, key, sizeof(key), series, sizeof(series),
                                         type, sizeof(type), &multiplier, &scale, &baseline,
                                         pattern, sizeof(pattern)),
                     SWMM_OK);
            if (node == swmm_node_index(engine, "J1") && QString::fromUtf8(key) == "FLOW")
                flow = i;
        }
        QVERIFY(flow >= 0);
        QCOMPARE(swmm_ext_inflow_set_baseline(engine, flow, -2.0), SWMM_OK);
        const auto authored = compoundState(engine, ref.kind, "J1");
        NodeCompoundTableModel legacy(ref);
        QVERIFY(legacy.setData(compoundCell(&legacy, "FLOW", "baseline"), 2.5));
        undo.undo();
        QCOMPARE(compoundState(engine, ref.kind, "J1"), authored);
    }

    void compoundNamedReferencesDuplicatesAndMissingAssignments()
    {
        auto layer = openBulkCompoundLayer();
        QVERIFY(layer);
        const auto e = layer->engine();
        QUndoStack undo;
        NodeCompoundEditRef ref;
        ref.layer = layer.get();
        ref.engine = e;
        ref.nodeName = "J1";
        ref.nodeNames = {"J1", "J2"};
        ref.undoStack = &undo;
        NodeCompoundTableModel inflows(ref);
        const auto first = compoundState(e, ref.kind, "J1"),
                   second = compoundState(e, ref.kind, "J2");
        QVERIFY(!inflows.setData(compoundCell(&inflows, "FLOW", "series"),
                                 "PC")); // Curve names are not time series.
        QCOMPARE(undo.count(), 0);
        QCOMPARE(compoundState(e, ref.kind, "J1"), first);
        QVERIFY(inflows.setData(compoundCell(&inflows, "FLOW", "series"), QString()));
        QCOMPARE(undo.count(), 1);
        undo.undo();
        QCOMPARE(compoundState(e, ref.kind, "J1"), first);
        QCOMPARE(compoundState(e, ref.kind, "J2"), second);
        undo.clear();
        auto missing = ref;
        missing.nodeNames = {"J1", "O1"};
        NodeCompoundTableModel clearMissing(missing);
        QVERIFY(clearMissing.setData(compoundCell(&clearMissing, "FLOW", "series"), QString()));
        // O1 has an unrelated PB inflow, but clearing its absent FLOW binding must not add one.
        QCOMPARE(compoundState(e, ref.kind, "O1").size(), 1);
        undo.undo();
        QCOMPARE(compoundState(e, ref.kind, "J1"), first);
        undo.clear();
        QCOMPARE(swmm_ext_inflow_add(e, swmm_node_index(e, "J2"), "FLOW", "", "FLOW", 1, 1, 99, ""),
                 SWMM_OK);
        const int count = swmm_ext_inflow_count(e);
        QVERIFY(!inflows.setData(compoundCell(&inflows, "FLOW", "baseline"), 8.0));
        QVERIFY(inflows.lastError().contains("duplicate"));
        QCOMPARE(undo.count(), 0);
        QCOMPARE(swmm_ext_inflow_count(e), count);
        QCOMPARE(compoundState(e, ref.kind, "J1"), first);
        ref.kind = NodeCompoundEditRef::Rdii;
        ref.nodeNames = {"J1", "O1"};
        NodeCompoundTableModel rdii(ref);
        const auto old = compoundState(e, ref.kind, "J1");
        QVERIFY(!rdii.setData(compoundCell(&rdii, "Assignment", "area"), 15.0));
        QCOMPARE(undo.count(), 0);
        QCOMPARE(compoundState(e, ref.kind, "J1"), old);
        QVERIFY(compoundState(e, ref.kind, "O1").isEmpty());
        QVERIFY(rdii.setData(compoundCell(&rdii, "Assignment", "group"), "UH2"));
        QCOMPARE(undo.count(), 1);
        undo.undo();
        QCOMPARE(compoundState(e, ref.kind, "J1"), old);
        QVERIFY(compoundState(e, ref.kind, "O1").isEmpty());
    }

    void compoundUndoSurvivesRenameAndRejectsReusedTargets()
    {
        auto layer = openBulkCompoundLayer();
        QVERIFY(layer);
        const auto e = layer->engine();
        QUndoStack undo;
        NodeCompoundEditRef ref;
        ref.layer = layer.get();
        ref.engine = e;
        ref.nodeName = "J1";
        ref.nodeNames = {"J1", "j1", "J2"};
        ref.undoStack = &undo;
        NodeCompoundTableModel model(ref);
        const auto before = compoundState(e, ref.kind, "J1"),
                   control = compoundState(e, ref.kind, "X1");
        QSignalSpy notifications(layer.get(), &SWMMModelLayer::attributeChanged);
        QVERIFY(model.setData(compoundCell(&model, "FLOW", "baseline"), 12.0));
        QCOMPARE(notifications.count(), 2);
        QVERIFY(layer->applyRename("J1", "Renamed", SWMMModelLayer::kKindNode));
        undo.undo();
        QCOMPARE(compoundState(e, ref.kind, "Renamed"), before);
        undo.redo();
        const auto edited = compoundState(e, ref.kind, "Renamed");
        QVERIFY(layer->applyNodeDelete("J2"));
        QVERIFY(layer->applyNodeAdd("J2", SWMM_NODE_JUNCTION, 0, 0));
        undo.undo();
        QCOMPARE(compoundState(e, ref.kind, "Renamed"), edited);
        QVERIFY(compoundState(e, ref.kind, "J2").isEmpty());
        QVERIFY(!model.setData(compoundCell(&model, "FLOW", "baseline"), 25.0));
        QCOMPARE(compoundState(e, ref.kind, "X1"), control);
        undo.clear();
        ref.nodeNames = {"Renamed"};
        ref.nodeName = "Renamed";
        NodeCompoundTableModel closing(ref);
        QVERIFY(closing.setData(compoundCell(&closing, "FLOW", "scale"), 4.0));
        const auto retained = compoundState(e, ref.kind, "Renamed");
        emit layer->engineAboutToClose();
        undo.undo();
        QCOMPARE(compoundState(e, ref.kind, "Renamed"), retained);
        QVERIFY(!closing.setData(compoundCell(&closing, "FLOW", "scale"), 5.0));
    }

    void compoundTreatmentUndoKeepsLongExpressions()
    {
        auto layer = openBulkCompoundLayer();
        QVERIFY(layer);
        const auto e = layer->engine();
        const auto longText = QString("R = 0.1") + QString(" + 0").repeated(400);
        QCOMPARE(swmm_treatment_set(e, swmm_node_index(e, "J1"), swmm_pollutant_index(e, "PA"),
                                    longText.toUtf8().constData()),
                 SWMM_OK);
        QUndoStack undo;
        NodeCompoundEditRef ref;
        ref.layer = layer.get();
        ref.engine = e;
        ref.nodeName = "J1";
        ref.kind = NodeCompoundEditRef::Treatment;
        ref.undoStack = &undo;
        NodeCompoundTableModel model(ref);
        QCOMPARE(compoundCell(&model, "PA", "expression").data(Qt::EditRole).toString(), longText);
        QVERIFY(model.setData(compoundCell(&model, "PA", "expression"), "R = 0.4"));
        undo.undo();
        QCOMPARE(compoundState(e, ref.kind, "J1").first().toList()[1].toList()[0].toString(),
                 longText);
        undo.clear();
        const QString coupled = "R = R_PB * 0.5 + PB * 1e-5";
        QCOMPARE(swmm_treatment_set(e, swmm_node_index(e, "J2"), swmm_pollutant_index(e, "PA"),
                                    coupled.toUtf8().constData()),
                 SWMM_OK);
        ref.nodeNames = {"J1", "J2"};
        NodeCompoundTableModel selected(ref);
        openswmmvis::ui::TreatmentExpressionEdit editor(e);
        QSignalSpy verdict(&editor, &openswmmvis::ui::TreatmentExpressionEdit::validationChanged);
        editor.setExpression(coupled);
        QVERIFY(verdict.last()[0].toBool());
        QVERIFY(selected.setData(compoundCell(&selected, "PA", "expression"), "R = R_PB * 0.25"));
        undo.undo();
        QCOMPARE(compoundState(e, ref.kind, "J1").first().toList()[1].toList()[0].toString(),
                 longText);
        QCOMPARE(compoundState(e, ref.kind, "J2").first().toList()[1].toList()[0].toString(),
                 coupled);
        editor.setExpression("R = R_Missing * 0.5");
        QVERIFY(!verdict.last()[0].toBool());
        NodeCompoundEditDialog dialog(ref);
        dialog.show();
        auto *table = dialog.findChild<QTableView *>("nodeCompoundTable");
        QVERIFY(table);
        auto *viewModel = dynamic_cast<NodeCompoundTableModel *>(table->model());
        QVERIFY(viewModel);
        table->edit(compoundCell(viewModel, "PA", "expression"));
        QPointer<openswmmvis::ui::TreatmentExpressionEdit> active =
            table->findChild<openswmmvis::ui::TreatmentExpressionEdit *>();
        QVERIFY(active);
        editor.setValidationEngine(nullptr);
        layer.reset(); // A queued/debounced validation must not touch the released engine.
        if (active)
            active->validateNow();
    }

    void compoundTableContextUsesTheSelectedRowsAndSharedUndo()
    {
        auto layer = openBulkCompoundLayer();
        QVERIFY(layer);
        const auto e = layer->engine();
        const auto first = compoundState(e, NodeCompoundEditRef::Inflows, "J1"),
                   second = compoundState(e, NodeCompoundEditRef::Inflows, "J2"),
                   control = compoundState(e, NodeCompoundEditRef::Inflows, "X1");
        MapCanvas canvas;
        SelectionManager selection;
        AttributeTablePanel panel;
        panel.setProject(layer.get(), &selection, &canvas);
        panel.showModelCategory(layer.get(), SWMMModelLayer::CatJunctions);
        panel.resize(1400, 650);
        panel.show();
        selection.select({{SWMMObjectRef::Node, "J1"}, {SWMMObjectRef::Node, "J2"}},
                         SelectionManager::Replace);
        QCoreApplication::processEvents();
        auto *view = panel.findChild<QTableView *>();
        QVERIFY(view);
        QModelIndex clicked;
        for (int row = 0; row < view->model()->rowCount(); ++row)
            if (view->model()->index(row, 0).data().toString() == "J1")
                for (int col = 0; col < view->model()->columnCount(); ++col)
                    if (view->model()->headerData(col, Qt::Horizontal).toString() ==
                        "External Inflows")
                        clicked = view->model()->index(row, col);
        QVERIFY(clicked.isValid());
        QSignalSpy dirty(layer.get(), &SWMMModelLayer::modelEdited);
        QVERIFY(view->model()->setData(clicked, clicked.data(Qt::EditRole), Qt::EditRole));
        QCOMPARE(dirty.count(), 0); // Finishing a navigation cell must not dirty the model.
        view->scrollTo(clicked);
        QCoreApplication::processEvents();
        bool opened = false;
        QTimer::singleShot(0, &panel, [&] {
            auto *menu = panel.findChild<QMenu *>(QString(), Qt::FindDirectChildrenOnly);
            if (!menu)
                return;
            for (auto *action : menu->actions())
                if (action->text().startsWith("Edit External Inflows for 2")) {
                    action->trigger();
                    opened = true;
                    break;
                }
            menu->close();
        });
        QVERIFY(QMetaObject::invokeMethod(&panel, "onContextMenuRequested",
                                          Q_ARG(QPoint, view->visualRect(clicked).center())));
        QVERIFY(opened);
        auto *dialog = panel.findChild<NodeCompoundEditDialog *>();
        QVERIFY(dialog);
        QCOMPARE(dialog->findChild<QDialogButtonBox *>()->standardButtons(),
                 QDialogButtonBox::StandardButtons(QDialogButtonBox::Close));
        auto *table = dialog->findChild<QTableView *>("nodeCompoundTable");
        QVERIFY(table);
        auto *model = dynamic_cast<NodeCompoundTableModel *>(table->model());
        QVERIFY(model);
        const auto series = compoundCell(model, "FLOW", "series");
        table->edit(series);
        auto *combo = table->findChild<QComboBox *>();
        QVERIFY(combo);
        combo->setCurrentIndex(combo->findData(QString()));
        QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, combo->currentIndex()));
        QTRY_COMPARE(canvas.undoStack()->count(), 1);
        table->setCurrentIndex(compoundCell(model, "FLOW", "series"));
        auto *editReference = dialog->findChild<QPushButton *>("editNodeCompoundReference");
        QVERIFY(editReference);
        QVERIFY(editReference->isEnabled());
        QTest::mouseClick(editReference, Qt::LeftButton);
        auto *picker = dialog->findChild<DataObjectPickerEditor *>();
        QVERIFY(picker);
        picker->applyPickedName("TS1");
        QTRY_COMPARE(canvas.undoStack()->count(), 2);
        picker->window()->close();
        dialog->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        canvas.undoStack()->undo();
        canvas.undoStack()->undo();
        QCOMPARE(compoundState(e, NodeCompoundEditRef::Inflows, "J1"), first);
        QCOMPARE(compoundState(e, NodeCompoundEditRef::Inflows, "J2"), second);
        QCOMPARE(compoundState(e, NodeCompoundEditRef::Inflows, "X1"), control);
    }

    void bulkDividerRecords_data() { bulkOutfallTypeAndStage_data(); }
    void bulkDividerRecords()
    {
        QFETCH(bool, si);
        auto layer = openBulkRunoffLayer(si);
        QVERIFY(layer);
        const auto engine = layer->engine();
        const char *names[] = {"D1", "D2", "D3"};
        QList<QVariantList> before;
        for (auto name : names) { before << dividerState(engine, name); QCOMPARE(before.last().size(), 8); }
        const auto control = dividerState(engine, "DC");
        MapCanvas canvas;
        PropertiesPanel properties;
        properties.setProject(layer.get());
        properties.showSelection(&canvas, {{SWMMObjectRef::Node, "D1"}, {SWMMObjectRef::Node, "D2"}, {SWMMObjectRef::Node, "D3"}});
        properties.resize(700, 1000); properties.show();
        auto *tree = properties.findChild<QTreeView *>(); tree->expandAll();
        const auto method = bulkProperty(properties, "Divider Type");
        QCOMPARE(method.data().toString(), QString("Mixed"));
        tree->edit(method);
        auto *combo = qobject_cast<QComboBox *>(tree->indexWidget(method));
        QVERIFY(combo);
        QVERIFY(!(combo->model()->index(combo->findData(2), 0).flags() & Qt::ItemIsEnabled));
        combo->setCurrentIndex(combo->findData(3));
        QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, combo->currentIndex()));
        QTRY_COMPARE(canvas.undoStack()->count(), 1);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        const auto height = bulkProperty(properties, "Weir Height");
        QVERIFY(height.flags() & Qt::ItemIsEditable);
        tree->edit(height);
        auto *spin = qobject_cast<QDoubleSpinBox *>(tree->indexWidget(height));
        QVERIFY(spin);
        spin->setFocus(); spin->selectAll(); QTest::keyClicks(spin, "2.125"); QTest::keyClick(spin, Qt::Key_Return);
        QTRY_COMPARE(canvas.undoStack()->count(), 2);
        for (int i = 0; i < 3; ++i) {
            const auto state = dividerState(engine, names[i]);
            QCOMPARE(state[0].toInt(), 3); QCOMPARE(state[1], before[i][1]);
            QCOMPARE(state[3], before[i][3]); QCOMPARE(state[4].toDouble(), 2.125);
            QCOMPARE(state.mid(6), before[i].mid(6));
        }
        const auto weirSaved = QDir(dataDir()).filePath(si ? "multi_selection/weirs-edited-SI.inp" : "multi_selection/weirs-edited-US.inp");
        QCOMPARE(swmm_model_write(engine, weirSaved.toUtf8().constData()), SWMM_OK);
        SWMMModelLayer weirs(weirSaved); QList<QString> weirWarnings, weirErrors;
        QVERIFY2(weirs.loadModel(weirWarnings, weirErrors), qPrintable(weirErrors.join(' ')));
        for (auto name : names) QCOMPARE(dividerState(weirs.engine(), name).mid(3), dividerState(engine, name).mid(3));
        canvas.undoStack()->undo(); canvas.undoStack()->undo();
        for (int i = 0; i < 3; ++i) QCOMPARE(dividerState(engine, names[i]), before[i]);
        QCOMPARE(dividerState(engine, "DC"), control);
        const auto curve = bulkProperty(properties, "Diversion Curve");
        tree->edit(curve);
        auto *picker = qobject_cast<DataObjectPickerEditor *>(tree->indexWidget(curve));
        QVERIFY(picker); picker->applyPickedName("Div2");
        QTRY_COMPARE(canvas.undoStack()->index(), 1);
        for (auto name : names) { QCOMPARE(dividerState(engine, name)[0].toInt(), 2); QCOMPARE(dividerState(engine, name)[2].toString(), QString("Div2")); }
        const auto saved = QDir(dataDir()).filePath(si ? "multi_selection/dividers-edited-SI.inp" : "multi_selection/dividers-edited-US.inp");
        QCOMPARE(swmm_model_write(engine, saved.toUtf8().constData()), SWMM_OK);
        SWMMModelLayer reopened(saved); QList<QString> warnings, errors;
        QVERIFY2(reopened.loadModel(warnings, errors), qPrintable(errors.join(' ')));
        for (auto name : names) {
            const auto current = dividerState(engine, name), persisted = dividerState(reopened.engine(), name);
            QCOMPARE(persisted[0], current[0]); QCOMPARE(persisted[1], current[1]); QCOMPARE(persisted[2], current[2]);
            QCOMPARE(persisted.mid(6), current.mid(6));
        }
        canvas.undoStack()->undo();
        for (int i = 0; i < 3; ++i) QCOMPARE(dividerState(engine, names[i]), before[i]);
    }

    void bulkInfiltrationRecords_data() { bulkOutfallTypeAndStage_data(); }
    void bulkInfiltrationRecords()
    {
        QFETCH(bool, si);
        auto layer = openBulkRunoffLayer(si); QVERIFY(layer);
        const auto engine = layer->engine();
        const char *names[] = {"S1", "H2", "H3"};
        QList<QVariantList> before;
        for (auto name : names) { before << infilState(engine, name); QCOMPARE(before.last().size(), 5); }
        const auto control = infilState(engine, "HC");
        MapCanvas canvas; PropertiesPanel properties; properties.setProject(layer.get());
        const QList<SWMMObjectRef> refs{{SWMMObjectRef::Subcatchment, "S1"}, {SWMMObjectRef::Subcatchment, "H2"}, {SWMMObjectRef::Subcatchment, "H3"}};
        properties.showSelection(&canvas, refs); properties.resize(700, 1000); properties.show();
        auto *tree = properties.findChild<QTreeView *>(); tree->expandAll();
        const auto method = bulkProperty(properties, "Infiltration Model");
        tree->edit(method);
        auto *combo = qobject_cast<QComboBox *>(tree->indexWidget(method)); QVERIFY(combo);
        combo->setCurrentIndex(combo->findData(2)); QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, combo->currentIndex()));
        QTRY_COMPARE(canvas.undoStack()->count(), 1);
        QCOMPARE(infilState(engine, "H3"), before[2]);
        QCOMPARE(infilState(engine, "S1")[1].toDouble(), si ? 76.2 : 3.0);
        QVERIFY(!(bulkProperty(properties, "Max. Infil. Rate").flags() & Qt::ItemIsEditable));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        const auto conductivity = bulkProperty(properties, "Conductivity");
        tree->edit(conductivity);
        auto *spin = qobject_cast<QDoubleSpinBox *>(tree->indexWidget(conductivity)); QVERIFY(spin);
        spin->setFocus(); spin->selectAll(); QTest::keyClicks(spin, "2.125"); QTest::keyClick(spin, Qt::Key_Tab);
        QTRY_COMPARE(canvas.undoStack()->count(), 2);
        for (auto name : names) QCOMPARE(infilState(engine, name)[2].toDouble(), 2.125);
        canvas.undoStack()->undo(); canvas.undoStack()->undo();
        for (int i = 0; i < 3; ++i) QCOMPARE(infilState(engine, names[i]), before[i]);
        QCOMPARE(infilState(engine, "HC"), control);
        canvas.undoStack()->redo(); canvas.undoStack()->redo();
        const auto saved = QDir(dataDir()).filePath(si ? "multi_selection/infil-edited-SI.inp" : "multi_selection/infil-edited-US.inp");
        QCOMPARE(swmm_model_write(engine, saved.toUtf8().constData()), SWMM_OK);
        SWMMModelLayer reopened(saved); QList<QString> warnings, errors;
        QVERIFY2(reopened.loadModel(warnings, errors), qPrintable(errors.join(' ')));
        for (auto name : names) QCOMPARE(infilState(reopened.engine(), name), infilState(engine, name));
    }

    void bulkRunoffReferences_data()
    {
        QTest::addColumn<QString>("key"); QTest::addColumn<QString>("label"); QTest::addColumn<QString>("choice");
        QTest::newRow("gage") << QString("Rain gage") << QString("Rain Gage") << QString("GC");
        QTest::newRow("snowpack") << QString("Snow pack") << QString("Snow Pack") << QString("Snow2");
        QTest::newRow("aquifer") << QString("Aquifer") << QString("Aquifer") << QString("AQ2");
        QTest::newRow("node-outlet") << QString("Outlet") << QString("Outlet") << QString("Node: J2");
        QTest::newRow("subcatchment-outlet") << QString("Outlet") << QString("Outlet") << QString("Subcatchment: H3");
    }
    void bulkRunoffReferences()
    {
        QFETCH(QString, key); QFETCH(QString, label); QFETCH(QString, choice);
        auto layer = openBulkRunoffLayer(); QVERIFY(layer);
        const auto engine = layer->engine();
        for (auto name : {"Snow1", "Snow2"}) QCOMPARE(swmm_snowpack_add(engine, name), SWMM_OK);
        for (auto name : {"AQ1", "AQ2"}) QCOMPARE(swmm_aquifer_add(engine, name), SWMM_OK);
        const int s1 = swmm_subcatch_index(engine, "S1"), h2 = swmm_subcatch_index(engine, "H2");
        QCOMPARE(swmm_subcatch_set_outlet_subcatch(engine, h2, swmm_subcatch_index(engine, "H3")), SWMM_OK);
        QCOMPARE(swmm_subcatch_set_snowpack(engine, s1, "Snow1"), SWMM_OK);
        QCOMPARE(swmm_subcatch_set_aquifer(engine, s1, 0), SWMM_OK);
        for (int sub : {s1, h2}) {
            QCOMPARE(swmm_subcatch_set_gw_node(engine, sub, swmm_node_index(engine, "J1")), SWMM_OK);
            QCOMPARE(swmm_subcatch_set_gw_params(engine, sub, 100, 1, 1, .5, 1, 0, 0, 90), SWMM_OK);
        }
        SWMMAttributeTableModel model; model.setSource(layer.get(), SWMMModelLayer::CatSubcatchments);
        const int col = colFor(model.columnSpecs(), key);
        const auto original1 = model.index(model.rowForName("S1"), col).data(Qt::EditRole).value<DataObjectRef>();
        const auto original2 = model.index(model.rowForName("H2"), col).data(Qt::EditRole).value<DataObjectRef>();
        const auto control = model.index(model.rowForName("HC"), col).data(Qt::EditRole).value<DataObjectRef>();
        MapCanvas canvas; PropertiesPanel properties; properties.setProject(layer.get());
        properties.showSelection(&canvas, {{SWMMObjectRef::Subcatchment, "S1"}, {SWMMObjectRef::Subcatchment, "H2"}});
        properties.resize(700, 1000); properties.show(); auto *tree = properties.findChild<QTreeView *>(); tree->expandAll();
        const auto field = bulkProperty(properties, label); QVERIFY(field.isValid()); tree->edit(field);
        auto *picker = qobject_cast<DataObjectPickerEditor *>(tree->indexWidget(field)); QVERIFY(picker);
        picker->applyPickedName(choice); QTRY_COMPARE(canvas.undoStack()->count(), 1);
        for (auto name : {"S1", "H2"}) {
            const auto ref = model.index(model.rowForName(name), col).data(Qt::EditRole).value<DataObjectRef>();
            QCOMPARE(ref.currentName, key == "Outlet" ? choice.mid(choice.indexOf(": ") + 2) : choice);
            if (key == "Outlet") QCOMPARE(ref.outletTarget, choice.startsWith("Node: ") ? DataObjectRef::NodeOutlet : DataObjectRef::SubcatchmentOutlet);
        }
        const auto saved = QDir(dataDir()).filePath("multi_selection/runoff-reference-" + QString(key).replace(' ', '_') + ".inp");
        QCOMPARE(swmm_model_write(engine, saved.toUtf8().constData()), SWMM_OK);
        SWMMModelLayer reopened(saved); QList<QString> warnings, errors;
        QVERIFY2(reopened.loadModel(warnings, errors), qPrintable(errors.join(' ')));
        SWMMAttributeTableModel reopenedModel; reopenedModel.setSource(&reopened, SWMMModelLayer::CatSubcatchments);
        for (auto name : {"S1", "H2"}) {
            const auto actual = reopenedModel.index(reopenedModel.rowForName(name), colFor(reopenedModel.columnSpecs(), key)).data(Qt::EditRole).value<DataObjectRef>();
            const auto expected = model.index(model.rowForName(name), col).data(Qt::EditRole).value<DataObjectRef>();
            QCOMPARE(actual.currentName, expected.currentName); QCOMPARE(actual.outletTarget, expected.outletTarget);
        }
        canvas.undoStack()->undo();
        QCOMPARE(model.index(model.rowForName("S1"), col).data(Qt::EditRole).value<DataObjectRef>(), original1);
        QCOMPARE(model.index(model.rowForName("H2"), col).data(Qt::EditRole).value<DataObjectRef>(), original2);
        QCOMPARE(model.index(model.rowForName("HC"), col).data(Qt::EditRole).value<DataObjectRef>(), control);
        if (key == "Outlet") {
            auto cycle = original1; cycle.currentName = "S1"; cycle.outletTarget = DataObjectRef::SubcatchmentOutlet;
            QUndoStack undo; QString error;
            QVERIFY(!model.applyScalarEdits({"H2", "S1"}, {{key, QVariant::fromValue(cycle)}}, &undo, &error));
            QCOMPARE(undo.count(), 0);
            for (auto target : {DataObjectRef::NodeOutlet, DataObjectRef::SubcatchmentOutlet}) {
                auto ambiguous = original1; ambiguous.currentName = "X1"; ambiguous.outletTarget = target;
                QVERIFY(!model.applyScalarEdits({"H2", "S1"}, {{key, QVariant::fromValue(ambiguous)}}, &undo, &error));
                QVERIFY(error.contains("names both")); QCOMPARE(undo.count(), 0);
                QCOMPARE(model.index(model.rowForName("S1"), col).data(Qt::EditRole).value<DataObjectRef>(), original1);
                QCOMPARE(model.index(model.rowForName("H2"), col).data(Qt::EditRole).value<DataObjectRef>(), original2);
            }
        }
    }

    void bulkRainSourceFilesCommitAndUndoTogether()
    {
        auto layer = openBulkRunoffLayer(); QVERIFY(layer);
        const auto engine = layer->engine();
        const char *names[] = {"S1", "G2", "G3"}; QList<QVariantList> before;
        for (auto name : names) { before << rainSourceState(engine, name); QCOMPARE(before.last().size(), 6); }
        const auto control = rainSourceState(engine, "GC");
        MapCanvas canvas; PropertiesPanel properties; properties.setProject(layer.get());
        properties.showSelection(&canvas, {{SWMMObjectRef::RainGage, "S1"}, {SWMMObjectRef::RainGage, "G2"}, {SWMMObjectRef::RainGage, "G3"}});
        properties.resize(700, 1000); properties.show(); auto *tree = properties.findChild<QTreeView *>(); tree->expandAll();
        const auto field = bulkProperty(properties, "Rain File (path)"); QVERIFY(field.isValid());
        tree->edit(field); auto *editor = tree->indexWidget(field); QVERIFY(editor);
        auto *line = editor->findChild<QLineEdit *>(); QVERIFY(line);
        QTest::keyClick(line, Qt::Key_Escape); QCOMPARE(canvas.undoStack()->count(), 0);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        tree->edit(field); editor = tree->indexWidget(field); QVERIFY(editor); line = editor->findChild<QLineEdit *>(); QVERIFY(line);
        const auto file = QDir(dataDir()).filePath("multi_selection/runoff_files/two.csv");
        line->setFocus(); line->selectAll(); QTest::keyClicks(line, file); QTest::keyClick(line, Qt::Key_Return);
        QTRY_COMPARE(canvas.undoStack()->count(), 1);
        for (auto name : names) { QCOMPARE(rainSourceState(engine, name)[0].toInt(), 1); QCOMPARE(rainSourceState(engine, name)[2].toString(), file); }
        canvas.undoStack()->undo();
        for (int i = 0; i < 3; ++i) QCOMPARE(rainSourceState(engine, names[i]), before[i]);
        canvas.undoStack()->redo();
        SWMMAttributeTableModel model; model.setSource(layer.get(), SWMMModelLayer::CatRainGages);
        // The same file editor commits a table path while it still has focus.
        SelectionManager selection; AttributeTablePanel table;
        table.setProject(layer.get(), &selection, &canvas);
        table.showModelCategory(layer.get(), SWMMModelLayer::CatRainGages); table.resize(1100, 600); table.show();
        auto *view = table.findChild<QTableView *>(); QVERIFY(view);
        int row = -1;
        for (int r = 0; r < view->model()->rowCount(); ++r)
            if (view->model()->index(r, 0).data().toString() == "G2") row = r;
        const auto cell = view->model()->index(row, colFor(model.columnSpecs(), "Rain file"));
        QVERIFY(cell.isValid()); view->scrollTo(cell); view->edit(cell);
        auto *fileEditor = view->indexWidget(cell); QVERIFY(fileEditor);
        auto *fileLine = fileEditor->findChild<QLineEdit *>(); QVERIFY(fileLine);
        const auto otherFile = QDir(dataDir()).filePath("multi_selection/runoff_files/one.csv");
        fileLine->setFocus(); fileLine->selectAll(); QTest::keyClicks(fileLine, otherFile); QTest::keyClick(fileLine, Qt::Key_Return);
        QTRY_COMPARE(canvas.undoStack()->count(), 2);
        QCOMPARE(rainSourceState(engine, "G2")[2].toString(), otherFile);
        canvas.undoStack()->undo();
        QCOMPARE(rainSourceState(engine, "G2")[2].toString(), file);
        QString error;
        QVERIFY2(model.applyScalarEdits({"S1", "G2", "G3"}, {{"Rain file format", 6}, {"Rain file column", "B"}}, canvas.undoStack(), &error), qPrintable(error));
        const int count = canvas.undoStack()->count();
        QVERIFY(!model.applyScalarEdits({"S1", "G2", "G3"}, {{"Rain file column", "Missing"}}, canvas.undoStack(), &error));
        QCOMPARE(canvas.undoStack()->count(), count);
        const auto saved = QDir(dataDir()).filePath("multi_selection/rain-sources-edited.inp");
        QCOMPARE(swmm_model_write(engine, saved.toUtf8().constData()), SWMM_OK);
        SWMMModelLayer reopened(saved); QList<QString> warnings, errors;
        QVERIFY2(reopened.loadModel(warnings, errors), qPrintable(errors.join(' ')));
        for (auto name : names) QCOMPARE(rainSourceState(reopened.engine(), name), rainSourceState(engine, name));
        QCOMPARE(rainSourceState(engine, "GC"), control);
        auto series = model.index(model.rowForName("S1"), colFor(model.columnSpecs(), "Series name")).data(Qt::EditRole).value<DataObjectRef>();
        series.currentName = "TS1";
        const auto fileBefore = rainSourceState(engine, "G3");
        QVERIFY(model.applyScalarEdits({"S1", "G2", "G3"}, {{"Series name", QVariant::fromValue(series)}}, canvas.undoStack(), &error));
        for (auto name : names) QCOMPARE(rainSourceState(engine, name)[0].toInt(), 0);
        canvas.undoStack()->undo(); QCOMPARE(rainSourceState(engine, "G3"), fileBefore);
    }

    void bulkRunoffRollbackAndStaleReferencesLeaveEveryTargetUnchanged()
    {
        auto layer = openBulkRunoffLayer(); QVERIFY(layer);
        const auto engine = layer->engine();
        SWMMAttributeTableModel model; model.setSource(layer.get(), SWMMModelLayer::CatSubcatchments);
        QUndoStack undo; QString error;
        const auto first = infilState(engine, "S1"), second = infilState(engine, "H2");
        double area = 0;
        const int sub = swmm_subcatch_index(engine, "S1");
        QCOMPARE(swmm_subcatch_get_area(engine, sub, &area), SWMM_OK);
        // Area is written before the invalid coupled Horton record. The batch must restore it.
        QVERIFY(!model.applyScalarEdits({"S1", "H2"}, {{"Area", 99.0}, {"Max. rate", .1}}, &undo, &error));
        QCOMPARE(infilState(engine, "S1"), first); QCOMPARE(infilState(engine, "H2"), second);
        double actual = 0; QCOMPARE(swmm_subcatch_get_area(engine, sub, &actual), SWMM_OK); QCOMPARE(actual, area);
        QCOMPARE(undo.count(), 0);
        QCOMPARE(swmm_subcatch_set_gage(engine, swmm_subcatch_index(engine, "H2"), swmm_gage_index(engine, "G3")), SWMM_OK);
        const int column = colFor(model.columnSpecs(), "Rain gage");
        auto ref = model.index(model.rowForName("S1"), column).data(Qt::EditRole).value<DataObjectRef>(); ref.currentName = "G2";
        QVERIFY(model.applyScalarEdits({"H2", "S1"}, {{"Rain gage", QVariant::fromValue(ref)}}, &undo, &error));
        QCOMPARE(swmm_gage_delete(engine, swmm_gage_index(engine, "GC"), nullptr), SWMM_OK);
        undo.undo();
        QCOMPARE(model.index(model.rowForName("H2"), column).data(Qt::EditRole).value<DataObjectRef>().currentName, QString("G3"));
        undo.redo();
        QCOMPARE(swmm_gage_delete(engine, swmm_gage_index(engine, "S1"), nullptr), SWMM_OK);
        undo.undo(); // A missing reference on the last target must be found before changing H2.
        for (auto name : {"H2", "S1"}) QCOMPARE(model.index(model.rowForName(name), column).data(Qt::EditRole).value<DataObjectRef>().currentName, QString("G2"));
    }

    void rainSourceUndoRestoresAnOriginallyUnassignedSeries()
    {
        auto layer = openBulkRunoffLayer(); QVERIFY(layer);
        const auto engine = layer->engine();
        QCOMPARE(swmm_gage_set_timeseries(engine, swmm_gage_index(engine, "G2"), ""), SWMM_OK);
        const auto before = rainSourceState(engine, "G2");
        SWMMAttributeTableModel model; model.setSource(layer.get(), SWMMModelLayer::CatRainGages);
        auto ref = model.index(model.rowForName("G2"), colFor(model.columnSpecs(), "Series name")).data(Qt::EditRole).value<DataObjectRef>(); ref.currentName = "TS1";
        QUndoStack undo; QString error;
        QVERIFY(model.applyScalarEdits({"G2"}, {{"Series name", QVariant::fromValue(ref)}}, &undo, &error));
        undo.undo(); QCOMPARE(rainSourceState(engine, "G2"), before);
    }

    void bulkOutfallTypeAndStage_data()
    {
        QTest::addColumn<bool>("si");
        QTest::newRow("US") << false;
        QTest::newRow("SI") << true;
    }

    void bulkOutfallTypeAndStage()
    {
        QFETCH(bool, si);
        auto layer = openBulkNodeLayer(si);
        QVERIFY(layer);
        const auto engine = layer->engine();
        const char *names[] = {"O1", "O2", "O3"};
        QList<QVariantList> before;
        for (auto name : names) { before << outfallState(engine, name); QCOMPARE(before.last().size(), 5); }
        const auto control = outfallState(engine, "OC");
        MapCanvas canvas;
        PropertiesPanel panel;
        panel.setProject(layer.get());
        const QList<SWMMObjectRef> refs{{SWMMObjectRef::Node, "O1"}, {SWMMObjectRef::Node, "O2"}, {SWMMObjectRef::Node, "O3"}};
        panel.showSelection(&canvas, refs);
        panel.resize(700, 900);
        panel.show();
        auto *tree = panel.findChild<QTreeView *>();
        tree->expandAll();
        auto type = bulkProperty(panel, "Boundary Type");
        QVERIFY(type.isValid());
        QCOMPARE(type.data().toString(), QString("Mixed"));
        tree->edit(type);
        auto *combo = qobject_cast<QComboBox *>(tree->indexWidget(type));
        QVERIFY(combo);
        QVERIFY(!(combo->model()->index(combo->findData(3), 0).flags() & Qt::ItemIsEnabled));
        QVERIFY(!(combo->model()->index(combo->findData(4), 0).flags() & Qt::ItemIsEnabled));
        QTest::keyClick(combo, Qt::Key_Escape);
        QCOMPARE(canvas.undoStack()->count(), 0);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        tree->edit(type);
        combo = qobject_cast<QComboBox *>(tree->indexWidget(type));
        QVERIFY(combo);
        combo->showPopup();
        auto fixed = combo->model()->index(combo->findData(2), 0);
        combo->view()->scrollTo(fixed);
        QTest::mouseClick(combo->view()->viewport(), Qt::LeftButton, Qt::NoModifier, combo->view()->visualRect(fixed).center());
        QTRY_COMPARE(canvas.undoStack()->count(), 1);
        for (int i = 0; i < 3; ++i) {
            auto expected = before[i];
            expected[0] = 2;
            if (i != 1) expected[1] = before[i][4]; // Newly FIXED starts at invert, never a table index.
            QCOMPARE(outfallState(engine, names[i]), expected);
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        auto stage = bulkProperty(panel, "Fixed Stage Elevation");
        QTRY_VERIFY(stage.flags() & Qt::ItemIsEditable);
        tree->edit(stage);
        auto *spin = qobject_cast<QDoubleSpinBox *>(tree->indexWidget(stage));
        QVERIFY(spin);
        spin->setFocus();
        spin->selectAll();
        QTest::keyClicks(spin, "92.125");
        QTest::keyClick(spin, Qt::Key_Return);
        QTRY_COMPARE(canvas.undoStack()->count(), 2);
        for (auto name : names) QCOMPARE(outfallState(engine, name)[1].toDouble(), 92.125);
        QCOMPARE(outfallState(engine, "OC"), control);
        canvas.undoStack()->undo();
        canvas.undoStack()->undo();
        for (int i = 0; i < 3; ++i) QCOMPARE(outfallState(engine, names[i]), before[i]);
        canvas.undoStack()->redo();
        canvas.undoStack()->redo();
        panel.showSelection(&canvas, refs);
        QCOMPARE(bulkProperty(panel, "Boundary Type").data().toString(), QString("FIXED"));
        const auto saved = QDir(dataDir()).filePath(si ? "multi_selection/outfalls-edited-SI.inp" : "multi_selection/outfalls-edited-US.inp");
        QCOMPARE(swmm_model_write(engine, saved.toUtf8().constData()), SWMM_OK);
        SWMMModelLayer reopened(saved);
        QList<QString> warnings, errors;
        QVERIFY2(reopened.loadModel(warnings, errors), qPrintable(errors.join(' ')));
        for (auto name : names) QCOMPARE(outfallState(reopened.engine(), name), outfallState(engine, name));
        QCOMPARE(outfallState(reopened.engine(), "OC"), control);
    }

    void bulkStorageShapes_data() { bulkOutfallTypeAndStage_data(); }

    void bulkStorageShapes()
    {
        QFETCH(bool, si);
        auto layer = openBulkNodeLayer(si);
        QVERIFY(layer);
        const auto engine = layer->engine();
        const char *names[] = {"ST1", "ST2", "ST3"};
        QList<QVariantList> before;
        for (auto name : names) { before << storageState(engine, name); QCOMPARE(before.last().size(), 8); }
        const auto control = storageState(engine, "STControl");
        MapCanvas canvas;
        PropertiesPanel panel;
        panel.setProject(layer.get());
        const QList<SWMMObjectRef> refs{{SWMMObjectRef::Node, "ST1"}, {SWMMObjectRef::Node, "ST2"}, {SWMMObjectRef::Node, "ST3"}};
        panel.showSelection(&canvas, refs);
        panel.resize(700, 1000);
        panel.show();
        auto *tree = panel.findChild<QTreeView *>();
        tree->expandAll();
        auto shape = bulkProperty(panel, "Storage Shape");
        QCOMPARE(shape.data().toString(), QString("Mixed"));
        QVERIFY(!(bulkProperty(panel, "Shape Parameter 1").flags() & Qt::ItemIsEditable));
        tree->edit(shape);
        auto *combo = qobject_cast<QComboBox *>(tree->indexWidget(shape));
        QVERIFY(combo);
        QVERIFY(!(combo->model()->index(combo->findData(openswmmvis::kStorageTabularId), 0).flags() & Qt::ItemIsEnabled));
        combo->setCurrentIndex(combo->findData(openswmmvis::kStorageCylindricalId));
        QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, combo->currentIndex()));
        QTRY_COMPARE(canvas.undoStack()->count(), 1);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        auto dimension = bulkProperty(panel, "Shape Parameter 1");
        QTRY_VERIFY(dimension.flags() & Qt::ItemIsEditable);
        QVERIFY(!(bulkProperty(panel, "Functional Coefficient (A)").flags() & Qt::ItemIsEditable));
        tree->edit(dimension);
        auto *spin = qobject_cast<QDoubleSpinBox *>(tree->indexWidget(dimension));
        QVERIFY(spin);
        spin->setFocus();
        spin->selectAll();
        QTest::keyClicks(spin, "8.5");
        QTest::keyClick(spin, Qt::Key_Tab);
        QTRY_COMPARE(canvas.undoStack()->count(), 2);
        for (int i = 0; i < 3; ++i) {
            const auto state = storageState(engine, names[i]);
            QCOMPARE(state[0].toInt(), openswmmvis::kStorageCylindricalId);
            QCOMPARE(state[1].toList(), (i == 2 ? QVariantList{8.5, 4.0, .5} : QVariantList{8.5, 1.0, 1.0}));
            QCOMPARE(state.mid(2), before[i].mid(2));
        }
        QCOMPARE(storageState(engine, "STControl"), control);
        canvas.undoStack()->undo();
        canvas.undoStack()->undo();
        for (int i = 0; i < 3; ++i) QCOMPARE(storageState(engine, names[i]), before[i]);
        canvas.undoStack()->redo();
        canvas.undoStack()->redo();
        panel.showSelection(&canvas, refs);
        QCOMPARE(bulkProperty(panel, "Storage Shape").data().toString(), QString("CYLINDRICAL"));
        const auto saved = QDir(dataDir()).filePath(si ? "multi_selection/storage-edited-SI.inp" : "multi_selection/storage-edited-US.inp");
        QCOMPARE(swmm_model_write(engine, saved.toUtf8().constData()), SWMM_OK);
        SWMMModelLayer reopened(saved);
        QList<QString> warnings, errors;
        QVERIFY2(reopened.loadModel(warnings, errors), qPrintable(errors.join(' ')));
        for (auto name : names) QCOMPARE(storageState(reopened.engine(), name), storageState(engine, name));
        QCOMPARE(storageState(reopened.engine(), "STControl"), control);
    }

    void bulkNodeReferenceUndo_data()
    {
        QTest::addColumn<int>("kind");
        QTest::newRow("tidal") << 0;
        QTest::newRow("stage-series") << 1;
        QTest::newRow("storage-curve") << 2;
    }

    void bulkNodeReferenceUndo()
    {
        QFETCH(int, kind);
        auto layer = openBulkNodeLayer();
        QVERIFY(layer);
        const auto engine = layer->engine();
        const bool storage = kind == 2;
        const auto category = storage ? SWMMModelLayer::CatStorage : SWMMModelLayer::CatOutfalls;
        const char *first = storage ? "ST1" : "O1", *second = storage ? "ST2" : "O3";
        if (kind == 1)
            QCOMPARE(swmm_node_set_outfall_timeseries(engine, swmm_node_index(engine, second), swmm_table_index(engine, "Stage1")), SWMM_OK);
        const auto state = [=](const char *name) { return storage ? storageState(engine, name) : outfallState(engine, name); };
        const auto originalFirst = state(first), originalSecond = state(second);
        const QString label = storage ? "Storage Curve" : kind == 0 ? "Tidal Curve" : "Stage Time Series";
        const QString key = storage ? "Storage curve" : kind == 0 ? "Tidal curve" : "Stage series";
        const QString chosen = storage ? "Area2" : kind == 0 ? "Tide2" : "Stage2";
        MapCanvas canvas;
        PropertiesPanel properties;
        properties.setProject(layer.get());
        properties.showSelection(&canvas, {{SWMMObjectRef::Node, first}, {SWMMObjectRef::Node, second}});
        properties.resize(700, 900);
        properties.show();
        auto *tree = properties.findChild<QTreeView *>();
        tree->expandAll();
        const auto field = bulkProperty(properties, label);
        QVERIFY(field.isValid());
        tree->edit(field);
        auto *picker = qobject_cast<DataObjectPickerEditor *>(tree->indexWidget(field));
        QVERIFY(picker);
        auto *combo = picker->findChild<QComboBox *>();
        QVERIFY(combo && combo->findText(chosen) >= 0);
        combo->setCurrentIndex(combo->findText(chosen));
        QTRY_COMPARE(canvas.undoStack()->count(), 1);
        for (auto name : {first, second}) QCOMPARE(state(name)[1].toString(), chosen);
        auto wrong = picker->value();
        wrong.currentName = "PC"; // Existing PUMP curve, never a storage/tidal/series reference.
        QVERIFY(!tree->model()->setData(field, QVariant::fromValue(wrong)));
        if (!storage) {
            wrong.currentName.clear();
            QVERIFY(!tree->model()->setData(field, QVariant::fromValue(wrong)));
        }
        QCOMPARE(canvas.undoStack()->count(), 1);
        canvas.undoStack()->undo();
        QCOMPARE(state(first), originalFirst);
        QCOMPARE(state(second), originalSecond);
        // The same picker commits a table edit while focused, with complete record Undo.
        SelectionManager selection;
        AttributeTablePanel table;
        table.setProject(layer.get(), &selection, &canvas);
        table.showModelCategory(layer.get(), category);
        table.resize(1000, 600);
        table.show();
        auto *view = table.findChild<QTableView *>();
        QVERIFY(view);
        int row = -1, column = -1;
        for (int r = 0; r < view->model()->rowCount(); ++r)
            if (view->model()->index(r, 0).data().toString() == second) row = r;
        for (int c = 0; c < view->model()->columnCount(); ++c)
            if (view->model()->headerData(c, Qt::Horizontal).toString() == label) column = c;
        const auto cell = view->model()->index(row, column);
        QVERIFY(cell.isValid());
        view->scrollTo(cell);
        view->edit(cell);
        auto *tablePicker = qobject_cast<DataObjectPickerEditor *>(view->indexWidget(cell));
        QVERIFY(tablePicker);
        tablePicker->setFocus();
        tablePicker->applyPickedName(chosen);
        QCOMPARE(state(second)[1].toString(), chosen);
        selection.select({{SWMMObjectRef::Node, first}, {SWMMObjectRef::Node, second}}, SelectionManager::Replace);
        QCoreApplication::processEvents();
        QVERIFY(applyBulkCell(table, view, cell));
        QCOMPARE(canvas.undoStack()->count(), 2);
        for (auto name : {first, second}) QCOMPARE(state(name)[1].toString(), chosen);
        const auto saved = QDir(dataDir()).filePath(QString("multi_selection/node-reference-%1.inp").arg(kind));
        QCOMPARE(swmm_model_write(engine, saved.toUtf8().constData()), SWMM_OK);
        SWMMModelLayer reopened(saved);
        QList<QString> warnings, errors;
        QVERIFY2(reopened.loadModel(warnings, errors), qPrintable(errors.join(' ')));
        for (auto name : {first, second})
            QCOMPARE(storage ? storageState(reopened.engine(), name) : outfallState(reopened.engine(), name), state(name));
        canvas.undoStack()->undo();
        QCOMPARE(state(first), originalFirst);
        canvas.undoStack()->undo();
        QCOMPARE(state(second), originalSecond);
        // A table type choice commits before focus leaves the combo, and Undo restores its reference.
        if (auto *editor = view->indexWidget(cell)) QTest::keyClick(editor, Qt::Key_Escape);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        int typeColumn = -1;
        for (int c = 0; c < view->model()->columnCount(); ++c)
            if (view->model()->headerData(c, Qt::Horizontal).toString() ==
                (storage ? "Storage Shape" : "Boundary Type")) typeColumn = c;
        const auto typeCell = view->model()->index(row, typeColumn);
        QVERIFY(typeCell.isValid());
        view->scrollTo(typeCell);
        view->edit(typeCell);
        auto *typeCombo = qobject_cast<QComboBox *>(view->indexWidget(typeCell));
        QVERIFY(typeCombo);
        typeCombo->showPopup();
        const auto option = typeCombo->model()->index(typeCombo->findData(storage ? openswmmvis::kStorageFunctionalId : 0), 0);
        typeCombo->view()->scrollTo(option);
        QTest::mouseClick(typeCombo->view()->viewport(), Qt::LeftButton, Qt::NoModifier, typeCombo->view()->visualRect(option).center());
        QTRY_COMPARE(state(second)[0].toInt(), storage ? openswmmvis::kStorageFunctionalId : 0);
        QCOMPARE(canvas.undoStack()->count(), 1);
        canvas.undoStack()->undo();
        QCOMPARE(state(second), originalSecond);
        // Detach two different records. Reindex a saved reference, then restore it by name.
        const int firstNode = swmm_node_index(engine, first);
        if (storage) QCOMPARE(swmm_node_set_storage_curve(engine, firstNode, swmm_table_index(engine, "Area2")), SWMM_OK);
        else QCOMPARE(swmm_node_set_outfall_stage(engine, firstNode, 93.25), SWMM_OK);
        const auto beforeFirst = state(first), beforeSecond = state(second);
        SWMMAttributeTableModel model;
        model.setSource(layer.get(), category);
        QUndoStack undo;
        QString error;
        if (storage) {
            auto clear = model.index(model.rowForName(second), colFor(model.columnSpecs(), key)).data(Qt::EditRole).value<DataObjectRef>();
            clear.currentName.clear();
            QUndoStack clearUndo;
            QVERIFY(model.applyScalarEdits({first, second}, {{key, QVariant::fromValue(clear)}}, &clearUndo, &error));
            for (auto name : {first, second}) QCOMPARE(state(name)[0].toInt(), openswmmvis::kStorageFunctionalId);
            clearUndo.undo();
            QCOMPARE(state(first), beforeFirst);
            QCOMPARE(state(second), beforeSecond);
        } else {
            for (int type : {-1, 3, 4, 99})
                QVERIFY(!model.applyScalarEdits({first, second}, {{"Outfall type", type}}, &undo, &error));
            QCOMPARE(undo.count(), 0);
        }
        const QString shapeKey = storage ? "Storage shape" : "Outfall type";
        QVERIFY2(model.applyScalarEdits({first, second}, {{shapeKey, storage ? openswmmvis::kStorageFunctionalId : 0}}, &undo, &error), qPrintable(error));
        QCOMPARE(swmm_table_delete(engine, swmm_table_index(engine, "Unused"), nullptr), SWMM_OK);
        undo.undo();
        QCOMPARE(state(first), beforeFirst);
        QCOMPARE(state(second), beforeSecond);
        undo.redo();
        const auto detachedFirst = state(first), detachedSecond = state(second);
        const char *oldRef = storage ? "Area1" : kind == 0 ? "Tide" : "Stage1";
        QCOMPARE(swmm_table_delete(engine, swmm_table_index(engine, oldRef), nullptr), SWMM_OK);
        undo.undo(); // Preflight every saved reference before touching the first target.
        QCOMPARE(state(first), detachedFirst);
        QCOMPARE(state(second), detachedSecond);
    }

    void bulkStorageRejectsInvalidBatchesAndPreservesCoefficientSiblings()
    {
        auto layer = openBulkNodeLayer();
        QVERIFY(layer);
        const auto engine = layer->engine();
        SWMMAttributeTableModel model;
        model.setSource(layer.get(), SWMMModelLayer::CatStorage);
        QUndoStack undo;
        QString error;
        QVERIFY(model.applyScalarEdits({"ST1", "ST2"}, {{"Storage shape", openswmmvis::kStorageConicalId}}, &undo, &error));
        const auto first = storageState(engine, "ST1"), second = storageState(engine, "ST2");
        const int count = undo.count();
        QVERIFY(!model.applyScalarEdits({"ST1", "ST2"}, {{"Max depth", 21.0}, {"Shape param 1", 0.0}}, &undo, &error));
        QCOMPARE(storageState(engine, "ST1"), first);
        QCOMPARE(storageState(engine, "ST2"), second);
        QCOMPARE(undo.count(), count);
        QVERIFY(!model.applyScalarEdits({"ST1", "ST2"}, {{"Storage shape", openswmmvis::kStorageTabularId}}, &undo, &error));
        QVERIFY(!model.applyScalarEdits({"ST1", "ST2"}, {{"Func coeff A", 5.0}}, &undo, &error));
        QVERIFY(model.applyScalarEdits({"ST1", "ST2"}, {{"Storage shape", openswmmvis::kStorageFunctionalId}}, &undo, &error));
        const auto a = storageState(engine, "ST1"), b = storageState(engine, "ST2");
        QVERIFY(model.applyScalarEdits({"ST1", "ST2"}, {{"Func coeff A", 7.25}, {"Func const C", 40.5}}, &undo, &error));
        for (auto name : {"ST1", "ST2"}) {
            const auto values = storageState(engine, name)[1].toList();
            QCOMPARE(values[0].toDouble(), 7.25);
            QCOMPARE(values[2].toDouble(), 40.5);
        }
        undo.undo();
        QCOMPARE(storageState(engine, "ST1"), a);
        QCOMPARE(storageState(engine, "ST2"), b);
        // Dimension meanings disagree even where the numeric value is unchanged on one row.
        QCOMPARE(swmm_node_set_storage_shape(engine, swmm_node_index(engine, "ST2"), openswmmvis::kStoragePyramidalId), SWMM_OK);
        QCOMPARE(swmm_node_set_storage_shape(engine, swmm_node_index(engine, "ST1"), openswmmvis::kStorageConicalId), SWMM_OK);
        QVERIFY(!model.applyScalarEdits({"ST1", "ST2"}, {{"Shape param 1", 1.0}}, &undo, &error));
    }

    void bulkNodeReferencesKeepCurveAndSeriesNamespacesDistinct()
    {
        auto layer = openBulkNodeLayer();
        QVERIFY(layer);
        const auto engine = layer->engine();
        for (auto name : {"Area2", "Tide2"}) {
            QCOMPARE(swmm_timeseries_add(engine, name), SWMM_OK);
            const int index = swmm_table_index(engine, name); // Unqualified lookup prefers the new series.
            int type = -1;
            QCOMPARE(swmm_table_get_type(engine, index, &type), SWMM_OK);
            QCOMPARE(type, 0);
            double x = 0, y = 0;
            QCOMPARE(swmm_table_get_point(engine, swmm_table_index(engine, "Stage1"), 0, &x, &y), SWMM_OK);
            QCOMPARE(swmm_table_add_point(engine, index, x, 95), SWMM_OK);
            QCOMPARE(swmm_table_add_point(engine, index, x + 1, 96), SWMM_OK);
        }
        SWMMAttributeTableModel storage, outfall;
        storage.setSource(layer.get(), SWMMModelLayer::CatStorage);
        outfall.setSource(layer.get(), SWMMModelLayer::CatOutfalls);
        auto refFor = [](SWMMAttributeTableModel &model, const char *name, const char *key, const char *chosen) {
            auto ref = model.index(model.rowForName(name), colFor(model.columnSpecs(), key)).data(Qt::EditRole).value<DataObjectRef>();
            ref.currentName = chosen;
            return QVariant::fromValue(ref);
        };
        const auto oldStorage = storageState(engine, "ST1"), oldOutfall = outfallState(engine, "O1");
        QUndoStack undo;
        QString error;
        QVERIFY2(storage.applyScalarEdits({"ST1", "ST2"}, {{"Storage curve", refFor(storage, "ST1", "Storage curve", "Area2")}}, &undo, &error), qPrintable(error));
        QVERIFY2(outfall.applyScalarEdits({"O1", "O2"}, {{"Tidal curve", refFor(outfall, "O1", "Tidal curve", "Tide2")}}, &undo, &error), qPrintable(error));
        const int node = swmm_node_index(engine, "O1");
        int index = -1, type = -1;
        QCOMPARE(swmm_node_get_outfall_tidal(engine, node, &index), SWMM_OK);
        QCOMPARE(swmm_table_get_type(engine, index, &type), SWMM_OK);
        QCOMPARE(type, 6);
        QVERIFY(outfall.applyScalarEdits({"O1", "O2"}, {{"Stage series", refFor(outfall, "O1", "Stage series", "Tide2")}}, &undo, &error));
        QCOMPARE(swmm_node_get_outfall_timeseries(engine, node, &index), SWMM_OK);
        QCOMPARE(swmm_table_get_type(engine, index, &type), SWMM_OK);
        QCOMPARE(type, 0);
        undo.undo();
        QCOMPARE(outfallState(engine, "O1")[0].toInt(), 3);
        QCOMPARE(swmm_node_get_outfall_tidal(engine, node, &index), SWMM_OK);
        QCOMPARE(swmm_table_get_type(engine, index, &type), SWMM_OK);
        QCOMPARE(type, 6);
        undo.undo();
        undo.undo();
        QCOMPARE(storageState(engine, "ST1"), oldStorage);
        QCOMPARE(outfallState(engine, "O1"), oldOutfall);
    }

    void bulkCrossSectionsCommitShapeAndDimensions_data()
    {
        QTest::addColumn<bool>("si");
        QTest::newRow("US") << false;
        QTest::newRow("SI") << true;
    }

    void bulkCrossSectionsCommitShapeAndDimensions()
    {
        QFETCH(bool, si);
        auto layer = openBulkNetworkLayer(si);
        QVERIFY(layer);
        const auto engine = layer->engine();
        const char *names[] = {"C1", "X1", "C2"};
        const int shapes[] = {SWMM_XSECT_CIRCULAR, SWMM_XSECT_RECT_CLOSED, SWMM_XSECT_TRAPEZOIDAL};
        QList<QVariantList> before;
        for (int i = 0; i < 3; ++i) {
            const int link = swmm_link_index(engine, names[i]);
            QCOMPARE(swmm_link_set_xsect(engine, link, shapes[i], 2.123456789 + i,
                                        1.25 + i, .5 + i, .75 + i), SWMM_OK);
            QCOMPARE(swmm_link_set_barrels(engine, link, i + 2), SWMM_OK);
            QCOMPARE(swmm_link_set_offset_up(engine, link, .125 + i), SWMM_OK);
            QCOMPARE(swmm_link_set_offset_dn(engine, link, .25 + i), SWMM_OK);
            QCOMPARE(swmm_link_set_culvert_code(engine, link, i + 1), SWMM_OK);
            before.append(conduitSection(engine, names[i]));
            QCOMPARE(before.last().size(), 10);
        }
        QVERIFY(layer->applyLinkAdd("Control", SWMM_LINK_CONDUIT, "J1", "O1", {}));
        const auto control = conduitSection(engine, "Control");
        MapCanvas canvas;
        PropertiesPanel panel;
        panel.setProject(layer.get());
        const QList<SWMMObjectRef> refs{{SWMMObjectRef::Link, "C1"}, {SWMMObjectRef::Link, "X1"},
                                       {SWMMObjectRef::Link, "C2"}};
        panel.showSelection(&canvas, refs);
        panel.resize(700, 900);
        panel.show();
        auto *tree = panel.findChild<QTreeView *>();
        tree->expandAll();
        const auto shape = bulkProperty(panel, "Cross-section Shape");
        QVERIFY(shape.isValid());
        QCOMPARE(shape.data().toString(), QString("Mixed"));
        QVERIFY(!(bulkProperty(panel, "Geom 1").flags() & Qt::ItemIsEditable));
        tree->edit(shape);
        auto *choice = qobject_cast<QComboBox *>(tree->indexWidget(shape));
        QVERIFY(choice);
        QVERIFY(!(choice->model()->index(choice->findData(int(SWMM_XSECT_IRREGULAR)), 0).flags() & Qt::ItemIsEnabled));
        QVERIFY(!(choice->model()->index(choice->findData(int(SWMM_XSECT_CUSTOM)), 0).flags() & Qt::ItemIsEnabled));
        QTest::keyClick(choice, Qt::Key_Escape);
        QCOMPARE(canvas.undoStack()->count(), 0);
        for (int i = 0; i < 3; ++i) QCOMPARE(conduitSection(engine, names[i]), before[i]);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        tree->edit(shape);
        choice = qobject_cast<QComboBox *>(tree->indexWidget(shape));
        QVERIFY(choice);
        choice->showPopup();
        const auto rect = choice->model()->index(choice->findData(int(SWMM_XSECT_RECT_CLOSED)), 0);
        choice->view()->scrollTo(rect);
        QTest::mouseClick(choice->view()->viewport(), Qt::LeftButton, Qt::NoModifier,
                          choice->view()->visualRect(rect).center());
        QTRY_COMPARE(canvas.undoStack()->count(), 1);
        QTRY_VERIFY(bulkProperty(panel, "Geom 1").flags() & Qt::ItemIsEditable);
        for (int i = 0; i < 3; ++i) {
            auto expected = before[i];
            expected[0] = int(SWMM_XSECT_RECT_CLOSED);
            QCOMPARE(conduitSection(engine, names[i]), expected);
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        const auto depth = bulkProperty(panel, "Geom 1");
        tree->edit(depth);
        auto *spin = qobject_cast<QDoubleSpinBox *>(tree->indexWidget(depth));
        QVERIFY(spin);
        spin->setFocus();
        spin->selectAll();
        QTest::keyClicks(spin, "5.125");
        QTest::keyClick(spin, Qt::Key_Return);
        QTRY_COMPARE(canvas.undoStack()->count(), 2);
        for (int i = 0; i < 3; ++i) {
            auto expected = before[i];
            expected[0] = int(SWMM_XSECT_RECT_CLOSED);
            expected[1] = 5.125;
            QCOMPARE(conduitSection(engine, names[i]), expected);
        }
        QCOMPARE(conduitSection(engine, "Control"), control);
        canvas.undoStack()->undo();
        canvas.undoStack()->undo();
        for (int i = 0; i < 3; ++i) QCOMPARE(conduitSection(engine, names[i]), before[i]);
        canvas.undoStack()->redo();
        canvas.undoStack()->redo();
        panel.showSelection(&canvas, refs);
        QCOMPARE(bulkProperty(panel, "Cross-section Shape").data().toString(), QString("RECT_CLOSED"));
        QCOMPARE(bulkProperty(panel, "Geom 1").data(Qt::EditRole).toDouble(), 5.125);
        const auto saved = QDir(dataDir()).filePath(si ? "multi_selection/xsections-SI.inp"
                                                      : "multi_selection/xsections-US.inp");
        QCOMPARE(swmm_model_write(engine, saved.toUtf8().constData()), SWMM_OK);
        SWMMModelLayer reopened(saved);
        QList<QString> warnings, errors;
        QVERIFY2(reopened.loadModel(warnings, errors), qPrintable(errors.join(' ')));
        for (const char *name : names)
            QCOMPARE(conduitSection(reopened.engine(), name), conduitSection(engine, name));
    }

    void bulkCrossSectionTupleAndTableUseOneUndoStep()
    {
        auto layer = openBulkNetworkLayer();
        QVERIFY(layer);
        const auto engine = layer->engine();
        const auto first = conduitSection(engine, "C1"), second = conduitSection(engine, "X1");
        SWMMAttributeTableModel model;
        model.setSource(layer.get(), SWMMModelLayer::CatConduits);
        QUndoStack undo;
        QString error;
        QVERIFY2(model.applyScalarEdits({"C1", "X1"},
                   {{"Shape", int(SWMM_XSECT_RECT_CLOSED)}, {"Geom 1", 3.125}, {"Geom 2", 4.25}},
                   &undo, &error), qPrintable(error));
        QCOMPARE(undo.count(), 1);
        for (const char *name : {"C1", "X1"}) {
            const auto record = conduitSection(engine, name);
            QCOMPARE(record[0].toInt(), int(SWMM_XSECT_RECT_CLOSED));
            QCOMPARE(record[1].toDouble(), 3.125);
            QCOMPARE(record[2].toDouble(), 4.25);
        }
        undo.undo();
        QCOMPARE(conduitSection(engine, "C1"), first);
        QCOMPARE(conduitSection(engine, "X1"), second);
        undo.redo();
        MapCanvas canvas;
        SelectionManager selection;
        AttributeTablePanel panel;
        panel.setProject(layer.get(), &selection, &canvas);
        panel.showModelCategory(layer.get(), SWMMModelLayer::CatConduits);
        auto *view = panel.findChild<QTableView *>();
        QVERIFY(view);
        // Copy the first row's shape through the real table row action.
        QCOMPARE(swmm_link_set_xsect(engine, swmm_link_index(engine, "C1"),
                                    SWMM_XSECT_CIRCULAR, 2, 1.25, 0, 0), SWMM_OK);
        emit layer->attributeChanged("C1");
        selection.select({{SWMMObjectRef::Link, "C1"}, {SWMMObjectRef::Link, "X1"}}, SelectionManager::Replace);
        QCoreApplication::processEvents();
        int column = -1, row = -1;
        for (int c = 0; c < view->model()->columnCount(); ++c)
            if (view->model()->headerData(c, Qt::Horizontal).toString() == "Cross-section Shape") column = c;
        for (int r = 0; r < view->model()->rowCount(); ++r)
            if (view->model()->index(r, 0).data().toString() == "C1") row = r;
        QVERIFY(applyBulkCell(panel, view, view->model()->index(row, column)));
        QCOMPARE(canvas.undoStack()->count(), 1);
        QCOMPARE(conduitSection(engine, "X1")[0].toInt(), int(SWMM_XSECT_CIRCULAR));
        canvas.undoStack()->undo();
        QCOMPARE(conduitSection(engine, "X1")[0].toInt(), int(SWMM_XSECT_RECT_CLOSED));
    }

    void bulkCrossSectionProfileRestoresByName_data()
    {
        QTest::addColumn<bool>("street");
        QTest::newRow("transect") << false;
        QTest::newRow("street") << true;
    }

    void bulkCrossSectionProfileRestoresByName()
    {
        QFETCH(bool, street);
        auto layer = openBulkNetworkLayer();
        QVERIFY(layer);
        const auto engine = layer->engine();
        if (street) {
            QCOMPARE(swmm_street_add(engine, "Profile"), SWMM_OK);
            QCOMPARE(swmm_street_set_params(engine, 0, 5, .5, 2, .013, .1, 1, 1, 0, 0, .013), SWMM_OK);
        } else {
            for (const char *name : {"Unused", "Profile"}) {
                QCOMPARE(swmm_transect_add(engine, name), SWMM_OK);
                const int index = swmm_transect_index(engine, name);
                QCOMPARE(swmm_transect_set_roughness(engine, index, .04, .04, .03), SWMM_OK);
                QCOMPARE(swmm_transect_add_station(engine, index, 0, 10), SWMM_OK);
                QCOMPARE(swmm_transect_add_station(engine, index, 5, 5), SWMM_OK);
                QCOMPARE(swmm_transect_add_station(engine, index, 10, 10), SWMM_OK);
            }
        }
        const int profileShape = street ? SWMM_XSECT_STREET : SWMM_XSECT_IRREGULAR;
        const int profileIndex = street ? 0 : 1;
        QCOMPARE(swmm_link_set_xsect(engine, swmm_link_index(engine, "C1"),
                                    profileShape, profileIndex, 0, 0, 0), SWMM_OK);
        const auto original = conduitSection(engine, "C1"), other = conduitSection(engine, "X1");
        MapCanvas canvas;
        PropertiesPanel properties;
        properties.setProject(layer.get());
        properties.showSelection(&canvas, {{SWMMObjectRef::Link, "C1"}, {SWMMObjectRef::Link, "X1"}});
        properties.resize(700, 900);
        properties.show();
        auto *tree = properties.findChild<QTreeView *>();
        tree->expandAll();
        const auto profileField = bulkProperty(properties, street ? "Street Profile" : "Transect Profile");
        QVERIFY(profileField.isValid());
        QCOMPARE(profileField.data().toString(), QString("Mixed"));
        tree->edit(profileField);
        auto *picker = qobject_cast<DataObjectPickerEditor *>(tree->indexWidget(profileField));
        QVERIFY(picker);
        auto *choices = picker->findChild<QComboBox *>();
        QVERIFY(choices && choices->findText("Profile") >= 0);
        choices->setCurrentIndex(choices->findText("Profile"));
        QTRY_COMPARE(canvas.undoStack()->count(), 1);
        QCOMPARE(conduitSection(engine, "X1")[0].toInt(), profileShape);
        QCOMPARE(conduitSection(engine, "C1"), original);
        auto wrongKind = picker->value();
        wrongKind.kind = street ? DataObjectRef::Transect : DataObjectRef::Street;
        QVERIFY(!tree->model()->setData(profileField, QVariant::fromValue(wrongKind)));
        auto clear = picker->value();
        clear.currentName.clear();
        QVERIFY(!tree->model()->setData(profileField, QVariant::fromValue(clear)));
        QCOMPARE(canvas.undoStack()->count(), 1);
        canvas.undoStack()->undo();
        QCOMPARE(conduitSection(engine, "X1"), other);
        QCOMPARE(conduitSection(engine, "C1"), original);
        // The same picker commits a table cell while it still has focus.
        SelectionManager selection;
        AttributeTablePanel table;
        table.setProject(layer.get(), &selection, &canvas);
        table.showModelCategory(layer.get(), SWMMModelLayer::CatConduits);
        table.resize(1000, 600);
        table.show();
        auto *view = table.findChild<QTableView *>();
        QVERIFY(view);
        int row = -1, column = -1;
        for (int r = 0; r < view->model()->rowCount(); ++r)
            if (view->model()->index(r, 0).data().toString() == "X1") row = r;
        for (int c = 0; c < view->model()->columnCount(); ++c)
            if (view->model()->headerData(c, Qt::Horizontal).toString() ==
                (street ? "Street Profile" : "Transect Profile")) column = c;
        const auto cell = view->model()->index(row, column);
        QVERIFY(cell.isValid());
        view->scrollTo(cell);
        view->edit(cell);
        auto *tablePicker = qobject_cast<DataObjectPickerEditor *>(view->indexWidget(cell));
        QVERIFY(tablePicker);
        tablePicker->setFocus();
        tablePicker->applyPickedName("Profile");
        QCOMPARE(conduitSection(engine, "X1")[0].toInt(), profileShape);
        const auto saved = QDir(dataDir()).filePath(street ? "multi_selection/xsections-street.inp"
                                                          : "multi_selection/xsections-transect.inp");
        QCOMPARE(swmm_model_write(engine, saved.toUtf8().constData()), SWMM_OK);
        SWMMModelLayer reopened(saved);
        QList<QString> warnings, errors;
        QVERIFY2(reopened.loadModel(warnings, errors), qPrintable(errors.join(' ')));
        for (const char *name : {"C1", "X1"}) {
            const auto section = conduitSection(reopened.engine(), name);
            QCOMPARE(section[0].toInt(), profileShape);
            const int profile = int(section[1].toDouble());
            const char *id = street ? swmm_street_id(reopened.engine(), profile)
                                    : swmm_transect_id(reopened.engine(), profile);
            QVERIFY(id);
            QCOMPARE(QString::fromUtf8(id), QString("Profile"));
        }
        canvas.undoStack()->undo();
        QCOMPARE(conduitSection(engine, "X1"), other);
        SWMMAttributeTableModel model;
        model.setSource(layer.get(), SWMMModelLayer::CatConduits);
        QUndoStack undo;
        QString error;
        QVERIFY2(model.applyScalarEdits({"C1", "X1"}, {{"Shape", int(SWMM_XSECT_RECT_CLOSED)}},
                                       &undo, &error), qPrintable(error));
        QVERIFY(conduitSection(engine, "C1")[1].toDouble() > 0); // Physical depth, not an index.
        if (!street) QCOMPARE(swmm_transect_remove(engine, 0), SWMM_OK); // Profile moves from index 1 to 0.
        undo.undo();
        auto expected = original;
        expected[1] = 0.0;
        QCOMPARE(conduitSection(engine, "C1"), expected);
        QCOMPARE(conduitSection(engine, "X1"), other);
        undo.redo();
        QCOMPARE(conduitSection(engine, "C1")[0].toInt(), int(SWMM_XSECT_RECT_CLOSED));
        if (!street) {
            QCOMPARE(swmm_transect_remove(engine, 0), SWMM_OK);
            const auto beforeUndo = conduitSection(engine, "C1");
            undo.undo(); // A deleted profile cannot redirect the saved reference.
            QCOMPARE(conduitSection(engine, "C1"), beforeUndo);
            QCOMPARE(conduitSection(engine, "X1")[0].toInt(), int(SWMM_XSECT_RECT_CLOSED));
        }
    }

    void bulkCrossSectionInvalidShapeLeavesEveryTargetUnchanged()
    {
        auto layer = openBulkNetworkLayer();
        QVERIFY(layer);
        SWMMAttributeTableModel model;
        model.setSource(layer.get(), SWMMModelLayer::CatConduits);
        const auto first = conduitSection(layer->engine(), "C1"), second = conduitSection(layer->engine(), "X1");
        QUndoStack undo;
        QString error;
        for (int shape : {-1, 1000, int(SWMM_XSECT_IRREGULAR), int(SWMM_XSECT_STREET), int(SWMM_XSECT_CUSTOM)})
            QVERIFY(!model.applyScalarEdits({"C1", "X1"}, {{"Shape", shape}}, &undo, &error));
        QVERIFY(!model.applyScalarEdits({"C1", "X1"}, {{"Geom 2", -1.0}}, &undo, &error));
        QCOMPARE(undo.count(), 0);
        QCOMPARE(conduitSection(layer->engine(), "C1"), first);
        QCOMPARE(conduitSection(layer->engine(), "X1"), second);
        QVERIFY(model.applyScalarEdits({"C1", "X1"}, {{"Shape", int(SWMM_XSECT_CIRCULAR)}}, &undo, &error));
        QCOMPARE(undo.count(), 0);
    }

    void bulkCrossSectionFilledOffsetsSurviveUndo()
    {
        auto layer = openBulkNetworkLayer();
        QVERIFY(layer);
        const auto engine = layer->engine();
        const int first = swmm_link_index(engine, "C1");
        QCOMPARE(swmm_link_set_xsect(engine, first, SWMM_XSECT_FILLED_CIRCULAR, 3, .5, .25, .125), SWMM_OK);
        QCOMPARE(swmm_link_set_offset_up(engine, first, .25), SWMM_OK);
        QCOMPARE(swmm_link_set_offset_dn(engine, first, .375), SWMM_OK);
        const auto original = conduitSection(engine, "C1");
        SWMMAttributeTableModel model;
        model.setSource(layer.get(), SWMMModelLayer::CatConduits);
        QUndoStack undo;
        QString error;
        QVERIFY2(model.applyScalarEdits({"C1", "X1"}, {{"Shape", int(SWMM_XSECT_RECT_CLOSED)}},
                                       &undo, &error), qPrintable(error));
        QCOMPARE(conduitSection(engine, "C1")[6], original[6]);
        QCOMPARE(conduitSection(engine, "C1")[7], original[7]);
        undo.undo();
        QCOMPARE(conduitSection(engine, "C1"), original);
        undo.redo();
        QCOMPARE(conduitSection(engine, "C1")[6], original[6]);
        QCOMPARE(conduitSection(engine, "C1")[7], original[7]);
    }

    void bulkCrossSectionCustomProfileStaysProtected()
    {
        auto base = openBulkNetworkLayer();
        QVERIFY(base);
        QFile input(base->modelFilePath());
        QVERIFY(input.open(QIODevice::ReadOnly));
        auto text = input.readAll();
        const int start = text.indexOf("[XSECTIONS]");
        const int end = text.indexOf("[REPORT]", start);
        QVERIFY(start >= 0 && end > start);
        text.replace(start, end - start,
                     "[XSECTIONS]\nC1 CUSTOM 2 CustomProfile 0 0 1\n"
                     "X1 CIRCULAR 1 0 0 0 1\nC2 CIRCULAR 1 0 0 0 1\n");
        text += "\n[CURVES]\nCustomProfile SHAPE 0 0\nCustomProfile .5 1\nCustomProfile 1 0\n";
        const auto path = QDir(dataDir()).filePath("multi_selection/xsections-custom.inp");
        QFile output(path);
        QVERIFY(output.open(QIODevice::WriteOnly | QIODevice::Truncate));
        output.write(text);
        output.close();
        SWMMModelLayer layer(path);
        QList<QString> warnings, errors;
        QVERIFY2(layer.loadModel(warnings, errors), qPrintable(errors.join(' ')));
        MapCanvas canvas;
        PropertiesPanel panel;
        panel.setProject(&layer);
        panel.showSelection(&canvas, {{SWMMObjectRef::Link, "C1"}, {SWMMObjectRef::Link, "X1"}});
        const auto first = conduitSection(layer.engine(), "C1"), second = conduitSection(layer.engine(), "X1");
        QCOMPARE(first[0].toInt(), int(SWMM_XSECT_CUSTOM));
        auto shape = bulkProperty(panel, "Cross-section Shape");
        QVERIFY(shape.isValid() && !(shape.flags() & Qt::ItemIsEditable));
        QVERIFY(!panel.findChild<QTreeView *>()->model()->setData(shape, int(SWMM_XSECT_CIRCULAR)));
        QCOMPARE(conduitSection(layer.engine(), "C1"), first);
        QCOMPARE(conduitSection(layer.engine(), "X1"), second);
        QCOMPARE(canvas.undoStack()->count(), 0);
        // Barrels remain editable without rebuilding CUSTOM's resolved shape curve.
        const int link = swmm_link_index(layer.engine(), "C1");
        SWMM_XSect section = nullptr;
        QCOMPARE(swmm_link_create_xsect(layer.engine(), link, &section), SWMM_OK);
        double depth = 0, area = 0;
        QCOMPARE(swmm_xsect_full_properties(section, &depth, &area, nullptr, nullptr, nullptr, nullptr), SWMM_OK);
        QCOMPARE(swmm_xsect_free(section), SWMM_OK);
        auto barrels = bulkProperty(panel, "Barrels");
        QVERIFY(barrels.isValid() && (barrels.flags() & Qt::ItemIsEditable));
        QVERIFY(panel.findChild<QTreeView *>()->model()->setData(barrels, 3));
        QCOMPARE(canvas.undoStack()->count(), 1);
        for (bool restore : {false, true}) {
            if (restore) canvas.undoStack()->undo();
            QCOMPARE(conduitSection(layer.engine(), "C1")[5].toInt(), restore ? first[5].toInt() : 3);
            QCOMPARE(swmm_link_create_xsect(layer.engine(), link, &section), SWMM_OK);
            double actualDepth = 0, actualArea = 0;
            QCOMPARE(swmm_xsect_full_properties(section, &actualDepth, &actualArea, nullptr, nullptr, nullptr, nullptr), SWMM_OK);
            QCOMPARE(swmm_xsect_free(section), SWMM_OK);
            QCOMPARE(actualDepth, depth);
            QCOMPARE(actualArea, area);
        }
    }

    void mixedLinkPropertiesCommitImmediatelyAndOfferSubtypeScope()
    {
        auto layer = openBulkNetworkLayer();
        QVERIFY(layer);
        MapCanvas canvas;
        PropertiesPanel panel;
        panel.setProject(layer.get());
        const QList<SWMMObjectRef> refs{{SWMMObjectRef::Link, "C1"}, {SWMMObjectRef::Link, "X1"},
                                       {SWMMObjectRef::Link, "C2"}, {SWMMObjectRef::Link, "P1"}};
        panel.showSelection(&canvas, refs);
        QVERIFY(panel.windowTitle().contains("Links"));
        auto *tree = panel.findChild<QTreeView *>();
        panel.resize(700, 800);
        panel.show();
        tree->expandAll();
        auto tag = bulkProperty(panel, "Tag");
        QVERIFY(tag.isValid() && (tag.flags() & Qt::ItemIsEditable));
        auto roughness = bulkProperty(panel, "Manning's n");
        QVERIFY(roughness.isValid() && !(roughness.flags() & Qt::ItemIsEditable));
        tree->edit(tag);
        auto *line = qobject_cast<QLineEdit *>(tree->indexWidget(tag));
        QVERIFY(line);
        QTest::keyClicks(line, "shared");
        QTest::keyClick(line, Qt::Key_Return);
        QTRY_COMPARE(canvas.undoStack()->count(), 1);
        for (const auto &name : {"C1", "X1", "C2"})
            QCOMPARE(bulkLinkTag(layer.get(), SWMMModelLayer::CatConduits, name), QString("shared"));
        QCOMPARE(bulkLinkTag(layer.get(), SWMMModelLayer::CatPumps, "P1"), QString("shared"));
        QCOMPARE(bulkLinkTag(layer.get(), SWMMModelLayer::CatPumps, "P2"), QString());
        canvas.undoStack()->undo();
        QCOMPARE(bulkLinkTag(layer.get(), SWMMModelLayer::CatPumps, "P1"), QString());
        canvas.undoStack()->redo();
        auto *scope = panel.findChild<QComboBox *>("propertiesSelectionScope");
        QVERIFY(scope);
        const int conduits = scope->findText("Conduits (3)");
        QVERIFY(conduits > 0);
        scope->setCurrentIndex(conduits);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        roughness = bulkProperty(panel, "Manning's n");
        QVERIFY(roughness.flags() & Qt::ItemIsEditable);
        tree->edit(roughness);
        auto *spin = qobject_cast<QDoubleSpinBox *>(tree->indexWidget(roughness));
        QVERIFY(spin);
        spin->selectAll();
        QTest::keyClicks(spin, "0.025");
        QTest::keyClick(spin, Qt::Key_Tab);
        QTRY_COMPARE(canvas.undoStack()->count(), 2);
        for (const auto &name : {"C1", "X1", "C2"}) {
            double value = 0;
            QCOMPARE(swmm_link_get_roughness(layer->engine(), swmm_link_index(layer->engine(), name), &value), 0);
            QCOMPARE(value, .025);
        }
        panel.showSelection(&canvas, refs);
        QCOMPARE(bulkProperty(panel, "Tag").data().toString(), QString("shared"));
        const auto saved = QDir(dataDir()).filePath("multi_selection/network-bulk-saved.inp");
        QCOMPARE(swmm_model_write(layer->engine(), saved.toUtf8().constData()), SWMM_OK);
        SWMMModelLayer reopened(saved);
        QList<QString> warnings, errors;
        QVERIFY(reopened.loadModel(warnings, errors));
        QCOMPARE(bulkLinkTag(&reopened, SWMMModelLayer::CatPumps, "P1"), QString("shared"));
        double value = 0;
        QCOMPARE(swmm_link_get_roughness(reopened.engine(), swmm_link_index(reopened.engine(), "C2"), &value), 0);
        QCOMPARE(value, .025);
    }

    void mixedNodesUseCommonFieldsAndKeepTypedIdentity()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        MapCanvas canvas;
        PropertiesPanel panel;
        panel.setProject(layer.get());
        panel.showSelection(&canvas, {{SWMMObjectRef::Node, "J1"}, {SWMMObjectRef::Node, "O1"}});
        auto *tree = panel.findChild<QTreeView *>();
        const auto invert = bulkProperty(panel, "Invert Elevation (ft)");
        QVERIFY(invert.isValid() && (invert.flags() & Qt::ItemIsEditable));
        QVERIFY(tree->model()->setData(invert, 85.0));
        QCOMPARE(canvas.undoStack()->count(), 1);
        for (const char *name : {"J1", "O1"}) {
            double value = 0;
            QCOMPARE(swmm_node_get_invert_elev(layer->engine(), swmm_node_index(layer->engine(), name), &value), 0);
            QCOMPARE(value, 85.0);
        }
        canvas.undoStack()->undo();
        double value = 0;
        QCOMPARE(swmm_node_get_invert_elev(layer->engine(), swmm_node_index(layer->engine(), "J1"), &value), 0);
        QCOMPARE(value, 100.0);
        QCOMPARE(swmm_node_get_invert_elev(layer->engine(), swmm_node_index(layer->engine(), "O1"), &value), 0);
        QCOMPARE(value, 90.0);
    }

    void bulkEndpointPickerChecksEveryLinkAndUpdatesGeometry()
    {
        auto layer = openBulkNetworkLayer();
        QVERIFY(layer);
        MapCanvas canvas;
        PropertiesPanel panel;
        panel.setProject(layer.get());
        panel.showSelection(&canvas, {{SWMMObjectRef::Link, "C1"}, {SWMMObjectRef::Link, "X1"}});
        panel.resize(700, 800);
        panel.show();
        auto *tree = panel.findChild<QTreeView *>();
        tree->expandAll();
        const auto from = bulkProperty(panel, "From Node");
        QVERIFY(from.isValid());
        QVERIFY(!tree->model()->setData(from, "O1")); // The second link would connect to itself.
        QCOMPARE(canvas.undoStack()->count(), 0);
        tree->edit(from);
        auto *picker = qobject_cast<LinkEndpointComboBox *>(tree->indexWidget(from));
        QVERIFY(picker);
        QVERIFY(picker->findText("O1") < 0);
        QVERIFY(picker->findText("X1") < 0);
        QVERIFY(picker->findText("J2") >= 0);
        picker->setFocus();
        picker->lineEdit()->selectAll();
        QTest::keyClicks(picker->lineEdit(), "J2");
        QTest::keyClick(picker->lineEdit(), Qt::Key_Return);
        QTRY_COMPARE(canvas.undoStack()->count(), 1);
        for (const char *name : {"C1", "X1"}) {
            const int link = swmm_link_index(layer->engine(), name);
            int node = -1;
            QCOMPARE(swmm_link_get_from_node(layer->engine(), link, &node), 0);
            QCOMPARE(QString::fromUtf8(swmm_node_id(layer->engine(), node)), QString("J2"));
            QCOMPARE(layer->cachedLinkPolyline(link).first(), QPointF(500, 500));
        }
        canvas.undoStack()->undo();
        int node = -1;
        QCOMPARE(swmm_link_get_from_node(layer->engine(), swmm_link_index(layer->engine(), "C1"), &node), 0);
        QCOMPARE(QString::fromUtf8(swmm_node_id(layer->engine(), node)), QString("J1"));
        canvas.undoStack()->redo();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        tree->edit(from);
        picker = qobject_cast<LinkEndpointComboBox *>(tree->indexWidget(from));
        QVERIFY(picker);
        picker->setFocus();
        picker->lineEdit()->selectAll();
        QTest::keyClicks(picker->lineEdit(), "J3");
        QTest::keyClick(picker->lineEdit(), Qt::Key_Escape);
        QCoreApplication::processEvents();
        QCOMPARE(swmm_link_get_from_node(layer->engine(), swmm_link_index(layer->engine(), "C1"), &node), 0);
        QCOMPARE(QString::fromUtf8(swmm_node_id(layer->engine(), node)), QString("J2"));
        QCOMPARE(canvas.undoStack()->count(), 1);
    }

    void bulkPumpCurvePickerCommitsOnChoiceAndChecksReferenceKind()
    {
        auto layer = openBulkNetworkLayer();
        QVERIFY(layer);
        MapCanvas canvas;
        PropertiesPanel panel;
        panel.setProject(layer.get());
        panel.showSelection(&canvas, {{SWMMObjectRef::Link, "P1"}, {SWMMObjectRef::Link, "P2"}});
        panel.resize(700, 800);
        panel.show();
        auto *tree = panel.findChild<QTreeView *>();
        tree->expandAll();
        const auto curve = bulkProperty(panel, "Pump Curve");
        QVERIFY(curve.isValid());
        QCOMPARE(curve.data().toString(), QString("Mixed"));
        tree->edit(curve);
        auto *picker = qobject_cast<DataObjectPickerEditor *>(tree->indexWidget(curve));
        QVERIFY(picker);
        picker->applyPickedName("PC3");
        QTRY_COMPARE(canvas.undoStack()->count(), 1);
        picker->applyPickedName("PC3"); // Re-choosing the stored value has no undo entry.
        QCOMPARE(canvas.undoStack()->count(), 1);
        for (const char *name : {"P1", "P2"}) {
            int table = -1;
            QCOMPARE(swmm_link_get_pump_curve(layer->engine(), swmm_link_index(layer->engine(), name), &table), 0);
            QCOMPARE(QString::fromUtf8(swmm_table_id(layer->engine(), table)), QString("PC3"));
        }
        auto invalid = picker->value();
        invalid.currentName = "TS1";
        QVERIFY(!tree->model()->setData(curve, QVariant::fromValue(invalid)));
        QCOMPARE(canvas.undoStack()->count(), 1);
        canvas.undoStack()->undo();
        QCOMPARE(curve.data().toString(), QString("Mixed"));
        int table = -1;
        QCOMPARE(swmm_link_get_pump_curve(layer->engine(), swmm_link_index(layer->engine(), "P1"), &table), 0);
        QCOMPARE(QString::fromUtf8(swmm_table_id(layer->engine(), table)), QString("PC"));
        QCOMPARE(swmm_link_get_pump_curve(layer->engine(), swmm_link_index(layer->engine(), "P2"), &table), 0);
        QCOMPARE(QString::fromUtf8(swmm_table_id(layer->engine(), table)), QString("PC2"));
    }

    void bulkUndoFollowsRenameAndRejectsReusedTargetId()
    {
        auto layer = openBulkNetworkLayer();
        QVERIFY(layer);
        MapCanvas canvas;
        PropertiesPanel panel;
        panel.setProject(layer.get());
        panel.showSelection(&canvas, {{SWMMObjectRef::Link, "C1"}, {SWMMObjectRef::Link, "P1"}});
        auto *tree = panel.findChild<QTreeView *>();
        QVERIFY(tree->model()->setData(bulkProperty(panel, "Tag"), "batch"));
        QVERIFY(layer->applyRename("C1", "Renamed", SWMMModelLayer::kKindLink));
        canvas.undoStack()->undo();
        QCOMPARE(bulkLinkTag(layer.get(), SWMMModelLayer::CatConduits, "Renamed"), QString());
        canvas.undoStack()->redo();
        QVERIFY(layer->applyLinkDelete("P1"));
        QVERIFY(layer->applyLinkAdd("P1", SWMM_LINK_PUMP, "J1", "O1", {}));
        canvas.undoStack()->undo();
        QCOMPARE(bulkLinkTag(layer.get(), SWMMModelLayer::CatPumps, "P1"), QString());
        QCOMPARE(bulkLinkTag(layer.get(), SWMMModelLayer::CatConduits, "Renamed"), QString("batch"));
        QVERIFY(!tree->model()->setData(bulkProperty(panel, "Tag"), "stale"));
    }

    void bulkOutfallRoutingUsesTheSubcatchmentNamespace()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        QVERIFY(layer->applyNodeAdd("O2", SWMM_NODE_OUTFALL, 2000, 500));
        MapCanvas canvas;
        PropertiesPanel panel;
        panel.setProject(layer.get());
        panel.showSelection(&canvas, {{SWMMObjectRef::Node, "O1"}, {SWMMObjectRef::Node, "O2"}});
        auto *aggregate = panel.findChild<SWMMAggregateSelection *>();
        QVERIFY(aggregate);
        const int col = colFor(aggregate->source()->columnSpecs(), "Route to");
        auto ref = aggregate->editorData(col).value<DataObjectRef>();
        QCOMPARE(ref.kind, DataObjectRef::Subcatchment);
        ref.currentName = "S1"; // A rain gage also has this ID.
        auto *tree = panel.findChild<QTreeView *>();
        const auto route = bulkProperty(panel, "Route To Subcatchment");
        QVERIFY(route.isValid());
        QVERIFY(tree->model()->setData(route, QVariant::fromValue(ref)));
        QCOMPARE(canvas.undoStack()->count(), 1);
        for (const char *name : {"O1", "O2"}) {
            int target = -1;
            QCOMPARE(swmm_node_get_outfall_route_to(layer->engine(), swmm_node_index(layer->engine(), name), &target), 0);
            QCOMPARE(QString::fromUtf8(swmm_subcatch_id(layer->engine(), target)), QString("S1"));
        }
        ref.kind = DataObjectRef::RainGage;
        QVERIFY(!tree->model()->setData(route, QVariant::fromValue(ref)));
        QCOMPARE(canvas.undoStack()->count(), 1);
        canvas.undoStack()->undo();
        for (const char *name : {"O1", "O2"}) {
            int target = -2;
            QCOMPARE(swmm_node_get_outfall_route_to(layer->engine(), swmm_node_index(layer->engine(), name), &target), 0);
            QCOMPARE(target, -1);
        }
    }

    void mixedVirtualNodeDepthDoesNotCombineDifferentMeanings()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        int node = -1, link = -1;
        QString error;
        QVERIFY2(layer->applyInsertVirtualJunction("C1", .5, "VBulk", "CSplit", &node, &link, &error),
                 qPrintable(error));
        MapCanvas canvas;
        PropertiesPanel panel;
        panel.setProject(layer.get());
        panel.showSelection(&canvas, {{SWMMObjectRef::Node, "J1"}, {SWMMObjectRef::Node, "VBulk"}});
        const auto depth = bulkProperty(panel, "Maximum Depth (ft)");
        QVERIFY(depth.isValid());
        QVERIFY(!(depth.flags() & Qt::ItemIsEditable));
        QVERIFY(depth.data(Qt::ToolTipRole).toString().contains("different meanings"));
        QVERIFY(bulkProperty(panel, "Tag").flags() & Qt::ItemIsEditable);
    }

    void propertiesEditsSelectedNodesAndLinks()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        MapCanvas canvas;
        PropertiesPanel panel;
        panel.setProject(layer.get());
        panel.showSelection(&canvas, {{SWMMObjectRef::Node, "J1"}, {SWMMObjectRef::Node, "X1"}});
        auto *aggregate = panel.findChild<SWMMAggregateSelection *>();
        QVERIFY(aggregate);
        auto *source = aggregate->source();
        const int depth = colFor(source->columnSpecs(), "Max depth");
        QVERIFY(depth >= 0);
        auto *tree = panel.findChild<QTreeView *>();
        QVERIFY(tree);
        const auto property = [tree](const QString &label) {
            auto *model = tree->model();
            for (int g = 0; g < model->rowCount(); ++g) {
                const auto group = model->index(g, 0);
                for (int r = 0; r < model->rowCount(group); ++r)
                    if (model->index(r, 0, group).data().toString() == label)
                        return model->index(r, 1, group);
            }
            return QModelIndex();
        };
        QVERIFY(!panel.findChild<QPushButton *>("meshAggregateApply"));
        QVERIFY(tree->model()->setData(property(source->headerData(depth, Qt::Horizontal).toString()), 12.5));
        QCOMPARE(canvas.undoStack()->count(), 1);
        for (const char *name : {"J1", "X1"}) {
            double value = 0;
            QCOMPARE(swmm_node_get_max_depth(layer->engine(), swmm_node_index(layer->engine(), name), &value), 0);
            QCOMPARE(value, 12.5);
        }
        // X1 exists in both namespaces. Resolve it using the selected family.
        panel.showSelection(&canvas, {{SWMMObjectRef::Link, "C1"}, {SWMMObjectRef::Link, "X1"}});
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        aggregate = panel.findChild<SWMMAggregateSelection *>();
        QVERIFY(aggregate);
        const int roughness = colFor(aggregate->source()->columnSpecs(), "Roughness");
        QVERIFY(roughness >= 0);
        QVERIFY(tree->model()->setData(property(aggregate->source()->headerData(roughness, Qt::Horizontal).toString()), .02));
        QCOMPARE(canvas.undoStack()->count(), 2);
        for (const char *name : {"C1", "X1"}) {
            double value = 0;
            QCOMPARE(swmm_link_get_roughness(layer->engine(), swmm_link_index(layer->engine(), name), &value), 0);
            QCOMPARE(value, 0.02);
        }
        // Replay after the original Properties model is destroyed and its family changes.
        panel.clear();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        canvas.undoStack()->undo();
        double value = 0;
        QCOMPARE(swmm_link_get_roughness(layer->engine(), swmm_link_index(layer->engine(), "X1"), &value), 0);
        QCOMPARE(value, 0.013);
        canvas.undoStack()->undo();
        QCOMPARE(swmm_node_get_max_depth(layer->engine(), swmm_node_index(layer->engine(), "X1"), &value), 0);
        QCOMPARE(value, 10.0);
        canvas.undoStack()->redo();
        canvas.undoStack()->redo();

        const QString outDir = QDir(dataDir()).filePath("multi_selection");
        QVERIFY(QDir().mkpath(outDir));
        const auto saved = QDir(outDir).filePath("network_batch.inp").toUtf8();
        QCOMPARE(swmm_model_write(layer->engine(), saved.constData()), 0);
        SWMM_Engine reopened = swmm_engine_create();
        QCOMPARE(swmm_engine_open(reopened, saved.constData(), nullptr, nullptr, nullptr), 0);
        QCOMPARE(swmm_link_get_roughness(reopened, swmm_link_index(reopened, "X1"), &value), 0);
        QCOMPARE(value, 0.02);
        QCOMPARE(swmm_node_get_max_depth(reopened, swmm_node_index(reopened, "X1"), &value), 0);
        QCOMPARE(value, 12.5);
        swmm_engine_destroy(reopened);
    }

    void rejectedBatchPreservesRedoAndTableUndoIdentity()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        QUndoStack undo;
        SWMMAttributeTableModel table;
        table.setSource(layer.get(), SWMMModelLayer::CatConduits);
        table.setUndoStack(&undo);
        const int length = colFor(table.columnSpecs(), "Length");
        const int row = table.rowForName("C1");
        QVERIFY(table.setData(table.index(row, length), 987.0));
        table.setSource(layer.get(), SWMMModelLayer::CatJunctions);
        undo.undo();
        double value = 0;
        QCOMPARE(swmm_link_get_length(layer->engine(), swmm_link_index(layer->engine(), "C1"), &value), 0);
        QCOMPARE(value, 1000.0);
        QVERIFY(undo.canRedo());
        QString error;
        QVERIFY(!table.applyScalarEdits({"J1", "X1"}, {{"Max depth", -1.0}}, &undo, &error));
        QVERIFY(undo.canRedo());
        QCOMPARE(undo.index(), 0);
        undo.redo();
        QCOMPARE(swmm_link_get_length(layer->engine(), swmm_link_index(layer->engine(), "C1"), &value), 0);
        QCOMPARE(value, 987.0);
    }

    void junctionStorageTableEditUndoAndSave()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMMAttributeTableModel model;
        QUndoStack undo;
        model.setUndoStack(&undo);
        model.setSource(layer.get(), SWMMModelLayer::CatJunctions);
        const int col = colFor(model.columnSpecs(), QStringLiteral("Storage area"));
        QVERIFY(col >= 0);
        const int ni = layer->soaIndexAt(SWMMModelLayer::CatJunctions, 0);
        const QModelIndex cell = model.index(0, col);
        QVERIFY(model.setData(cell, 0.125, Qt::EditRole));
        double area = 0;
        QCOMPARE(swmm_node_get_storage_area(layer->engine(), ni, &area), 0);
        QCOMPARE(area, 0.125);
        undo.undo();
        QCOMPARE(swmm_node_get_storage_area(layer->engine(), ni, &area), 0);
        QCOMPARE(area, 0.0);
        undo.redo();
        QCOMPARE(swmm_node_get_storage_area(layer->engine(), ni, &area), 0);
        QCOMPARE(area, 0.125);
        const QString outDir = QDir(dataDir()).filePath(QStringLiteral("junction_storage_issue10"));
        QVERIFY(QDir().mkpath(outDir));
        const QByteArray saved = QDir(outDir).filePath(QStringLiteral("table-edited.inp")).toUtf8();
        QCOMPARE(swmm_model_write(layer->engine(), saved.constData()), 0);
        SWMM_Engine reopened = swmm_engine_create();
        QCOMPARE(swmm_engine_open(reopened, saved.constData(), nullptr, nullptr, nullptr), 0);
        const QByteArray name = model.objectNameAt(0).toUtf8();
        QCOMPARE(swmm_node_get_storage_area(reopened, swmm_node_index(reopened, name.constData()), &area), 0);
        QCOMPARE(area, 0.125);
        swmm_engine_destroy(reopened);
    }

    void meshBulkApplyRefreshesOnce_data()
    {
        QTest::addColumn<QString>("kind");
        QTest::addColumn<QString>("key");
        QTest::addColumn<double>("value");
        QTest::newRow("cells") << QString("c") << QString("mannings") << 0.04;
        QTest::newRow("edges") << QString("e") << QString("conveyance") << 0.5;
        QTest::newRow("vertices") << QString("v") << QString("z") << 42.0;
    }

    void meshBulkApplyRefreshesOnce()
    {
        QFETCH(QString, kind);
        QFETCH(QString, key);
        QFETCH(double, value);
        auto layer = openLayer();
        QVERIFY(layer);
        mesh::MeshResult meshData;
        for (int i = 0; i < 512; ++i) {
            const int first = meshData.vertices.size();
            for (const auto &xy : {QPointF(i * 2, 0), QPointF(i * 2 + 1, 0), QPointF(i * 2, 1)}) {
                mesh::MeshVertex vertex;
                vertex.xy = xy;
                vertex.z = 0.0;
                meshData.vertices.append(vertex);
            }
            mesh::MeshTriangle triangle;
            triangle.v0 = first;
            triangle.v1 = first + 1;
            triangle.v2 = first + 2;
            triangle.mannings = 0.02;
            meshData.triangles.append(triangle);
        }
        meshData.ok = true;
        SWMM2DMeshLayer meshLayer(meshData, QStringLiteral("bulk-mesh.2dm"));
        MapCanvas canvas;
        canvas.addLayer(&meshLayer, false);
        meshLayer.setActiveMesh(true);
        SelectionManager selection;
        MeshEditingToolbar toolbar(QStringLiteral("Mesh"));
        toolbar.rebindCanvas(&canvas);
        toolbar.rebindSelectionManager(&selection);
        AttributeTablePanel panel;
        panel.setProject(layer.get(), &selection, &canvas);
        panel.refresh();
        auto *combo = panel.findChild<QComboBox *>();
        auto *view = panel.findChild<QTableView *>();
        auto *model = panel.findChild<MeshAttributeTableModel *>();
        QVERIFY(combo && view && model);
        const auto sourceKey = QString("mesh:%1:%2").arg(meshLayer.layerId(), kind);
        const int category = combo->findData(sourceKey);
        QVERIFY(category >= 0);
        combo->setCurrentIndex(category);
        const int column = colFor(model->columnSpecs(), qPrintable(key));
        QVERIFY(column >= 0);
        const auto clickedSource = model->index(model->rowCount() - 1, column);
        const double original = model->index(0, column).data(Qt::EditRole).toDouble();
        QVERIFY(model->setData(clickedSource, value));
        canvas.undoStack()->clear();
        QSet<SWMMObjectRef> selected;
        for (int row = 0; row < model->rowCount(); ++row)
            selected.insert(model->refForRow(row));
        selection.select(selected, SelectionManager::Replace);
        QCOMPARE(view->selectionModel()->selectedRows().size(), model->rowCount());
        auto *proxy = qobject_cast<QSortFilterProxyModel *>(view->model());
        QVERIFY(proxy);
        const auto clicked = proxy->mapFromSource(clickedSource);
        QSignalSpy attributes(&meshLayer, &SWMM2DMeshLayer::attributeChanged);
        QSignalSpy refreshes(model, &QAbstractItemModel::dataChanged);
        QSignalSpy dirty(layer.get(), &SWMMModelLayer::modelEdited);
        // Mirror the main window's dirty bridge; it must be coalesced too.
        connect(&panel, &AttributeTablePanel::objectEdited, layer.get(),
                [&](const QString &) { layer->markEdited(); });
        QElapsedTimer timer;
        timer.start();
        QVERIFY(applyBulkCell(panel, view, clicked));
        qInfo() << kind << "bulk apply:" << model->rowCount() << "rows in" << timer.elapsed()
                << "ms; mesh refreshes:" << attributes.count();
        QCOMPARE(attributes.count(), 1);
        QCOMPARE(refreshes.count(), 1);
        QCOMPARE(dirty.count(), 1);
        QVERIFY(selection.selection() == selected);
        QCOMPARE(canvas.undoStack()->count(), 1);
        for (int row = 0; row < model->rowCount(); ++row)
            QCOMPARE(model->index(row, column).data(Qt::EditRole).toDouble(), value);
        canvas.undoStack()->undo();
        QCOMPARE(model->index(0, column).data(Qt::EditRole).toDouble(), original);
        QCOMPARE(clickedSource.data(Qt::EditRole).toDouble(), value);
        canvas.undoStack()->redo();
        QCOMPARE(model->index(0, column).data(Qt::EditRole).toDouble(), value);
    }

    void meshCouplingAreaBulkDialog_data()
    {
        QTest::addColumn<QString>("style");
        QTest::addColumn<bool>("cancel");
        QTest::addColumn<bool>("activeEditor");
        QTest::addColumn<bool>("cdSet");
        QTest::addColumn<bool>("bulk");
        for (const QString &style : {QString("Windows"), QString("Fusion")}) {
            for (bool activeEditor : {false, true}) {
                const QString suffix = activeEditor ? "-editor" : "-plain";
                QTest::newRow(qPrintable(style + "-apply" + suffix)) << style << false << activeEditor << false << true;
                QTest::newRow(qPrintable(style + "-cancel" + suffix)) << style << true << activeEditor << false << true;
            }
            // The former "refresh" rows also re-entered the Mesh 2D toolbar's
            // inline vertex editors mid-edit. Those editors left the ribbon
            // (process-aware editing plan, Phase 2), so the rows keep their
            // fixture (an explicit Cd on every vertex) without the re-entry.
            QTest::newRow(qPrintable(style + "-cd-single")) << style << false << false << true << false;
            QTest::newRow(qPrintable(style + "-cd-bulk")) << style << false << false << true << true;
        }
    }

    void meshCouplingAreaBulkDialog()
    {
        QFETCH(QString, style);
        QFETCH(bool, cancel);
        QFETCH(bool, activeEditor);
        QFETCH(bool, cdSet);
        QFETCH(bool, bulk);
        // Keep style ownership local; changing the application style can
        // leave native macOS popup callbacks referring to a deleted style.
        auto testStyle = std::unique_ptr<QStyle>(QStyleFactory::create(style));
        QVERIFY(testStyle);
        auto layer = openLayer();
        QVERIFY(layer);
        mesh::MeshResult meshData;
        const QVector<double> originalAreas{1., 2., 5., 4.};
        for (int i = 0; i < 4; ++i) {
            mesh::MeshVertex v;
            v.xy = QPointF(i % 2, i / 2);
            v.z = i + 10.;
            v.tag = QString("keep%1").arg(i);
            v.coupledNode = i == 3 ? QString() : QStringLiteral("J1");
            v.couplingArea = originalAreas[i];
            v.couplingCdSet = cdSet;
            v.couplingAreaSet = activeEditor;
            meshData.vertices.append(v);
        }
        mesh::MeshTriangle t;
        t.v0 = 0; t.v1 = 1; t.v2 = 2; meshData.triangles.append(t);
        meshData.ok = true;
        SWMM2DMeshLayer meshLayer(meshData, QStringLiteral("coupling-bulk.2dm"));
        MapCanvas canvas;
        canvas.addLayer(&meshLayer, false);
        meshLayer.setActiveMesh(true);
        SelectionManager selection;
        MeshEditingToolbar toolbar(QStringLiteral("Mesh"));
        toolbar.rebindCanvas(&canvas);
        toolbar.rebindSelectionManager(&selection);
        AttributeTablePanel panel;
        toolbar.setStyle(testStyle.get());
        for (auto *widget : toolbar.findChildren<QWidget *>()) widget->setStyle(testStyle.get());
        panel.setStyle(testStyle.get());
        for (auto *widget : panel.findChildren<QWidget *>()) widget->setStyle(testStyle.get());
        panel.setProject(layer.get(), &selection, &canvas);
        panel.refresh();
        auto *combo = panel.findChild<QComboBox *>();
        auto *view = panel.findChild<QTableView *>();
        auto *model = panel.findChild<MeshAttributeTableModel *>();
        QVERIFY(combo && view && model);
        const int category = combo->findData(QString("mesh:%1:v").arg(meshLayer.layerId()));
        QVERIFY(category >= 0);
        combo->setCurrentIndex(category);
        const int column = colFor(model->columnSpecs(), "couplingArea");
        QVERIFY(column >= 0);
        QSet<SWMMObjectRef> selected;
        for (int i = 0; i < (bulk ? 4 : 1); ++i) selected.insert(model->refForRow(i));
        selection.select(selected, SelectionManager::Replace);
        QCOMPARE(view->selectionModel()->selectedRows().size(), bulk ? 4 : 1);
        auto *proxy = qobject_cast<QSortFilterProxyModel *>(view->model());
        QVERIFY(proxy);
        QSignalSpy changes(&meshLayer, &SWMM2DMeshLayer::attributeChanged);
        const auto clicked = proxy->mapFromSource(model->index(0, column));
        if (activeEditor) {
            panel.resize(1200, 600);
            panel.show();
            view->scrollTo(clicked);
            QCoreApplication::processEvents();
            view->selectionModel()->setCurrentIndex(clicked, QItemSelectionModel::NoUpdate);
            view->edit(clicked);
            QVERIFY(view->indexWidget(clicked));
        }
        if (bulk)
            QVERIFY(applyBulkCell(panel, view, clicked, true, 5., true, cancel));
        else
            QVERIFY(model->setData(model->index(0, column), 5.));
        QCoreApplication::processEvents();
        for (int i = 0; i < 4; ++i) {
            const auto &v = meshLayer.mesh().vertices[i];
            const bool edited = !cancel && i < 3 && (bulk || i == 0);
            QCOMPARE(v.couplingArea, edited ? 5. : originalAreas[i]);
            QCOMPARE(v.couplingAreaSet, activeEditor || edited);
            QCOMPARE(v.couplingCdSet, cdSet);
            QCOMPARE(v.coupledNode, i == 3 ? QString() : QStringLiteral("J1"));
            QCOMPARE(v.tag, QString("keep%1").arg(i));
            QCOMPARE(v.z, i + 10.);
        }
        QCOMPARE(changes.count(), cancel ? 0 : 1);
        QCOMPARE(canvas.undoStack()->count(), cancel ? 0 : 1);
        QCOMPARE(view->selectionModel()->selectedRows().size(), bulk ? 4 : 1);
        if (!cancel) {
            canvas.undoStack()->undo();
            for (int i = 0; i < 4; ++i) {
                QCOMPARE(meshLayer.mesh().vertices[i].couplingArea, originalAreas[i]);
                QCOMPARE(meshLayer.mesh().vertices[i].couplingAreaSet, activeEditor);
            }
            canvas.undoStack()->redo();
            QCOMPARE(meshLayer.mesh().vertices[0].couplingArea, 5.);
        }
        const int cdColumn = colFor(model->columnSpecs(), "couplingCd");
        QVERIFY(cdColumn >= 0);
        const double areaBeforeCd = meshLayer.mesh().vertices[0].couplingArea;
        QVERIFY(model->setData(model->index(0, cdColumn), .8));
        QCOMPARE(meshLayer.mesh().vertices[0].couplingCd, .8);
        QCOMPARE(meshLayer.mesh().vertices[0].couplingArea, areaBeforeCd);
    }

    void dataBulkApplyRetainsUndoAndReferences()
    {
        auto layer = std::make_unique<SWMMModelLayer>(
            QDir(dataDir()).filePath("data_object_table_fixture.inp"), nullptr);
        QList<QString> warnings, errors;
        QVERIFY(layer->loadModel(warnings, errors));
        auto *registry = qobject_cast<openswmmvis::street::StreetRegistry *>(
            layer->ensureStreetRegistry());
        QVERIFY(registry);
        DataObjectAttributeTableModel model;
        QUndoStack undo;
        model.setSource(layer.get(), SWMMModelLayer::DataStreets);
        model.setUndoStack(&undo);
        const int column = colFor(model.columnSpecs(), "roadRoughness");
        QVERIFY(column >= 0);
        const auto engineRoughness = [&](const char *name) {
            double roughness = -1.0;
            swmm_street_get_params(layer->engine(), swmm_street_index(layer->engine(), name),
                                   nullptr, nullptr, nullptr, &roughness, nullptr, nullptr,
                                   nullptr, nullptr, nullptr, nullptr);
            return roughness;
        };
        QSignalSpy edits(&model, &DataObjectAttributeTableModel::objectEdited);
        model.setDataForRows(column, {0, 1, -1, model.rowCount()}, 0.02);
        QCOMPARE(edits.count(), 2);
        QCOMPARE(undo.count(), 1);
        for (int row = 0; row < model.rowCount(); ++row)
            QCOMPARE(model.index(row, column).data(Qt::EditRole).toDouble(), 0.02);
        QCOMPARE(engineRoughness("ST1"), 0.02);
        QCOMPARE(engineRoughness("ST2"), 0.02);
        model.setDataForRows(column, {0, 1}, 0.02); // no-op
        model.setDataForRows(column, {0, 1}, QStringLiteral("invalid"));
        model.setDataForRows(0, {0, 1}, QStringLiteral("read-only name"));
        QCOMPARE(undo.count(), 1);
        QCOMPARE(edits.count(), 2);
        // Replay remains tied to the original category after rebinding the view.
        model.setSource(layer.get(), SWMMModelLayer::DataPollutants);
        undo.undo();
        QCOMPARE(registry->findByName("ST1")->roadRoughness(), 0.016);
        QCOMPARE(registry->findByName("ST2")->roadRoughness(), 0.016);
        QCOMPARE(engineRoughness("ST1"), 0.016);
        QCOMPARE(engineRoughness("ST2"), 0.016);
        undo.redo();
        QCOMPARE(registry->findByName("ST1")->roadRoughness(), 0.02);
        QCOMPARE(registry->findByName("ST2")->roadRoughness(), 0.02);
        QCOMPARE(engineRoughness("ST1"), 0.02);
        QCOMPARE(engineRoughness("ST2"), 0.02);
    }

    /** Applying a value every selected row already holds is no edit, so it leaves no undo step
     *  (the Properties aggregate leaves none either; process-aware plan Phase 3). */
    void meshBulkApplyOfHeldValueLeavesNoUndoStep()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        for (bool distinct : {false, true}) {
            mesh::MeshResult meshData;
            for (int i = 0; i < 3; ++i) {
                mesh::MeshVertex v;
                v.xy = QPointF(i % 2, i / 2);
                v.z = distinct ? 10. + i : 10.;
                meshData.vertices.append(v);
            }
            mesh::MeshTriangle t;
            t.v0 = 0; t.v1 = 1; t.v2 = 2; meshData.triangles.append(t);
            meshData.ok = true;
            SWMM2DMeshLayer meshLayer(meshData, QStringLiteral("held-value.2dm"));
            MapCanvas canvas;
            canvas.addLayer(&meshLayer, false);
            SelectionManager selection;
            AttributeTablePanel panel;
            panel.setProject(layer.get(), &selection, &canvas);
            panel.refresh();
            auto *combo = panel.findChild<QComboBox *>();
            auto *view = panel.findChild<QTableView *>();
            auto *model = panel.findChild<MeshAttributeTableModel *>();
            QVERIFY(combo && view && model);
            combo->setCurrentIndex(combo->findData(QString("mesh:%1:v").arg(meshLayer.layerId())));
            const int column = colFor(model->columnSpecs(), "z");
            QSet<SWMMObjectRef> selected;
            for (int i = 0; i < 3; ++i)
                selected.insert(model->refForRow(i));
            selection.select(selected, SelectionManager::Replace);
            auto *proxy = qobject_cast<QSortFilterProxyModel *>(view->model());
            QVERIFY(proxy);
            QVERIFY(applyBulkCell(panel, view, proxy->mapFromSource(model->index(0, column))));
            QCoreApplication::processEvents();
            for (int i = 0; i < 3; ++i)
                QCOMPARE(meshLayer.mesh().vertices[i].z, 10.);
            // Positive control: when rows change, the batch is one step.
            QCOMPARE(canvas.undoStack()->count(), distinct ? 1 : 0);
        }
    }

    void bulkApplyDefersSorting_data()
    {
        QTest::addColumn<bool>("filtered");
        QTest::newRow("sorted") << false;
        QTest::newRow("filtered") << true;
    }

    /** Bulk apply must edit the captured rows with one sort/filter pass and one undo step. */
    void bulkApplyDefersSorting()
    {
        QFETCH(bool, filtered);
        auto layer = openLayer();
        QVERIFY(layer);
        constexpr int count = 256;
        {
            SWMMModelLayer::BulkEdit bulk(layer.get());
            for (int i = 0; i < count; ++i) {
                const auto name = QString("Bulk%1").arg(i, 4, 10, QLatin1Char('0'));
                int node = -1;
                QVERIFY(layer->applyNodeAdd(name, SWMM_NODE_JUNCTION, i, 0.0, &node));
                QCOMPARE(swmm_node_set_invert_elev(layer->engine(), node, i + 1.0), SWMM_OK);
            }
        }
        SelectionManager selection;
        MapCanvas canvas;
        AttributeTablePanel panel;
        panel.setProject(layer.get(), &selection, &canvas);
        panel.refresh();
        auto *combo = panel.findChild<QComboBox *>();
        auto *view = panel.findChild<QTableView *>();
        auto *model = panel.findChild<SWMMAttributeTableModel *>();
        auto *proxy = qobject_cast<QSortFilterProxyModel *>(view ? view->model() : nullptr);
        QVERIFY(combo && view && model && proxy);
        combo->setCurrentIndex(combo->findData(int(SWMMModelLayer::CatJunctions)));
        const int column = colFor(model->columnSpecs(), "Invert elev");
        QVERIFY(column >= 0);
        const auto cellFor = [&](int i) {
            const auto name = QString("Bulk%1").arg(i, 4, 10, QLatin1Char('0'));
            return model->index(model->rowForName(name), column);
        };
        view->sortByColumn(column, Qt::AscendingOrder);
        if (filtered) {
            auto *query = panel.findChild<QLineEdit *>();
            QVERIFY(query);
            query->setText("[Invert elev] < 256");
            QVERIFY(QMetaObject::invokeMethod(&panel, "onQueryApplyClicked"));
        }
        QSet<SWMMObjectRef> selected;
        for (int i = 0; i < count - 1; i += 2)
            selected.insert({SWMMObjectRef::Node,
                             QString("Bulk%1").arg(i, 4, 10, QLatin1Char('0'))});
        selection.select(selected, SelectionManager::Replace);
        QCOMPARE(view->selectionModel()->selectedRows().size(), selected.size());
        const double value = filtered ? 512.0 : 255.0;
        const auto clicked = proxy->mapFromSource(cellFor(254));
        QVERIFY(clicked.isValid());
        QSignalSpy layouts(proxy, &QAbstractItemModel::layoutChanged);
        QSignalSpy edits(&panel, &AttributeTablePanel::objectEdited);
        QElapsedTimer timer;
        timer.start();
        QVERIFY(applyBulkCell(panel, view, clicked, filtered, value));
        qInfo() << "bulk apply:" << timer.elapsed() << "ms; layout changes:" << layouts.count();
        const int layoutCount = layouts.count();
        if (!filtered)
            QVERIFY(selection.selection() == selected);
        QCOMPARE(edits.count(), selected.size() - (filtered ? 0 : 1));
        QCOMPARE(canvas.undoStack()->count(), 1);
        for (int i = 0; i < count; ++i) {
            const auto index = cellFor(i);
            QCOMPARE(index.data(Qt::EditRole).toDouble(), i % 2 == 0 ? value : i + 1.0);
            if (i % 2 == 0)
                QCOMPARE(proxy->mapFromSource(index).isValid(), !filtered);
        }
        canvas.undoStack()->undo();
        for (int i = 0; i < count; ++i)
            QCOMPARE(cellFor(i).data(Qt::EditRole).toDouble(), i + 1.0);
        canvas.undoStack()->redo();
        for (const auto &ref : selected)
            QCOMPARE(model->index(model->rowForName(ref.name), column).data(Qt::EditRole).toDouble(),
                     value);
        QVERIFY2(layoutCount <= 1, "Bulk apply must sort once, rather than after every row");
    }

    void linkEndpointsAreEditable_data()
    {
        QTest::addColumn<int>("kind");
        QTest::addColumn<bool>("upstream");
        for (int kind = SWMM_LINK_CONDUIT; kind <= SWMM_LINK_OUTLET; ++kind) {
            QTest::newRow(qPrintable(QString("type%1-from").arg(kind))) << kind << true;
            QTest::newRow(qPrintable(QString("type%1-to").arg(kind))) << kind << false;
        }
    }

    /** Both endpoint columns support validated edits, undo and persistence for every link type. */
    void linkEndpointsAreEditable()
    {
        QFETCH(int, kind);
        QFETCH(bool, upstream);
        auto layer = openLayer();
        QVERIFY(layer);
        int link = -1;
        const QVector<QPointF> bends = {QPointF(25, 35), QPointF(45, 55)};
        QVERIFY(layer->applyLinkAdd("LPicker", kind, "J1", "X1", bends, &link));
        const auto category = SWMMModelLayer::Category(int(SWMMModelLayer::CatConduits) + kind);
        QUndoStack undo;
        SWMMAttributeTableModel model;
        model.setSource(layer.get(), category);
        model.setUndoStack(&undo);
        int row = -1;
        for (int r = 0; r < model.rowCount(); ++r)
            if (model.objectNameAt(r) == "LPicker")
                row = r;
        QVERIFY(row >= 0);
        const auto specs = model.columnSpecs();
        const int col = colFor(specs, upstream ? "From node" : "To node");
        QVERIFY(col >= 0);
        QCOMPARE(specs[col].editor, EditorKind::LinkEndpoint);
        const auto cell = model.index(row, col);
        QVERIFY(model.flags(cell) & Qt::ItemIsEditable);
        const QString original = upstream ? "J1" : "X1";
        QCOMPARE(cell.data().toString(), original);
        for (const QString invalid :
             {QString(), QString("missing"), upstream ? QString("X1") : QString("J1")})
            QVERIFY(!model.setData(cell, invalid));
        QCOMPARE(undo.count(), 0);
        QSignalSpy geometry(layer.get(), &SWMMModelLayer::geometryChanged);
        QSignalSpy dirty(layer.get(), &SWMMModelLayer::modelEdited);
        QVERIFY(model.setData(cell, "O1"));
        QCOMPARE(cell.data().toString(), QString("O1"));
        QCOMPARE(layer->cachedLinkInteriorVertices(link), bends);
        const auto polyline = layer->cachedLinkPolyline(link);
        const auto oldPolyline = polyline;
        double x = 0, y = 0;
        QVERIFY(layer->cachedNodeCoord(layer->nodeIndex("O1"), &x, &y));
        QCOMPARE(upstream ? polyline.first() : polyline.last(), QPointF(x, y));
        QCOMPARE(geometry.count(), 1);
        QCOMPARE(dirty.count(), 1);
        QCOMPARE(undo.count(), 1);
        undo.undo();
        QCOMPARE(cell.data().toString(), original);
        undo.redo();
        QCOMPARE(cell.data().toString(), QString("O1"));
        QCOMPARE(layer->cachedLinkPolyline(link), oldPolyline);
        SWMMLinkPropertyAdapter adapter(layer->engine(), "LPicker");
        adapter.setModelLayer(layer.get());
        auto ref = upstream ? adapter.fromNodeRef() : adapter.toNodeRef();
        ref.nodeName = original;
        if (upstream)
            adapter.setFromNodeRef(ref);
        else
            adapter.setToNodeRef(ref);
        QCOMPARE(upstream ? adapter.fromNode() : adapter.toNode(), original);
        ref.nodeName = "O1";
        if (upstream)
            adapter.setFromNodeRef(ref);
        else
            adapter.setToNodeRef(ref);
        QCOMPARE(upstream ? adapter.fromNode() : adapter.toNode(), QString("O1"));
        QTemporaryDir saved;
        QVERIFY(saved.isValid());
        const auto path = saved.filePath("endpoints.inp");
        QCOMPARE(swmm_model_write(layer->engine(), path.toUtf8().constData()), SWMM_OK);
        SWMMModelLayer reloaded(path);
        QList<QString> warnings, errors;
        QVERIFY(reloaded.loadModel(warnings, errors));
        SWMMLinkPropertyAdapter reread(reloaded.engine(), "LPicker");
        QCOMPARE(upstream ? reread.fromNode() : reread.toNode(), QString("O1"));
    }

    /** The table's live delegate searches existing nodes and rejects incomplete IDs. */
    void linkEndpointPickerUsesCompletion()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        AttributeTablePanel panel;
        panel.setProject(layer.get(), nullptr, nullptr);
        panel.showModelCategory(layer.get(), SWMMModelLayer::CatConduits);
        auto *view = panel.findChild<QTableView *>();
        QVERIFY(view);
        auto *model = view->model();
        int col = -1;
        for (int c = 0; c < model->columnCount(); ++c)
            if (model->headerData(c, Qt::Horizontal).toString() == "From Node")
                col = c;
        QVERIFY(col >= 0);
        int row = -1;
        for (int r = 0; r < model->rowCount(); ++r)
            if (model->index(r, 0).data().toString() == "C1")
                row = r;
        QVERIFY(row >= 0);
        const auto cell = model->index(row, col);
        auto *delegate = view->itemDelegateForIndex(cell);
        QWidget host;
        std::unique_ptr<QWidget> widget(delegate->createEditor(&host, {}, cell));
        auto *combo = qobject_cast<LinkEndpointComboBox *>(widget.get());
        QVERIFY(combo);
        delegate->setEditorData(combo, cell);
        QCOMPARE(combo->currentText(), QString("J1"));
        QVERIFY(combo->findText("X1") < 0); // Opposite endpoint.
        combo->completer()->setCompletionPrefix("o");
        QCOMPARE(combo->completer()->completionCount(), 1);
        QCOMPARE(combo->completer()->currentCompletion(), QString("O1"));
        combo->setEditText("missing");
        delegate->setModelData(combo, model, cell);
        QCOMPARE(cell.data().toString(), QString("J1"));
        combo->setEditText("O1");
        delegate->setModelData(combo, model, cell);
        // Reconnecting a link reloads the table, so `cell` is stale here: reading
        // it touched freed proxy mapping memory (Guard Malloc faults every time;
        // ordinary runs crashed depending on heap layout). Find C1 again.
        int after = -1;
        for (int r = 0; r < model->rowCount(); ++r)
            if (model->index(r, 0).data().toString() == "C1")
                after = r;
        QVERIFY(after >= 0);
        QCOMPARE(model->index(after, col).data().toString(), QString("O1"));
    }

    void linkEndpointPickerProtectsVirtualConnections()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        int virtualNode = -1, otherLink = -1;
        QString error;
        QVERIFY2(layer->applyInsertVirtualJunction("C1", 0.5, "VPicker", "CSplit", &virtualNode,
                                                   &otherLink, &error),
                 qPrintable(error));
        SWMMAttributeTableModel model;
        model.setSource(layer.get(), SWMMModelLayer::CatConduits);
        const int to = colFor(model.columnSpecs(), "To node");
        const int from = colFor(model.columnSpecs(), "From node");
        QVERIFY(to >= 0 && from >= 0);
        const auto fixed = model.index(0, to); // C1 now ends at VPicker.
        QCOMPARE(fixed.data().toString(), QString("VPicker"));
        QVERIFY(!(model.flags(fixed) & Qt::ItemIsEditable));
        QVERIFY(!model.setData(fixed, "O1"));
        // A different link must not create a third connection at that virtual node.
        const int link = swmm_link_index(layer->engine(), "X1");
        QVERIFY(!layer->applyLinkEndpoint(link, true, virtualNode));
        LinkEndpointComboBox picker;
        picker.setValue({layer->engine(), "X1", true, "X1"});
        QVERIFY(picker.findText("VPicker") < 0);
        const auto ref = fixed.data(openswmmvis::kLinkEndpointRole).value<LinkEndpointRef>();
        picker.setValue(ref);
        QCOMPARE(picker.count(), 1);
        QCOMPARE(picker.currentText(), QString("VPicker"));
    }

    void linkEndpointPickerHandlesLargeNodeLists()
    {
        const auto engine = swmm_engine_new();
        QVERIFY(engine);
        const auto cleanup = qScopeGuard([engine] { swmm_engine_destroy(engine); });
        for (int i = 0; i < 20000; ++i) {
            const auto id = QString("N%1").arg(i, 5, 10, QLatin1Char('0')).toUtf8();
            QCOMPARE(swmm_node_add(engine, id.constData(), SWMM_NODE_JUNCTION), SWMM_OK);
        }
        QCOMPARE(swmm_link_add(engine, "L", SWMM_LINK_CONDUIT), SWMM_OK);
        QCOMPARE(swmm_link_set_nodes(engine, 0, 0, 1), SWMM_OK);
        LinkEndpointComboBox picker;
        picker.setValue({engine, "L", true, "N00000"});
        QCOMPARE(picker.count(), 19999);
        picker.completer()->setCompletionPrefix("n1999");
        QCOMPARE(picker.completer()->completionCount(), 10);
        QCOMPARE(picker.completer()->currentCompletion(), QString("N19990"));
        QCOMPARE(picker.currentText(), QString("N00000")); // Searching alone never edits a link.
    }

    /** A viewport outside the old link bbox must draw and select the reconnected link immediately. */
    void linkEndpointGraphicsTrackEdits()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        layer->setCategoryVisible(SWMMModelLayer::CatConduits, false);
        layer->setShowNodes(false);
        layer->setShowSubcatchments(false);
        layer->setShowRainGages(false);
        layer->setShowLabels(false);
        layer->setQsgRenderKinds({});
        int link = -1;
        QVERIFY(layer->applyLinkAdd("LGraphics", SWMM_LINK_CONDUIT, "J1", "X1",
                                    {QPointF(500, 100)}, &link));
        SWMMLayerQSGRenderer renderer;
        renderer.setLayer(layer.get());
        const auto contentRevision = renderer.contentRevision();
        const auto geometryRevision = layer->geomRevision();
        const auto paint = [&] {
            QImage image(400, 80, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::white);
            QPainter painter(&image);
            painter.scale(2, 2);
            painter.translate(-1400, 60);
            QStyleOptionGraphicsItem option;
            option.exposedRect = QRectF(1400, -60, 200, 40);
            SWMMLayerItem item(layer.get());
            item.paint(&painter, &option, nullptr);
            return image;
        };
        const auto before = paint();
        QVERIFY(!layer->linksInRect(1400, 20, 1600, 60).contains("LGraphics"));
        SWMMAttributeTableModel model;
        QUndoStack undo;
        model.setSource(layer.get(), SWMMModelLayer::CatConduits);
        model.setUndoStack(&undo);
        int row = -1;
        for (int r = 0; r < model.rowCount(); ++r)
            if (model.objectNameAt(r) == "LGraphics")
                row = r;
        QVERIFY(row >= 0);
        const auto cell = model.index(row, colFor(model.columnSpecs(), "To node"));
        QVERIFY(model.setData(cell, "O1"));
        QVERIFY(layer->geomRevision() > geometryRevision);
        QVERIFY(renderer.contentRevision() > contentRevision);
        QVERIFY(layer->linksInRect(1400, 20, 1600, 60).contains("LGraphics"));
        const auto after = paint();
        QVERIFY(after != before);
        undo.undo();
        QVERIFY(!layer->linksInRect(1400, 20, 1600, 60).contains("LGraphics"));
        QCOMPARE(paint(), before);
        undo.redo();
        QCOMPARE(paint(), after);
    }

#ifdef HAVE_QPROPERTYMODEL
    /** Exercise the actual Properties dock's editor registration and immediate selection commit. */
    void linkPropertyPickerCommitsSelection()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMMConduitPropertyAdapter adapter(layer->engine(), "C1");
        adapter.setModelLayer(layer.get());
        MapCanvas canvas;
        layer->setEditCanvas(&canvas);
        PropertiesPanel panel;
        panel.setProject(layer.get());
        panel.showObject(&adapter);
        auto *tree = panel.findChild<QTreeView *>();
        QVERIFY(tree);
        const auto find = [&](auto &&self, const QModelIndex &parent) -> QModelIndex {
            for (int r = 0; r < tree->model()->rowCount(parent); ++r) {
                auto row = tree->model()->index(r, 0, parent);
                if (row.data().toString() == "From Node")
                    return row.siblingAtColumn(1);
                const auto child = self(self, row);
                if (child.isValid())
                    return child;
            }
            return {};
        };
        const auto cell = find(find, {});
        QVERIFY(cell.isValid());
        QVERIFY(cell.flags() & Qt::ItemIsEditable);
        panel.resize(500, 650);
        panel.show();
        tree->expandAll();
        tree->edit(cell);
        auto *combo = tree->findChild<LinkEndpointComboBox *>();
        QVERIFY(combo);
        QCOMPARE(combo->currentText(), QString("J1"));
        combo->setCurrentIndex(combo->findText("O1"));
        QVERIFY(QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, combo->currentIndex())));
        QCOMPARE(adapter.fromNode(), QString("O1"));
        QCOMPARE(cell.data().toString(), QString("O1"));
        QCOMPARE(canvas.undoStack()->count(), 1);
        canvas.undoStack()->undo();
        QCOMPARE(adapter.fromNode(), QString("J1"));
        canvas.undoStack()->redo();
        QCOMPARE(adapter.fromNode(), QString("O1"));
        auto invalid = adapter.fromNodeRef();
        invalid.nodeName = "X1";
        adapter.setFromNodeRef(invalid);
        QCOMPARE(adapter.fromNode(), QString("O1"));
    }
#endif

    /**
     *
     * @brief Require station-id keys to remain ungrouped while statistic/coupling aliases share
     * their intended groups.
     * Pins naming classification without loading an engine or constructing a table.
     *
     */
    void groupingDoesNotMistakeStationForStatistics()
    {
        QVERIFY(openswmmvis::attributeGroup("stationId").isEmpty());
        QVERIFY(openswmmvis::attributeGroup("gage_station_id").isEmpty());
        QCOMPARE(openswmmvis::attributeGroup("statMaxDepth"),
                 openswmmvis::attributeGroup("node_stat_max_depth"));
        QCOMPARE(openswmmvis::attributeGroup("couplingType"),
                 openswmmvis::attributeGroup("node_2d_coupling_type"));
    }

#ifdef HAVE_QPROPERTYMODEL
    /**
     *
     * @brief Exercise grouped property-model mapping/edit/reset invariants with the optional
     * property library.
     * Loads a junction adapter, requires coupling/output subgroup structure, three flooding
     * rows and a read-only first flooding value. Edits/restores invert elevation, edits
     * coupling diameter text, refreshes/clears/rebinds and switches to an outfall schema.
     * A model tester checks Qt invariants. Local finder borrows exact display label and
     * optional parent index, returning first direct child or invalid; it is not recursive.
     * Only compiled with HAVE_QPROPERTYMODEL; no hydraulic run or project save.
     *
     */
    void propertyGroupsPreserveEditingAndReset()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        const QString name = layer->objectNameAt(SWMMModelLayer::CatJunctions, 0);
        QVERIFY(!name.isEmpty());
        SWMMJunctionPropertyAdapter adapter(layer->engine(), name);
        adapter.setModelLayer(layer.get());
        QPropertyModel source(nullptr);
        GroupedPropertyModel grouped;
        grouped.setSourceModel(&source);
        source.setData(QVariant::fromValue<QObject *>(&adapter));
        QAbstractItemModelTester tester(&grouped,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        auto find = [&](const QString &label, const QModelIndex &parent = QModelIndex()) {
            for (int r = 0; r < grouped.rowCount(parent); ++r) {
                auto index = grouped.index(r, 0, parent);
                if (index.data().toString() == label)
                    return index;
            }
            return QModelIndex();
        };
        auto coupling = find("2D Coupling");
        auto output = find("Output Summary");
        QVERIFY(coupling.isValid());
        QVERIFY(output.isValid());
        QCOMPARE(output.row(), grouped.rowCount() - 1);
        QVERIFY(find("Setup", coupling).isValid());
        auto geometry = find("Geometry", coupling);
        QVERIFY(geometry.isValid());
        QVERIFY(find("Exchange", coupling).isValid());
        QVERIFY(find("Node Depth", output).isValid());
        auto flooding = find("Node Flooding", output);
        QCOMPARE(grouped.rowCount(flooding), 3);
        QVERIFY(!(grouped.flags(grouped.index(0, 1, flooding)) & Qt::ItemIsEditable));
        auto properties = find("Properties");
        auto invert = find(adapter.displayLabelFor("invertElev"), properties).siblingAtColumn(1);
        QVERIFY(invert.isValid());
        QCOMPARE(grouped.mapFromSource(grouped.mapToSource(invert)), invert);
        double previous = adapter.invertElev();
        QVERIFY(grouped.setData(invert, previous + 0.25));
        QCOMPARE(adapter.invertElev(), previous + 0.25);
        QVERIFY(grouped.setData(invert, previous));
        const auto diameter =
            find(adapter.displayLabelFor("couplingDiameter"), geometry).siblingAtColumn(1);
        QVERIFY(diameter.isValid());
        QVERIFY(grouped.setData(diameter, QStringLiteral("1.2")));
        QCOMPARE(adapter.couplingDiameter(), QStringLiteral("1.2"));
        source.refreshValues();
        QVERIFY(find("Geometry", find("2D Coupling")).isValid());
        source.clear();
        QCOMPARE(grouped.rowCount(), 0);
        source.setData(QVariant::fromValue<QObject *>(&adapter));
        QVERIFY(find("Output Summary").isValid());
        SWMMOutfallPropertyAdapter outfall(layer->engine(), QStringLiteral("O1"));
        source.setData(QVariant::fromValue<QObject *>(&outfall));
        const auto outfallCoupling = find("2D Coupling");
        QVERIFY(find("Outfall", outfallCoupling).isValid());
        QVERIFY(!find("Geometry", outfallCoupling).isValid());
        source.clear();
    }
#endif

    // =====================================================================
    // 1 — conduit cross-section block
    // =====================================================================

    /**
     *
     * @brief Verify derived column registration, read-only flags, value invalidation and cleanup.
     *
     */
    void computedExtensionColumnsStayReadOnlyAndRefresh()
    {
        struct Provider : QObject, OpenSWMM::Sdk::IComputedAttributeProvider {
            double factor = 2.;
            QVector<OpenSWMM::Render::AttributeField>
            fields(OpenSWMMVis::SwmmCategory category) const override
            {
                if (category != OpenSWMMVis::CatConduits)
                    return {};
                return {{"scaled", "Derived length", QMetaType::Double, false, "m"}};
            }
            QVariant value(OpenSWMMVis::SwmmCategory, const QString &, const QString &,
                           const QVariantMap &base) const override
            {
                return base.value("length").toDouble() * factor;
            }
        };
        auto layer = openLayer();
        QVERIFY(layer);
        SWMMAttributeTableModel model;
        model.setSource(layer.get(), SWMMModelLayer::CatConduits);
        auto provider = std::make_unique<Provider>();
        const QString id = "ext.tests.columns";
        const auto changed = [source = layer.get()](bool schema) {
            emit source->extensionAttributesChanged(schema);
        };
        QString error;
        QVERIFY(openswmmvis::registerExtensionAttributes(layer.get(), id, provider.get(), provider.get(), changed, &error));
        const int column = colFor(model.columnSpecs(), id + ".scaled");
        QVERIFY(column >= 0);
        QVERIFY(model.rowCount() > 0);
        QVERIFY(!(model.flags(model.index(0, column)) & Qt::ItemIsEditable));
        const auto original = layer->identifyByName(model.objectNameAt(0), SWMMModelLayer::kKindLink).value("length").toDouble();
        QCOMPARE(model.data(model.index(0, column), Qt::EditRole).toDouble(), original * 2.);
        provider->factor = 3.;
        openswmmvis::refreshExtensionAttributes(layer.get(), id);
        QCOMPARE(model.data(model.index(0, column), Qt::EditRole).toDouble(), original * 3.);
        provider.reset();
        QCOMPARE(colFor(model.columnSpecs(), id + ".scaled"), -1);
    }

    /** @brief Require the conduit cross-section columns and their intended editor kinds. */
    void conduitCrossSectionBlockIsPresent()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMMAttributeTableModel model;
        model.setSource(layer.get(), SWMMModelLayer::CatConduits);
        const auto specs = model.columnSpecs();

        QVERIFY(colFor(specs, QStringLiteral("XSection")) >= 0);
        QVERIFY(colFor(specs, QStringLiteral("Barrels")) >= 0);
        for (int k = 1; k <= 4; ++k)
            QVERIFY2(colFor(specs, QStringLiteral("Geom %1").arg(k)) >= 0,
                     qPrintable(QStringLiteral("Geom %1 column missing").arg(k)));

        QCOMPARE(specs[colFor(specs, QStringLiteral("XSection"))].editor, EditorKind::Compound);
        QCOMPARE(specs[colFor(specs, QStringLiteral("Barrels"))].editor, EditorKind::Integer);
        QCOMPARE(specs[colFor(specs, QStringLiteral("Geom 1"))].editor, EditorKind::Numeric);
    }

    /**
     *
     * @brief Require all four stored geom cells to remain visible-valued and editable on the
     * circular fixture conduit.
     * Also pins that circular geom two is not shape-consumed. Does not commit a value or
     * claim every shape/row combination is tested by this case.
     *
     */
    void geomCellsStayEditableOnCircular()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMMAttributeTableModel model;
        model.setSource(layer.get(), SWMMModelLayer::CatConduits);
        const auto specs = model.columnSpecs();
        QVERIFY(model.rowCount() > 0);

        // Row 0 of the fixture is a CIRCULAR conduit — only geom1 "applies".
        const int shapeCol = colFor(specs, QStringLiteral("Geom 1"));
        QVERIFY(shapeCol >= 0);
        QVERIFY(!openswmmvis::xsectGeomApplies(SWMM_XSECT_CIRCULAR, 2));

        for (int k = 1; k <= 4; ++k) {
            const int c = colFor(specs, QStringLiteral("Geom %1").arg(k));
            const QModelIndex idx = model.index(0, c);
            QVERIFY2(model.flags(idx) & Qt::ItemIsEditable,
                     qPrintable(QStringLiteral("Geom %1 not editable on CIRCULAR").arg(k)));
            QVERIFY2(model.data(idx, Qt::DisplayRole).isValid(),
                     qPrintable(QStringLiteral("Geom %1 blank on CIRCULAR").arg(k)));
        }
    }

    /**
     *
     * @brief Commit an otherwise unused circular geom-two value and require raw engine/readback
     * retention.
     * Writes 2.75 in authored project units, resolves the conduit id/index and checks raw g2
     * and unchanged circular shape. Does not initialize topology or save/reopen an input file.
     *
     */
    void geomEditRoundTripsThroughEngine()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMMAttributeTableModel model;
        model.setSource(layer.get(), SWMMModelLayer::CatConduits);
        const auto specs = model.columnSpecs();
        const int c = colFor(specs, QStringLiteral("Geom 2"));
        QVERIFY(c >= 0);

        const QModelIndex idx = model.index(0, c);
        QVERIFY(model.setData(idx, 2.75, Qt::EditRole));

        const QString name = model.objectNameAt(0);
        const int li = swmm_link_index(layer->engine(), name.toUtf8().constData());
        QVERIFY(li >= 0);
        int shape = 0;
        double g1 = 0, g2 = 0, g3 = 0, g4 = 0;
        QCOMPARE(swmm_link_get_xsect(layer->engine(), li, &shape, &g1, &g2, &g3, &g4), SWMM_OK);
        QCOMPARE(g2, 2.75);
        // The shape itself must not have moved.
        QCOMPARE(shape, SWMM_XSECT_CIRCULAR);
    }

    /**
     *
     * @brief Require an irregular section's transect-index geom one to stay locked while geoms
     * two through four remain editable.
     * Creates/configures a three-station transect only when the fixture has none, assigns
     * index zero, reloads and checks flags. Does not exercise CUSTOM/STREET picker indices
     * or validate hydraulic results of that temporary editing configuration.
     *
     */
    void pickerIndexGeomStaysLocked()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMMAttributeTableModel model;
        model.setSource(layer.get(), SWMMModelLayer::CatConduits);
        const auto specs = model.columnSpecs();

        const QString name = model.objectNameAt(0);
        const int li = swmm_link_index(layer->engine(), name.toUtf8().constData());
        QVERIFY(li >= 0);
        // set_xsect(IRREGULAR, index) now validates and binds the transect
        // (previously it stored the index as a depth and left the reference
        // dangling — the .inp save corruption family), so index 0 must name a
        // real transect in the fixture.
        SWMM_Engine eng = layer->engine();
        if (swmm_transect_count(eng) == 0) {
            QCOMPARE(swmm_transect_add(eng, "T_SCHEMA"), SWMM_OK);
            const int ti = swmm_transect_index(eng, "T_SCHEMA");
            QCOMPARE(swmm_transect_set_roughness(eng, ti, 0.04, 0.04, 0.03), SWMM_OK);
            QCOMPARE(swmm_transect_add_station(eng, ti, 0.0, 10.0), SWMM_OK);
            QCOMPARE(swmm_transect_add_station(eng, ti, 5.0, 5.0), SWMM_OK);
            QCOMPARE(swmm_transect_add_station(eng, ti, 10.0, 10.0), SWMM_OK);
        }
        QCOMPARE(swmm_link_set_xsect(layer->engine(), li, SWMM_XSECT_IRREGULAR, 0, 0, 0, 0),
                 SWMM_OK);
        model.reload();

        const int g1 = colFor(specs, QStringLiteral("Geom 1"));
        QVERIFY(!(model.flags(model.index(0, g1)) & Qt::ItemIsEditable));
        for (int k = 2; k <= 4; ++k) {
            const int c = colFor(specs, QStringLiteral("Geom %1").arg(k));
            QVERIFY2(model.flags(model.index(0, c)) & Qt::ItemIsEditable,
                     qPrintable(QStringLiteral("Geom %1 locked on IRREGULAR").arg(k)));
        }
    }

    // =====================================================================
    // 2 — dynamics columns are a contiguous suffix
    // =====================================================================

    /**
     *
     * @brief Require top-level attribute group names never to reappear after another group within
     * each spatial schema.
     * Uses the prefix before group-path slash and treats empty as Model Attributes;
     * checks schema ordering across allCategories(), without numerical result assertions.
     *
     */
    void attributeGroupsAreContiguous()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        for (const auto category : allCategories()) {
            SWMMAttributeTableModel model;
            model.setSource(layer.get(), category);
            QSet<QString> completed;
            QString previous;
            for (int column = 0; column < model.columnCount(); ++column) {
                QString group =
                    model.headerData(column, Qt::Horizontal, openswmmvis::AttributeGroupRole)
                        .toString()
                        .section(" / ", 0, 0);
                if (group.isEmpty())
                    group = QStringLiteral("Model Attributes");
                if (group != previous) {
                    QVERIFY2(!completed.contains(group),
                             qPrintable(QString("Group %1 reappears in category %2 at column %3")
                                            .arg(group)
                                            .arg(int(category))
                                            .arg(column)));
                    completed.insert(group);
                    previous = group;
                }
            }
        }
    }

    /**
     *
     * @brief Require statistic-tagged columns to form a read-only suffix in every spatial schema.
     * After the first _stat_ tag rejects later inputs, checks ReadOnly editor/group path,
     * keeps Output Summary out of exported header text and checks noneditable row-zero
     * flags when rows exist. Does not require initialized statistics or nonempty categories.
     *
     */
    void dynamicsColumnsAreAppendedLast()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        for (const auto cat : allCategories()) {
            SWMMAttributeTableModel model;
            model.setSource(layer.get(), cat);
            const auto specs = model.columnSpecs();

            int firstDynamic = -1;
            for (int i = 0; i < specs.size(); ++i) {
                if (isDynamics(specs[i])) {
                    if (firstDynamic < 0)
                        firstDynamic = i;
                } else if (firstDynamic >= 0) {
                    QFAIL(qPrintable(QStringLiteral("category %1: input column '%2' at %3 sits "
                                                    "AFTER the dynamics block (starts at %4)")
                                         .arg(int(cat))
                                         .arg(specs[i].key)
                                         .arg(i)
                                         .arg(firstDynamic)));
                }
            }
            // Whatever is in the block must be read-only — a statistic is not
            // an input, and commitValueDirect() refuses ReadOnly writes.
            for (int i = std::max(firstDynamic, 0); i < specs.size(); ++i) {
                if (!isDynamics(specs[i]))
                    continue;
                QCOMPARE(specs[i].editor, EditorKind::ReadOnly);
                QVERIFY(model.headerData(i, Qt::Horizontal, openswmmvis::AttributeGroupRole)
                            .toString()
                            .startsWith("Output Summary / "));
                // Group headings must not become part of exported column names.
                QVERIFY(!model.headerData(i, Qt::Horizontal).toString().contains("Output Summary"));
                if (model.rowCount() > 0)
                    QVERIFY(!(model.flags(model.index(0, i)) & Qt::ItemIsEditable));
            }
        }
    }

    /**
     *
     * @brief Require at least one statistic-tagged column in each non-gage spatial schema and
     * none for rain gages.
     * Pins column presence only, without running a simulation or checking statistic values.
     *
     */
    void everyRoutedCategoryHasADynamicsBlock()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        // Rain gages are the one category the engine keeps no statistics for.
        for (const auto cat : allCategories()) {
            if (cat == SWMMModelLayer::CatRainGages)
                continue;
            SWMMAttributeTableModel model;
            model.setSource(layer.get(), cat);
            const auto specs = model.columnSpecs();
            int n = 0;
            for (const auto &s : specs)
                if (isDynamics(s))
                    ++n;
            QVERIFY2(n > 0, qPrintable(QStringLiteral("category %1 has no dynamics "
                                                      "columns")
                                           .arg(int(cat))));
        }
        SWMMAttributeTableModel gages;
        gages.setSource(layer.get(), SWMMModelLayer::CatRainGages);
        for (const auto &s : gages.columnSpecs())
            QVERIFY(!isDynamics(s));
    }

    /**
     *
     * @brief Require pump schema to expose shared maximum flow and pump cycle/time/volume
     * statistic keys.
     * Checks metadata presence even if the fixture has no pump rows; does not run a pump.
     *
     */
    void pumpsCarryTheirUtilisationStatistics()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMMAttributeTableModel model;
        model.setSource(layer.get(), SWMMModelLayer::CatPumps);
        const auto specs = model.columnSpecs();
        // Shared link block plus the three pump-only ones.
        QVERIFY(colFor(specs, QStringLiteral("Max flow (stat)")) >= 0);
        QVERIFY(colFor(specs, QStringLiteral("Pump cycles")) >= 0);
        QVERIFY(colFor(specs, QStringLiteral("Pump on time (hr)")) >= 0);
        QVERIFY(colFor(specs, QStringLiteral("Pump volume")) >= 0);
    }

    // =====================================================================
    // 3 — attribute coverage the engine API offers
    // =====================================================================

    /**
     *
     * @brief Require the conduit Slope column to be read-only and produce a positive numeric
     * row-zero value.
     * Uses the fixture's native authored slope convention; does not assert an exact numeric
     * value, recompute geometry or test a post-run statistic.
     *
     */
    void conduitExposesComputedSlope()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMMAttributeTableModel model;
        model.setSource(layer.get(), SWMMModelLayer::CatConduits);
        const auto specs = model.columnSpecs();
        const int c = colFor(specs, QStringLiteral("Slope"));
        QVERIFY2(c >= 0, "conduit Slope column missing");
        QCOMPARE(specs[c].editor, EditorKind::ReadOnly);

        // Reads through the getter-only tag, not identifyByName.
        const QModelIndex idx = model.index(0, c);
        QVERIFY(!(model.flags(idx) & Qt::ItemIsEditable));
        bool ok = false;
        const double slope = model.data(idx, Qt::DisplayRole).toDouble(&ok);
        QVERIFY(ok);
        QVERIFY(slope > 0.0); // fixture drops 5 ft over 1000 ft
    }

    /**
     *
     * @brief Assign/clear a subcatchment snow-pack ref across table, property adapter and modal
     * picker.
     * Adds Snow1, requires a Snowpack DataObjectRef, selects Use Snow Pack through a queued
     * modal callback and verifies clearing via the adapter refreshes the table ref.
     * Writes a reviewed screenshot to surface_r1_out/snowpack-assignment.png under dataDir(),
     * checking directory creation/image save. Callback borrows local state only while modal
     * exec runs. No snow simulation or input-file persistence is tested.
     *
     */
    void snowPackPickerSharesAssignmentWithProperties()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMM_Engine eng = layer->engine();
        QCOMPARE(swmm_snowpack_add(eng, "Snow1"), SWMM_OK);
        SWMMAttributeTableModel model;
        model.setSource(layer.get(), SWMMModelLayer::CatSubcatchments);
        const int c = colFor(model.columnSpecs(), QStringLiteral("Snow pack"));
        QVERIFY(c >= 0);
        QVERIFY(model.rowCount() > 0);
        const auto idx = model.index(0, c);
        auto ref = model.data(idx, Qt::EditRole).value<DataObjectRef>();
        QCOMPARE(ref.kind, DataObjectRef::Snowpack);
        ref.currentName = "Snow1";
        QVERIFY(model.setData(idx, QVariant::fromValue(ref), Qt::EditRole));
        const auto sub = layer->objectNameAt(SWMMModelLayer::CatSubcatchments, 0);
        SWMMSubcatchPropertyAdapter adapter(eng, sub);
        QCOMPARE(adapter.snowPackRef().currentName, QStringLiteral("Snow1"));
        auto *registry = qobject_cast<openswmmvis::snowpack::SnowpackRegistry *>(
            layer->ensureSnowpackRegistry());
        QVERIFY(registry);
        bool selected = false;
        QTimer::singleShot(0, [&] {
            auto *dialog = qobject_cast<openswmmvis::ui::SnowpackEditorDialog *>(
                QApplication::activeModalWidget());
            if (!dialog)
                return;
            auto *buttons = dialog->findChild<QDialogButtonBox *>();
            if (!buttons) {
                dialog->reject();
                return;
            }
            auto *use = buttons->button(QDialogButtonBox::Close);
            selected = use && use->text() == QStringLiteral("Use Snow Pack");
            if (use)
                QTest::mouseClick(use, Qt::LeftButton);
            else
                dialog->reject();
        });
        QCOMPARE(
            openswmmvis::ui::SnowpackEditorDialog::pickSnowpack(registry, layer.get(), "Snow1"),
            QStringLiteral("Snow1"));
        QVERIFY(selected);
        registerDataObjectRefConverter();
        QTableView view;
        view.setModel(&model);
        for (int k = 0; k < model.columnCount(); ++k)
            view.setColumnHidden(k, k != c && model.columnSpecs()[k].key != QStringLiteral("Name"));
        view.resize(640, 240);
        view.setWindowTitle(QStringLiteral("Subcatchment snow-pack assignment"));
        view.show();
        view.resizeColumnsToContents();
        QTest::qWait(30);
        const QString out = QDir(dataDir()).filePath(QStringLiteral("surface_r1_out"));
        QVERIFY(QDir().mkpath(out));
        QVERIFY(view.grab().save(QDir(out).filePath(QStringLiteral("snowpack-assignment.png"))));
        ref.currentName.clear();
        adapter.setSnowPackRef(ref);
        model.refreshObject(sub);
        QVERIFY(model.data(idx, Qt::EditRole).value<DataObjectRef>().currentName.isEmpty());
    }

    /**
     *
     * @brief Require editable subcatchment Tag schema and round-trip basin-7 through the model.
     * Checks row-zero display after setData, without save/reopen or undo coverage.
     *
     */
    void subcatchmentExposesTag()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMMAttributeTableModel model;
        model.setSource(layer.get(), SWMMModelLayer::CatSubcatchments);
        const auto specs = model.columnSpecs();
        const int c = colFor(specs, QStringLiteral("Tag"));
        QVERIFY2(c >= 0, "subcatchment Tag column missing");
        QVERIFY(model.rowCount() > 0);

        const QModelIndex idx = model.index(0, c);
        QVERIFY(model.flags(idx) & Qt::ItemIsEditable);
        QVERIFY(model.setData(idx, QStringLiteral("basin-7"), Qt::EditRole));
        QCOMPARE(model.data(idx, Qt::DisplayRole).toString(), QStringLiteral("basin-7"));
    }

    /**
     *
     * @brief Require storage schema keys for exfiltration suction, conductivity and deficit.
     * Metadata presence is the assertion; no storage row values, editability or seepage run.
     *
     */
    void storageExposesExfiltrationParameters()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMMAttributeTableModel model;
        model.setSource(layer.get(), SWMMModelLayer::CatStorage);
        const auto specs = model.columnSpecs();
        for (const char *key : {"Exfil suction", "Exfil conduct.", "Exfil deficit"})
            QVERIFY2(
                colFor(specs, QString::fromLatin1(key)) >= 0,
                qPrintable(
                    QStringLiteral("storage %1 column missing").arg(QString::fromLatin1(key))));
    }

    /**
     *
     * @brief Require the outfall Route to column and its Compound editor kind.
     * Does not commit a destination or verify routing/run behavior.
     *
     */
    void outfallExposesRouteTo()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMMAttributeTableModel model;
        model.setSource(layer.get(), SWMMModelLayer::CatOutfalls);
        const auto specs = model.columnSpecs();
        const int c = colFor(specs, QStringLiteral("Route to"));
        QVERIFY2(c >= 0, "outfall Route To column missing");
        QCOMPARE(specs[c].editor, EditorKind::Compound);
    }

    // =====================================================================
    // Panel-level: the delegates actually reach the view
    // =====================================================================

    /**
     *
     * @brief Require real grouped-header label clicks to sort names and numeric elevations in
     * both directions.
     * Sets J1/X1 elevations to 2/10 so lexicographic numeric order would fail, clicks Name
     * and Invert columns twice over two source-switch passes, and checks row identities/
     * sort indicators. Also saves a nonclickable header state to exercise restoration repair.
     * Can persist column layouts through panel destruction; no project data-file save.
     *
     */
    void columnHeaderClicksSortRows()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        const auto engine = layer->engine();
        // Deliberately use values whose numeric and text orders differ.
        QCOMPARE(swmm_node_set_invert_elev(engine, swmm_node_index(engine, "J1"), 2.0), SWMM_OK);
        QCOMPARE(swmm_node_set_invert_elev(engine, swmm_node_index(engine, "X1"), 10.0), SWMM_OK);
        AttributeTablePanel panel;
        auto *view = panel.findChild<QTableView *>();
        QVERIFY(view);
        QVERIFY(view->horizontalHeader()->sectionsClickable());
        panel.setProject(layer.get(), nullptr, nullptr);
        panel.resize(1000, 600);
        panel.show();

        for (int pass = 0; pass < 2; ++pass) {
            panel.showModelCategory(layer.get(), SWMMModelLayer::CatJunctions);
            QCoreApplication::processEvents();
            SWMMAttributeTableModel schema;
            schema.setSource(layer.get(), SWMMModelLayer::CatJunctions);
            const int numericColumn = colFor(schema.columnSpecs(), QStringLiteral("Invert elev"));
            QVERIFY(numericColumn >= 0);
            QCOMPARE(view->model()->rowCount(), 2);
            auto *header = view->horizontalHeader();
            for (const int column : {0, numericColumn}) {
                view->sortByColumn(column, Qt::DescendingOrder);
                const QPoint click(header->sectionViewportPosition(column) +
                                       header->sectionSize(column) / 2,
                                   header->height() - 5); // ordinary label below the group band
                for (const auto order : {Qt::AscendingOrder, Qt::DescendingOrder}) {
                    QTest::mouseClick(header->viewport(), Qt::LeftButton, Qt::NoModifier, click);
                    QCOMPARE(header->sortIndicatorSection(), column);
                    QCOMPARE(header->sortIndicatorOrder(), order);
                    QCOMPARE(view->model()->index(0, 0).data().toString(),
                             order == Qt::AscendingOrder ? QStringLiteral("J1")
                                                         : QStringLiteral("X1"));
                    QCOMPARE(view->model()->index(1, 0).data().toString(),
                             order == Qt::AscendingOrder ? QStringLiteral("X1")
                                                         : QStringLiteral("J1"));
                }
            }
            // Reproduce header state saved by the broken grouped header.
            // Restoring column widths must not restore disabled sorting clicks.
            header->setSectionsClickable(false);
            panel.showModelCategory(layer.get(), SWMMModelLayer::CatRainGages);
        }
    }

    /**
     *
     * @brief Exercise wide/narrow category switches repeatedly, requiring live editors and a
     * renderable panel.
     * Across three passes and six category visits, flushes deferred deletes, requires each
     * cell to resolve a delegate, bounds direct-child delegate count by current width and
     * grabs a non-null widget image. Does not save that image or prove general leak freedom.
     *
     */
    void categorySwitchesRetainLiveDelegates()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        AttributeTablePanel panel;
        panel.setProject(layer.get(), nullptr, nullptr);
        auto *view = panel.findChild<QTableView *>();
        QVERIFY(view);
        panel.resize(1000, 600);
        panel.show();
        // Narrower schemas used to leave high-numbered column bindings behind
        // when their delegates were deleted. Returning to a wider table then
        // crashed in Qt's drawCell() on a null delegate.
        const QList<SWMMModelLayer::Category> categories = {
            SWMMModelLayer::CatConduits,      SWMMModelLayer::CatRainGages,
            SWMMModelLayer::CatJunctions,     SWMMModelLayer::CatOutfalls,
            SWMMModelLayer::CatSubcatchments, SWMMModelLayer::CatConduits};
        for (int pass = 0; pass < 3; ++pass) {
            for (const auto category : categories) {
                panel.showModelCategory(layer.get(), category);
                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                QVERIFY(view->model()->rowCount() > 0);
                for (int column = 0; column < view->model()->columnCount(); ++column) {
                    QVERIFY2(view->itemDelegateForIndex(view->model()->index(0, column)),
                             qPrintable(QString("Missing delegate in category %1, column %2")
                                            .arg(int(category))
                                            .arg(column)));
                }
                // Retired editors must be released as well as detached.
                QVERIFY(
                    panel.findChildren<QStyledItemDelegate *>(QString(), Qt::FindDirectChildrenOnly)
                        .size() <= view->model()->columnCount());
                QCoreApplication::processEvents();
                QVERIFY(!panel.grab().isNull());
            }
        }
    }

    /**
     *
     * @brief Require a real conduit table to show its compound cross-section and all geom
     * delegates.
     * Checks visible columns, a compound Edit affordance and non-null per-geom delegates.
     * Local header finder borrows an exact display label and returns first logical column
     * or minus one. Does not open the compound editor or commit geometry values.
     *
     */
    void panelInstallsCrossSectionDelegates()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        AttributeTablePanel panel;
        panel.setProject(layer.get(), nullptr, nullptr);
        panel.refresh();

        auto *combo = panel.findChild<QComboBox *>();
        QVERIFY(combo);
        int catIdx = -1;
        for (int i = 0; i < combo->count(); ++i)
            if (combo->itemText(i).startsWith(QStringLiteral("Conduits")))
                catIdx = i;
        QVERIFY(catIdx >= 0);
        combo->setCurrentIndex(catIdx);

        auto *view = panel.findChild<QTableView *>();
        QVERIFY(view);
        auto *m = view->model();
        QVERIFY(m);

        auto headerCol = [&](const QString &label) {
            for (int c = 0; c < m->columnCount(); ++c)
                if (m->headerData(c, Qt::Horizontal).toString() == label)
                    return c;
            return -1;
        };

        const int xs = headerCol(QStringLiteral("Cross Section"));
        QVERIFY(xs >= 0);
        QVERIFY(!view->isColumnHidden(xs));
        auto *del = qobject_cast<QStyledItemDelegate *>(view->itemDelegateForColumn(xs));
        QVERIFY2(del, "no delegate on the Cross Section column");
        // The compound cell renders a summary plus the "Edit…" affordance.
        const QString shown = del->displayText(m->index(0, xs).data(Qt::EditRole), QLocale());
        QVERIFY2(shown.contains(QStringLiteral("Edit")),
                 qPrintable(QStringLiteral("Cross Section cell shows '%1'").arg(shown)));

        for (int k = 1; k <= 4; ++k) {
            const int c = headerCol(QStringLiteral("Geom %1").arg(k));
            QVERIFY(c >= 0);
            QVERIFY(!view->isColumnHidden(c));
            QVERIFY(view->itemDelegateForColumn(c) != nullptr);
        }
    }

    // =====================================================================
    // Panel-level: the query bar filters on the columns it names
    // =====================================================================
    //
    // The filter proxy used to materialise EVERY column of every row (twice)
    // to test a predicate that reads one. It now resolves the predicate's
    // field names to columns once and reads only those. That is only a
    // performance change if the accepted-row set is unchanged, so these pin
    // the semantics that made the old build "work": a field is matchable by
    // its ColumnSpec key, by its label, or by the unit-suffixed header the
    // view displays.

    /**
     *
     * @brief Require schema key, label and unit-suffixed header aliases to select identical row
     * sets.
     * Applies plain-decimal predicates true for all junctions, then inverted predicates false
     * for all, proving the field is actually evaluated. Local match counter borrows field
     * and operator strings, sends Return and returns visible row count. Does not benchmark
     * column-read counts or prove behavior for arbitrary unsupported field names.
     *
     */
    void queryAcceptsKeyLabelAndHeaderSpellings()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        AttributeTablePanel panel;
        panel.setProject(layer.get(), nullptr, nullptr);
        panel.refresh();

        auto *combo = panel.findChild<QComboBox *>();
        QVERIFY(combo);
        int catIdx = -1;
        for (int i = 0; i < combo->count(); ++i)
            if (combo->itemText(i).startsWith(QStringLiteral("Junctions")))
                catIdx = i;
        QVERIFY(catIdx >= 0);
        combo->setCurrentIndex(catIdx);

        auto *view = panel.findChild<QTableView *>();
        auto *edit = panel.findChild<QLineEdit *>();
        QVERIFY(view && edit);
        auto *m = view->model();
        QVERIFY(m);
        const int unfiltered = m->rowCount();
        QVERIFY2(unfiltered > 0, "fixture has no junctions");

        // "Invert elev" is the ColumnSpec key; "Invert Elev" the label; the
        // header adds a unit suffix. All three must select the same rows.
        SWMMAttributeTableModel probe;
        probe.setSource(layer.get(), SWMMModelLayer::CatJunctions);
        const auto specs = probe.columnSpecs();
        const int col = colFor(specs, QStringLiteral("Invert elev"));
        QVERIFY2(col >= 0, "fixture schema lost the Invert elev column");

        QString header;
        for (int c = 0; c < m->columnCount(); ++c)
            if (m->headerData(c, Qt::Horizontal).toString().startsWith(specs[col].label))
                header = m->headerData(c, Qt::Horizontal).toString();
        QVERIFY(!header.isEmpty());

        // Use bounded plain decimals for both alias predicates. A parse error
        // would make Apply return early WITHOUT touching the
        // filter, which would leave every assertion below reading the
        // previous query's result instead of this one's.
        auto matchCount = [&](const QString &field, const char *op) {
            edit->setText(QStringLiteral("[%1] %2 -99999").arg(field, op));
            QTest::keyClick(edit, Qt::Key_Return);
            return view->model()->rowCount();
        };

        // A predicate true for every row keeps every row...
        QCOMPARE(matchCount(specs[col].key, ">"), unfiltered);
        QCOMPARE(matchCount(specs[col].label, ">"), unfiltered);
        QCOMPARE(matchCount(header, ">"), unfiltered);

        // ...and the same predicate inverted drops them all, which proves
        // the column is actually being READ rather than silently skipped
        // (a skipped column would leave the row count at `unfiltered` in
        // both directions).
        QCOMPARE(matchCount(specs[col].key, "<"), 0);
        QCOMPARE(matchCount(specs[col].label, "<"), 0);
        QCOMPARE(matchCount(header, "<"), 0);
    }

    /**
     *
     * @brief Require unknown and every nonqueryable schema field to report errors without
     * replacing a valid one-row filter.
     * Escapes the chosen object's quote in its valid query, checks error styling and row
     * count after each refused draft. Does not inspect the selection bus or persist queries.
     *
     */
    void queryOnUnresolvableFieldPreservesPreviousFilter()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        AttributeTablePanel panel;
        panel.setProject(layer.get(), nullptr, nullptr);
        panel.refresh();

        auto *view = panel.findChild<QTableView *>();
        auto *edit = panel.findChild<QLineEdit *>("attributeQuery");
        auto *model = panel.findChild<SWMMAttributeTableModel *>();
        QVERIFY(view && edit && model);
        QString name = view->model()->index(0, 0).data().toString();
        name.replace('\'', "''");
        edit->setText(QString("Name = '%1'").arg(name));
        QTest::keyClick(edit, Qt::Key_Return);
        QCOMPARE(view->model()->rowCount(), 1);
        QVERIFY(edit->styleSheet().isEmpty());

        edit->setText(QStringLiteral("NOT [No Such Column] = 1"));
        QTest::keyClick(edit, Qt::Key_Return);
        QCOMPARE(view->model()->rowCount(), 1);
        QVERIFY(!edit->styleSheet().isEmpty());
        for (const auto &spec : model->columnSpecs()) {
            if (spec.queryable)
                continue;
            edit->setText(QString("[%1] = 1").arg(spec.key));
            QTest::keyClick(edit, Qt::Key_Return);
            QCOMPARE(view->model()->rowCount(), 1);
            QVERIFY(!edit->styleSheet().isEmpty());
        }
    }

    // =====================================================================
    // 5 — compound-cell summaries follow the engine
    // =====================================================================
    //
    // Compound cells (External Inflows, Dry Weather Flow, RDII, Treatment)
    // are served from m_inflowCountByNode & friends, filled once by
    // ensureCompoundCacheBuilt(). That cache was only ever dropped by
    // setSource() / reload() — NOT by refreshObject(), and not by the
    // Compound branch of commitValueDirect(), whose own comment claims it
    // "invalidates the row cache so the summary recomputes on the next
    // paint". It invalidated m_rowCache, which is not where the summary
    // comes from. So adding an inflow through the table's own compound
    // dialog left the cell reading "(none)" until a category switch.

    /**
     *
     * @brief Require a previously cached inflow compound summary to change after a direct engine
     * edit and refreshObject().
     * Builds the cache first, adds a FLOW external inflow with baseline 2.5 through the API,
     * refreshes the named junction and compares summary strings. Does not invoke the modal
     * compound editor, check a particular summary spelling or run hydraulics.
     *
     */
    void compoundSummaryRefreshesAfterEngineEdit()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMMAttributeTableModel model;
        model.setSource(layer.get(), SWMMModelLayer::CatJunctions);
        const auto specs = model.columnSpecs();
        const int col = colFor(specs, QStringLiteral("Inflows"));
        QVERIFY2(col >= 0, "junction schema lost the Inflows compound column");
        QVERIFY(model.rowCount() > 0);

        const QModelIndex idx = model.index(0, col);
        const QString name = model.objectNameAt(0);

        // Touch the cell first so the compound cache is genuinely built —
        // otherwise a later first-build would mask a missing invalidation.
        const QVariant before = model.data(idx, Qt::DisplayRole);
        QVERIFY(before.isValid());
        const QString summaryBefore = before.value<NodeCompoundEditRef>().summary;

        // Add an inflow behind the model's back, exactly as the compound
        // dialog does (it writes to the engine, then the table is told to
        // refresh the object).
        SWMM_Engine eng = layer->engine();
        QVERIFY(eng);
        const int ni = swmm_node_index(eng, name.toUtf8().constData());
        QVERIFY(ni >= 0);
        QCOMPARE(swmm_ext_inflow_add(eng, ni, "FLOW", "", "FLOW", 1.0, 1.0, 2.5, ""), SWMM_OK);

        model.refreshObject(name);

        const QString summaryAfter =
            model.data(idx, Qt::DisplayRole).value<NodeCompoundEditRef>().summary;
        QVERIFY2(summaryAfter != summaryBefore,
                 qPrintable(QStringLiteral("compound summary stayed '%1' after an engine edit + "
                                           "refreshObject — the compound cache was not invalidated")
                                .arg(summaryAfter)));
    }

    // =====================================================================
    // 4 — SoA index == engine index
    // =====================================================================
    //
    // SWMMAttributeTableModel::data() resolves a row's engine index with
    // SWMMModelLayer::soaIndexAt() instead of re-deriving it from the
    // object's name. That is only correct while the layer's SoA index and
    // the engine's index are the same number. The invariant is already
    // load-bearing elsewhere (applyNodeDelete feeds a swmm_node_index()
    // result straight into m_nodes.removeAt()), but nothing pinned it —
    // and a violation would be silent: the cell would show a NEIGHBOURING
    // object's value rather than going blank.

    /**
     *
     * @brief Require every loaded category row's displayed name to match its SoA/native engine
     * index identity.
     * Iterates allCategories() and all extant rows; empty categories contribute no row checks.
     * Does not mutate order or initialize/run a model.
     *
     */
    void soaIndexMatchesEngineIndex()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMM_Engine eng = layer->engine();
        QVERIFY(eng);

        for (auto cat : allCategories()) {
            const int n = layer->categoryCount(cat);
            for (int row = 0; row < n; ++row) {
                const QString name = layer->objectNameAt(cat, row);
                const int idx = layer->soaIndexAt(cat, row);
                QVERIFY2(
                    idx >= 0,
                    qPrintable(QStringLiteral("cat %1 row %2: no index").arg(int(cat)).arg(row)));
                QCOMPARE(QString::fromUtf8(engineIdFor(eng, cat, idx)), name);
            }
        }
    }

    /**
     *
     * @brief Require native index identity mapping to follow a real reversed junction display
     * permutation.
     * Builds an explicit order if needed, reverses it and first proves the visible first name
     * changed, then checks every row's name against engineIdFor(). Does not save the override.
     *
     */
    void soaIndexHonoursOrderingOverride()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMM_Engine eng = layer->engine();
        QVERIFY(eng);

        const auto cat = SWMMModelLayer::CatJunctions;
        const int n = layer->categoryCount(cat);
        QVERIFY2(n >= 2, "fixture needs >= 2 junctions to permute");

        // Reverse the default order — a permutation, so setObjectOrder
        // accepts it.
        QVector<int> order = layer->objectOrder(cat);
        if (order.isEmpty()) {
            order.reserve(n);
            for (int r = 0; r < n; ++r)
                order << layer->soaIndexAt(cat, r);
        }
        const QString firstBefore = layer->objectNameAt(cat, 0);
        std::reverse(order.begin(), order.end());
        layer->setObjectOrder(cat, order);

        // setObjectOrder rejects a non-permutation by silently returning,
        // which would leave this test asserting the DEFAULT path twice and
        // pin nothing. Prove the override actually took effect first.
        QVERIFY2(layer->objectNameAt(cat, 0) != firstBefore,
                 "setObjectOrder did not reorder — the override path below "
                 "would be untested");

        for (int row = 0; row < n; ++row) {
            const QString name = layer->objectNameAt(cat, row);
            const int idx = layer->soaIndexAt(cat, row);
            QVERIFY(idx >= 0);
            QCOMPARE(QString::fromUtf8(engineIdFor(eng, cat, idx)), name);
        }
    }

    // =====================================================================
    // 5 — [INITIAL_QUALITY] override columns (initial-quality UI round)
    // =====================================================================

    /**
     *
     * @brief Require initial-quality columns on node/link schemas and age gating after a schema
     * rebuild.
     * Adds TSS, checks editable-text/schema setter metadata for nodes/conduits, absence on
     * subcatchment/gage schemas and no reserved age key until WATER_AGE YES. Does not run
     * transport or commit constituent values in this case.
     *
     */
    void initialQualityColumnsFollowConstituentsAndScope()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMM_Engine eng = layer->engine();
        QCOMPARE(swmm_pollutant_add(eng, "TSS", 0 /*MG/L*/), SWMM_OK);

        SWMMAttributeTableModel model;
        const QString tssKey = QStringLiteral("initq:TSS");
        const QString ageKey = QStringLiteral("initq:__WATER_AGE__");

        // Node and link categories carry one editable Text column per
        // pollutant; the reserved age column stays hidden while the
        // option is off.
        for (auto cat : {SWMMModelLayer::CatJunctions, SWMMModelLayer::CatConduits}) {
            model.setSource(layer.get(), cat);
            const auto specs = model.columnSpecs();
            const int c = colFor(specs, tssKey);
            QVERIFY2(c >= 0, "initq:TSS column missing");
            QCOMPARE(specs[c].editor, EditorKind::Text);
            QVERIFY(!specs[c].setter.isEmpty());
            QCOMPARE(colFor(specs, ageKey), -1);
        }

        // Non-node/link categories get no initial-quality columns —
        // [INITIAL_QUALITY] scopes are NODE | LINK only.
        model.setSource(layer.get(), SWMMModelLayer::CatSubcatchments);
        QCOMPARE(colFor(model.columnSpecs(), tssKey), -1);
        model.setSource(layer.get(), SWMMModelLayer::CatRainGages);
        QCOMPARE(colFor(model.columnSpecs(), tssKey), -1);

        // Turning WATER_AGE on surfaces the reserved age column on the
        // next schema rebuild.
        QCOMPARE(swmm_options_set(eng, "WATER_AGE", "YES"), SWMM_OK);
        model.setSource(layer.get(), SWMMModelLayer::CatJunctions);
        QVERIFY(colFor(model.columnSpecs(), ageKey) >= 0);
    }

    /**
     *
     * @brief Require a junction TSS override to upsert/read/clear through the engine and table.
     * Checks blank unset display, exact row identity/TSS/value12.5, EditRole text and count
     * retention after garbage/negative refusals. Clearing removes the override and a second
     * clear returns false. Does not compare post-refusal value separately or save/reopen.
     *
     */
    void initialQualityCellRoundTripsThroughEngine()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMM_Engine eng = layer->engine();
        QCOMPARE(swmm_pollutant_add(eng, "TSS", 0 /*MG/L*/), SWMM_OK);

        SWMMAttributeTableModel model;
        model.setSource(layer.get(), SWMMModelLayer::CatJunctions);
        const int col = colFor(model.columnSpecs(), QStringLiteral("initq:TSS"));
        QVERIFY(col >= 0);
        const int row = model.rowForName(QStringLiteral("J1"));
        QVERIFY(row >= 0);
        const QModelIndex idx = model.index(row, col);
        QVERIFY(model.flags(idx) & Qt::ItemIsEditable);

        // Unset reads blank — the global initial concentration applies.
        QVERIFY(!model.data(idx, Qt::DisplayRole).isValid());

        // Numeric commit upserts the element's [INITIAL_QUALITY] row.
        QVERIFY(model.commitValueDirect(idx, QStringLiteral("12.5")));
        QCOMPARE(swmm_init_quality_count(eng), 1);
        int is_link = 0, elem = -1;
        char cons[64];
        double v = 0.0;
        QCOMPARE(swmm_init_quality_get(eng, 0, &is_link, &elem, cons, sizeof(cons), &v), SWMM_OK);
        QCOMPARE(is_link, 0);
        QCOMPARE(elem, swmm_node_index(eng, "J1"));
        QCOMPARE(QString::fromUtf8(cons), QStringLiteral("TSS"));
        QCOMPARE(v, 12.5);
        QCOMPARE(model.data(idx, Qt::EditRole).toString(), QStringLiteral("12.5"));

        // Garbage and negative pollutant commits are refused and leave
        // the store untouched (the engine setter validates the sign).
        QVERIFY(!model.commitValueDirect(idx, QStringLiteral("abc")));
        QVERIFY(!model.commitValueDirect(idx, QStringLiteral("-5")));
        QCOMPARE(swmm_init_quality_count(eng), 1);

        // Blank commit clears the override; a second blank is a no-op.
        QVERIFY(model.commitValueDirect(idx, QString()));
        QCOMPARE(swmm_init_quality_count(eng), 0);
        QVERIFY(!model.data(idx, Qt::DisplayRole).isValid());
        QVERIFY(!model.commitValueDirect(idx, QString()));
    }
};

QTEST_MAIN(TestAttributeTableSchema)
#include "test_attributetableschema.moc"
