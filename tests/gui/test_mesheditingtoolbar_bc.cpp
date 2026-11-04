/**
 * @file      test_mesheditingtoolbar_bc.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Mesh boundary editing after the inline editors left the ribbon (process-aware
 *            editing plan, Phase 2 gate).
 *
 * The Mesh 2D tab used to carry value editors (BC type combo and parameter pages, wall drag,
 * conveyance, vertex Z/tag/coupling, cell parameters) that appeared with the selection. They
 * moved to Properties (one element) and the attribute table (bulk, one undo step), including
 * the shared boundary-condition editor.
 * Every scenario the old toolbar test pinned is re-run here through those routes:
 * bulk edits are one command and undo per element, displaying a selection never writes,
 * mixed selections do not impersonate a uniform value, Properties shows each edge's own
 * record, the two views agree, invalid types are no-ops — plus the ribbon contract itself
 * (no value editors, width independent of the selection, process groups one at a time).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */
#include "layers/swmm2dmeshlayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "map/meshcommands.h"
#include "mesh/inpmeshreader.h"
#include "mesh/inpmeshwriter.h"
#include "mesh/meshcellgeom.h"
#include "mesh/meshedgebcremap.h"
#include "mesh/meshobjectref.h"
#include "mesh/meshresult.h"
#include "selection/selectionmanager.h"
#include "ui/panels/attributetablepanel.h"
#include "ui/panels/meshattributetablemodel.h"
#include "ui/panels/propertiespanel.h"
#include "ui/toolbars/mesheditingtoolbar.h"
#include "ui/toolbars/ribbongroup.h"

#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QLineEdit>
#include <QPushButton>
#include <QTest>
#include <QTreeView>

#include <cmath>

using Kind = MeshAttributeTableModel::Kind;
using mesh::MeshBCTypes;
using openswmmvis::ui::RibbonGroup;

namespace
{

void chooseBoundaryType(QComboBox *combo, int type)
{
    const int index = combo->findData(type);
    QVERIFY(index >= 0);
    combo->setCurrentIndex(index);
    QVERIFY(QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, index)));
}

void editBoundaryValue(QDialog *dialog, const QString &text)
{
    auto *value = dialog->findChild<QLineEdit *>("boundaryValue");
    QVERIFY(value);
    value->setText(text);
    value->setModified(true);
    QVERIFY(QMetaObject::invokeMethod(value, "editingFinished"));
}


/*! `wide` × 1 strip of unit quads, two triangles each (the
 *  test_meshcommands_vertex_edge fixture). */
mesh::MeshResult makeStrip(int wide)
{
    mesh::MeshResult m;
    for (int x = 0; x <= wide; ++x) {
        mesh::MeshVertex b;
        b.xy = QPointF(double(x), 0.0);
        m.vertices.append(b);
        mesh::MeshVertex t;
        t.xy = QPointF(double(x), 1.0);
        m.vertices.append(t);
    }
    for (int x = 0; x < wide; ++x) {
        const int b0 = x * 2, t0 = x * 2 + 1, b1 = (x + 1) * 2, t1 = (x + 1) * 2 + 1;
        mesh::MeshTriangle a;
        a.v0 = b0;
        a.v1 = b1;
        a.v2 = t1;
        m.triangles.append(a);
        mesh::MeshTriangle c;
        c.v0 = b0;
        c.v1 = t1;
        c.v2 = t0;
        m.triangles.append(c);
    }
    m.ok = true;
    return m;
}

/*! First \p n exterior edge slots on \p layer, in (tri, e) scan order. */
QVector<QPair<int, int>> boundarySlots(const SWMM2DMeshLayer &layer, int n)
{
    QVector<QPair<int, int>> out;
    for (int t = 0; t < layer.triangleCount() && out.size() < n; ++t)
        for (int k = 0; k < 3 && out.size() < n; ++k)
            if (layer.isBoundaryEdge(t, k))
                out.append({t, k});
    return out;
}

/*! First interior edge slot on \p layer. */
QPair<int, int> interiorSlot(const SWMM2DMeshLayer &layer)
{
    for (int t = 0; t < layer.triangleCount(); ++t)
        for (int k = 0; k < 3; ++k)
            if (!layer.isBoundaryEdge(t, k))
                return {t, k};
    return {-1, -1};
}

