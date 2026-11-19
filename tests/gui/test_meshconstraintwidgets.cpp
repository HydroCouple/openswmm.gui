// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/meshconstraintsourceswidget.h"
#include "ui/widgets/meshconstraintpreviewwidget.h"
#include "mesh/constraintaudit.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QTest>
#include <QTreeWidget>
#include <QDir>
#include <cmath>

class TestMeshConstraintWidgets : public QObject
{
    Q_OBJECT
private slots:
    void sourcePoliciesStayPhysicalAndRetainUnavailableSources()
    {
        MeshConstraintSourcesWidget widget;
        widget.setSources({{"boundary:a", "Boundary A", false, false},
                           {"line:b", "Surveyed line", true, false}});
        mesh::ConstraintSimplificationPolicy defaults;
        defaults.mode = mesh::ConstraintSimplificationPolicy::Mode::Simplify;
        defaults.maxDeviation = .2;
        widget.setDefaults(defaults);
        widget.fromJson({QJsonObject{{"id", "line:b"}, {"mode", 1}, {"maximumXYM", 1.}, {"maximumZM", .02}},
                         QJsonObject{{"id", "unavailable"}, {"mode", 0}}});
        auto *source = widget.findChild<QComboBox *>("constraintSourcePicker");
        auto *mode = widget.findChild<QComboBox *>("constraintSourceMode");
        auto *xy = widget.findChild<QDoubleSpinBox *>("constraintSourceXYValue");
        QVERIFY(source && mode && xy);
        source->setCurrentIndex(source->findData("line:b"));
        widget.setUnits(.3048, "ft");
        QVERIFY(std::abs(xy->value() - 1 / .3048) < 1e-6);
        mode->setCurrentIndex(0);
        QCOMPARE(widget.policy("line:b").maxDeviation.value(), 1.);
        QCOMPARE(widget.policy("line:b").maxZDeviation.value(), .02);
        widget.setUnits(1, "m");
        QCOMPARE(xy->value(), 1.);
        const auto json = QJsonDocument::fromJson(QJsonDocument(widget.toJson()).toJson()).array();
        MeshConstraintSourcesWidget restored;
        restored.setSources({{"boundary:a", "Boundary A", false, false}});
        restored.fromJson(json);
        QCOMPARE(restored.policy("line:b").maxDeviation.value(), 1.);
        QCOMPARE(restored.policy("unavailable").mode, 0);
        QCOMPARE(restored.findChild<QComboBox *>("constraintSourcePicker")->count(), 3);
        auto *reset = widget.findChild<QPushButton *>("constraintSourceReset");
        QVERIFY(reset && reset->isEnabled());
        reset->click();
        QVERIFY(!widget.policy("line:b").maxDeviation.has_value());
        QCOMPARE(widget.policy("unavailable").mode, 0);
    }
    void invalidSavedLimitsRequireAnExplicitCorrection()
    {
        MeshConstraintSourcesWidget widget;
        widget.setSources({{"line:b", "Surveyed line", true, false}});
        widget.fromJson({QJsonObject{{"id", "line:b"}, {"mode", 1}, {"maximumXYM", "invalid"}}});
        QVERIFY(!widget.policy("line:b").validate().isEmpty());
        QCOMPARE(widget.toJson()[0].toObject()["maximumXYM"].toString(), QString("Invalid saved value"));
        widget.findChild<QComboBox *>("constraintSourceMode")->setCurrentIndex(1);
        QVERIFY(!widget.policy("line:b").validate().isEmpty());
        widget.findChild<QPushButton *>("constraintSourceReset")->click();
        QVERIFY(widget.policy("line:b").validate().isEmpty());
    }
    void previewIsBoundedAndNeverChangesTheAuditedGeometry()
    {
        mesh::SimplificationFeature line;
        line.id = "survey:7"; line.label = "Surveyed bank — feature 7";
        line.policy.mode = mesh::ConstraintSimplificationPolicy::Mode::Simplify;
        line.policy.maxDeviation = 0;
        for (int i = 0; i < 100000; ++i) line.xy.append({double(i), 0});
        const auto result = mesh::simplifyConstraints({line});
        QVERIFY(result.ok);
        QCOMPARE(result.features[0].xy.size(), 2);
        const auto audit = mesh::auditConstraintSimplification({line}, result);
        QVERIFY2(audit.ok, qPrintable(audit.error));
        auto preview = mesh::makeConstraintPreview({line}, result, {}, 100, 2);
        QVERIFY(preview.displayReduced);
        qsizetype displayed = 0;
        for (const auto &feature : preview.features)
            displayed += feature.original.size() + feature.proposed.size() + feature.removed.size();
        QVERIFY(displayed <= 100);
        QCOMPARE(preview.removedTotal, quint64(99998));
        QCOMPARE(line.xy.size(), 100000);
        QCOMPARE(result.features[0].xy.size(), 2);
        preview.horizontalUnit = "m"; preview.verticalUnit = "m";
        MeshConstraintPreviewWidget widget;
        widget.resize(900, 650);
        widget.show();
        widget.setPreview(preview, "Independent audit passed.");
        auto *summary = widget.findChild<QLabel *>("constraintPreviewSummary");
        auto *features = widget.findChild<QTreeWidget *>("constraintPreviewFeatures");
        QVERIFY(summary && features);
        QVERIFY(summary->text().contains("sampled"));
        QCOMPARE(features->topLevelItemCount(), 1);
        features->setCurrentItem(features->topLevelItem(0));
        widget.markStale();
        QVERIFY(summary->text().contains("Inputs changed"));
        widget.setPreview(preview, "Independent audit passed.");
        QVERIFY(!summary->text().contains("Inputs changed"));
        widget.markStale();
        QVERIFY(summary->text().contains("Inputs changed"));
        widget.setPreview(preview, "Independent audit passed.");
        QTest::qWait(50); // Let the word-wrapped detail label settle before capturing.
        QDir().mkpath(MESH_CONSTRAINT_WIDGET_TEST_OUTPUT);
        QVERIFY(widget.grab().save(QString(MESH_CONSTRAINT_WIDGET_TEST_OUTPUT) + "/constraint-preview-" +
                                  qEnvironmentVariable("QT_QPA_PLATFORM", "native") + ".png"));
    }
};
QTEST_MAIN(TestMeshConstraintWidgets)
#include "test_meshconstraintwidgets.moc"
