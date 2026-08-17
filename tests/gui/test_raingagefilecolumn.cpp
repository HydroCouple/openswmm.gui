/*!
 * \file   test_raingagefilecolumn.cpp
 * \brief  Rain-gage file/column/format coupling in the Attribute Table
 *         (review follow-ups R3 and A-2).
 *
 * Two behaviours, both about the fact that a rain file's COLUMN belongs to the
 * FILE it was picked from:
 *
 *   R3 — editing the path reconciles the column in the same breath, and the
 *        whole thing undoes as ONE step. A second engine write made from inside
 *        commitValueDirect would sit outside the edit command's captured state,
 *        so undo would restore the path and keep the column: the tests here
 *        assert the undone state, not just the redone one.
 *
 *   A-2 — the file FORMAT is explicitly settable. Both the path and the column
 *         setters preserve USER_CSV engine-side, so without this a gage that
 *         ever had a column could never return to a standard `FILE "path"
 *         Station Units` rain file and its Station ID would stay unusable.
 *
 * Fixtures: `extcol_switch_multi.csv` (real headers time,rain_a,rain_b) and
 * `extcol_dialog_sample.tsf` (PCSWMM IDs: row, RG1/RG2) from the multi-column
 * series work, plus `typed_selection_fixture.inp` for the model itself.
 */

#include "layers/swmmmodellayer.h"
#include "ui/panels/swmmattributetablemodel.h"
#include "ui/properties/swmmraingagepropertyadapter.h"

#include <openswmm/engine/openswmm_gages.h>
#include <openswmm/engine/openswmm_model.h>

#include <QDir>
#include <QObject>
#include <QTest>
#include <QUndoStack>

#include <memory>

using openswmmvis::ColumnSpec;

namespace {

QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

QString fixture(const QString &name)
{
    return QDir(dataDir()).filePath(name);
}

std::unique_ptr<SWMMModelLayer> openLayer()
{
    auto layer = std::make_unique<SWMMModelLayer>(
        fixture(QStringLiteral("typed_selection_fixture.inp")), nullptr);
    QList<QString> warnings, errors;
    if (!layer->loadModel(warnings, errors)) return nullptr;
    return layer;
}

//! Column index for a spec's setter tag, or -1.
int colForSetter(const QList<ColumnSpec> &specs, const QString &tag)
{
    for (int i = 0; i < specs.size(); ++i)
        if (specs[i].setter == tag) return i;
    return -1;
}

} // namespace

class TestRainGageFileColumn : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void pathEditBindsFirstColumnAndUndoesAsOneStep();
    void pathEditKeepsAColumnTheNewFileStillHas();
    void pathEditClearsAColumnTheNewFileCannotResolve();
    void formatColumnIsPresentAndLeavesUserCsv();
    void adapterReconcilesTheColumnToo();

private:
    std::unique_ptr<SWMMModelLayer>   m_layer;
    std::unique_ptr<QUndoStack>       m_undo;
    std::unique_ptr<SWMMAttributeTableModel> m_model;
    int m_row = -1, m_pathCol = -1, m_colCol = -1, m_fmtCol = -1, m_stationCol = -1;

    //! The gage's engine index (fixture has exactly one gage).
    [[nodiscard]] int gageIdx() const
    {
        return swmm_gage_index(m_layer->engine(), "S1");
    }
};

void TestRainGageFileColumn::init()
{
    m_layer = openLayer();
    QVERIFY(m_layer);

    // The fixture's gage reads a TIMESERIES; the file rows only apply to a
    // FILE-source gage.
    QCOMPARE(swmm_gage_set_data_source(m_layer->engine(), gageIdx(), 1), 0);

    m_undo  = std::make_unique<QUndoStack>();
    m_model = std::make_unique<SWMMAttributeTableModel>();
    m_model->setSource(m_layer.get(), SWMMModelLayer::CatRainGages);
    m_model->setUndoStack(m_undo.get());

    const auto specs = m_model->columnSpecs();
    m_pathCol    = colForSetter(specs, QStringLiteral("gage_file_path"));
    m_colCol     = colForSetter(specs, QStringLiteral("gage_file_column"));
    m_fmtCol     = colForSetter(specs, QStringLiteral("gage_file_format"));
    m_stationCol = colForSetter(specs, QStringLiteral("gage_station_id"));
    QVERIFY(m_pathCol >= 0);
    QVERIFY(m_colCol  >= 0);
    QVERIFY(m_fmtCol  >= 0);

    m_row = 0;
    QCOMPARE(m_model->rowCount(), 1);
}

void TestRainGageFileColumn::pathEditBindsFirstColumnAndUndoesAsOneStep()
{
    const QString csv = fixture(QStringLiteral("extcol_switch_multi.csv"));

    QVERIFY(m_model->setData(m_model->index(m_row, m_pathCol), csv,
                             Qt::EditRole));

    // The multi-column file is bound to its first value column — leaving it
    // unbound made the writer emit the station grammar for a file that has no
    // station column (R3). "time" is the time column and is never offered.
    QCOMPARE(m_model->data(m_model->index(m_row, m_colCol), Qt::EditRole).toString(),
             QStringLiteral("rain_a"));
    // ...and the grammar followed it.
    QCOMPARE(m_model->data(m_model->index(m_row, m_fmtCol), Qt::EditRole).toInt(),
             int(SWMMRainGagePropertyAdapter::MultiColumn));

    // ONE undo step for the whole thing: three engine writes, one user action.
    QCOMPARE(m_undo->count(), 1);
    m_undo->undo();

    QCOMPARE(m_model->data(m_model->index(m_row, m_colCol), Qt::EditRole).toString(),
             QString());
    // The format flip has to undo as well. It is the engine's implicit
    // side-effect of setting a column, so it is recorded as its own command
    // inside the macro — without that, undo left the gage USER_CSV with no
    // column and Station ID greyed out for good.
    QCOMPARE(m_model->data(m_model->index(m_row, m_fmtCol), Qt::EditRole).toInt(),
             int(SWMMRainGagePropertyAdapter::AutoDetect));
}

