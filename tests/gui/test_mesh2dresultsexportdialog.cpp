/*!
 * \file   test_mesh2dresultsexportdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Gates the option collector for "Export 2D Results…": the Shapefile field
 * cap, the time-step selection helpers, the options round trip, and the
 * velocity gating. Widgets are reached by objectName only — no accessors are
 * added to the dialog for the test's benefit.
 */
#include "io/mesh2dresultsexport.h"
#include "ui/dialogs/mesh2dresultsexportdialog.h"

#include "dialog_a11y_checks.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QtTest>

using namespace openswmmvis::io;
using openswmmvis::ui::Mesh2DExportDialogInputs;
using openswmmvis::ui::Mesh2DResultsExportDialog;

namespace {

//! A run of \p n frames, five minutes apart, with velocity available.
Mesh2DExportDialogInputs makeInputs(int n, int current = 0, bool velocity = true)
{
    Mesh2DExportDialogInputs in;
    const QDateTime base(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC);
    for (int i = 0; i < n; ++i) in.times << base.addSecs(qint64(i) * 300);
    in.currentIndex = current;
    in.suggestedCellSize = 2.5;
    in.extentWidth = 500.0;
    in.extentHeight = 400.0;
    in.hasVelocity = velocity;
    in.lengthUnit = QStringLiteral("m");
    in.defaultDir = QDir::tempPath();
    in.defaultBaseName = QStringLiteral("run1");
    return in;
}

template <typename T>
T *child(QDialog &dlg, const char *name)
{
    T *w = dlg.findChild<T *>(QLatin1String(name));
    return w;
}

std::vector<int> selectedRows(QListWidget *list)
{
    std::vector<int> out;
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->isSelected()) out.push_back(i);
    return out;
}

} // namespace

class TestMesh2DResultsExportDialog : public QObject
{
    Q_OBJECT

private slots:
    void a11yInvariantsHold();
    void shapefileFieldCapBlocksExport();
    void strideSelectsEveryNth();
    void currentSelectsCurrentIndex();
    void optionsRoundTrip();
    void velocityDisabledWithoutVelocity();
    void oversizedRasterIsRefusedWithPixelCount();
};

//! Mnemonics unique, icon-only buttons named, persistence contract honoured.
void TestMesh2DResultsExportDialog::a11yInvariantsHold()
{
    Mesh2DResultsExportDialog dlg(makeInputs(12));
    swmmvis_test::assertDialogA11y(&dlg);
}

//! A Shapefile cannot carry 300 per-step columns; the dialog must say so and
//! refuse, and must accept the same selection once the format can hold it.
void TestMesh2DResultsExportDialog::shapefileFieldCapBlocksExport()
{
    Mesh2DResultsExportDialog dlg(makeInputs(300));
    auto *format = child<QComboBox>(dlg, "formatCombo");
    auto *list = child<QListWidget>(dlg, "timeStepList");
    auto *buttons = child<QDialogButtonBox>(dlg, "buttonBox");
    auto *validation = child<QLabel>(dlg, "validationLabel");
    QVERIFY(format && list && buttons && validation);

    const int shpIdx = format->findData(int(Mesh2DExportFormat::Shapefile));
    const int gpkgIdx = format->findData(int(Mesh2DExportFormat::GeoPackage));
    QVERIFY(shpIdx >= 0);
    QVERIFY(gpkgIdx >= 0);

    format->setCurrentIndex(shpIdx);
    list->selectAll();
    QCOMPARE(int(selectedRows(list).size()), 300);

    QVERIFY2(!buttons->button(QDialogButtonBox::Ok)->isEnabled(),
             "300 steps + max + cell_id exceeds the 255-field DBF cap");
    QVERIFY(validation->text().contains(QStringLiteral("255")));

    format->setCurrentIndex(gpkgIdx);
    QVERIFY2(buttons->button(QDialogButtonBox::Ok)->isEnabled(),
             "GeoPackage has no field cap, so the same selection is exportable");
    QVERIFY(validation->text().isEmpty());
}

//! "Every Nth" selects frames 0, N, 2N…
void TestMesh2DResultsExportDialog::strideSelectsEveryNth()
{
    Mesh2DResultsExportDialog dlg(makeInputs(72));
    auto *list = child<QListWidget>(dlg, "timeStepList");
    auto *stride = child<QSpinBox>(dlg, "strideSpin");
    auto *apply = child<QPushButton>(dlg, "applyStrideButton");
    QVERIFY(list && stride && apply);

    stride->setValue(10);
    apply->click();

    const std::vector<int> got = selectedRows(list);
    const std::vector<int> want{0, 10, 20, 30, 40, 50, 60, 70};
    QCOMPARE(got, want);
}

//! "Current" reduces the selection to the frame the animation is showing.
void TestMesh2DResultsExportDialog::currentSelectsCurrentIndex()
{
    Mesh2DResultsExportDialog dlg(makeInputs(20, /*current=*/7));
    auto *list = child<QListWidget>(dlg, "timeStepList");
    auto *current = child<QPushButton>(dlg, "selectCurrentButton");
    QVERIFY(list && current);

    list->selectAll();
    current->click();

    const std::vector<int> got = selectedRows(list);
    QCOMPARE(got, (std::vector<int>{7}));

    // The constructor also starts there, so the common case is one click.
    Mesh2DResultsExportDialog fresh(makeInputs(20, /*current=*/3));
    QCOMPARE(selectedRows(child<QListWidget>(fresh, "timeStepList")),
             (std::vector<int>{3}));
}

