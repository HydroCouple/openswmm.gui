/**
 * @file      test_meshprofile_combined.cpp
 * @brief     Tests for meshprofile combined.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

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
namespace
{
/**
 * @brief Construct an owned exact-water profile with three known dry sample stations.
 * @return Section hasResults/exactWaterGeometry true, stations 0/5/10, ground 10, signed
 * depths-1 and cell 0; scene positions lie on x axis. No external results source or file access.
 */
Section drySection()
{
    Section result;
    result.hasResults = true;
    result.exactWaterGeometry = true;
    for (double chain : {0., 5., 10.}) {
        Sample sample;
        sample.chainage = chain;
        sample.ground = 10;
        sample.scenePt = QPointF(chain, 0);
        sample.triIdx = 0;
        sample.signedDepthNow = -1;
        sample.signedMaxDepth = -1;
        result.samples.append(sample);
    }
    return result;
}
/**
 * @brief Construct an owned valid groundwater elevation series with independent style/time
 * metadata.
 * @return Magenta dashed 3 px Water table 12 m at stations 0/5/10, requested UTC 1000
 * s/effective 900 s since Unix epoch, valid cell 0 points and elevation role. No model/registry
 * mutation, Qt allocations may fail.
 */
SampledSeries groundwater()
{
    SampledSeries s;
    s.definition.id = "groundwater";
    s.definition.label = "Water table";
    s.definition.role = SeriesRole::Elevation;
    s.definition.pen = QPen(Qt::magenta, 3, Qt::DashLine);
    s.units = "m";
    s.unitsKnown = true;
    s.requestedTime = QDateTime::fromSecsSinceEpoch(1000, Qt::UTC);
    s.effectiveTime = QDateTime::fromSecsSinceEpoch(900, Qt::UTC);
    for (double chain : {0., 5., 10.}) {
        SeriesPoint point;
        point.chainage = chain;
        point.scenePt = QPointF(chain, 0);
        point.value = 12;
        point.status = Mesh2DValueStatus::Valid;
        point.cellId = 0;
        s.points.append(point);
    }
    return s;
}
/**
 * @brief Render a widget to local white image and count strongly magenta pixels.
 * @param[in,out] widget Live GUI-thread QWidget with positive size; render may perform pending
 * paint/layout work.
 * @return Pixel count where red/blue>180,green<80, used to detect fixture series independent of
 * labels. Owns image only, no widget ownership transfer or artifact write; requires GUI
 * rendering environment.
 */
