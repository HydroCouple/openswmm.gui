// Structured patch authoring: checked values, stable type identity, and keyboard metadata.
#include "project/openswmmvisworkspace.h"
#include "swmmvisprojectwindow.h"
#include "ui/dialogs/meshgenerationdialog.h"

#include <QDir>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QTabWidget>
#include <QGroupBox>
#include <QItemSelectionModel>
#include <QPushButton>
#include <QTableWidget>
#include <QTest>
#include <memory>

class TestMeshPatchDialog : public QObject
{
    Q_OBJECT
    std::unique_ptr<OpenSWMMVisWorkspace> m_workspace;
    std::unique_ptr<SWMMVisProjectWindow> m_window;

    static QPushButton *button(MeshGenerationDialog &dialog, const QString &text)
    {
        for (auto *group : dialog.findChildren<QGroupBox *>()) {
            if (group->title() != QStringLiteral("Structured quad patches")) continue;
            for (auto *candidate : group->findChildren<QPushButton *>())
                if (candidate->text().remove('&').contains(text)) return candidate;
        }
        return nullptr;
    }

    static void addSwept(MeshGenerationDialog &dialog)
    {
        auto *add = button(dialog, QStringLiteral("Add swept"));
        QVERIFY(add);
        add->click();
        const int row = dialog.m_patchTable->rowCount() - 1;
        dialog.m_patchTable->item(row, 1)->setText(QStringLiteral("0 0; 20 0; 40 0"));
    }

private slots:
    void initTestCase()
    {
        m_workspace.reset(OpenSWMMVisWorkspace::newInstance(QString(), nullptr));
        QVERIFY(m_workspace);
        const QString fixture = QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", "."))
            .filePath(QStringLiteral("typed_selection_fixture.inp"));
        m_window = std::make_unique<SWMMVisProjectWindow>(m_workspace.get(), fixture, nullptr);
        QList<QString> warnings, errors;
        QVERIFY2(m_window->loadModel(warnings, errors), qPrintable(errors.join('\n')));
    }

    void directionalMappedDefaultsPreserveScalarCollection()
    {
        MeshGenerationDialog dialog(m_window.get(), nullptr);
        auto *enabled = dialog.findChild<QCheckBox *>(QStringLiteral("mappedDirectionalSpacing"));
        auto *along = dialog.findChild<QDoubleSpinBox *>(QStringLiteral("mappedAlongSpacing"));
        auto *across = dialog.findChild<QDoubleSpinBox *>(QStringLiteral("mappedAcrossSpacing"));
        auto *axis = dialog.findChild<QDoubleSpinBox *>(QStringLiteral("mappedAlongAxis"));
        QVERIFY(enabled && along && across && axis);
        QVERIFY(!enabled->isChecked());
        QVERIFY(!along->isEnabled());
        QVERIFY(!across->isEnabled());
        QVERIFY(!axis->isEnabled());
        dialog.m_quadRegionSpacingSpin->setValue(17.5);
        MeshGenerationDialog::PipelineInputs input;
        QString error;
        QVERIFY2(dialog.collectInputs(&input, &error), qPrintable(error));
        QVERIFY(!input.quadRegionDefaults.directionalSpacing);
        QCOMPARE(input.quadRegionDefaults.spacing, 17.5);
        QCOMPARE(input.quadRegionDefaults.hAlong, 0.0);
        QCOMPARE(input.quadRegionDefaults.hAcross, 0.0);
        QCOMPARE(input.quadRegionDefaults.mappedAlongAngleDeg, 0.0);
    }

    void directionalMappedValuesCollect_data()
    {
        QTest::addColumn<int>("mode");
        QTest::newRow("mapped") << int(mesh::QuadRegionMode::Mapped);
        QTest::newRow("auto") << int(mesh::QuadRegionMode::Auto);
    }