mesh::MeshEdgeBC stageTsBC(const QString &ts)
{
    mesh::MeshEdgeBC bc;
    bc.type = MeshBCTypes::Type::SpecifiedStageTS;
    bc.tseries = ts;
    return bc;
}

int colFor(const MeshAttributeTableModel &model, const char *key)
{
    const auto specs = model.columnSpecs();
    for (int i = 0; i < specs.size(); ++i)
        if (specs[i].key == QLatin1String(key))
            return i;
    return -1;
}

/*! Everything one test needs, wired the way SWMMVis wires it: the ribbon,
 *  the edge table and the property grid over one mesh, canvas and selection. */
struct Rig {
    MapCanvas canvas;
    SWMM2DMeshLayer *layer; // owned by the canvas
    SelectionManager sel;
    MeshEditingToolbar tb{QStringLiteral("t")};
    MeshAttributeTableModel table;
    PropertiesPanel props;

    explicit Rig(int wide = 4) : Rig(makeStrip(wide)) {}
    explicit Rig(const mesh::MeshResult &m) : layer(new SWMM2DMeshLayer(m, QString()))
    {
        canvas.addLayer(layer, /*pushUndo=*/false);
        tb.rebindCanvas(&canvas);
        tb.rebindSelectionManager(&sel);
        table.setCanvas(&canvas);
        table.setSource(layer, Kind::Edge);
        // Properties follows the selection the way SWMMVis drives it: one
        // mesh element shows, anything else clears.
        QObject::connect(&sel, &SelectionManager::selectionChanged, &props,
                         [this](const QSet<SWMMObjectRef> &current, const QSet<SWMMObjectRef> &,
                                const QSet<SWMMObjectRef> &) {
                             if (current.size() == 1)
                                 props.showMeshObject(&canvas, *current.cbegin());
                             else
                                 props.clear();
                         });
    }
    SWMMObjectRef edgeRef(const QPair<int, int> &slot) const
    {
        return mesh::MeshObjectRef::edge(layer->sourcePath(), slot.first, slot.second);
    }
    int row(const QPair<int, int> &slot) const { return table.rowForRef(edgeRef(slot)); }
    void select(const QVector<QPair<int, int>> &edgeSlots)
    {
        QSet<SWMMObjectRef> refs;
        for (const auto &slot : edgeSlots)
            refs.insert(edgeRef(slot));
        sel.select(refs, SelectionManager::Mode::Replace);
    }
    /*! Open the attribute table's shared editor for the given edge slots. */
    void editBoundaryConditions(const QVector<QPair<int, int>> &edgeSlots)
    {
        QList<int> rows;
        for (const auto &slot : edgeSlots)
            rows.append(row(slot));
        table.editBoundaryCondition(rows, &props);
    }
    /*! The Properties value cell for the table column \p key of the shown element. */
    QModelIndex field(const char *key)
    {
        auto *tree = props.findChild<QTreeView *>();
        if (!tree)
            return {};
        auto *model = tree->model();
        const auto label = table.headerData(colFor(table, key), Qt::Horizontal);
        for (int i = 0; i < model->rowCount(); ++i) {
            const auto group = model->index(i, 0);
            for (int j = 0; j < model->rowCount(group); ++j)
                if (model->index(j, 0, group).data() == label)
                    return model->index(j, 1, group);
        }
        return {};
    }
    /*! True when the Properties tree shows \p text as some field's value. */
    bool propertiesShow(const QString &text)
    {
        auto *tree = props.findChild<QTreeView *>();
        if (!tree)
            return false;
        auto *model = tree->model();
        for (int i = 0; i < model->rowCount(); ++i) {
            const auto group = model->index(i, 0);
            for (int j = 0; j < model->rowCount(group); ++j) {
                const auto cell = model->index(j, 1, group);
                if (cell.data(Qt::DisplayRole).toString() == text ||
                    cell.data(Qt::EditRole).toString() == text)
                    return true;
            }
        }
        return false;
    }
    QDialog *boundaryDialog() const
    {
        for (QWidget *w : QApplication::topLevelWidgets())
            if (auto *d = qobject_cast<QDialog *>(w);
                d && d->objectName() == QLatin1String("meshBoundaryDialog") && d->isVisible())
                return d;
        return props.findChild<QDialog *>(QStringLiteral("meshBoundaryDialog"));
    }
    QAction *action(const char *name) const { return tb.findChild<QAction *>(QLatin1String(name)); }
};

} // namespace