int magentaPixels(QWidget &widget)
{
    QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    widget.render(&image);
    int count = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
            const QColor c = image.pixelColor(x, y);
            if (c.red() > 180 && c.blue() > 180 && c.green() < 80)
                ++count;
        }
    return count;
}
}
class TestMeshProfileCombined : public QObject
{
    Q_OBJECT
private slots:
    /**
     * @brief Register display/accessibility expectations for static, envelope, missing-report
     * and unavailable sources.
     * @note QtTest rows carry temporal enum/error/expected label; static error overrides Static
     * to Unavailable, reported missing effective time maps No report available. No external
     * source opened.
     */
    void sampleTimesDistinguishStaticEnvelopeAndUnavailable_data()
    {
        QTest::addColumn<int>("temporal");
        QTest::addColumn<QString>("error");
        QTest::addColumn<QString>("expected");
        using T = openswmmvis::io::Mesh2DResultVariable::Temporal;
        QTest::newRow("static") << int(T::Static) << QString() << QString("Static");
        QTest::newRow("envelope") << int(T::Envelope) << QString() << QString("Whole-run maximum");
        QTest::newRow("missing-report")
            << int(T::Reported) << QString() << QString("No report available");
        QTest::newRow("missing-static-source")
            << int(T::Static) << QString("Source file unavailable") << QString("Unavailable");
    }
    /**
     * @brief Verify sample table display/accessibility conveys temporal availability rather than
     * fabricated timestamps.
     * @note Uses data rows with absent effective time, checks table row 6,column 8 display and
     * AccessibleText, source error included in AccessibleDescription when present. Local
     * table/series fixture only.
     */
    void sampleTimesDistinguishStaticEnvelopeAndUnavailable()
    {
        QFETCH(int, temporal);
        QFETCH(QString, error);
        QFETCH(QString, expected);
        auto section = drySection();
        auto series = groundwater();
        series.effectiveTime = {};
        series.error = error;
        series.descriptor.temporal =
            static_cast<openswmmvis::io::Mesh2DResultVariable::Temporal>(temporal);
        section.series.append(series);
        Definition definition;
        definition.horizontalUnits = "m";
        MeshProfileSamplesModel table;
        table.setSection(section, definition);
        QCOMPARE(table.data(table.index(6, 8)).toString(), expected);
        QCOMPARE(table.data(table.index(6, 8), Qt::AccessibleTextRole).toString(), expected);
        if (!error.isEmpty())
            QVERIFY(table.data(table.index(6, 8), Qt::AccessibleDescriptionRole)
                        .toString()
                        .contains(error));
    }
    /**
     * @brief Verify cursor announcements preserve series temporal/error status in elevation and
     * scalar tracks.
     * @note Static elevation remains described after station 5; unavailable scalar keeps source
     * error after cursor movement and keyboard End reaches station 10. Queries borrowed
     * QAccessible interfaces tied to widgets; requires GUI/event environment, does not alter
     * source files.
     */
    void seriesStatusRemainsAccessibleWhileMovingProfileCursor()
    {
        auto section = drySection();
        auto series = groundwater();
        series.effectiveTime = {};
        series.descriptor.temporal = openswmmvis::io::Mesh2DResultVariable::Temporal::Static;
        section.series.append(series);
        MeshProfilePlotWidget elevation;
        elevation.setProfile(section);
        auto *accessible = QAccessible::queryAccessibleInterface(&elevation);
        QVERIFY(accessible);
        QVERIFY(accessible->text(QAccessible::Description).contains("Static"));
        elevation.setCursorChainage(5);
        QVERIFY(accessible->text(QAccessible::Description).contains("Station 5"));
        QVERIFY(accessible->text(QAccessible::Description).contains("Static"));
        section.series[0].definition.role = SeriesRole::Scalar;
        section.series[0].error = "Source file unavailable";
        MeshProfileTracksWidget tracks;
        tracks.setSection(section);
        auto *trackAccessible = QAccessible::queryAccessibleInterface(&tracks);
        QVERIFY(trackAccessible);
        QVERIFY(
            trackAccessible->text(QAccessible::Description).contains("Source file unavailable"));
        tracks.setCursorChainage(5);
        QVERIFY(trackAccessible->text(QAccessible::Description).contains("Station 5"));
        QVERIFY(
            trackAccessible->text(QAccessible::Description).contains("Source file unavailable"));
        tracks.show();
        tracks.setFocus();
        QTest::keyClick(&tracks, Qt::Key_End);
        QVERIFY(trackAccessible->text(QAccessible::Description).contains("Station 10"));
    }
    /**
     * @brief Verify groundwater above dry terrain extends visible range and paints independently
     * of water fill.
     * @note 700 x 350 profile, magenta series>100 pixels and y range beyond 12; hiding series
     * removes all magenta pixels with legend/time label disabled. Pure synthetic rendering, no
     * native model/results.
     */
    void groundwaterEmergenceRemainsVisibleAboveDryGround()
    {
        auto section = drySection();
        section.series.append(groundwater());
        MeshProfilePlotOptions options;
        options.setLegendVisible(false);
        options.setShowTimeLabel(false);
        MeshProfilePlotWidget widget;
        widget.resize(700, 350);
        widget.setOptions(&options);
        widget.setProfile(section);
        QVERIFY(widget.visibleDataRange().bottom() > 12);
        QVERIFY(magentaPixels(widget) > 100);
        section.series[0].definition.visible = false;
        widget.setProfile(section);
        QCOMPARE(magentaPixels(widget), 0);
    }
    /**
     * @brief Verify missing scalar station plus breakBefore prevents a false connecting
     * groundwater line.
     * @note Three-point fixture with middle Missing and final break must paint 0 magenta pixels,
     * not bridge unavailable data; legend/time labels disabled to keep pixel classifier
     * specific.
     */
    void unavailableGroundwaterDoesNotCreateFalseConnectingLine()
    {
        auto section = drySection();
        auto series = groundwater();
        series.points[1].status = Mesh2DValueStatus::Missing;
        series.points[2].breakBefore = true;
        section.series.append(series);
        MeshProfilePlotOptions options;
        options.setLegendVisible(false);
        options.setShowTimeLabel(false);
        MeshProfilePlotWidget widget;
        widget.resize(700, 350);
        widget.setOptions(&options);
        widget.setProfile(section);
        QCOMPARE(magentaPixels(widget), 0);
    }
    /**
     * @brief Verify Right/Left/End keyboard navigation updates station cursor, signal and
     * accessibility text.
     * @note Sequence reaches 0,5,10,5 on three-station profile, at least 4 cursor-change signals
     * and description contains 5. Live shown/focused widget required, local UI state only.
     */
    void keyboardNavigatesStationsAndAnnouncesPosition()
    {
        MeshProfilePlotWidget widget;
        widget.setProfile(drySection());
        widget.show();
        widget.setFocus();
        QSignalSpy moved(&widget, &MeshProfilePlotWidget::cursorChainageChanged);
        QTest::keyClick(&widget, Qt::Key_Right);
        QVERIFY(widget.hasCursor());
        QCOMPARE(widget.cursorChainage(), 0.0);
        QTest::keyClick(&widget, Qt::Key_Right);
        QCOMPARE(widget.cursorChainage(), 5.0);
        QTest::keyClick(&widget, Qt::Key_End);
        QCOMPARE(widget.cursorChainage(), 10.0);
        QTest::keyClick(&widget, Qt::Key_Left);
        QCOMPARE(widget.cursorChainage(), 5.0);
        QVERIFY(moved.count() >= 4);
        QVERIFY(widget.accessibleDescription().contains("5"));
    }
    /**
     * @brief Verify profile sample table retains explicit zero, unknown units, waterless status
     * and effective time.
     * @note Scalar chloride fixture yields 9 rows,0 display value, unknown-unit text, Waterless
     * text and 00:15 UTC effective time; UserRole chainage 10 and value header remain usable. No
     * live file/engine dependency.
     */
    void tablePreservesUnknownUnitsStatusesAndEffectiveTime()
    {
        auto section = drySection();
        auto series = groundwater();
        series.definition.role = SeriesRole::Scalar;
        series.definition.label = "SAT chloride";
        series.unitsKnown = false;
        series.units.clear();
        series.points[0].value = 0;
        series.points[1].status = Mesh2DValueStatus::Waterless;
        section.series.append(series);
        Definition definition;
        definition.horizontalUnits = "m";
        MeshProfileSamplesModel table;
        table.setSection(section, definition);
        QCOMPARE(table.rowCount(), 9);
        QCOMPARE(table.data(table.index(6, 4)).toString(), QString("0"));
        QVERIFY(table.data(table.index(6, 5)).toString().contains("unknown"));
        QVERIFY(table.data(table.index(7, 6)).toString().contains("Waterless"));
        QVERIFY(table.data(table.index(6, 8)).toString().contains("00:15"));
        QCOMPARE(table.data(table.index(8, 0), Qt::UserRole).toDouble(), 10.0);
        QVERIFY(!table.headerData(4, Qt::Horizontal).toString().isEmpty());
    }
    /**
     * @brief Verify styled profile options round-trip once and invalid pen JSON refuses without
     * partial mutation.
     * @note Copies colors/widths/styles/fill visibility/legend opacity/time offset, expects one
     * changed signal, then malformed color diagnostic and exact prior JSON retained. No settings
     * file persistence.
     */
    void savedDisplayOptionsRoundTripAndInvalidInputIsAtomic()
    {
        MeshProfilePlotOptions source;
        source.setWseLinePen(QPen(QColor(30, 80, 160, 120), 3.5, Qt::DashDotLine));
        source.setGroundLinePen(QPen(Qt::darkRed, 2.3, Qt::DotLine));
        source.setShowDepthFill(false);
        source.setLegendOpacity(0.6);
        source.setTimeLabelOffset(QPointF(20, 30));
        MeshProfilePlotOptions restored;
        QSignalSpy changed(&restored, &MeshProfilePlotOptions::changed);
        QString error;
        QVERIFY(restored.fromJson(source.toJson(), &error));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(restored.toJson(), source.toJson());
        const auto good = restored.toJson();
        auto bad = good;
        auto values = bad["values"].toObject();
        values["wseLinePen"] = QJsonObject{{"color", "not a color"}, {"width", 2}, {"style", 1}};
        bad["values"] = values;
        QVERIFY(!restored.fromJson(bad, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(restored.toJson(), good);
    }
    /**
     * @brief Verify unavailable source series remain editable with stable identity and
     * independent opacity.
     * @note Two missing-source editor rows show unavailable, accessible opacity control labels
     * Series 1; changing first to.3 leaves second.7 and original variable key intact. No missing
     * file opened or replaced.
     */
    void restoredUnavailableSeriesRetainsIdentityAndIndependentStyle()
    {
        Definition definition;
        definition.id = "section";
        definition.primarySourceId = "source";
        definition.sceneCRS = "LOCAL";
        definition.horizontalUnits = "m";
        definition.scenePolyline = {QPointF(0, 0), QPointF(10, 0)};
        definition.sources.append({"source", "missing.h5", {}});
        auto first = groundwater().definition;
        first.sourceId = "source";
        first.variableKey = "groundwater:missing";
        auto second = first;
        second.id = "second";
        second.label = "Another quantity";
        second.opacity = 0.7;
        definition.series = {first, second};
        MeshProfileSeriesEditor editor;
        editor.setDefinition(definition);
        editor.setSources({});
        auto *table = editor.findChild<QTableWidget *>("sectionSeries");
        QVERIFY(table);
        QCOMPARE(table->rowCount(), 2);
        QVERIFY(table->item(0, 2)->text().contains("unavailable"));
        auto *opacity = qobject_cast<QDoubleSpinBox *>(table->cellWidget(0, 8));
        QVERIFY(opacity);
        QVERIFY(opacity->accessibleName().contains("Series 1"));
        opacity->setValue(0.3);
        QCOMPARE(editor.definition().series[0].opacity, 0.3);
        QCOMPARE(editor.definition().series[1].opacity, 0.7);
        QCOMPARE(editor.definition().series[0].variableKey, first.variableKey);
    }
    /**
     * @brief Verify scalar profile tracks paint native-unit series and respect per-series
     * visibility.
     * @note Chloride mg/L magenta line paints>50 pixels and accessible description preserves
     * units; hiding removes all magenta. Synthetic GUI render only, no automatic
     * scalar-to-elevation conversion.
     */
    void scalarTracksRetainNativeUnitsAndIndependentVisibility()
    {
        auto section = drySection();
        auto series = groundwater();
        series.definition.role = SeriesRole::Scalar;
        series.definition.label = "Chloride";
        series.units = "mg/L";
        section.series.append(series);
        MeshProfileTracksWidget tracks;
        tracks.resize(700, 180);
        tracks.setSection(section);
        QVERIFY(magentaPixels(tracks) > 50);
        QVERIFY(tracks.accessibleDescription().contains("mg/L"));
        section.series[0].definition.visible = false;
        tracks.setSection(section);
        QCOMPARE(magentaPixels(tracks), 0);
    }
};
QTEST_MAIN(TestMeshProfileCombined)
#include "test_meshprofile_combined.moc"