    void directionalMappedValuesCollect()
    {
        QFETCH(int, mode);
        MeshGenerationDialog dialog(m_window.get(), nullptr);
        auto *enabled = dialog.findChild<QCheckBox *>(QStringLiteral("mappedDirectionalSpacing"));
        auto *along = dialog.findChild<QDoubleSpinBox *>(QStringLiteral("mappedAlongSpacing"));
        auto *across = dialog.findChild<QDoubleSpinBox *>(QStringLiteral("mappedAcrossSpacing"));
        auto *axis = dialog.findChild<QDoubleSpinBox *>(QStringLiteral("mappedAlongAxis"));
        QVERIFY(enabled && along && across && axis);
        enabled->setChecked(true);
        QVERIFY(along->isEnabled() && across->isEnabled() && axis->isEnabled());
        QVERIFY(!dialog.m_quadRegionSpacingSpin->isEnabled());
        dialog.m_quadRegionModeCombo->setCurrentIndex(dialog.m_quadRegionModeCombo->findData(mode));
        along->setValue(24.5);
        across->setValue(3.5);
        axis->setValue(32.5);
        MeshGenerationDialog::PipelineInputs input;
        QString error;
        QVERIFY2(dialog.collectInputs(&input, &error), qPrintable(error));
        QVERIFY(input.quadRegionDefaults.directionalSpacing);
        QCOMPARE(input.quadRegionDefaults.hAlong, 24.5);
        QCOMPARE(input.quadRegionDefaults.hAcross, 3.5);
        QCOMPARE(input.quadRegionDefaults.mappedAlongAngleDeg, 32.5);
        for (auto *spin : {along, across, axis}) {
            QVERIFY(!spin->accessibleName().isEmpty());
            QVERIFY(!spin->accessibleDescription().isEmpty());
            bool buddy = false;
            for (auto *label : dialog.findChildren<QLabel *>())
                if (label->buddy() == spin) buddy = true;
            QVERIFY(buddy);
        }
        QVERIFY(along->toolTip().contains(QStringLiteral("mesh CRS")));
        QVERIFY(axis->toolTip().contains(QStringLiteral("counter-clockwise")));
        enabled->setChecked(false);
        QVERIFY(dialog.m_quadRegionSpacingSpin->isEnabled());
        MeshGenerationDialog::PipelineInputs scalar;
        QVERIFY2(dialog.collectInputs(&scalar, &error), qPrintable(error));
        QVERIFY(!scalar.quadRegionDefaults.directionalSpacing);
        QCOMPARE(scalar.quadRegionDefaults.hAlong, 0.0);
        QCOMPARE(scalar.quadRegionDefaults.hAcross, 0.0);
    }

    void directionalMappedRejectsIncompatibleMode_data()
    {
        QTest::addColumn<int>("mode");
        QTest::newRow("free") << int(mesh::QuadRegionMode::Free);
        QTest::newRow("submapped") << int(mesh::QuadRegionMode::Submapped);
        QTest::newRow("triangles") << int(mesh::QuadRegionMode::TrianglesOnly);
    }

    void directionalMappedRejectsIncompatibleMode()
    {
        QFETCH(int, mode);
        MeshGenerationDialog dialog(m_window.get(), nullptr);
        auto *enabled = dialog.findChild<QCheckBox *>(QStringLiteral("mappedDirectionalSpacing"));
        QVERIFY(enabled);
        enabled->setChecked(true);
        dialog.m_quadRegionModeCombo->setCurrentIndex(dialog.m_quadRegionModeCombo->findData(mode));
        auto *tabs = dialog.findChild<QTabWidget *>(QStringLiteral("meshQualityTabs"));
        QVERIFY(tabs);
        tabs->setCurrentIndex(0);
        dialog.show();
        MeshGenerationDialog::PipelineInputs input;
        QString error;
        QVERIFY(!dialog.collectInputs(&input, &error));
        QVERIFY2(error.contains(QStringLiteral("Mapped")) && error.contains(QStringLiteral("Auto")), qPrintable(error));
        QCOMPARE(tabs->tabText(tabs->currentIndex()), QStringLiteral("Quads"));
        QCOMPARE(dialog.focusWidget(), dialog.m_quadRegionModeCombo);
        QVERIFY(enabled->isChecked()); // Preserve the requested configuration for correction.
    }

    void directionalMappedRejectsZeroSpacing_data()
    {
        QTest::addColumn<QString>("control");
        QTest::newRow("along") << QStringLiteral("mappedAlongSpacing");
        QTest::newRow("across") << QStringLiteral("mappedAcrossSpacing");
    }

    void directionalMappedRejectsZeroSpacing()
    {
        QFETCH(QString, control);
        MeshGenerationDialog dialog(m_window.get(), nullptr);
        auto *enabled = dialog.findChild<QCheckBox *>(QStringLiteral("mappedDirectionalSpacing"));
        auto *spacing = dialog.findChild<QDoubleSpinBox *>(control);
        QVERIFY(enabled && spacing);
        enabled->setChecked(true);
        spacing->setValue(0.0);
        dialog.show();
        MeshGenerationDialog::PipelineInputs input;
        QString error;
        QVERIFY(!dialog.collectInputs(&input, &error));
        QVERIFY2(error.contains(QStringLiteral("greater than 0")), qPrintable(error));
        QCOMPARE(dialog.focusWidget(), spacing);
    }