//! Every widget reaches the options struct, and the path is reduced to a stem.
void TestMesh2DResultsExportDialog::optionsRoundTrip()
{
    Mesh2DResultsExportDialog dlg(makeInputs(30, /*current=*/0));

    child<QComboBox>(dlg, "formatCombo")->setCurrentIndex(
        child<QComboBox>(dlg, "formatCombo")->findData(int(Mesh2DExportFormat::GeoTiff)));
    child<QLineEdit>(dlg, "outputPathEdit")->setText(
        QDir(QDir::tempPath()).filePath(QStringLiteral("myrun.tif")));
    child<QCheckBox>(dlg, "depthCheck")->setChecked(true);
    child<QCheckBox>(dlg, "headCheck")->setChecked(false);
    child<QCheckBox>(dlg, "vmagCheck")->setChecked(true);
    child<QCheckBox>(dlg, "vxCheck")->setChecked(false);
    child<QCheckBox>(dlg, "vyCheck")->setChecked(false);
    child<QCheckBox>(dlg, "includeMaxCheck")->setChecked(true);
    child<QCheckBox>(dlg, "maskDryCheck")->setChecked(false);
    child<QDoubleSpinBox>(dlg, "cellSizeSpin")->setValue(1.25);
    child<QComboBox>(dlg, "interpCombo")->setCurrentIndex(
        child<QComboBox>(dlg, "interpCombo")->findData(int(Mesh2DInterp::Idw)));
    child<QSpinBox>(dlg, "idwNeighboursSpin")->setValue(12);

    auto *list = child<QListWidget>(dlg, "timeStepList");
    list->clearSelection();
    for (int row : {2, 5, 9}) list->item(row)->setSelected(true);

    const Mesh2DExportOptions o = dlg.options();
    QCOMPARE(o.format, Mesh2DExportFormat::GeoTiff);
    QCOMPARE(o.variables, unsigned(Mesh2DDepth | Mesh2DVmag));
    QCOMPARE(o.timeSteps, (std::vector<int>{2, 5, 9}));
    QVERIFY(o.includeMax);
    QVERIFY(!o.maskDry);
    QCOMPARE(o.interp, Mesh2DInterp::Idw);
    QCOMPARE(o.idwNeighbours, 12);
    QVERIFY(std::abs(o.cellSize - 1.25) < 1e-12);
    QCOMPARE(o.basePath, QDir(QDir::tempPath()).filePath(QStringLiteral("myrun")));
    QVERIFY2(!o.basePath.endsWith(QStringLiteral(".tif")),
             "the writers append their own suffix and extension");

    // The neighbour count only applies to inverse distance weighting.
    QVERIFY(child<QSpinBox>(dlg, "idwNeighboursSpin")->isEnabled());
    child<QComboBox>(dlg, "interpCombo")->setCurrentIndex(
        child<QComboBox>(dlg, "interpCombo")->findData(int(Mesh2DInterp::GreenGauss)));
    QVERIFY(!child<QSpinBox>(dlg, "idwNeighboursSpin")->isEnabled());
}

//! A run written without the VELOCITY output group offers no velocity.
void TestMesh2DResultsExportDialog::velocityDisabledWithoutVelocity()
{
    Mesh2DResultsExportDialog dlg(makeInputs(10, 0, /*velocity=*/false));
    for (const char *name : {"vxCheck", "vyCheck", "vmagCheck"}) {
        auto *c = child<QCheckBox>(dlg, name);
        QVERIFY2(c, name);
        QVERIFY2(!c->isEnabled(), name);
        QVERIFY2(!c->isChecked(), name);
        QVERIFY2(!c->toolTip().isEmpty(), "the reason must be discoverable");
    }
    QCOMPARE(dlg.options().variables & unsigned(Mesh2DVx | Mesh2DVy | Mesh2DVmag), 0u);

    // Depth and water surface are unaffected.
    QVERIFY(child<QCheckBox>(dlg, "depthCheck")->isEnabled());
    QVERIFY(child<QCheckBox>(dlg, "headCheck")->isEnabled());
}

//! The grid label reports the real raster dimensions, and a cell size that
//! would build an unusable raster blocks the export instead of running out of
//! memory mid-write.
void TestMesh2DResultsExportDialog::oversizedRasterIsRefusedWithPixelCount()
{
    Mesh2DResultsExportDialog dlg(makeInputs(10));            // 500 x 400 extent
    auto *format = child<QComboBox>(dlg, "formatCombo");
    auto *cellSize = child<QDoubleSpinBox>(dlg, "cellSizeSpin");
    auto *gridInfo = child<QLabel>(dlg, "gridInfoLabel");
    auto *buttons = child<QDialogButtonBox>(dlg, "buttonBox");
    QVERIFY(format && cellSize && gridInfo && buttons);
    format->setCurrentIndex(format->findData(int(Mesh2DExportFormat::GeoTiff)));

    cellSize->setValue(2.0);                                   // 250 x 200 px
    QVERIFY2(gridInfo->text().contains(QStringLiteral("250")), qPrintable(gridInfo->text()));
    QVERIFY2(gridInfo->text().contains(QStringLiteral("200")), qPrintable(gridInfo->text()));
    QVERIFY(buttons->button(QDialogButtonBox::Ok)->isEnabled());

    cellSize->setValue(0.001);                                 // 500k x 400k px
    QVERIFY2(!buttons->button(QDialogButtonBox::Ok)->isEnabled(),
             "2e11 pixels must be refused before the export starts");
    QVERIFY(child<QLabel>(dlg, "validationLabel")->text().contains(QStringLiteral("500000")));

    // A vector format has no grid, so the same cell size is irrelevant to it.
    format->setCurrentIndex(format->findData(int(Mesh2DExportFormat::GeoPackage)));
    QVERIFY(buttons->button(QDialogButtonBox::Ok)->isEnabled());
}

QTEST_MAIN(TestMesh2DResultsExportDialog)
#include "test_mesh2dresultsexportdialog.moc"
