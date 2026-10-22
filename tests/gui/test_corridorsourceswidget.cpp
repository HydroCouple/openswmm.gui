// Selected corridor recipes remain editable and never silently expand to all features.

/**
 * @file      test_corridorsourceswidget.cpp
 * @brief     Tests for corridorsourceswidget.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

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
    /**
     *
     * @brief Find a named descendant control of the requested widget type.
     * @tparam T QObject-derived control type used by Qt's typed recursive lookup.
     * @param widget Borrowed live corridor editor; no ownership transfer.
     * @param name Borrowed Latin-1 object name, consumed synchronously.
     * @return Borrowed first matching child or null; callers assert/control validity before
     * use. Does not check visibility, enabled state or retained pointer lifetime.
     *
     */
    template <class T> static T *control(CorridorSourcesWidget &widget, const char *name)
    {
        return widget.findChild<T *>(QString::fromLatin1(name));
    }
    /**
     *
     * @brief Build a detached saved recipe with deliberately unavailable source path and retained
     * digest metadata.
     * @return Owned value selecting IDs nine/42, width15, along0, across3, reach-A tag,
     * source/mesh CRS strings and dependency path. Does not validate or create the missing
     * file; tests use this value to distinguish draft edits from metadata preservation.
     *
     */
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
    /**
     *
     * @brief Create the persistent two-feature GeoJSON fixture used for selection and schema
     * tests.
     * Uses SWMMVIS_CORRIDOR_WIDGET_TEST_OUTPUT or tests/output/corridor_sources_widget,
     * creates directories and truncates selected_roads.geojson. Checks complete byte write;
     * fixture uses IDs seven/19 and numeric width_m/lanes plus string name properties.
     * Does not clean the file afterward or preserve an existing file at that path.
     *
     */
    void initTestCase()
    {
        const QString dir =
            qEnvironmentVariable("SWMMVIS_CORRIDOR_WIDGET_TEST_OUTPUT",
                                 QStringLiteral("tests/output/corridor_sources_widget"));
        QVERIFY(QDir().mkpath(dir));
        m_path = QFileInfo(QDir(dir).filePath(QStringLiteral("selected_roads.geojson")))
                     .absoluteFilePath();
        QFile fixture(m_path);
        QVERIFY(fixture.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const QByteArray json = R"({"type":"FeatureCollection","features":[
          {"type":"Feature","id":7,"properties":{"width_m":12.5,"lanes":2,"name":"road A"},"geometry":{"type":"LineString","coordinates":[[0,0],[20,0]]}},
          {"type":"Feature","id":19,"properties":{"width_m":8.5,"lanes":1,"name":"river B"},"geometry":{"type":"LineString","coordinates":[[0,30],[20,30]]}}
        ]})";
        QCOMPARE(fixture.write(json), json.size());
    }

    /**
     *
     * @brief Register one/two/three selected-id counts to pin the exactly-two bank selection
     * rule.
     * The third id is deliberately absent from the physical fixture; this widget captures
     * selection metadata rather than loading every selected geometry.
     *
     */
    void bankPairSelection_data()
    {
        QTest::addColumn<int>("count");
        QTest::newRow("one-bank-refused") << 1;
        QTest::newRow("two-banks") << 2;
        QTest::newRow("three-banks-refused") << 3;
    }
    /**
     *
     * @brief Require bank mode to disable width controls, reject wrong selection counts and
     * preserve a valid two-bank recipe.
     * Checks accessibility name, inline guidance, locked width cells, editable spacing,
     * sorted IDs seven/19, across5 and zero along; switching to centerline reenables width
     * controls. Does not create a corridor mesh or read selected feature geometry.
     *
     */
    void bankPairSelection()
    {
        QFETCH(int, count);
        GISVectorLayer layer(m_path);
        QSet<long long> ids{7};
        if (count >= 2)
            ids.insert(19);
        if (count >= 3)
            ids.insert(42);
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
            QVERIFY(
                control<QLabel>(widget, "corridorMessage")->text().contains(QStringLiteral("two")));
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

    /**
     *
     * @brief Require a missing-layer restored bank recipe to retain role/digest while accepting
     * across/along edits.
     * Checks not-on-map label, locked width cells, selected IDs, across4, along8.5, original
     * width and digest. Does not open the unavailable source or generate geometry.
     *
     */
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

    /**
     *
     * @brief Register one-id, duplicate-id and width-field inconsistencies in saved bank recipes.
     * Supplies scenario strings to the paired validation case.
     *
     */
    void inconsistentRestoredBankPairIsNotSilentlyRepaired_data()
    {
        QTest::addColumn<QString>("invalid");
        QTest::newRow("one-id") << QStringLiteral("one-id");
        QTest::newRow("duplicate-ids") << QStringLiteral("duplicate-ids");
        QTest::newRow("width-field") << QStringLiteral("width-field");
    }
    /**
     *
     * @brief Require invalid restored bank metadata to fail validation without converting the
     * caller's saved output into a bank.
     * Keeps the one draft row, requires nonempty error and checks the preexisting output's
     * bankPair remains false; does not compare every output field or silently repair input.
     *
     */
    void inconsistentRestoredBankPairIsNotSilentlyRepaired()
    {
        QFETCH(QString, invalid);
        CorridorSourcesWidget widget;
        auto bank = saved();
        bank.bankPair = true;
        if (invalid == "one-id")
            bank.featureIds = {7};
        if (invalid == "duplicate-ids")
            bank.featureIds = {7, 7};
        if (invalid == "width-field")
            bank.widthField = QStringLiteral("width_m");
        widget.setSources({bank});
        QVector<mesh::CorridorSource> rows{saved()};
        QString error;
        QVERIFY(!widget.sources(&rows, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!rows[0].bankPair);
        QCOMPARE(control<QTableWidget>(widget, "corridorSourcesTable")->rowCount(), 1);
    }

    /**
     *
     * @brief Require Add to snapshot selected IDs and assigned CRS while preserving the OGR
     * feature cursor.
     * Assigns EPSG:32617, consumes first feature, captures only ID19, then changes map
     * selection and confirms the saved ID remains19. Next OGR read must still be19;
     * checks source path/layer/WKT and empty mesh CRS/digest. No geometry transformation.
     *
     */
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

    /**
     *
     * @brief Require Add without selected features to retain zero rows and show Select guidance
     * inline.
     * Checks no active modal widget; does not assert exact label text or keyboard focus.
     *
     */
    void noSelectionProducesInlineGuidance()
    {
        GISVectorLayer layer(m_path);
        CorridorSourcesWidget widget;
        widget.setLayers({&layer});
        control<QPushButton>(widget, "corridorAdd")->click();
        QCOMPARE(control<QTableWidget>(widget, "corridorSourcesTable")->rowCount(), 0);
        QVERIFY(
            control<QLabel>(widget, "corridorMessage")->text().contains(QStringLiteral("Select")));
        QVERIFY(!QApplication::activeModalWidget());
    }

    /**
     *
     * @brief Require the field picker to offer numeric width_m/lanes and exclude string name.
     * Adds a field-width recipe, then checks width_m and exact selected IDs seven/19;
     * no actual feature width values or generated patch dimensions are inspected.
     *
     */
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

    /**
     *
     * @brief Require detached restored recipes to accept numeric edits while preserving source
     * identity/CRS/digest/dependencies.
     * Checks width21.5/across4 and original metadata, then assigning width_m must clear the
     * saved geometry digest. Missing source availability is accepted for this draft path;
     * no physical file read or final geometry validation occurs.
     *
     */
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

    /**
     *
     * @brief Register eight invalid width/across/along drafts including blanks, NaN, overflow and
     * infinity.
     * Supplies logical table column and original draft string; fractional across and
     * negative along cover integer/domain validation separately.
     *
     */
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

    /**
     *
     * @brief Require invalid restored numeric drafts to stay visible, identify row one and focus
     * the failing column.
     * Seeds caller output with saved() and checks its width remains15 after refusal;
     * draft text is preserved exactly. Does not compare every output field or repair input.
     *
     */
    void invalidSavedNumbersRetainDraftAndOutput()
    {
        QFETCH(int, column);
        QFETCH(QString, value);
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

    /**
     *
     * @brief Require invalid new along text to prevent Add and zero along to create a valid
     * single recipe.
     * Checks inline Along guidance and resulting along0, without inspecting every row field
     * or building corridor geometry.
     *
     */
    void invalidNewValuesDoNotAddRowsAndZeroAlongIsAllowed()
    {
        GISVectorLayer layer(m_path);
        layer.setSelectedFeatureIds({7});
        CorridorSourcesWidget widget;
        widget.setLayers({&layer});
        control<QLineEdit>(widget, "corridorAlong")->setText(QStringLiteral("oops"));
        control<QPushButton>(widget, "corridorAdd")->click();
        QCOMPARE(control<QTableWidget>(widget, "corridorSourcesTable")->rowCount(), 0);
        QVERIFY(
            control<QLabel>(widget, "corridorMessage")->text().contains(QStringLiteral("Along")));
        control<QLineEdit>(widget, "corridorAlong")->setText(QStringLiteral("0"));
        control<QPushButton>(widget, "corridorAdd")->click();
        QVector<mesh::CorridorSource> rows;
        QString error;
        QVERIFY2(widget.sources(&rows, &error), qPrintable(error));
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows[0].along, 0.0);
    }

    /**
     *
     * @brief Require a restored empty-id recipe to fail with selected-features guidance and keep
     * its draft row.
     * Caller output stays empty; no implicit all-feature expansion or dataset read occurs.
     *
     */
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

    /**
     *
     * @brief Require editing a connected recipe's width field to string name to fail at the field
     * column.
     * Checks numeric-field diagnostic and logical column two focus; does not compare exact
     * error wording or the previously valid recipe after correcting it.
     *
     */
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

    /**
     *
     * @brief Require a selected layer without assigned CRS to produce no recipe and
     * coordinate-reference guidance.
     * Clears only the in-memory layer SRS; does not change GeoJSON content or open a modal
     * projection-assignment dialog.
     *
     */
    void sourceWithoutAssignedCrsIsNotAdded()
    {
        GISVectorLayer layer(m_path);
        layer.setSRS(nullptr);
        layer.setSelectedFeatureIds({7});
        CorridorSourcesWidget widget;
        widget.setLayers({&layer});
        control<QPushButton>(widget, "corridorAdd")->click();
        QCOMPARE(control<QTableWidget>(widget, "corridorSourcesTable")->rowCount(), 0);
        QVERIFY(control<QLabel>(widget, "corridorMessage")
                    ->text()
                    .contains(QStringLiteral("coordinate reference")));
    }

    /**
     *
     * @brief Require deleting the selected source layer to preserve an already captured recipe
     * and make later Add harmless.
     * Processes events after unique_ptr destruction, clicks Add again and checks one row
     * with IDseven; does not dereference the destroyed layer or validate its saved geometry.
     *
     */
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

    /**
     *
     * @brief Require invalid along draft validation to select both containing tabs and the
     * failing logical column.
     * Embeds the editor in a scroll area under two initially unselected tab pages; widget
     * ownership follows Qt parentage. Does not assert screen visibility or scroll position.
     *
     */
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

    /**
     *
     * @brief Require removing first/third selected rows to preserve the middle tagged recipe and
     * accessible controls.
     * Checks table name, buddies for mnemonic labels, unique label/button mnemonic keys,
     * nonempty button names and disabled auto-default. Does not run a screen reader or
     * assert keyboard focus order; assumes each ampersand has a following mnemonic char.
     *
     */
    void multipleRowsRemoveAndAccessibleControls()
    {
        CorridorSourcesWidget widget;
        auto row = saved();
        widget.setSources({row, row, row});
        auto *table = control<QTableWidget>(widget, "corridorSourcesTable");
        table->item(1, 6)->setText(QStringLiteral("survivor"));
        for (int r : {0, 2})
            table->selectionModel()->select(table->model()->index(r, 0),
                                            QItemSelectionModel::Select |
                                                QItemSelectionModel::Rows);
        control<QPushButton>(widget, "corridorRemove")->click();
        QCOMPARE(table->rowCount(), 1);
        QVector<mesh::CorridorSource> rows;
        QString error;
        QVERIFY(widget.sources(&rows, &error));
        QCOMPARE(rows[0].tag, QStringLiteral("survivor"));
        QVERIFY(!table->accessibleName().isEmpty());
        for (auto *label : widget.findChildren<QLabel *>())
            if (label->text().contains('&'))
                QVERIFY(label->buddy());
        QSet<QChar> mnemonicKeys;
        for (auto *label : widget.findChildren<QLabel *>()) {
            const int marker = label->text().indexOf('&');
            if (marker < 0)
                continue;
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