    void rejectsInvalidNumericFields_data()
    {
        QTest::addColumn<int>("column");
        QTest::addColumn<QString>("value");
        QTest::addColumn<QString>("field");
        QTest::newRow("empty-along") << 3 << QString() << QStringLiteral("Along");
        QTest::newRow("text-along") << 3 << QStringLiteral("oops") << QStringLiteral("Along");
        QTest::newRow("nan-along") << 3 << QStringLiteral("nan") << QStringLiteral("Along");
        QTest::newRow("infinite-along") << 3 << QStringLiteral("inf") << QStringLiteral("Along");
        QTest::newRow("overflow-along") << 3 << QStringLiteral("1e309") << QStringLiteral("Along");
        QTest::newRow("fractional-across") << 2 << QStringLiteral("2.5") << QStringLiteral("Across");
        QTest::newRow("integer-overflow") << 2 << QStringLiteral("2147483648") << QStringLiteral("Across");
        QTest::newRow("empty-width") << 4 << QString() << QStringLiteral("width");
        QTest::newRow("infinite-width") << 4 << QStringLiteral("inf") << QStringLiteral("width");
    }

    void rejectsInvalidNumericFields()
    {
        QFETCH(int, column);
        QFETCH(QString, value);
        QFETCH(QString, field);
        MeshGenerationDialog dialog(m_window.get(), nullptr);
        addSwept(dialog);
        dialog.m_patchTable->item(0, column)->setText(value);
        MeshGenerationDialog::PipelineInputs input;
        QString error;
        QVERIFY(!dialog.collectInputs(&input, &error));
        QVERIFY2(error.contains(QStringLiteral("row 1")), qPrintable(error));
        QVERIFY2(error.contains(field, Qt::CaseInsensitive), qPrintable(error));
        QCOMPARE(dialog.m_patchTable->currentRow(), 0);
        QCOMPARE(dialog.m_patchTable->currentColumn(), column);
        QCOMPARE(dialog.m_patchTable->item(0, column)->text(), value);
        QVERIFY(input.patches.isEmpty());
    }

    void rejectsInvalidFourCornerCounts_data()
    {
        QTest::addColumn<int>("column");
        QTest::addColumn<QString>("value");
        for (int column : {2, 3}) {
            const QByteArray prefix = column == 2 ? "N-" : "M-";
            QTest::newRow((prefix + "empty").constData()) << column << QString();
            QTest::newRow((prefix + "fraction").constData()) << column << QStringLiteral("1.5");
            QTest::newRow((prefix + "overflow").constData()) << column << QStringLiteral("2147483648");
            QTest::newRow((prefix + "nonfinite").constData()) << column << QStringLiteral("inf");
        }
    }

    void rejectsInvalidFourCornerCounts()
    {
        QFETCH(int, column);
        QFETCH(QString, value);
        MeshGenerationDialog dialog(m_window.get(), nullptr);
        auto *add = button(dialog, QStringLiteral("Add four"));
        QVERIFY(add);
        add->click();
        dialog.m_patchTable->item(0, 1)->setText(QStringLiteral("0 0; 20 0; 20 20; 0 20"));
        dialog.m_patchTable->item(0, column)->setText(value);
        MeshGenerationDialog::PipelineInputs input;
        QString error;
        QVERIFY(!dialog.collectInputs(&input, &error));
        QVERIFY2(error.contains(column == 2 ? QStringLiteral("N cells") : QStringLiteral("M cells")), qPrintable(error));
        QCOMPARE(dialog.m_patchTable->currentColumn(), column);
        QCOMPARE(dialog.m_patchTable->item(0, column)->text(), value);
        QVERIFY(input.patches.isEmpty());
    }

    void positiveAlongSpacingUsesMeshUnits()
    {
        MeshGenerationDialog dialog(m_window.get(), nullptr);
        addSwept(dialog);
        dialog.m_patchTable->item(0, 3)->setText(QStringLiteral("10"));
        MeshGenerationDialog::PipelineInputs input;
        QString error;
        QVERIFY2(dialog.collectInputs(&input, &error), qPrintable(error));
        QCOMPARE(input.patches.size(), 1);
        QCOMPARE(input.patches.first().xy.size(), 15); // Five stations, two cells across.
        QCOMPARE(input.patches.first().quads.size(), 8);
        QCOMPARE(qAbs(input.patches.first().xy.first().y()), 5.0); // Total width 10.
    }

