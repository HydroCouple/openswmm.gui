// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Caleb Buahin.
#include "render/subcatchmentgeometry.h"

#include <QDir>
#include <QImage>
#include <QPainter>
#include <QTest>

#include <cmath>

using namespace OpenSWMM::Render::SubcatchmentGeometry;

class TestSubcatchmentGeometry : public QObject
{
    Q_OBJECT
private slots:
    void ringsAndFill_data()
    {
        QTest::addColumn<QVector<QPointF>>("vertices");
        QTest::addColumn<double>("area");
        const QVector<QPointF> square{{10, 10}, {90, 10}, {90, 90}, {10, 90}};
        QTest::newRow("open") << square << 6400.;
        QTest::newRow("closed") << (square + QVector<QPointF>{{10, 10}}) << 6400.;
        QTest::newRow("multipart")
            << QVector<QPointF>{{10, 10},  {90, 10},  {90, 90},  {10, 90},  {10, 10},
                                {130, 30}, {190, 30}, {190, 90}, {130, 90}, {130, 30}}
            << 10000.;
        QTest::newRow("hole-and-island")
            << QVector<QPointF>{{10, 10},  {90, 10},  {90, 90},  {10, 90}, {10, 10},
                                {30, 30},  {70, 30},  {70, 70},  {30, 70}, {30, 30},
                                {130, 30}, {190, 30}, {190, 90}, {130, 90}}
            << 8400.;
        QTest::newRow("concave") << QVector<QPointF>{{10, 10}, {90, 10}, {90, 40},
                                                     {50, 40}, {50, 90}, {10, 90}}
                                 << 4400.;
        QTest::newRow("collinear-and-clockwise")
            << QVector<QPointF>{{10, 90}, {90, 90}, {90, 50}, {90, 10}, {10, 10}} << 6400.;
        QTest::newRow("empty") << QVector<QPointF>{} << 0.;
        QTest::newRow("degenerate") << QVector<QPointF>{{10, 10}, {20, 20}} << 0.;
    }

    void ringsAndFill()
    {
        QFETCH(QVector<QPointF>, vertices);
        QFETCH(double, area);
        const auto outline = path(vertices);
        const auto triangles = triangulate(vertices);
        double actualArea = 0.;
        QImage cpu(210, 110, QImage::Format_RGB32), gpu(cpu.size(), cpu.format());
        cpu.fill(Qt::white);
        gpu.fill(Qt::white);
        {
            QPainter p(&cpu);
            p.fillPath(outline, Qt::blue);
        }
        {
            QPainter p(&gpu);
            p.setPen(Qt::NoPen);
            p.setBrush(Qt::blue);
            for (int i = 0; i < triangles.size(); i += 3) {
                const auto a = triangles[i], b = triangles[i + 1], c = triangles[i + 2];
                actualArea += std::abs((b.x() - a.x()) * (c.y() - a.y()) -
                                       (b.y() - a.y()) * (c.x() - a.x())) *
                              .5;
                QVERIFY(outline.contains((a + b + c) / 3.));
                p.drawPolygon(QPolygonF{a, b, c});
            }
        }
        QCOMPARE(actualArea, area);
        // Compare interior pixels to avoid rasterizer edge inclusion differences.
        for (int y = 15; y < 100; y += 10)
            for (int x = 15; x < 200; x += 10)
                QCOMPARE(gpu.pixel(x, y), cpu.pixel(x, y));
        const QString out = QStringLiteral(SUBCATCHMENT_TEST_OUTPUT);
        QVERIFY(QDir().mkpath(out));
        QVERIFY(cpu.save(out + '/' + QTest::currentDataTag() + "-cpu.png"));
        QVERIFY(gpu.save(out + '/' + QTest::currentDataTag() + "-triangles.png"));
    }

    void noBridgesOrHolePicking()
    {
        const QVector<QPointF> points{{10, 10},  {90, 10},  {90, 90},  {10, 90}, {10, 10},
                                      {30, 30},  {70, 30},  {70, 70},  {30, 70}, {30, 30},
                                      {130, 30}, {190, 30}, {190, 90}, {130, 90}};
        QCOMPARE(rings(points).size(), 3);
        QVERIFY(path(points).contains({20, 20}));
        QVERIFY(contains(points, {20, 20}));
        QVERIFY(path(points).contains({150, 50}));
        QVERIFY(contains(points, {150, 50}));
        QVERIFY(!path(points).contains({50, 50}));
        QVERIFY(!contains(points, {50, 50}));
        QVERIFY(!path(points).contains({110, 50}));
        QVERIFY(!contains(points, {110, 50}));
    }
};

QTEST_MAIN(TestSubcatchmentGeometry)
#include "test_subcatchmentgeometry.moc"
