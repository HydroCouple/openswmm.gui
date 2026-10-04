// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/models/lidnodelayermodel.h"
#include "ui/sectionview/lidlayerdiagram.h"
#include <QtTest>
using namespace openswmmvis::ui;
using namespace openswmmvis::sectionview;
class TestLidNodeLayers : public QObject {
    Q_OBJECT
private slots:
    void arbitraryRowsAndReordering() {
        LidNodeLayerModel model;
        model.append(0);
        for (int i = 0; i < 60; ++i) model.append(i % 2 ? 2 : 1);
        model.append(3);
        QCOMPARE(model.rowCount(), 62);
        QVERIFY(model.setData(model.index(1, 1), 321.5));
        model.move(1, 1);
        QCOMPARE(model.data(model.index(2, 1)).toDouble(), 321.5);
        model.remove(1);
        QCOMPARE(model.rowCount(), 61);
        QVERIFY(!model.setData(model.index(0, 1), -1));
    }
    void defaultsRespectUnits() {
        LidNodeLayerModel si, us;
        si.append(1, true); us.append(1, false);
        QVERIFY(qAbs(si.layers[0].params[0] - us.layers[0].params[0] * 25.4) < 1.e-10);
        QCOMPARE(si.layers[0].params[1], us.layers[0].params[1]);
    }
    void diagramRetainsRepeatedMedia() {
        LidDiagramInput in; in.type = LidType::Node;
        for (int i = 0; i < 40; ++i) in.orderedLayers.append({LidLayer::Soil, 100, .45, 25});
        const auto diagram = buildLidLayerDiagram(in);
        int media = 0;
        for (const auto& polygon : diagram.polys) if (polygon.role == DiagramRole::Media) ++media;
        QCOMPARE(media, 40);
    }
};
QTEST_MAIN(TestLidNodeLayers)
#include "test_lid_node_layers.moc"
