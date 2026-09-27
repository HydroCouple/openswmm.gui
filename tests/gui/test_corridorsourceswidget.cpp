// Selected corridor recipes remain editable and never silently expand to all features.
#include "ui/widgets/corridorsourceswidget.h"
#include "layers/gisvectorlayer.h"
#include "map/spatialreferencesystem.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTabWidget>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QTest>
#include <ogrsf_frmts.h>
#include <memory>

class TestCorridorSourcesWidget : public QObject
{
    Q_OBJECT
    QString m_path;
    template<class T> static T *control(CorridorSourcesWidget &widget, const char *name)
    {
        return widget.findChild<T *>(QString::fromLatin1(name));
    }
    static mesh::CorridorSource saved()
    {
        mesh::CorridorSource row;
        row.path = QStringLiteral("/missing/corridors.gpkg");
        row.layerName = QStringLiteral("river_corridors");
        row.sourceCRSWkt = QStringLiteral("assigned source CRS");
        row.meshCRSWkt = QStringLiteral("recorded mesh CRS");
        row.featureIds = {9, 42};
        row.width = 15;
        row.along = 0;
        row.across = 3;
        row.tag = QStringLiteral("reach A");
        row.geometryDigest = QStringLiteral("saved geometry digest");
        row.sourceFiles = {QStringLiteral("/missing/corridors.gpkg")};
        return row;
    }
private slots:
    void initTestCase()
    {
        const QString dir = qEnvironmentVariable("SWMMVIS_CORRIDOR_WIDGET_TEST_OUTPUT",
            QStringLiteral("tests/output/corridor_sources_widget"));
        QVERIFY(QDir().mkpath(dir));
        m_path = QFileInfo(QDir(dir).filePath(QStringLiteral("selected_roads.geojson"))).absoluteFilePath();
        QFile fixture(m_path);
        QVERIFY(fixture.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const QByteArray json = R"({"type":"FeatureCollection","features":[
          {"type":"Feature","id":7,"properties":{"width_m":12.5,"lanes":2,"name":"road A"},"geometry":{"type":"LineString","coordinates":[[0,0],[20,0]]}},
          {"type":"Feature","id":19,"properties":{"width_m":8.5,"lanes":1,"name":"river B"},"geometry":{"type":"LineString","coordinates":[[0,30],[20,30]]}}
        ]})";
        QCOMPARE(fixture.write(json), json.size());
    }

    void bankPairSelection_data()
    {
        QTest::addColumn<int>("count");
        QTest::newRow("one-bank-refused") << 1;
        QTest::newRow("two-banks") << 2;
        QTest::newRow("three-banks-refused") << 3;
    }
    void bankPairSelection()
    {
        QFETCH(int, count);
        GISVectorLayer layer(m_path);
        QSet<long long> ids{7};
        if (count >= 2) ids.insert(19);
        if (count >= 3) ids.insert(42);
        layer.setSelectedFeatureIds(ids);
        CorridorSourcesWidget widget;
        widget.setLayers({&layer});
        auto *mode = control<QComboBox>(widget, "corridorSourceMode");
        QVERIFY(mode);
        QVERIFY(!mode->accessibleName().isEmpty());
        const int bank = mode->findData(QStringLiteral("bankPair"));
        QVERIFY(bank >= 0);
        mode->setCurrentIndex(bank);
        QVERIFY(!control<QComboBox>(widget, "corridorWidthMode")->isEnabled());
        QVERIFY(!control<QComboBox>(widget, "corridorWidthField")->isEnabled());
        QVERIFY(!control<QLineEdit>(widget, "corridorWidth")->isEnabled());
        control<QLineEdit>(widget, "corridorAcross")->setText(QStringLiteral("5"));
        control<QLineEdit>(widget, "corridorAlong")->setText(QStringLiteral("0"));
        control<QPushButton>(widget, "corridorAdd")->click();
        auto *table = control<QTableWidget>(widget, "corridorSourcesTable");
        if (count != 2) {
            QCOMPARE(table->rowCount(), 0);
            QVERIFY(control<QLabel>(widget, "corridorMessage")->text().contains(QStringLiteral("two")));
            return;
        }
        QCOMPARE(table->rowCount(), 1);
        QVERIFY(table->item(0, 0)->text().contains(QStringLiteral("Bank pair")));
        QVERIFY(!(table->item(0, 2)->flags() & Qt::ItemIsEditable));
        QVERIFY(!(table->item(0, 3)->flags() & Qt::ItemIsEditable));
        QVERIFY(table->item(0, 4)->flags() & Qt::ItemIsEditable);
        QVERIFY(table->item(0, 5)->flags() & Qt::ItemIsEditable);
        QVector<mesh::CorridorSource> rows;
        QString error;
        QVERIFY2(widget.sources(&rows, &error), qPrintable(error));
        QCOMPARE(rows.size(), 1);
        QVERIFY(rows[0].bankPair);
        QVERIFY(rows[0].widthField.isEmpty());
        QCOMPARE(rows[0].featureIds, (QVector<qint64>{7, 19}));
        QCOMPARE(rows[0].across, 5);
        QCOMPARE(rows[0].along, 0.0);
        mode->setCurrentIndex(mode->findData(QStringLiteral("centerline")));
        QVERIFY(control<QComboBox>(widget, "corridorWidthMode")->isEnabled());
        QVERIFY(control<QLineEdit>(widget, "corridorWidth")->isEnabled());
    }

    void restoredBankPairRetainsRoleAndEditableSpacing()
    {
        CorridorSourcesWidget widget;
        auto bank = saved();
        bank.bankPair = true;
        widget.setSources({bank});
        widget.setLayers({});
        auto *table = control<QTableWidget>(widget, "corridorSourcesTable");
        QVERIFY(table->item(0, 0)->text().contains(QStringLiteral("Bank pair")));
        QVERIFY(table->item(0, 0)->text().contains(QStringLiteral("not on map")));
        QVERIFY(!(table->item(0, 2)->flags() & Qt::ItemIsEditable));
        QVERIFY(!(table->item(0, 3)->flags() & Qt::ItemIsEditable));
        table->item(0, 4)->setText(QStringLiteral("4"));
        table->item(0, 5)->setText(QStringLiteral("8.5"));
        QVector<mesh::CorridorSource> rows;
        QString error;
        QVERIFY2(widget.sources(&rows, &error), qPrintable(error));
        QVERIFY(rows[0].bankPair);
        QCOMPARE(rows[0].featureIds, bank.featureIds);
        QCOMPARE(rows[0].across, 4);
        QCOMPARE(rows[0].along, 8.5);
        QCOMPARE(rows[0].width, bank.width);
        QCOMPARE(rows[0].geometryDigest, bank.geometryDigest);
    }

    void inconsistentRestoredBankPairIsNotSilentlyRepaired_data()
    {
        QTest::addColumn<QString>("invalid");
        QTest::newRow("one-id") << QStringLiteral("one-id");
        QTest::newRow("duplicate-ids") << QStringLiteral("duplicate-ids");
        QTest::newRow("width-field") << QStringLiteral("width-field");
    }
    void inconsistentRestoredBankPairIsNotSilentlyRepaired()
    {
        QFETCH(QString, invalid);
        CorridorSourcesWidget widget;
        auto bank = saved();
        bank.bankPair = true;
        if (invalid == "one-id") bank.featureIds = {7};
        if (invalid == "duplicate-ids") bank.featureIds = {7, 7};
        if (invalid == "width-field") bank.widthField = QStringLiteral("width_m");
        widget.setSources({bank});
        QVector<mesh::CorridorSource> rows{saved()};
        QString error;
        QVERIFY(!widget.sources(&rows, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!rows[0].bankPair);
        QCOMPARE(control<QTableWidget>(widget, "corridorSourcesTable")->rowCount(), 1);
    }

    void capturesOnlySelectedIdsAndAssignedCrsWithoutReadingFeatures()
    {
        GISVectorLayer layer(m_path);
        QVERIFY(layer.ogrLayer());
        layer.setSRS(new SpatialReferenceSystem(QStringLiteral("EPSG"), 32617, &layer), true);
        layer.setSelectedFeatureIds({19});
        layer.ogrLayer()->ResetReading();
        std::unique_ptr<OGRFeature, decltype(&OGRFeature::DestroyFeature)> first(
            layer.ogrLayer()->GetNextFeature(), &OGRFeature::DestroyFeature);
        QVERIFY(first);
        QCOMPARE(first->GetFID(), 7);
        CorridorSourcesWidget widget;
        widget.setLayers({&layer});
        control<QPushButton>(widget, "corridorAdd")->click();
        QVector<mesh::CorridorSource> rows;
        QString error;
        QVERIFY2(widget.sources(&rows, &error), qPrintable(error));
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows[0].featureIds, QVector<qint64>{19});
        QCOMPARE(rows[0].path, m_path);
        QCOMPARE(rows[0].layerName, layer.ogrLayerName());
        QCOMPARE(rows[0].sourceCRSWkt, layer.srs()->toWkt());
        QVERIFY(rows[0].meshCRSWkt.isEmpty());
        QVERIFY(rows[0].geometryDigest.isEmpty());
        layer.setSelectedFeatureIds({7});
        QVERIFY(widget.sources(&rows, &error));
        QCOMPARE(rows[0].featureIds, QVector<qint64>{19});
        std::unique_ptr<OGRFeature, decltype(&OGRFeature::DestroyFeature)> second(
            layer.ogrLayer()->GetNextFeature(), &OGRFeature::DestroyFeature);
        QVERIFY(second);
        QCOMPARE(second->GetFID(), 19); // Widget inspected schema only, not feature cursor.
    }

    void noSelectionProducesInlineGuidance()
    {
        GISVectorLayer layer(m_path);
        CorridorSourcesWidget widget;
        widget.setLayers({&layer});
        control<QPushButton>(widget, "corridorAdd")->click();
        QCOMPARE(control<QTableWidget>(widget, "corridorSourcesTable")->rowCount(), 0);
        QVERIFY(control<QLabel>(widget, "corridorMessage")->text().contains(QStringLiteral("Select")));
        QVERIFY(!QApplication::activeModalWidget());
    }

    void widthFieldChoicesAreNumericSchemaNames()
    {
        GISVectorLayer layer(m_path);
        layer.setSelectedFeatureIds({7, 19});
        CorridorSourcesWidget widget;
        widget.setLayers({&layer});
        auto *fields = control<QComboBox>(widget, "corridorWidthField");
        QVERIFY(fields->findData(QStringLiteral("width_m")) >= 0);
        QVERIFY(fields->findData(QStringLiteral("lanes")) >= 0);
        QCOMPARE(fields->findData(QStringLiteral("name")), -1);
        control<QComboBox>(widget, "corridorWidthMode")->setCurrentIndex(1);
        fields->setCurrentIndex(fields->findData(QStringLiteral("width_m")));
        control<QPushButton>(widget, "corridorAdd")->click();
        QVector<mesh::CorridorSource> rows;
        QString error;
        QVERIFY2(widget.sources(&rows, &error), qPrintable(error));
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows[0].widthField, QStringLiteral("width_m"));
        QCOMPARE(rows[0].featureIds, (QVector<qint64>{7, 19}));
    }

    void restoredMissingLayersRemainEditableAndPreserveMetadata()
    {
        CorridorSourcesWidget widget;
        const auto original = saved();
        widget.setSources({original});
        widget.setLayers({});
        auto *table = control<QTableWidget>(widget, "corridorSourcesTable");
        QCOMPARE(table->rowCount(), 1);
        QVERIFY(table->item(0, 0)->text().contains(QStringLiteral("river_corridors")));
        table->item(0, 3)->setText(QStringLiteral("21.5"));
        table->item(0, 4)->setText(QStringLiteral("4"));
        QVector<mesh::CorridorSource> rows;
        QString error;
        QVERIFY2(widget.sources(&rows, &error), qPrintable(error));
        QCOMPARE(rows[0].width, 21.5);
        QCOMPARE(rows[0].across, 4);
        QCOMPARE(rows[0].path, original.path);
        QCOMPARE(rows[0].featureIds, original.featureIds);
        QCOMPARE(rows[0].sourceCRSWkt, original.sourceCRSWkt);
        QCOMPARE(rows[0].meshCRSWkt, original.meshCRSWkt);
        QCOMPARE(rows[0].geometryDigest, original.geometryDigest);
        QCOMPARE(rows[0].sourceFiles, original.sourceFiles);
        table->item(0, 2)->setText(QStringLiteral("width_m"));
        QVERIFY(widget.sources(&rows, &error));
        QVERIFY(rows[0].geometryDigest.isEmpty());
        QCOMPARE(rows[0].widthField, QStringLiteral("width_m"));
    }

    void invalidSavedNumbersRetainDraftAndOutput_data()
    {
        QTest::addColumn<int>("column");
        QTest::addColumn<QString>("value");
        QTest::newRow("empty-width") << 3 << QString();
        QTest::newRow("nan-width") << 3 << QStringLiteral("nan");
        QTest::newRow("fraction-across") << 4 << QStringLiteral("2.5");
        QTest::newRow("overflow-across") << 4 << QStringLiteral("2147483648");
        QTest::newRow("empty-along") << 5 << QString();
        QTest::newRow("text-along") << 5 << QStringLiteral("oops");
        QTest::newRow("infinite-along") << 5 << QStringLiteral("inf");
        QTest::newRow("negative-along") << 5 << QStringLiteral("-1");
    }

    void invalidSavedNumbersRetainDraftAndOutput()
    {
        QFETCH(int, column); QFETCH(QString, value);
        CorridorSourcesWidget widget;
        widget.setSources({saved()});
        auto *table = control<QTableWidget>(widget, "corridorSourcesTable");
        table->item(0, column)->setText(value);
        QVector<mesh::CorridorSource> rows{saved()};
        QString error;
        QVERIFY(!widget.sources(&rows, &error));
        QVERIFY2(error.contains(QStringLiteral("row 1")), qPrintable(error));
        QCOMPARE(table->currentColumn(), column);
        QCOMPARE(table->item(0, column)->text(), value);
        QCOMPARE(rows[0].width, 15.0); // Caller output is unchanged on failure.
    }

    void invalidNewValuesDoNotAddRowsAndZeroAlongIsAllowed()
    {
        GISVectorLayer layer(m_path);
        layer.setSelectedFeatureIds({7});
        CorridorSourcesWidget widget;
        widget.setLayers({&layer});
        control<QLineEdit>(widget, "corridorAlong")->setText(QStringLiteral("oops"));
        control<QPushButton>(widget, "corridorAdd")->click();
        QCOMPARE(control<QTableWidget>(widget, "corridorSourcesTable")->rowCount(), 0);
        QVERIFY(control<QLabel>(widget, "corridorMessage")->text().contains(QStringLiteral("Along")));
        control<QLineEdit>(widget, "corridorAlong")->setText(QStringLiteral("0"));
        control<QPushButton>(widget, "corridorAdd")->click();
        QVector<mesh::CorridorSource> rows;
        QString error;
        QVERIFY2(widget.sources(&rows, &error), qPrintable(error));
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows[0].along, 0.0);
    }

    void emptyRestoredSelectionNeverMeansAllFeatures()
    {
        CorridorSourcesWidget widget;
        auto row = saved();
        row.featureIds.clear();
        widget.setSources({row});
        QVector<mesh::CorridorSource> rows;
        QString error;
        QVERIFY(!widget.sources(&rows, &error));
        QVERIFY2(error.contains(QStringLiteral("selected features")), qPrintable(error));
        QVERIFY(rows.isEmpty());
        QCOMPARE(control<QTableWidget>(widget, "corridorSourcesTable")->rowCount(), 1);
    }

    void editedFieldMustBeAnActualNumericFieldWhenLayerIsAvailable()
    {
        GISVectorLayer layer(m_path);
        layer.setSelectedFeatureIds({7});
        CorridorSourcesWidget widget;
        widget.setLayers({&layer});
        control<QPushButton>(widget, "corridorAdd")->click();
        auto *table = control<QTableWidget>(widget, "corridorSourcesTable");
        table->item(0, 2)->setText(QStringLiteral("name"));
        QVector<mesh::CorridorSource> rows;
        QString error;
        QVERIFY(!widget.sources(&rows, &error));
        QCOMPARE(table->currentColumn(), 2);
        QVERIFY2(error.contains(QStringLiteral("numeric field")), qPrintable(error));
    }

    void sourceWithoutAssignedCrsIsNotAdded()
    {
        GISVectorLayer layer(m_path);
        layer.setSRS(nullptr);
        layer.setSelectedFeatureIds({7});
        CorridorSourcesWidget widget;
        widget.setLayers({&layer});
        control<QPushButton>(widget, "corridorAdd")->click();
        QCOMPARE(control<QTableWidget>(widget, "corridorSourcesTable")->rowCount(), 0);
        QVERIFY(control<QLabel>(widget, "corridorMessage")->text().contains(QStringLiteral("coordinate reference")));
    }

    void deletedLayerCannotLeaveDanglingComboOrEraseSavedRows()
    {
        auto layer = std::make_unique<GISVectorLayer>(m_path);
        layer->setSelectedFeatureIds({7});
        CorridorSourcesWidget widget;
        widget.setLayers({layer.get()});
        control<QPushButton>(widget, "corridorAdd")->click();
        layer.reset();
        QCoreApplication::processEvents();
        control<QPushButton>(widget, "corridorAdd")->click();
        QVector<mesh::CorridorSource> rows;
        QString error;
        QVERIFY2(widget.sources(&rows, &error), qPrintable(error));
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows[0].featureIds, QVector<qint64>{7});
    }

    void validationRevealsNestedAuthoringTabs()
    {
        QTabWidget outer;
        outer.addTab(new QWidget, QStringLiteral("Other"));
        auto *inner = new QTabWidget;
        inner->addTab(new QWidget, QStringLiteral("Other"));
        auto *scroll = new QScrollArea;
        auto *widget = new CorridorSourcesWidget;
        scroll->setWidget(widget);
        scroll->setWidgetResizable(true);
        inner->addTab(scroll, QStringLiteral("Corridors"));
        outer.addTab(inner, QStringLiteral("Quality"));
        widget->setSources({saved()});
        auto *table = control<QTableWidget>(*widget, "corridorSourcesTable");
        table->item(0, 5)->setText(QStringLiteral("bad"));
        QVector<mesh::CorridorSource> rows;
        QString error;
        QVERIFY(!widget->sources(&rows, &error));
        QCOMPARE(outer.currentIndex(), 1);
        QCOMPARE(inner->currentIndex(), 1);
        QCOMPARE(table->currentColumn(), 5);
    }

    void multipleRowsRemoveAndAccessibleControls()
    {
        CorridorSourcesWidget widget;
        auto row = saved();
        widget.setSources({row, row, row});
        auto *table = control<QTableWidget>(widget, "corridorSourcesTable");
        table->item(1, 6)->setText(QStringLiteral("survivor"));
        for (int r : {0, 2}) table->selectionModel()->select(table->model()->index(r, 0),
            QItemSelectionModel::Select | QItemSelectionModel::Rows);
        control<QPushButton>(widget, "corridorRemove")->click();
        QCOMPARE(table->rowCount(), 1);
        QVector<mesh::CorridorSource> rows;
        QString error;
        QVERIFY(widget.sources(&rows, &error));
        QCOMPARE(rows[0].tag, QStringLiteral("survivor"));
        QVERIFY(!table->accessibleName().isEmpty());
        for (auto *label : widget.findChildren<QLabel *>())
            if (label->text().contains('&')) QVERIFY(label->buddy());
        QSet<QChar> mnemonicKeys;
        for (auto *label : widget.findChildren<QLabel *>()) {
            const int marker = label->text().indexOf('&');
            if (marker < 0) continue;
            const QChar key = label->text().at(marker + 1).toLower();
            QVERIFY(!mnemonicKeys.contains(key));
            mnemonicKeys.insert(key);
        }
        for (auto *button : widget.findChildren<QPushButton *>()) {
            QVERIFY(!button->accessibleName().isEmpty());
            QVERIFY(!button->autoDefault());
            const int marker = button->text().indexOf('&');
            if (marker >= 0) {
                const QChar key = button->text().at(marker + 1).toLower();
                QVERIFY(!mnemonicKeys.contains(key));
                mnemonicKeys.insert(key);
            }
        }
    }
};
QTEST_MAIN(TestCorridorSourcesWidget)
#include "test_corridorsourceswidget.moc"