class TestMeshEditingToolbarBC : public QObject
{
    Q_OBJECT

private slots:
    /*! Wall drag on two of three edges through the table: one undo command,
     *  undo/redo per edge, INP round trip and remap retention (the wall-
     *  friction verification fixture). */
    void wallDragBulkThroughTableUndoAndRemap()
    {
        Rig rig;
        const auto bs = boundarySlots(*rig.layer, 3);
        QCOMPARE(bs.size(), 3);
        const int column = colFor(rig.table, "wallDrag");
        QVERIFY(column >= 0);
        const int before = rig.canvas.undoStack()->count();
        rig.table.setDataForRows(column, {rig.row(bs[0]), rig.row(bs[1])}, 0.02);
        QCOMPARE(rig.canvas.undoStack()->count(), before + 1);
        const int a = mesh::edgeSlot(bs[0].first, bs[0].second);
        const int b = mesh::edgeSlot(bs[1].first, bs[1].second);
        const int c = mesh::edgeSlot(bs[2].first, bs[2].second);
        QCOMPARE(rig.layer->edgeBCs()[a].wallDrag, 0.02);
        QCOMPARE(rig.layer->edgeBCs()[b].wallDrag, 0.02);
        QCOMPARE(rig.layer->edgeBCs()[c].wallDrag, 0.);
        rig.canvas.undoStack()->undo();
        QCOMPARE(rig.layer->edgeBCs()[a].wallDrag, 0.);
        QCOMPARE(rig.layer->edgeBCs()[b].wallDrag, 0.);
        rig.canvas.undoStack()->redo();
        QCOMPARE(rig.layer->edgeBCs()[a].wallDrag, 0.02);
        QCOMPARE(rig.table.data(rig.table.index(rig.row(bs[0]), column)).toDouble(), 0.02);
        QVERIFY(mesh::InpMeshWriter::buildBCSectionText(rig.layer->edgeBCs()).contains("WALL_DRAG"));

        auto refined = makeStrip(8);
        for (auto &v : refined.vertices)
            v.xy.setX(v.xy.x() * .5);
        const auto remap = mesh::remapEdgeBCs(rig.layer->mesh(), rig.layer->edgeBCs(), refined);
        int retained = 0;
        for (const auto &bc : remap.bcs)
            if (bc.wallDrag == .02)
                ++retained;
        QCOMPARE(retained, 4);
        QCOMPARE(remap.droppedAssignments, 0);

        const auto dir = QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", "."))
                             .absoluteFilePath("../../verification/wall_friction_2026-10-24/gui-results");
        QDir().mkpath(dir);
        QFile file(QDir(dir).filePath("gui-wall.inp"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QByteArray text = "[2D_VERTICES]\n";
        for (const auto &v : rig.layer->mesh().vertices)
            text += QString("%1 %2 0\n").arg(v.xy.x()).arg(v.xy.y()).toUtf8();
        text += "[2D_TRIANGLES]\n";
        for (const auto &t : rig.layer->mesh().triangles)
            text += QString("%1 %2 %3 .01 .5\n").arg(t.v0).arg(t.v1).arg(t.v2).toUtf8();
        text += mesh::InpMeshWriter::buildBCSectionText(rig.layer->edgeBCs()).toUtf8();
        file.write(text);
        file.close();
        const auto reopened = mesh::InpMeshReader::read(file.fileName());
        QVERIFY2(reopened.errorMsg.isEmpty(), qPrintable(reopened.errorMsg));
        QCOMPARE(reopened.edgeBCs[a].wallDrag, .02);
    }

    /*! The reported bug, now in Properties: after a bulk assignment each
     *  edge shows its OWN time series, not the last one written. */
    void propertiesShowEachEdgesOwnTimeseries()
    {
        Rig rig;
        const auto bs = boundarySlots(*rig.layer, 2);
        QVERIFY(mesh::pushEdgeBCEdit(rig.layer, {bs[0]}, stageTsBC("TS_A"), nullptr) > 0);
        QVERIFY(mesh::pushEdgeBCEdit(rig.layer, {bs[1]}, stageTsBC("TS_B"), nullptr) > 0);
        rig.props.showMeshObject(&rig.canvas, rig.edgeRef(bs[0]));
        QVERIFY(rig.field("tseries").isValid());
        QCOMPARE(rig.field("tseries").data(Qt::EditRole).toString(), QStringLiteral("TS_A"));
        rig.props.showMeshObject(&rig.canvas, rig.edgeRef(bs[1]));
        QCOMPARE(rig.field("tseries").data(Qt::EditRole).toString(), QStringLiteral("TS_B"));
    }

    /*! Selection changes are display-only with the ribbon and the table
     *  panel bound: no undo entries, no BC bytes changed. */
    void selectionChangeNeverWrites()
    {
        Rig rig;
        AttributeTablePanel panel;
        panel.setProject(nullptr, &rig.sel, &rig.canvas);
        panel.showMeshTable(rig.layer, 1);
        const auto bs = boundarySlots(*rig.layer, 3);
        QVERIFY(mesh::pushEdgeBCEdit(rig.layer, {bs[0]}, stageTsBC("TS_A"), nullptr) > 0);
        QVERIFY(mesh::pushEdgeBCEdit(rig.layer, {bs[1]}, stageTsBC("TS_B"), nullptr) > 0);
        const int undoBefore = rig.canvas.undoStack()->count();
        const auto bcBefore = rig.layer->edgeBCs();
        rig.select({bs[0], bs[1]});
        rig.select({bs[0], bs[1], bs[2]});
        rig.select({bs[1]});
        // Properties really hydrated from the selection (the old toolbar's
        // editors did), and displaying it wrote nothing.
        QCOMPARE(rig.field("tseries").data(Qt::EditRole).toString(), QStringLiteral("TS_B"));
        rig.select({bs[0]});
        QCOMPARE(rig.field("tseries").data(Qt::EditRole).toString(), QStringLiteral("TS_A"));
        QCoreApplication::processEvents();
        QCOMPARE(rig.canvas.undoStack()->count(), undoBefore);
        const auto bcAfter = rig.layer->edgeBCs();
        QCOMPARE(bcAfter.size(), bcBefore.size());
        for (int i = 0; i < bcBefore.size(); ++i) {
            QCOMPARE(bcAfter[i].type, bcBefore[i].type);
            QCOMPARE(bcAfter[i].tseries, bcBefore[i].tseries);
            QCOMPARE(bcAfter[i].head, bcBefore[i].head);
        }
    }

    /*! A mixed selection does not impersonate a uniform value: the table's
     *  boundary-condition editor shows mixed types or parameters without writing. */
    void mixedSelectionOpensEditorUndecided()
    {
        Rig rig;
        const auto bs = boundarySlots(*rig.layer, 2);
        QVERIFY(mesh::pushEdgeBCEdit(rig.layer, {bs[0]}, stageTsBC("TS_A"), nullptr) > 0);
        // Positive control: a uniform selection opens with its own record.
        rig.select({bs[0]});
        rig.editBoundaryConditions({bs[0]});
        {
            QDialog *dialog = rig.boundaryDialog();
            QVERIFY(dialog);
            auto *type = dialog->findChild<QComboBox *>(QStringLiteral("boundaryType"));
            QCOMPARE(type->currentData().toInt(), int(MeshBCTypes::Type::SpecifiedStageTS));
            QCOMPARE(dialog->findChild<QLineEdit *>(QStringLiteral("boundaryValue"))->text(),
                     QStringLiteral("TS_A"));
            QVERIFY(dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Close));
            dialog->reject();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
        for (int pass = 0; pass < 2; ++pass) {
            if (pass == 1) // same type, different series
                QVERIFY(mesh::pushEdgeBCEdit(rig.layer, {bs[1]}, stageTsBC("TS_B"), nullptr) > 0);
            rig.select({bs[0], bs[1]});
            rig.editBoundaryConditions({bs[0], bs[1]});
            QDialog *dialog = rig.boundaryDialog();
            QVERIFY(dialog);
            auto *type = dialog->findChild<QComboBox *>(QStringLiteral("boundaryType"));
            QVERIFY(type);
            if (pass == 0)
                QCOMPARE(type->currentIndex(), -1);
            else {
                QCOMPARE(type->currentData().toInt(), int(MeshBCTypes::Type::SpecifiedStageTS));
                auto *value = dialog->findChild<QLineEdit *>("boundaryValue");
                QVERIFY(value->text().isEmpty());
                QCOMPARE(value->placeholderText(), QString("Mixed"));
            }
            auto *buttons = dialog->findChild<QDialogButtonBox *>();
            QVERIFY(buttons && !buttons->button(QDialogButtonBox::Ok));
            dialog->reject();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
        // Positive control for a multi-edge selection: two edges that agree
        // open with their shared record, like one edge does.
        QVERIFY(mesh::pushEdgeBCEdit(rig.layer, {bs[1]}, stageTsBC("TS_A"), nullptr) > 0);
        rig.select({bs[0], bs[1]});
        rig.editBoundaryConditions({bs[0], bs[1]});
        QDialog *dialog = rig.boundaryDialog();
        QVERIFY(dialog);
        auto *type = dialog->findChild<QComboBox *>(QStringLiteral("boundaryType"));
        QCOMPARE(type->currentData().toInt(), int(MeshBCTypes::Type::SpecifiedStageTS));
        QCOMPARE(dialog->findChild<QLineEdit *>(QStringLiteral("boundaryValue"))->text(),
                 QStringLiteral("TS_A"));
        QVERIFY(dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Close));
        dialog->reject();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    /*! Vertices and cells too: Properties hydrates each one shown, and showing
     *  them writes nothing (the old ribbon editors hydrated these fields). */
    void vertexAndCellReselectNeverWrites()
    {
        mesh::MeshResult m = makeStrip(4);
        m.vertices[0].tag = QStringLiteral("inlet");
        m.vertices[0].coupledNode = QStringLiteral("J1");
        m.vertices[0].couplingCd = 0.4;
        m.vertices[0].couplingArea = 2.5;
        m.vertices[1].tag = QStringLiteral("crest");
        m.vertices[1].z = 7.25;
        m.triangles[0].tag = QStringLiteral("pond");
        m.triangles[0].mannings = 0.035;
        m.triangles[0].initDepth = 0.1;
        m.triangles[1].tag = QStringLiteral("field");
        m.triangles[1].mannings = 0.05;
        Rig rig(m);
        const QString path = rig.layer->sourcePath();
        const auto show = [&rig](const SWMMObjectRef &ref) {
            rig.sel.select(QSet<SWMMObjectRef>{ref}, SelectionManager::Mode::Replace);
        };
        const int undoBefore = rig.canvas.undoStack()->count();
        const auto vertsBefore = rig.layer->mesh().vertices;
        const auto trisBefore = rig.layer->mesh().triangles;

        show(mesh::MeshObjectRef::vertex(path, 0));
        QVERIFY(rig.propertiesShow(QStringLiteral("inlet")));
        show(mesh::MeshObjectRef::vertex(path, 1));
        QVERIFY(rig.propertiesShow(QStringLiteral("crest")));
        show(mesh::MeshObjectRef::vertex(path, 0));
        show(mesh::MeshObjectRef::cell(path, 0));
        QVERIFY(rig.propertiesShow(QStringLiteral("pond")));
        show(mesh::MeshObjectRef::cell(path, 1));
        QVERIFY(rig.propertiesShow(QStringLiteral("field")));
        show(mesh::MeshObjectRef::cell(path, 0));
        QCoreApplication::processEvents();

        QCOMPARE(rig.canvas.undoStack()->count(), undoBefore);
        const auto &verts = rig.layer->mesh().vertices;
        QCOMPARE(verts.size(), vertsBefore.size());
        for (int i = 0; i < verts.size(); ++i) {
            QCOMPARE(verts[i].z, vertsBefore[i].z);
            QCOMPARE(verts[i].tag, vertsBefore[i].tag);
            QCOMPARE(verts[i].coupledNode, vertsBefore[i].coupledNode);
            QCOMPARE(verts[i].couplingCd, vertsBefore[i].couplingCd);
            QCOMPARE(verts[i].couplingArea, vertsBefore[i].couplingArea);
            QCOMPARE(verts[i].couplingCdSet, vertsBefore[i].couplingCdSet);
        }
        // NaN-aware: an unset Manning's n or depth must stay unset.
        const auto same = [](double a, double b) {
            return (std::isnan(a) && std::isnan(b)) || a == b;
        };
        const auto &tris = rig.layer->mesh().triangles;
        QCOMPARE(tris.size(), trisBefore.size());
        for (int i = 0; i < tris.size(); ++i) {
            QCOMPARE(tris[i].tag, trisBefore[i].tag);
            QVERIFY2(same(tris[i].mannings, trisBefore[i].mannings), qPrintable(QString::number(i)));
            QVERIFY2(same(tris[i].initDepth, trisBefore[i].initDepth),
                     qPrintable(QString::number(i)));
        }
    }

    /*! A real bulk edit is ONE undoable command and every selected edge gets
     *  the value — through the table's bulk apply and through the editor. */
    void bulkAssignIsOneCommand()
    {
        Rig rig;
        const auto bs = boundarySlots(*rig.layer, 3);
        for (int i = 0; i < 3; ++i)
            QVERIFY(mesh::pushEdgeBCEdit(rig.layer, {bs[i]},
                                         stageTsBC(QStringLiteral("TS_%1").arg(QChar('A' + i))),
                                         nullptr) > 0);
        const int undoBefore = rig.canvas.undoStack()->count();
        rig.table.setDataForRows(colFor(rig.table, "tseries"),
                                 {rig.row(bs[0]), rig.row(bs[1]), rig.row(bs[2])}, "TS_B");
        QCOMPARE(rig.canvas.undoStack()->count(), undoBefore + 1);
        for (const auto &slot : bs)
            QCOMPARE(rig.layer->edgeBCs()[mesh::edgeSlot(slot.first, slot.second)].tseries,
                     QStringLiteral("TS_B"));
        rig.canvas.undoStack()->undo();
        QCOMPARE(rig.layer->edgeBCs()[mesh::edgeSlot(bs[0].first, bs[0].second)].tseries,
                 QStringLiteral("TS_A"));
        QCOMPARE(rig.layer->edgeBCs()[mesh::edgeSlot(bs[2].first, bs[2].second)].tseries,
                 QStringLiteral("TS_C"));

        // The table's editor: choose Specified stage and commit a stage value.
        rig.select({bs[0], bs[1], bs[2]});
        rig.editBoundaryConditions({bs[0], bs[1], bs[2]});
        QDialog *dialog = rig.boundaryDialog();
        QVERIFY(dialog);
        auto *type = dialog->findChild<QComboBox *>(QStringLiteral("boundaryType"));
        // Each committed field writes immediately, including with redo history present.
        const int beforeEditor = rig.canvas.undoStack()->index();
        chooseBoundaryType(type, int(MeshBCTypes::Type::SpecifiedStageConst));
        editBoundaryValue(dialog, "3.25");
        QCOMPARE(rig.canvas.undoStack()->index(), beforeEditor + 2);
        for (const auto &slot : bs) {
            const auto &bc = rig.layer->edgeBCs()[mesh::edgeSlot(slot.first, slot.second)];
            QCOMPARE(bc.type, MeshBCTypes::Type::SpecifiedStageConst);
            QCOMPARE(bc.head, 3.25);
        }
    }

    /*! A negative Normal Depth slope feeds the domain. The removed toolbar
     *  page allowed it; the editor and the table must keep allowing it. */
    void negativeNormalSlopeIsPrescribable()
    {
        Rig rig;
        const auto bs = boundarySlots(*rig.layer, 2);
        rig.select({bs[0]});
        auto *edit = rig.props.findChild<QPushButton *>(QStringLiteral("editMeshBoundary"));
        QVERIFY(edit);
        edit->click();
        QDialog *dialog = rig.boundaryDialog();
        QVERIFY(dialog);
        auto *type = dialog->findChild<QComboBox *>(QStringLiteral("boundaryType"));
        chooseBoundaryType(type, int(MeshBCTypes::Type::NormalFlow));
        editBoundaryValue(dialog, "-0.002");
        const auto &first = rig.layer->edgeBCs()[mesh::edgeSlot(bs[0].first, bs[0].second)];
        QCOMPARE(first.type, MeshBCTypes::Type::NormalFlow);
        QCOMPARE(first.slope, -0.002);

        mesh::MeshEdgeBC normal;
        normal.type = MeshBCTypes::Type::NormalFlow;
        normal.slope = 0.001;
        QVERIFY(mesh::pushEdgeBCEdit(rig.layer, {bs[1]}, normal, nullptr) > 0);
        QVERIFY(rig.table.setData(rig.table.index(rig.row(bs[1]), colFor(rig.table, "slope")),
                                  -0.004));
        QCOMPARE(rig.layer->edgeBCs()[mesh::edgeSlot(bs[1].first, bs[1].second)].slope, -0.004);
    }

    /*! One model, two views: a table edit shows in Properties and a
     *  Properties edit shows in the table. */
    void propertiesAndTableAgree()
    {
        Rig rig;
        const auto bs = boundarySlots(*rig.layer, 1);
        QVERIFY(mesh::pushEdgeBCEdit(rig.layer, {bs[0]}, stageTsBC("TS_A"), nullptr) > 0);
        const int tsColumn = colFor(rig.table, "tseries");
        QVERIFY(rig.table.setData(rig.table.index(rig.row(bs[0]), tsColumn), "TS_C"));
        rig.props.showMeshObject(&rig.canvas, rig.edgeRef(bs[0]));
        QCOMPARE(rig.field("tseries").data(Qt::EditRole).toString(), QStringLiteral("TS_C"));

        const auto conveyance = rig.field("conveyance");
        QVERIFY(conveyance.isValid() && (conveyance.flags() & Qt::ItemIsEditable));
        auto *tree = rig.props.findChild<QTreeView *>();
        QVERIFY(tree->model()->setData(conveyance, 0.25));
        QCOMPARE(rig.table.data(rig.table.index(rig.row(bs[0]), colFor(rig.table, "conveyance")),
                                Qt::EditRole)
                     .toDouble(),
                 0.25);
    }

    /*! An out-of-range BC type through the table is a no-op: no record
     *  change, no undo entry. */
    void invalidTypeIsNoop()
    {
        Rig rig;
        const auto bs = boundarySlots(*rig.layer, 1);
        const int before = rig.canvas.undoStack()->count();
        const auto bcBefore = rig.layer->edgeBCs();
        rig.table.setData(rig.table.index(rig.row(bs[0]), colFor(rig.table, "bcType")), 999);
        QCOMPARE(rig.canvas.undoStack()->count(), before);
        QCOMPARE(rig.layer->edgeBCs()[mesh::edgeSlot(bs[0].first, bs[0].second)].type,
                 bcBefore[mesh::edgeSlot(bs[0].first, bs[0].second)].type);
    }

    /*! The ribbon holds selectors and commands only, so its width cannot
     *  change with the selection; boundary conditions are edited in the panels. */
    void ribbonHoldsNoValueEditors()
    {
        Rig rig;
        QVERIFY(rig.tb.findChildren<QAbstractSpinBox *>().isEmpty());
        QVERIFY(rig.tb.findChildren<QLineEdit *>().isEmpty());
        QStringList combos;
        for (auto *combo : rig.tb.findChildren<QComboBox *>())
            combos << combo->objectName();
        combos.sort();
        QCOMPARE(combos, (QStringList{QStringLiteral("meshActiveCombo"),
                                      QStringLiteral("meshProcessCombo")}));

        const auto width = [&rig] {
            int total = 0;
            for (auto *group : rig.tb.findChildren<RibbonGroup *>(Qt::FindDirectChildrenOnly))
                if (!group->isHidden())
                    total += group->groupWidths().full;
            return total;
        };
        rig.tb.resize(2400, 110); // wide enough that no group overflows the bar
        rig.tb.show();
        const int idle = width();
        QVERIFY(!rig.action("actionMeshBoundaryCondition"));
        const auto bs = boundarySlots(*rig.layer, 2);
        rig.select({bs[0], bs[1]});
        QCOMPARE(width(), idle);
        rig.select({interiorSlot(*rig.layer)});
        QCOMPARE(width(), idle);
        QSet<SWMMObjectRef> cells{mesh::MeshObjectRef::cell(rig.layer->sourcePath(), 0),
                                  mesh::MeshObjectRef::cell(rig.layer->sourcePath(), 1)};
        rig.sel.select(cells, SelectionManager::Mode::Replace);
        QCOMPARE(width(), idle);
        rig.sel.select({mesh::MeshObjectRef::vertex(rig.layer->sourcePath(), 0)},
                       SelectionManager::Mode::Replace);
        QCOMPARE(width(), idle);
    }

    /*! The picker shows only the chosen process's populated group and its status. */
    void processPickerShowsOneGroup()
    {
        Rig rig;
        RibbonGroup *flow = nullptr;
        for (auto *group : rig.tb.findChildren<RibbonGroup *>(Qt::FindDirectChildrenOnly))
            if (group->caption() == QLatin1String("Surface Flow"))
                flow = group;
        auto *gwAction = new QAction(QStringLiteral("Aquifer"), &rig.tb);
        rig.tb.addProcessActions(MeshEditingToolbar::Process::Groundwater, {gwAction});
        rig.tb.setProcessStatusProvider([](MeshEditingToolbar::Process p) {
            return p == MeshEditingToolbar::Process::Snow ? QStringLiteral("off") : QString();
        });
        rig.tb.resize(2400, 110); // wide enough that no group overflows the bar
        rig.tb.show();
        const auto groupOf = [&rig](QAction *action) -> RibbonGroup * {
            for (auto *group : rig.tb.findChildren<RibbonGroup *>(Qt::FindDirectChildrenOnly))
                if (group->groupActions().contains(action))
                    return group;
            return nullptr;
        };
        // A group's toolbar slot decides whether it shows; the bar hides the
        // widget itself on its next layout pass.
        const auto shown = [&rig](RibbonGroup *group) {
            for (QAction *slot : rig.tb.actions())
                if (rig.tb.widgetForAction(slot) == group)
                    return slot->isVisible();
            return false;
        };
        QVERIFY(flow && groupOf(gwAction));
        QVERIFY(flow->groupActions().isEmpty());
        QCOMPARE(rig.tb.currentProcess(), MeshEditingToolbar::Process::SurfaceFlow);
        QVERIFY(!shown(flow));
        QVERIFY(!shown(groupOf(gwAction)));
        rig.tb.setCurrentProcess(MeshEditingToolbar::Process::Groundwater);
        QVERIFY(!shown(flow));
        QVERIFY(shown(groupOf(gwAction)));
        auto *picker = rig.tb.findChild<QComboBox *>(QStringLiteral("meshProcessCombo"));
        QVERIFY(picker->itemText(picker->findData(int(MeshEditingToolbar::Process::Snow)))
                    .endsWith(QStringLiteral("off")));
        // The picker grows to its longest label instead of clipping the status.
        const int narrow = picker->sizeHint().width();
        rig.tb.setProcessStatusProvider([](MeshEditingToolbar::Process p) {
            return p == MeshEditingToolbar::Process::SurfaceQuality
                       ? QStringLiteral("no pollutants in this model")
                       : QString();
        });
        QVERIFY(picker->sizeHint().width() > narrow);
        QVERIFY(picker->parentWidget()->sizeHint().width() >= picker->sizeHint().width());
        rig.tb.setProcessStatusProvider([](MeshEditingToolbar::Process p) {
            return p == MeshEditingToolbar::Process::Snow ? QStringLiteral("off") : QString();
        });
        // Informational only: a process that is off stays selectable.
        rig.tb.setCurrentProcess(MeshEditingToolbar::Process::Snow);
        QCOMPARE(rig.tb.currentProcess(), MeshEditingToolbar::Process::Snow);
        // The menu/palette mirror follows the picker and drives it.
        QVERIFY(rig.action("actionMeshProcessSnow")->isChecked());
        QVERIFY(rig.action("actionMeshProcessSnow")->text().endsWith(QStringLiteral("off")));
        rig.action("actionMeshProcessGroundwater")->trigger();
        QCOMPARE(rig.tb.currentProcess(), MeshEditingToolbar::Process::Groundwater);
        QCOMPARE(picker->currentData().toInt(), int(MeshEditingToolbar::Process::Groundwater));
        QVERIFY(shown(groupOf(gwAction)));
    }
};

QTEST_MAIN(TestMeshEditingToolbarBC)
#include "test_mesheditingtoolbar_bc.moc"
