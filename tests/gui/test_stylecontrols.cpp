/**
 * @file      test_stylecontrols.cpp
 * @brief     Tests for stylecontrols.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "ui/widgets/colorbutton.h"
#include "ui/widgets/classificationeditor.h"
#include "ui/dialogs/editors/classificationbindings.h"
#include "ui/dialogs/swmm2dresultsstylepanel.h"
#include "layers/swmm2dresultslayer.h"
#include "render/sublayers/resultscalarsublayer.h"
#include <QListWidget>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QStandardItemModel>
#include <QTableView>
#include "ui/widgets/sunpositionthumb.h"
#include "ui/widgets/labelconfigeditor.h"
#include "ui/dialogs/colorrampeditordialog.h"
#include "ui/dialogs/swmm2dmeshstylepanel.h"
#include "ui/theme/thememanager.h"
#include "layers/swmm2dmeshlayer.h"

#include <QAccessible>
#include <QColorDialog>
#include <QDir>
#include <QFormLayout>
#include <QImage>
#include <QSettings>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTimer>
#include <QtTest>

using namespace openswmmvis::ui;

namespace
{
class PanelVariableSource : public IMesh2DSource
{
public:
    /**
     *
     * @brief Report the triangular panel fixture vertex count.
     * @return Three geometry vertices, without reading data.
     *
     */
    int vertexCount() const override { return 3; }
    /**
     *
     * @brief Report the native panel fixture cell count.
     * @return One native cell, including in the quad subclass.
     *
     */
    int triangleCount() const override { return 1; }
    /**
     *
     * @brief Report one synthetic result frame.
     * @return One; individual read stubs do not validate time indices.
     *
     */
    int timeCount() const override { return 1; }
    /**
     *
     * @brief Replace geometry outputs with a flat unit right triangle.
     * @param x Borrowed output three x coordinates in fixture scene units.
     * @param y Borrowed output three y coordinates in matching order.
     * @param z Borrowed output three zero bed elevations.
     * @param t Borrowed output one zero-based triangle (0,1,2).
     * @return Always true on completion; sequential allocations can partially mutate
     * outputs on exception. No output references are retained.
     *
     */
    bool readMeshGeometry(std::vector<double> &x, std::vector<double> &y, std::vector<double> &z,
                          std::vector<std::array<int, 3>> &t) override
    {
        x = {0, 1, 0};
        y = {0, 0, 1};
        z = {0, 0, 0};
        t = {{0, 1, 2}};
        return true;
    }
    /**
     *
     * @brief Replace the panel fixture depth with one zero.
     * @param time Unused report index; not checked against timeCount.
     * @param v Borrowed output replaced by one zero depth in metres.
     * @return Always true unless allocation throws; no retained output reference.
     *
     */
    bool readDepthsAt(int time, std::vector<float> &v) override
    {
        v = {0};
        return true;
    }
    QVector<openswmmvis::io::Mesh2DResultVariable>
    /**
     *
     * @brief Describe two synthetic Nitrate variables used by unit-gating controls.
     * @param warnings Optional unused warning-list pointer, never read or written.
     * @return Owned surface and saturated-groundwater descriptors, one frame each;
     * surface uses known mg/L and groundwater deliberately has unknown empty units.
     * No dataset access or parent/source lifetime transfer occurs.
     *
     */
    faceVariables(QStringList *warnings = nullptr) const override
    {
        using V = openswmmvis::io::Mesh2DResultVariable;
        V a;
        a.dataset = "Mesh2_face_species_conc";
        a.species = "Nitrate";
        a.label = "Surface Nitrate";
        a.units = "mg/L";
        a.unitsKnown = true;
        a.frameCount = 1;
        V b = a;
        b.dataset = "Mesh2_face_gw_sat_species_conc";
        b.domain = V::Domain::Groundwater;
        b.zone = V::Zone::Saturated;
        b.label = "Saturated Nitrate";
        b.units.clear();
        b.unitsKnown = false;
        return {a, b};
    }
    /**
     *
     * @brief Return one zero scalar with domain-dependent availability for panel tests.
     * @param v Borrowed descriptor; Surface selects Valid, all other domains Waterless.
     * Other metadata is not validated.
     * @param time Unused report index, not range checked.
     * @param out Borrowed output replaced by one zero scalar; units follow v metadata.
     * @param status Borrowed output one matching Valid/Waterless status.
     * @return True on completion; sequential allocation failures can leave partial
     * outputs. No retained references, worker activity or dataset IO.
     *
     */
    bool readFaceVariableAt(const openswmmvis::io::Mesh2DResultVariable &v, int time,
                            std::vector<float> &out,
                            std::vector<openswmmvis::io::Mesh2DValueStatus> &status) override
    {
        using V = openswmmvis::io::Mesh2DResultVariable;
        using S = openswmmvis::io::Mesh2DValueStatus;
        out = {0};
        status = {v.domain == V::Domain::Surface ? S::Valid : S::Waterless};
        return true;
    }
};
class PanelQuadVariableSource : public PanelVariableSource
{
public:
    /**
     *
     * @brief Report four vertices for the single native quad fixture.
     * @return Four, while native cell count remains the inherited one.
     *
     */
    int vertexCount() const override { return 4; }
    /**
     *
     * @brief Replace outputs with a flat unit square represented by two display triangles.
     * @param x Borrowed output four x coordinates in fixture scene units.
     * @param y Borrowed output four matching y coordinates.
     * @param z Borrowed output four zero elevations.
     * @param t Borrowed output triangles (0,1,2) and (0,2,3).
     * @return True on ordinary completion; assignments allocate independently and may
     * partially complete on exception. Native cell topology is supplied by readCells.
     *
     */
    bool readMeshGeometry(std::vector<double> &x, std::vector<double> &y, std::vector<double> &z,
                          std::vector<std::array<int, 3>> &t) override
    {
        x = {0, 1, 1, 0};
        y = {0, 0, 1, 1};
        z = {0, 0, 0, 0};
        t = {{0, 1, 2}, {0, 2, 3}};
        return true;
    }
    /**
     *
     * @brief Replace outputs with one native counterclockwise square quad.
     * @param x Borrowed output four fixture-unit x coordinates.
     * @param y Borrowed output four fixture-unit y coordinates.
     * @param z Borrowed output four zero elevations.
     * @param c Borrowed output one native cell (0,1,2,3).
     * @return True after geometry and cell assignment; exceptions can partially update
     * outputs. Temporary display triangles are discarded; no references are retained.
     *
     */
    bool readCells(std::vector<double> &x, std::vector<double> &y, std::vector<double> &z,
                   std::vector<std::array<int, 4>> &c) override
    {
        std::vector<std::array<int, 3>> t;
        readMeshGeometry(x, y, z, t);
        c = {{0, 1, 2, 3}};
        return true;
    }
};
class TestClassificationBinding : public IClassificationBinding
{
public:
    OpenSWMM::Render::ClassificationScheme value;
    QPair<double, double> range{0.0, 10.0};
    QVector<double> samples;
    int writes = 0;
    /**
     *
     * @brief Read the fake binding scheme.
     * @return By-value scheme copy; mutations require setScheme to affect the binding.
     *
     */
    OpenSWMM::Render::ClassificationScheme scheme() const override { return value; }
    /**
     *
     * @brief Replace the fake binding scheme and count every write.
     * @param s Borrowed source copied into value without validation/equality suppression.
     * Increments writes after copy; performs no model mutation or signal emission.
     *
     */
    void setScheme(const OpenSWMM::Render::ClassificationScheme &s) override
    {
        value = s;
        ++writes;
    }
    /**
     *
     * @brief Read the configured fake data bounds.
     * @return By-value pair, initially [0,10], in the fixture scalar units.
     *
     */
    QPair<double, double> dataRange() const override { return range; }
    /**
     *
     * @brief Read configured fake classification samples.
     * @return Owned implicitly shared vector; initially empty, no dataset sampling.
     *
     */
    QVector<double> sampleValues() const override { return samples; }
    /**
     *
     * @brief Advertise custom bounds for all binding test cases.
     * @return Always true, without checking sample/range validity.
     *
     */
    bool supportsCustomRange() const override { return true; }
};
/**
 *
 * @brief Select the directory for style-control screenshots and isolated settings.
 * @return SWMMVIS_STYLE_TEST_OUTPUT verbatim when set, otherwise the relative
 * test_stylecontrols_output path. Does not create or validate the directory.
 *
 */