    void nonfinitePointSelectsPointsCell()
    {
        MeshGenerationDialog dialog(m_window.get(), nullptr);
        addSwept(dialog);
        dialog.m_patchTable->item(0, 1)->setText(QStringLiteral("0 0; inf 0"));
        MeshGenerationDialog::PipelineInputs input;
        QString error;
        QVERIFY(!dialog.collectInputs(&input, &error));
        QVERIFY2(error.contains(QStringLiteral("Points")), qPrintable(error));
        QCOMPARE(dialog.m_patchTable->currentColumn(), 1);
        QVERIFY(input.patches.isEmpty());
    }

    void explicitZeroKeepsCentrelineStations()
    {
        MeshGenerationDialog dialog(m_window.get(), nullptr);
        addSwept(dialog);
        MeshGenerationDialog::PipelineInputs input;
        QString error;
        QVERIFY2(dialog.collectInputs(&input, &error), qPrintable(error));
        QCOMPARE(input.patches.size(), 1);
        QCOMPARE(input.patches.first().xy.size(), 9); // Three stations, two cells across.
        QCOMPARE(input.patches.first().quads.size(), 4);
    }

    void translatedDisplayDoesNotChangePatchType()
    {
        MeshGenerationDialog dialog(m_window.get(), nullptr);
        addSwept(dialog);
        dialog.m_patchTable->item(0, 0)->setText(QStringLiteral("Canal traduit"));
        MeshGenerationDialog::PipelineInputs input;
        QString error;
        QVERIFY2(dialog.collectInputs(&input, &error), qPrintable(error));
        QCOMPARE(input.patches.first().quads.size(), 4);
    }

    void removeDeletesAllSelectedRows()
    {
        MeshGenerationDialog dialog(m_window.get(), nullptr);
        for (int i = 0; i < 3; ++i) {
            addSwept(dialog);
            dialog.m_patchTable->item(i, 5)->setText(QString::number(i));
        }
        auto *selection = dialog.m_patchTable->selectionModel();
        selection->clearSelection();
        for (int row : {0, 2})
            selection->select(dialog.m_patchTable->model()->index(row, 0),
                QItemSelectionModel::Select | QItemSelectionModel::Rows);
        auto *remove = button(dialog, QStringLiteral("Remove"));
        QVERIFY(remove);
        remove->click();
        QCOMPARE(dialog.m_patchTable->rowCount(), 1);
        QCOMPARE(dialog.m_patchTable->item(0, 5)->text(), QStringLiteral("1"));
        dialog.m_patchTable->clearSelection();
        QVERIFY(!remove->isEnabled());
    }

    void patchControlsExplainUnitsAndKeyboardBehavior()
    {
        MeshGenerationDialog dialog(m_window.get(), nullptr);
        auto *table = dialog.m_patchTable;
        QVERIFY(!table->accessibleName().isEmpty());
        QVERIFY(!table->accessibleDescription().isEmpty());
        QVERIFY(table->horizontalHeaderItem(2)->text().contains(QStringLiteral("cells")));
        QVERIFY(table->horizontalHeaderItem(3)->text().contains(QStringLiteral("spacing")));
        QVERIFY(table->horizontalHeaderItem(4)->text().contains(QStringLiteral("Total")));
        QVERIFY(table->horizontalHeaderItem(4)->text().contains(QStringLiteral("CRS units")));
        for (int c = 0; c < table->columnCount(); ++c) {
            QVERIFY(!table->horizontalHeaderItem(c)->toolTip().isEmpty());
            QVERIFY(!table->horizontalHeaderItem(c)->data(Qt::AccessibleDescriptionRole).toString().isEmpty());
        }
        for (const QString &text : {QStringLiteral("Add four"), QStringLiteral("Add swept"), QStringLiteral("Remove")}) {
            auto *action = button(dialog, text);
            QVERIFY(action);
            QVERIFY(!action->accessibleName().isEmpty());
            QVERIFY(!action->autoDefault());
            QVERIFY(!action->isDefault());
        }
    }
};

QTEST_MAIN(TestMeshPatchDialog)
#include "test_meshpatchdialog.moc"