void TestRainGageFileColumn::pathEditKeepsAColumnTheNewFileStillHas()
{
    const QString csv = fixture(QStringLiteral("extcol_switch_multi.csv"));
    QVERIFY(m_model->setData(m_model->index(m_row, m_pathCol), csv, Qt::EditRole));
    // Pick the second column explicitly.
    QVERIFY(m_model->setData(m_model->index(m_row, m_colCol),
                             QStringLiteral("rain_b"), Qt::EditRole));
    const int stepsBefore = m_undo->count();

    // Re-pointing at the SAME file must not steal the user's column choice.
    QVERIFY(m_model->setData(m_model->index(m_row, m_pathCol), csv, Qt::EditRole));
    QCOMPARE(m_model->data(m_model->index(m_row, m_colCol), Qt::EditRole).toString(),
             QStringLiteral("rain_b"));
    // A no-op edit pushes nothing at all.
    QCOMPARE(m_undo->count(), stepsBefore);
}

void TestRainGageFileColumn::pathEditClearsAColumnTheNewFileCannotResolve()
{
    QVERIFY(m_model->setData(m_model->index(m_row, m_pathCol),
                             fixture(QStringLiteral("extcol_switch_multi.csv")),
                             Qt::EditRole));
    QVERIFY(m_model->setData(m_model->index(m_row, m_colCol),
                             QStringLiteral("rain_b"), Qt::EditRole));

    // Switch to a file that has no "rain_b": keeping the stale name would write
    // a model whose run fails on a column the file does not contain.
    QVERIFY(m_model->setData(m_model->index(m_row, m_pathCol),
                             fixture(QStringLiteral("extcol_dialog_sample.tsf")),
                             Qt::EditRole));
    QCOMPARE(m_model->data(m_model->index(m_row, m_colCol), Qt::EditRole).toString(),
             QStringLiteral("RG1"));

    m_undo->undo();
    QCOMPARE(m_model->data(m_model->index(m_row, m_colCol), Qt::EditRole).toString(),
             QStringLiteral("rain_b"));
}

void TestRainGageFileColumn::formatColumnIsPresentAndLeavesUserCsv()
{
    QVERIFY(m_model->setData(m_model->index(m_row, m_pathCol),
                             fixture(QStringLiteral("extcol_switch_multi.csv")),
                             Qt::EditRole));
    QCOMPARE(m_model->data(m_model->index(m_row, m_fmtCol), Qt::EditRole).toInt(),
             int(SWMMRainGagePropertyAdapter::MultiColumn));

    // A-2: the way back out. Selecting a station-based format clears the column
    // engine-side, so the gage can be a standard rain file again.
    QVERIFY(m_model->setData(m_model->index(m_row, m_fmtCol),
                             int(SWMMRainGagePropertyAdapter::StandardRainFile),
                             Qt::EditRole));
    QCOMPARE(m_model->data(m_model->index(m_row, m_colCol), Qt::EditRole).toString(),
             QString());

    // Station ID is usable again — the row the format flip exists to release.
    if (m_stationCol >= 0) {
        QVERIFY(m_model->setData(m_model->index(m_row, m_stationCol),
                                 QStringLiteral("STA1"), Qt::EditRole));
        QCOMPARE(m_model->data(m_model->index(m_row, m_stationCol),
                               Qt::EditRole).toString(),
                 QStringLiteral("STA1"));
    }

    // And selecting the multi-column grammar clears the station id, because the
    // two grammars' row selectors are mutually exclusive.
    QVERIFY(m_model->setData(m_model->index(m_row, m_fmtCol),
                             int(SWMMRainGagePropertyAdapter::MultiColumn),
                             Qt::EditRole));
    if (m_stationCol >= 0)
        QCOMPARE(m_model->data(m_model->index(m_row, m_stationCol),
                               Qt::EditRole).toString(),
                 QString());
}

void TestRainGageFileColumn::adapterReconcilesTheColumnToo()
{
    // MVC: the Property Browser writes through the adapter, and it has to reach
    // the same state the table does — otherwise the same edit means two
    // different things depending on which editor made it.
    SWMMRainGagePropertyAdapter adapter(m_layer->engine(), QStringLiteral("S1"));
    adapter.setModelLayer(m_layer.get());

    adapter.setFilePath(fixture(QStringLiteral("extcol_switch_multi.csv")));
    QCOMPARE(adapter.fileColumn(), QStringLiteral("rain_a"));
    QCOMPARE(adapter.fileFormatEnum(), SWMMRainGagePropertyAdapter::MultiColumn);

    adapter.setFileColumn(QStringLiteral("rain_b"));
    adapter.setFilePath(fixture(QStringLiteral("extcol_dialog_sample.tsf")));
    QCOMPARE(adapter.fileColumn(), QStringLiteral("RG1"));

    // A-2 through the adapter.
    adapter.setFileFormat(int(SWMMRainGagePropertyAdapter::StandardRainFile));
    QCOMPARE(adapter.fileColumn(), QString());
    QCOMPARE(adapter.fileFormatEnum(),
             SWMMRainGagePropertyAdapter::StandardRainFile);
}

QTEST_MAIN(TestRainGageFileColumn)
#include "test_raingagefilecolumn.moc"
