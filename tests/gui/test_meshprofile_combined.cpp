#include <QtTest>
#include <QImage>
#include <QSignalSpy>
#include <QAccessible>
#include "plot/meshprofileplotwidget.h"
#include "plot/meshprofileplotoptions.h"
#include "plot/meshprofiletrackswidget.h"
#include "plot/meshprofileserieseditor.h"
#include <QTableWidget>
#include <QDoubleSpinBox>
#include <QComboBox>
#include "plot/profilesectionseries.h"

using namespace ProfileSection;
using openswmmvis::io::Mesh2DValueStatus;
namespace {
Section drySection()
{
    Section result; result.hasResults = true; result.exactWaterGeometry = true;
    for (double chain : {0., 5., 10.}) {
        Sample sample; sample.chainage = chain; sample.ground = 10;
        sample.scenePt = QPointF(chain, 0); sample.triIdx = 0;
        sample.signedDepthNow = -1; sample.signedMaxDepth = -1;
        result.samples.append(sample);
    }
    return result;
}
SampledSeries groundwater()
{
    SampledSeries s; s.definition.id = "groundwater";
    s.definition.label = "Water table"; s.definition.role = SeriesRole::Elevation;
    s.definition.pen = QPen(Qt::magenta, 3, Qt::DashLine);
    s.units = "m"; s.unitsKnown = true;
    s.requestedTime = QDateTime::fromSecsSinceEpoch(1000, Qt::UTC);
    s.effectiveTime = QDateTime::fromSecsSinceEpoch(900, Qt::UTC);
    for (double chain : {0., 5., 10.}) {
        SeriesPoint point; point.chainage = chain; point.scenePt = QPointF(chain, 0);
        point.value = 12; point.status = Mesh2DValueStatus::Valid; point.cellId = 0;
        s.points.append(point);
    }
    return s;
}
int magentaPixels(QWidget &widget)
{
    QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::white);
    widget.render(&image);
    int count = 0;
    for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) {
        const QColor c = image.pixelColor(x,y);
        if (c.red() > 180 && c.blue() > 180 && c.green() < 80) ++count;
    }
    return count;
}
}
class TestMeshProfileCombined : public QObject
{
    Q_OBJECT
private slots:
    void sampleTimesDistinguishStaticEnvelopeAndUnavailable_data()
    {
        QTest::addColumn<int>("temporal");QTest::addColumn<QString>("error");QTest::addColumn<QString>("expected");
        using T=openswmmvis::io::Mesh2DResultVariable::Temporal;
        QTest::newRow("static")<<int(T::Static)<<QString()<<QString("Static");
        QTest::newRow("envelope")<<int(T::Envelope)<<QString()<<QString("Whole-run maximum");
        QTest::newRow("missing-report")<<int(T::Reported)<<QString()<<QString("No report available");
        QTest::newRow("missing-static-source")<<int(T::Static)<<QString("Source file unavailable")<<QString("Unavailable");
    }
    void sampleTimesDistinguishStaticEnvelopeAndUnavailable()
    {
        QFETCH(int,temporal);QFETCH(QString,error);QFETCH(QString,expected);
        auto section=drySection();auto series=groundwater();
        series.effectiveTime={};series.error=error;
        series.descriptor.temporal=static_cast<openswmmvis::io::Mesh2DResultVariable::Temporal>(temporal);
        section.series.append(series);Definition definition;definition.horizontalUnits="m";
        MeshProfileSamplesModel table;table.setSection(section,definition);
        QCOMPARE(table.data(table.index(6,8)).toString(),expected);
        QCOMPARE(table.data(table.index(6,8),Qt::AccessibleTextRole).toString(),expected);
        if(!error.isEmpty())QVERIFY(table.data(table.index(6,8),Qt::AccessibleDescriptionRole).toString().contains(error));
    }
    void seriesStatusRemainsAccessibleWhileMovingProfileCursor()
    {
        auto section=drySection();auto series=groundwater();series.effectiveTime={};
        series.descriptor.temporal=openswmmvis::io::Mesh2DResultVariable::Temporal::Static;
        section.series.append(series);
        MeshProfilePlotWidget elevation;elevation.setProfile(section);
        auto *accessible=QAccessible::queryAccessibleInterface(&elevation);QVERIFY(accessible);
        QVERIFY(accessible->text(QAccessible::Description).contains("Static"));
        elevation.setCursorChainage(5);
        QVERIFY(accessible->text(QAccessible::Description).contains("Station 5"));
        QVERIFY(accessible->text(QAccessible::Description).contains("Static"));
        section.series[0].definition.role=SeriesRole::Scalar;
        section.series[0].error="Source file unavailable";
        MeshProfileTracksWidget tracks;tracks.setSection(section);
        auto *trackAccessible=QAccessible::queryAccessibleInterface(&tracks);QVERIFY(trackAccessible);
        QVERIFY(trackAccessible->text(QAccessible::Description).contains("Source file unavailable"));
        tracks.setCursorChainage(5);
        QVERIFY(trackAccessible->text(QAccessible::Description).contains("Station 5"));
        QVERIFY(trackAccessible->text(QAccessible::Description).contains("Source file unavailable"));
        tracks.show();tracks.setFocus();QTest::keyClick(&tracks,Qt::Key_End);
        QVERIFY(trackAccessible->text(QAccessible::Description).contains("Station 10"));
    }
    void groundwaterEmergenceRemainsVisibleAboveDryGround()
    {
        auto section = drySection(); section.series.append(groundwater());
        MeshProfilePlotOptions options; options.setLegendVisible(false); options.setShowTimeLabel(false);
        MeshProfilePlotWidget widget; widget.resize(700, 350); widget.setOptions(&options);
        widget.setProfile(section);
        QVERIFY(widget.visibleDataRange().bottom() > 12);
        QVERIFY(magentaPixels(widget) > 100);
        section.series[0].definition.visible = false; widget.setProfile(section);
        QCOMPARE(magentaPixels(widget), 0);
    }
    void unavailableGroundwaterDoesNotCreateFalseConnectingLine()
    {
        auto section = drySection(); auto series = groundwater();
        series.points[1].status = Mesh2DValueStatus::Missing;
        series.points[2].breakBefore = true;
        section.series.append(series);
        MeshProfilePlotOptions options; options.setLegendVisible(false); options.setShowTimeLabel(false);
        MeshProfilePlotWidget widget; widget.resize(700, 350); widget.setOptions(&options);
        widget.setProfile(section);
        QCOMPARE(magentaPixels(widget), 0);
    }
    void keyboardNavigatesStationsAndAnnouncesPosition()
    {
        MeshProfilePlotWidget widget; widget.setProfile(drySection()); widget.show(); widget.setFocus();
        QSignalSpy moved(&widget, &MeshProfilePlotWidget::cursorChainageChanged);
        QTest::keyClick(&widget, Qt::Key_Right);
        QVERIFY(widget.hasCursor()); QCOMPARE(widget.cursorChainage(), 0.0);
        QTest::keyClick(&widget, Qt::Key_Right); QCOMPARE(widget.cursorChainage(), 5.0);
        QTest::keyClick(&widget, Qt::Key_End); QCOMPARE(widget.cursorChainage(), 10.0);
        QTest::keyClick(&widget, Qt::Key_Left); QCOMPARE(widget.cursorChainage(), 5.0);
        QVERIFY(moved.count() >= 4);
        QVERIFY(widget.accessibleDescription().contains("5"));
    }
    void tablePreservesUnknownUnitsStatusesAndEffectiveTime()
    {
        auto section = drySection(); auto series = groundwater();
        series.definition.role = SeriesRole::Scalar; series.definition.label = "SAT chloride";
        series.unitsKnown = false; series.units.clear(); series.points[0].value = 0;
        series.points[1].status = Mesh2DValueStatus::Waterless;
        section.series.append(series);
        Definition definition; definition.horizontalUnits = "m";
        MeshProfileSamplesModel table; table.setSection(section, definition);
        QCOMPARE(table.rowCount(), 9);
        QCOMPARE(table.data(table.index(6, 4)).toString(), QString("0"));
        QVERIFY(table.data(table.index(6, 5)).toString().contains("unknown"));
        QVERIFY(table.data(table.index(7, 6)).toString().contains("Waterless"));
        QVERIFY(table.data(table.index(6, 8)).toString().contains("00:15"));
        QCOMPARE(table.data(table.index(8, 0), Qt::UserRole).toDouble(), 10.0);
        QVERIFY(!table.headerData(4, Qt::Horizontal).toString().isEmpty());
    }
    void savedDisplayOptionsRoundTripAndInvalidInputIsAtomic()
    {
        MeshProfilePlotOptions source;
        source.setWseLinePen(QPen(QColor(30,80,160,120),3.5,Qt::DashDotLine));
        source.setGroundLinePen(QPen(Qt::darkRed,2.3,Qt::DotLine));
        source.setShowDepthFill(false); source.setLegendOpacity(0.6);
        source.setTimeLabelOffset(QPointF(20,30));
        MeshProfilePlotOptions restored;
        QSignalSpy changed(&restored,&MeshProfilePlotOptions::changed);
        QString error; QVERIFY(restored.fromJson(source.toJson(),&error));
        QCOMPARE(changed.count(),1); QCOMPARE(restored.toJson(),source.toJson());
        const auto good=restored.toJson(); auto bad=good; auto values=bad["values"].toObject();
        values["wseLinePen"]=QJsonObject{{"color","not a color"},{"width",2},{"style",1}};
        bad["values"]=values; QVERIFY(!restored.fromJson(bad,&error));
        QVERIFY(!error.isEmpty()); QCOMPARE(restored.toJson(),good);
    }
    void restoredUnavailableSeriesRetainsIdentityAndIndependentStyle()
    {
        Definition definition; definition.id="section"; definition.primarySourceId="source";
        definition.sceneCRS="LOCAL"; definition.horizontalUnits="m"; definition.scenePolyline={QPointF(0,0),QPointF(10,0)};
        definition.sources.append({"source","missing.h5",{}});
        auto first=groundwater().definition; first.sourceId="source"; first.variableKey="groundwater:missing";
        auto second=first; second.id="second"; second.label="Another quantity"; second.opacity=0.7;
        definition.series={first,second};
        MeshProfileSeriesEditor editor; editor.setDefinition(definition); editor.setSources({});
        auto *table=editor.findChild<QTableWidget *>("sectionSeries"); QVERIFY(table); QCOMPARE(table->rowCount(),2);
        QVERIFY(table->item(0,2)->text().contains("unavailable"));
        auto *opacity=qobject_cast<QDoubleSpinBox *>(table->cellWidget(0,8)); QVERIFY(opacity);
        QVERIFY(opacity->accessibleName().contains("Series 1")); opacity->setValue(0.3);
        QCOMPARE(editor.definition().series[0].opacity,0.3); QCOMPARE(editor.definition().series[1].opacity,0.7);
        QCOMPARE(editor.definition().series[0].variableKey,first.variableKey);
    }
    void scalarTracksRetainNativeUnitsAndIndependentVisibility()
    {
        auto section = drySection(); auto series = groundwater();
        series.definition.role = SeriesRole::Scalar; series.definition.label = "Chloride";
        series.units = "mg/L"; section.series.append(series);
        MeshProfileTracksWidget tracks; tracks.resize(700, 180); tracks.setSection(section);
        QVERIFY(magentaPixels(tracks) > 50);
        QVERIFY(tracks.accessibleDescription().contains("mg/L"));
        section.series[0].definition.visible = false; tracks.setSection(section);
        QCOMPARE(magentaPixels(tracks), 0);
    }
};
QTEST_MAIN(TestMeshProfileCombined)
#include "test_meshprofile_combined.moc"