QString outputDir()
{
    return qEnvironmentVariable("SWMMVIS_STYLE_TEST_OUTPUT",
                                QStringLiteral("test_stylecontrols_output"));
}

class ObservedSunThumb : public SunPositionThumb
{
public:
    int paints = 0;

protected:
    /**
     *
     * @brief Count each sun-thumb paint before delegating to the production widget.
     * @param event Borrowed Qt paint event, valid for the synchronous GUI-thread call.
     * Increments paints even if base painting fails; does not retain/delete the event.
     *
     */
    void paintEvent(QPaintEvent *event) override
    {
        ++paints;
        SunPositionThumb::paintEvent(event);
    }
};
}

class TestStyleControls : public QObject
{
    Q_OBJECT
private slots:
    /**
     *
     * @brief Require scalar contour/label presentation controls and known-unit GIS export gating.
     * Checks accessible presentation naming, then switches to unknown-unit groundwater
     * metadata and requires GIS export disabled after deferred editor deletion.
     *
     */
    void additionalScalarPresentationAndGisUnitGate()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<PanelVariableSource>());
        auto *sub = layer.addResultSublayer(layer.resultVariables()[0].key());
        Swmm2DResultsStylePanel panel(&layer);
        auto *presentation = panel.findChild<QComboBox *>("additionalResultPresentation");
        QVERIFY(presentation);
        presentation->setCurrentIndex(1);
        QCOMPARE(sub->fillStyle()->presentation(),
                 OpenSWMM::Render::ResultScalarStyle::Presentation::Contours);
        auto *accessible = QAccessible::queryAccessibleInterface(presentation);
        QVERIFY(accessible);
        QVERIFY(!accessible->text(QAccessible::Name).isEmpty());
        presentation->setCurrentIndex(2);
        QCOMPARE(sub->fillStyle()->presentation(),
                 OpenSWMM::Render::ResultScalarStyle::Presentation::Labels);
        QVERIFY(panel.findChild<QPushButton *>("additionalResultGisExport")->isEnabled());
        sub->setVariableKey(layer.resultVariables()[1].key());
        panel.focusResult(sub->id());
        auto *variable = panel.findChild<QComboBox *>("additionalResultVariable");
        variable->setCurrentIndex(variable->findData(layer.resultVariables()[1].key()));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!panel.findChild<QPushButton *>("additionalResultGisExport")->isEnabled());
    }
    /**
     *
     * @brief Require two added scalar results to have independent opacity/missing-color edits and
     * permit selected removal.
     *
     */
    void additionalResultsIndependentAndRemovable()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<PanelVariableSource>());
        Swmm2DResultsStylePanel panel(&layer);
        auto *catalog = panel.findChild<QComboBox *>("additionalResultCatalog");
        QVERIFY(catalog);
        QCOMPARE(catalog->count(), 2);
        auto *add = panel.findChild<QPushButton *>("additionalResultAdd");
        QVERIFY(add);
        add->click();
        add->click();
        auto *list = panel.findChild<QListWidget *>("additionalResultList");
        QVERIFY(list);
        QCOMPARE(list->count(), 2);
        auto *a = qobject_cast<OpenSWMM::Render::ResultScalarSublayer *>(
            OpenSWMM::Render::ISublayerHost::findSublayer(
                layer, list->item(0)->data(Qt::UserRole).toString()));
        auto *b = qobject_cast<OpenSWMM::Render::ResultScalarSublayer *>(
            OpenSWMM::Render::ISublayerHost::findSublayer(
                layer, list->item(1)->data(Qt::UserRole).toString()));
        QVERIFY(a);
        QVERIFY(b);
        QVERIFY(a != b);
        auto *opacity = panel.findChild<QDoubleSpinBox *>("additionalResultOpacity");
        QVERIFY(opacity);
        opacity->setValue(35);
        QCOMPARE(b->opacity(), .35);
        QCOMPARE(a->opacity(), 1.);
        auto *missing = panel.findChild<ColorButton *>("additionalResultMissing");
        QVERIFY(missing);
        missing->setColor(Qt::red);
        QMetaObject::invokeMethod(missing, "colorChanged", Q_ARG(QColor, QColor(Qt::red)));
        QCOMPARE(b->fillStyle()->missingColor(), QColor(Qt::red));
        QVERIFY(a->fillStyle()->missingColor() != QColor(Qt::red));
        panel.findChild<QPushButton *>("additionalResultRemove")->click();
        QCOMPARE(list->count(), 1);
    }
    /**
     *
     * @brief Require a rejected classification-break draft and its editor instance to survive
     * cell selection changes without committing.
     *
     */
    void additionalResultsRetainDraftDuringSelectionChanges()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<PanelVariableSource>());
        auto *sub = layer.addResultSublayer(layer.resultVariables()[0].key());
        auto scheme = sub->fillStyle()->scheme();
        scheme.setUseCustomRange(true);
        scheme.setMode(OpenSWMM::Render::ClassificationScheme::ClassMode::Classified);
        scheme.setRangeMin(0);
        scheme.setRangeMax(10);
        scheme.setMethod(OpenSWMM::Render::BinMethod::Manual);
        scheme.setManualBreaks({3, 7});
        sub->fillStyle()->setScheme(scheme);
        Swmm2DResultsStylePanel panel(&layer);
        QVERIFY(panel.focusResult(sub->id()));
        auto *editor = panel.findChild<ClassificationEditor *>("additionalResultClassification");
        QVERIFY(editor);
        auto *table = editor->findChild<QTableView *>();
        QVERIFY(table);
        QVERIFY(table->model()->setData(table->model()->index(0, 1), QStringLiteral("oops")));
        QVERIFY(!editor->hasValidDraft());
        layer.highlightCells({0});
        QCOMPARE(panel.findChild<ClassificationEditor *>("additionalResultClassification"), editor);
        QCOMPARE(table->model()->index(0, 1).data().toString(), QStringLiteral("oops"));
        QVERIFY(!editor->hasValidDraft());
    }
    /**
     *
     * @brief Require an unavailable variable key to remain selected while classification and
     * export are disabled.
     *
     */
    void additionalResultsRetainUnavailableVariable()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<PanelVariableSource>());
        const QString key = "groundwater:missing:species:Unavailable";
        auto *sub = layer.addResultSublayer(key);
        QVERIFY(sub);
        Swmm2DResultsStylePanel panel(&layer);
        auto *selector = panel.findChild<QComboBox *>("additionalResultVariable");
        QVERIFY(selector);
        QCOMPARE(selector->currentData().toString(), key);
        QCOMPARE(sub->variableKey(), key);
        auto *editor = panel.findChild<ClassificationEditor *>("additionalResultClassification");
        QVERIFY(editor);
        QVERIFY(!editor->isEnabled());
        auto *exportButton = panel.findChild<QPushButton *>("additionalResultExport");
        QVERIFY(exportButton);
        QVERIFY(!exportButton->isEnabled());
    }
    /**
     *
     * @brief Require either display triangle of a selected native quad to produce one native-cell
     * inspection row with Valid status.
     *
     */
    void additionalResultsInspectionMapsQuadTrianglesToCell()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<PanelQuadVariableSource>());
        QVERIFY(layer.triCellMap().size() >= 2);
        layer.highlightCells({0, 1});
        layer.addResultSublayer(layer.resultVariables()[0].key());
        Swmm2DResultsStylePanel panel(&layer);
        auto *table = panel.findChild<QTableWidget *>("additionalResultInspection");
        QVERIFY(table);
        QCOMPARE(table->rowCount(), 1);
        QCOMPARE(table->item(0, 0)->text(), QStringLiteral("1 (first of 1 selected)"));
        layer.highlightCells({1});
        QCOMPARE(table->rowCount(), 1);
        QCOMPARE(table->item(0, 1)->text(), QStringLiteral("Valid"));
    }
    /**
     *
     * @brief Require scalar inspection to distinguish valid zero from waterless/unknown-unit data
     * and gate export.
     * Shows the results panel and writes additional_results_panel.png in outputDir();
     * requires GUI capture and writable output. Deferred widget deletion is processed.
     *
     */
    void additionalResultsInspectionAndUnitGate()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<PanelVariableSource>());
        layer.highlightCells({0});
        auto vars = layer.resultVariables();
        layer.addResultSublayer(vars[0].key());
        Swmm2DResultsStylePanel panel(&layer);
        auto *table = panel.findChild<QTableWidget *>("additionalResultInspection");
        QVERIFY(table);
        QCOMPARE(table->item(0, 1)->text(), QStringLiteral("Valid"));
        QCOMPARE(table->item(0, 2)->text(), QStringLiteral("0"));
        QVERIFY(panel.findChild<QPushButton *>("additionalResultExport")->isEnabled());
        QVERIFY(panel.focusResult(layer.sublayers().last()->id()));
        panel.resize(940, 860);
        panel.show();
        QTest::qWait(30);
        QVERIFY(QDir().mkpath(outputDir()));
        QVERIFY(panel.grab().save(QDir(outputDir()).filePath("additional_results_panel.png")));
        auto *selector = panel.findChild<QComboBox *>("additionalResultVariable");
        QVERIFY(selector);
        selector->setCurrentIndex(selector->findData(vars[1].key()));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        table = panel.findChild<QTableWidget *>("additionalResultInspection");
        QVERIFY(table);
        QCOMPARE(table->item(0, 1)->text(), QStringLiteral("Waterless"));
        QVERIFY(!panel.findChild<QPushButton *>("additionalResultExport")->isEnabled());
    }
    /**
     *
     * @brief Require deleting the associated layer to disable its panel and remove classification
     * editors after deferred cleanup.
     *
     */
    void resultsPanelInvalidatesWithLayer()
    {
        auto layer = std::make_unique<SWMM2DResultsLayer>();
        Swmm2DResultsStylePanel panel(layer.get());
        layer.reset();
        QVERIFY(!panel.isEnabled());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(panel.findChildren<ClassificationEditor *>().isEmpty());
    }
    /**
     *
     * @brief Require any discovered elevation combo row to be disabled.
     * The case permits no such row and does not assert that an elevation choice exists.
     *
     */
    void legacyElevationIsNotAnEnabledDeadControl()
    {
        SWMM2DResultsLayer layer;
        Swmm2DResultsStylePanel panel(&layer);
        for (auto *combo : panel.findChildren<QComboBox *>()) {
            const int row = combo->findData(QStringLiteral("elevation"));
            if (row >= 0)
                QVERIFY(
                    !(combo->model()->flags(combo->model()->index(row, 0)) & Qt::ItemIsEnabled));
        }
    }
    /**
     *
     * @brief Register text rows for nonnumeric, NaN, infinity, descending, duplicate and
     * outside-range manual breaks.
     *
     */
    void classificationBreakDraftValidation_data()
    {
        QTest::addColumn<QString>("text");
        QTest::newRow("not-a-number") << QStringLiteral("oops");
        QTest::newRow("nonfinite") << QStringLiteral("nan");
        QTest::newRow("infinity") << QStringLiteral("inf");
        QTest::newRow("descending") << QStringLiteral("9");
        QTest::newRow("duplicate") << QStringLiteral("7");
        QTest::newRow("outside-range") << QStringLiteral("-1");
    }
    /**
     *
     * @brief Require each invalid text draft to remain visible without binding writes, then
     * commit a corrected break of four.
     * Consumes the QString text column and checks a nonempty validation status.
     *
     */
    void classificationBreakDraftValidation()
    {
        QFETCH(QString, text);
        TestClassificationBinding binding;
        binding.value.setMethod(OpenSWMM::Render::BinMethod::Manual);
        binding.value.setManualBreaks({3.0, 7.0});
        ClassificationEditor editor(&binding);
        const auto original = binding.value.toJson();
        auto *table = editor.findChild<QTableView *>();
        QVERIFY(table);
        QVERIFY(table->model()->setData(table->model()->index(0, 1), text));
        QCOMPARE(binding.value.toJson(), original);
        QCOMPARE(binding.writes, 0);
        auto *status = editor.findChild<QLabel *>("classificationStatus");
        QVERIFY(status);
        QVERIFY(!status->text().isEmpty());
        // Keep the rejected draft available, then commit a corrected value.
        QCOMPARE(table->model()->index(0, 1).data().toString(), text);
        QVERIFY(table->model()->setData(table->model()->index(0, 1), QStringLiteral("4")));
        QCOMPARE(binding.value.manualBreaks(), QVector<double>({4.0, 7.0}));
    }
    /**
     *
     * @brief Require editing one manual break to preserve the other unedited high-precision
     * double.
     *
     */
    void classificationEditingOneBreakPreservesOtherExactValues()
    {
        TestClassificationBinding binding;
        binding.value.setMethod(OpenSWMM::Render::BinMethod::Manual);
        binding.value.setManualBreaks({1.234567890123, 7.987654321098});
        binding.value.setLabelPrecision(2);
        ClassificationEditor editor(&binding);
        auto *table = editor.findChild<QTableView *>();
        QVERIFY(table);
        QVERIFY(table->model()->setData(table->model()->index(0, 1), QStringLiteral("2.5")));
        QCOMPARE(binding.value.manualBreaks(), QVector<double>({2.5, 7.987654321098}));
    }
    /**
     *
     * @brief Require reversed custom bounds to stay uncommitted until the other endpoint makes
     * the draft valid.
     *
     */
    void classificationInvalidRangeRemainsDraft()
    {
        TestClassificationBinding binding;
        binding.value.setUseCustomRange(true);
        binding.value.setRangeMin(1.0);
        binding.value.setRangeMax(8.0);
        ClassificationEditor editor(&binding);
        const auto spins = editor.findChildren<QDoubleSpinBox *>();
        QCOMPARE(spins.size(), 2);
        spins[0]->setValue(9.0);
        QCOMPARE(binding.value.rangeMin(), 1.0);
        QCOMPARE(binding.value.rangeMax(), 8.0);
        QCOMPARE(binding.writes, 0);
        spins[1]->setValue(10.0);
        QCOMPARE(binding.value.rangeMin(), 9.0);
        QCOMPARE(binding.value.rangeMax(), 10.0);
    }
    /**
     *
     * @brief Require editing the maximum bound to preserve the untouched minimum double exactly.
     *
     */
    void classificationRangeEditPreservesUntouchedPrecision()
    {
        TestClassificationBinding binding;
        binding.value.setUseCustomRange(true);
        binding.value.setRangeMin(0.1234567890123);
        binding.value.setRangeMax(8.9876543210987);
        ClassificationEditor editor(&binding);
        const auto spins = editor.findChildren<QDoubleSpinBox *>();
        QCOMPARE(spins.size(), 2);
        spins[1]->setValue(9.5);
        QCOMPARE(binding.value.rangeMin(), 0.1234567890123);
        QCOMPARE(binding.value.rangeMax(), 9.5);
    }
    /**
     *
     * @brief Register QString name rows for mixed case, hyphenated, legacy and whitespace-trimmed
     * builtin ramp aliases.
     *
     */
    void classificationRampAliasesAreRecognized_data()
    {
        QTest::addColumn<QString>("name");
        QTest::newRow("internal-lowercase") << QStringLiteral("viridis");
        QTest::newRow("display-mixed-case") << QStringLiteral("vIrIdIs");
        QTest::newRow("internal-hyphenated") << QStringLiteral("water-depth");
        QTest::newRow("legacy-internal") << QStringLiteral("legacy-swmm-pollutant");
        QTest::newRow("trimmed") << QStringLiteral("  Viridis  ");
    }
    /**
     *
     * @brief Require accepted ramp aliases to show no status and an unknown name to show an
     * enabled theme-text unavailable status.
     * Consumes the QString name column; refresh reads binding state without a write.
     *
     */
    void classificationRampAliasesAreRecognized()
    {
        QFETCH(QString, name);
        TestClassificationBinding binding;
        binding.value.setRampName(name);
        ClassificationEditor editor(&binding);
        auto *status = editor.findChild<QLabel *>("classificationStatus");
        QVERIFY(status);
        QVERIFY2(status->text().isEmpty(), qPrintable(status->text()));
        binding.value.setRampName(QStringLiteral("missing-test-ramp"));
        editor.refresh();
        QVERIFY(status->text().contains(QStringLiteral("unavailable")));
        QVERIFY(status->isEnabled());
        QVERIFY(status->styleSheet().contains(QStringLiteral("palette(text)")));
    }
    /**
     *
     * @brief Require Quantile missing-sample status to clear after supplying finite samples and
     * refreshing.
     *
     */
    void classificationMissingSamplesStatusRefreshes()
    {
        TestClassificationBinding binding;
        binding.value.setMethod(OpenSWMM::Render::BinMethod::Quantile);
        ClassificationEditor editor(&binding);
        auto *status = editor.findChild<QLabel *>("classificationStatus");
        QVERIFY(status);
        QVERIFY(!status->text().isEmpty());
        binding.samples = {0.0, 0.1, 0.2, 0.3, 1.0, 5.0, 8.0, 10.0};
        editor.refresh();
        QVERIFY(status->text().isEmpty());
    }
    /**
     *
     * @brief Require light/dark/system theme changes to preserve scheme JSON and produce no
     * binding writes.
     * Shows a classification editor; leaves global ThemeManager in Light mode rather
     * than restoring the previous mode.
     *
     */
    void classificationThemePreservesScientificDefinition()
    {
        TestClassificationBinding binding;
        binding.value.setRampName("Viridis");
        binding.value.setColorOverride(0, QColor("#d24a18"));
        ClassificationEditor editor(&binding);
        editor.show();
        const auto before = binding.value.toJson();
        auto *theme = ThemeManager::instance();
        theme->setMode(ThemeManager::Mode::Light);
        QCoreApplication::processEvents();
        theme->setMode(ThemeManager::Mode::Dark);
        QCoreApplication::processEvents();
        QCOMPARE(binding.value.toJson(), before);
        QCOMPARE(binding.writes, 0);
        theme->setMode(ThemeManager::Mode::System);
        QCoreApplication::processEvents();
        QCOMPARE(binding.value.toJson(), before);
        theme->setMode(ThemeManager::Mode::Light);
    }
    /**
     *
     * @brief Create outputDir and configure process-wide isolated INI settings/non-native
     * dialogs.
     * Sets organization/application identity and the user settings path; these global
     * values and QApplication dialog attribute are not restored by the fixture.
     *
     */
    void initTestCase()
    {
        QVERIFY(QDir().mkpath(outputDir()));
        QCoreApplication::setOrganizationName(QStringLiteral("openswmm-test"));
        QCoreApplication::setApplicationName(QStringLiteral("stylecontrols"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, outputDir());
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    }

    /**
     *
     * @brief Require color-button accessible description to track RGB/alpha, transparency and
     * invalid color, with idempotent change signals.
     *
     */
    void colorDescription_tracksValue()
    {
        ColorButton button;
        auto *iface = QAccessible::queryAccessibleInterface(&button);
        QVERIFY(iface);
        QCOMPARE(iface->role(), QAccessible::Button);
        QVERIFY(iface->text(QAccessible::Description).contains("#000000"));
        QVERIFY(iface->text(QAccessible::Description).contains("255"));
        QSignalSpy changed(&button, &ColorButton::colorChanged);
        button.setColor(QColor(18, 52, 86, 128));
        QVERIFY(iface->text(QAccessible::Description).contains("#123456"));
        QVERIFY(iface->text(QAccessible::Description).contains("128"));
        QCOMPARE(changed.count(), 1);
        button.setColor(button.color());
        QCOMPARE(changed.count(), 1);
        button.setColor(QColor(18, 52, 86, 0));
        QVERIFY(iface->text(QAccessible::Description).contains("0%"));
        button.setColor(QColor());
        QVERIFY(iface->text(QAccessible::Description).contains("No colour"));

        ColorButton initial(QColor(171, 205, 239, 64));
        auto *initialIface = QAccessible::queryAccessibleInterface(&initial);
        QVERIFY(initialIface->text(QAccessible::Description).contains("#abcdef"));
        QVERIFY(initialIface->text(QAccessible::Description).contains("64"));
    }

    /**
     *
     * @brief Require a form buddy to supply the color-button accessible name, press action and
     * tab focus.
     *
     */
    void colorButton_preservesBuddyNameAndAction()
    {
        QWidget host;
        QFormLayout form(&host);
        auto *button = new ColorButton(&host);
        form.addRow(tr("&Water colour:"), button);
        auto *iface = QAccessible::queryAccessibleInterface(button);
        QVERIFY(iface);
        QVERIFY(iface->text(QAccessible::Name).contains("Water colour"));
        QVERIFY(iface->actionInterface());
        QVERIFY(iface->actionInterface()->actionNames().contains(
            QAccessibleActionInterface::pressAction()));
        QVERIFY(button->focusPolicy() & Qt::TabFocus);
    }

    /**
     *
     * @brief Register bool accept rows for keyboard color-picker acceptance and cancellation.
     *
     */
    void keyboardPicker_data()
    {
        QTest::addColumn<bool>("accept");
        QTest::newRow("accept") << true;
        QTest::newRow("cancel") << false;
    }

    /**
     *
     * @brief Require Space to open the color dialog and preserve/copy color with zero/one change
     * signals for cancel/accept.
     * Consumes bool accept; a 10-ms timer dismisses the active QColorDialog. Requires
     * a GUI event loop and fixture setup selecting non-native dialogs; checks updated
     * accessible color description after dismissal.
     *
     */
    void keyboardPicker()
    {
        QFETCH(bool, accept);
        ColorButton button(QColor(30, 60, 90));
        button.show();
        button.setFocus();
        QSignalSpy changed(&button, &ColorButton::colorChanged);
        const QColor selected(15, 45, 75, 128);
        bool opened = false;
        QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, &button, [&] {
            auto *picker = qobject_cast<QColorDialog *>(QApplication::activeModalWidget());
            if (!picker)
                return;
            opened = true;
            picker->setCurrentColor(selected);
            if (accept)
                picker->accept();
            else
                picker->reject();
        });
        dismiss.start(10);
        QTest::keyClick(&button, Qt::Key_Space);
        dismiss.stop();
        QVERIFY(opened);
        QCOMPARE(button.color(), accept ? selected : QColor(30, 60, 90));
        QCOMPARE(changed.count(), accept ? 1 : 0);
        auto *iface = QAccessible::queryAccessibleInterface(&button);
        QVERIFY(iface->text(QAccessible::Description).contains(button.color().name()));
    }

    /**
     *
     * @brief Require two ramp-stop color buttons to have distinct accessible names containing
     * their one-based row and colour.
     *
     */
    void rampStops_haveDistinctNames()
    {
        RasterColorRamp ramp;
        ramp.stops = {{0.0, Qt::blue}, {1.0, Qt::red}};
        ColorRampEditorDialog dialog(ramp);
        auto *table = dialog.findChild<QTableWidget *>();
        QVERIFY(table);
        QCOMPARE(table->rowCount(), 2);
        QSet<QString> names;
        for (int i = 0; i < table->rowCount(); ++i) {
            auto *button = qobject_cast<ColorButton *>(table->cellWidget(i, 1));
            QVERIFY(button);
            auto *iface = QAccessible::queryAccessibleInterface(button);
            const QString name = iface->text(QAccessible::Name);
            QVERIFY2(name.contains(QString::number(i + 1)), qPrintable(name));
            QVERIFY(name.contains("colour", Qt::CaseInsensitive));
            names.insert(name);
        }
        QCOMPARE(names.size(), 2);
    }

    /**
     *
     * @brief Require three distinct nonempty label-color accessible names including halo and
     * background.
     *
     */
    void labelColors_haveNames()
    {
        LabelConfigEditor editor;
        const auto buttons = editor.findChildren<ColorButton *>();
        QCOMPARE(buttons.size(), 3);
        QSet<QString> names;
        for (auto *button : buttons) {
            auto *iface = QAccessible::queryAccessibleInterface(button);
            const QString name = iface->text(QAccessible::Name);
            QVERIFY2(!name.isEmpty(), "Every label colour control needs a name");
            names.insert(name);
        }
        QCOMPARE(names.size(), 3);
        QVERIFY(names.contains(QStringLiteral("Halo colour")));
        QVERIFY(names.contains(QStringLiteral("Background colour")));
    }

    /**
     *
     * @brief Require all discovered mesh-style color buttons to be named, with more than one
     * boundary-colour control.
     *
     */
    void meshBoundaryColors_haveNames()
    {
        SWMM2DMeshLayer layer(mesh::MeshResult{});
        Swmm2DMeshStylePanel panel(&layer);
        const auto buttons = panel.findChildren<ColorButton *>();
        QVERIFY(!buttons.isEmpty());
        int boundaryNames = 0;
        for (auto *button : buttons) {
            auto *iface = QAccessible::queryAccessibleInterface(button);
            const QString name = iface->text(QAccessible::Name);
            QVERIFY2(!name.isEmpty(), "Every mesh style colour control needs a name");
            if (name.contains("boundary colour"))
                ++boundaryNames;
        }
        QVERIFY(boundaryNames > 1);
    }

    /**
     *
     * @brief Require a shown sun dial to repaint with theme changes and use palette Base at a
     * sampled interior pixel.
     * Writes sun_light.png, sun_dark.png and sun_light_again.png under outputDir();
     * requires GUI capture and writable output. Leaves the global theme in Light mode.
     *
     */
    void sunDial_tracksLiveTheme()
    {
        auto *theme = ThemeManager::instance();
        theme->setMode(ThemeManager::Mode::Light);
        ObservedSunThumb thumb;
        thumb.resize(110, 110);
        thumb.show();
        QTRY_VERIFY(thumb.paints > 0);
        auto verifyImage = [&](const QString &name) {
            const QImage image = thumb.grab().toImage();
            QVERIFY(image.save(outputDir() + "/" + name + ".png"));
            const qreal dpr = image.devicePixelRatio();
            // A point inside the dial, away from the sun, arrow and text.
            QCOMPARE(image.pixelColor(qRound(75 * dpr), qRound(55 * dpr)),
                     thumb.palette().color(QPalette::Base));
        };
        verifyImage("sun_light");
        const int beforeDark = thumb.paints;
        theme->setMode(ThemeManager::Mode::Dark);
        QTRY_VERIFY(thumb.paints > beforeDark);
        verifyImage("sun_dark");
        const int beforeLight = thumb.paints;
        theme->setMode(ThemeManager::Mode::Light);
        QTRY_VERIFY(thumb.paints > beforeLight);
        verifyImage("sun_light_again");
    }
};

QTEST_MAIN(TestStyleControls)
#include "test_stylecontrols.moc"
