/*!
 * \file   test_newfeaturelayerdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  The role-first New Feature Layer dialog
 *         (workplans/FEATURE_LAYER_ROLES_AND_FIELDS_PLAN_2026-09-30.md §5, R4).
 *
 * Runs the dialog without a canvas (no CRS, no Z source layers): what is
 * under test is the role → geometry → fields wiring and the schema it hands
 * FeatureLayer::create. The custom Choice field is carried all the way into a
 * GeoPackage and read back, so the "round-trips" claim covers the store too.
 * The GeoPackage lands in tests/output/feature_layer_roles_2026-09-30/
 * (reviewable — CLAUDE.md §4.1).
 */

#include "feature/featureroles.h"
#include "feature/featurestore.h"
#include "ui/dialogs/featurefieldeditor.h"
#include "ui/dialogs/newfeaturelayerdialog.h"

#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QTableWidget>
#include <QTest>

#ifndef FEATURE_ROLES_OUT_DIR
#define FEATURE_ROLES_OUT_DIR "feature_layer_roles_2026-09-30"
#endif

using namespace openswmmvis::feature;
using openswmmvis::ui::FeatureFieldEditor;
using openswmmvis::ui::NewFeatureLayerDialog;

namespace {

FieldDef choiceField()
{
    FieldDef f;
    f.name = QStringLiteral("surface");
    f.type = FieldType::Text;
    f.choiceSource = ChoiceSource::Fixed;
    f.choices = {{QStringLiteral("paved"), QStringLiteral("Paved")},
                 {QStringLiteral("grass"), QStringLiteral("Grass")},
                 {QStringLiteral("bare"), QString()}};
    f.defaultValue = QStringLiteral("grass");
    f.description = QStringLiteral("Ground cover");
    return f;
}

}   // namespace

class TestNewFeatureLayerDialog : public QObject
{
    Q_OBJECT

private slots:
    void roleSwitchSetsFieldsAndGeometryKinds()
    {
        NewFeatureLayerDialog dlg(nullptr);
        dlg.setRole(FeatureLayerRole::Region);
        QCOMPARE(dlg.offeredGeometries(),
                 (QVector<GeometryType>{GeometryType::Polygon, GeometryType::MultiPolygon}));
        QCOMPARE(dlg.geometryType(), GeometryType::Polygon);
        QCOMPARE(dlg.schema().names(),
                 (QStringList{QStringLiteral("h"), QStringLiteral("cells"), QStringLiteral("tag")}));

        dlg.setRole(FeatureLayerRole::Corridor);
        QCOMPARE(dlg.offeredGeometries(),
                 (QVector<GeometryType>{GeometryType::LineString,
                                        GeometryType::MultiLineString}));
        QCOMPARE(dlg.geometryType(), GeometryType::LineString);
        QCOMPARE(dlg.schema().names(),
                 (QStringList{QStringLiteral("width"), QStringLiteral("tag")}));

        dlg.setRole(FeatureLayerRole::General);
        QCOMPARE(dlg.offeredGeometries().size(), 6);
        QCOMPARE(dlg.role(), FeatureLayerRole::General);

        dlg.setRole(FeatureLayerRole::DomainBoundary);
        QCOMPARE(dlg.geometryType(), GeometryType::MultiPolygon);   // the role's default
        dlg.setGeometryType(GeometryType::Polygon);
        QCOMPARE(dlg.geometryType(), GeometryType::Polygon);
        dlg.setGeometryType(GeometryType::Point);                   // not offered
        QCOMPARE(dlg.geometryType(), GeometryType::Polygon);
    }

    void anUntickedOptionalFieldIsNotCreated()
    {
        NewFeatureLayerDialog dlg(nullptr);
        dlg.setRole(FeatureLayerRole::Region);
        QVERIFY(!dlg.setFieldIncluded(QStringLiteral("tag"), false));
        QVERIFY(!dlg.schema().contains(QStringLiteral("tag")));
        QVERIFY(dlg.setFieldIncluded(QStringLiteral("tag"), true));
        QVERIFY(dlg.schema().contains(QStringLiteral("tag")));
    }

    void requiredFieldsCannotBeUnticked()
    {
        NewFeatureLayerDialog dlg(nullptr);
        dlg.setRole(FeatureLayerRole::Region);
        QVERIFY(dlg.setFieldIncluded(QStringLiteral("h"), false));      // still ticked
        QVERIFY(dlg.setFieldIncluded(QStringLiteral("cells"), false));
        QVERIFY(dlg.schema().contains(QStringLiteral("h")));
        QVERIFY(dlg.schema().contains(QStringLiteral("cells")));
    }

    void defaultsAreEditedWithTheFieldsOwnEditor()
    {
        NewFeatureLayerDialog dlg(nullptr);
        dlg.setRole(FeatureLayerRole::Region);
        auto *h = dlg.findChild<QDoubleSpinBox *>(QStringLiteral("newFeatureDefault_h"));
        QVERIFY(h);
        h->setValue(5.0);
        const Schema s = dlg.schema();
        QCOMPARE(s.field(QStringLiteral("h"))->defaultValue.toDouble(), 5.0);
        QCOMPARE(s.field(QStringLiteral("cells"))->defaultValue.toString(),
                 QStringLiteral("auto"));
        // The registry's editor hints travel with the field.
        QCOMPARE(s.field(QStringLiteral("cells"))->choiceSource, ChoiceSource::Fixed);
        QCOMPARE(s.field(QStringLiteral("h"))->unit, FieldUnit::Length);
    }

    void aCustomChoiceFieldRoundTrips()
    {
        NewFeatureLayerDialog dlg(nullptr);
        dlg.setRole(FeatureLayerRole::General);
        QVERIFY(dlg.addCustomField(choiceField()));
        QVERIFY(!dlg.addCustomField(choiceField()));   // duplicate name
        const Schema offered = dlg.schema();   // schema() is by value
        const FieldDef *f = offered.field(QStringLiteral("surface"));
        QVERIFY(f);
        QCOMPARE(*f, choiceField());

        // Custom fields survive a change of role.
        dlg.setRole(FeatureLayerRole::Region);
        QVERIFY(dlg.schema().contains(QStringLiteral("surface")));

        // …and into the GeoPackage and back.
        QDir().mkpath(QStringLiteral(FEATURE_ROLES_OUT_DIR));
        const QString gpkg = QDir(QStringLiteral(FEATURE_ROLES_OUT_DIR))
                                 .filePath(QStringLiteral("new_dialog_custom.gpkg"));
        QFile::remove(gpkg);
        QString err;
        QVERIFY2(FeatureStore::createTable(gpkg, QStringLiteral("regions"),
                                           dlg.geometryType(), false, dlg.schema(),
                                           QString(), &err),
                 qPrintable(err));
        FeatureStore store;
        QVERIFY2(store.open(gpkg, QStringLiteral("regions"), &err), qPrintable(err));
        const Schema stored = store.schema();
        const FieldDef *back = stored.field(QStringLiteral("surface"));
        QVERIFY(back);
        QCOMPARE(back->defaultValue.toString(), QStringLiteral("grass"));
        if (FeatureStore::storesFieldMetadata()) {
            QCOMPARE(back->choiceSource, ChoiceSource::Fixed);
            QVERIFY(sameChoiceSet(back->choices, choiceField().choices));
            QCOMPARE(back->description, QStringLiteral("Ground cover"));
        }
    }

    void theFieldEditorValidates()
    {
        FieldDef f = choiceField();
        QVERIFY(FeatureFieldEditor::validate(f, f.name, {}).isEmpty());
        QVERIFY(!FeatureFieldEditor::validate(f, f.name, {QStringLiteral("SURFACE")}).isEmpty());
        QVERIFY(!FeatureFieldEditor::validate(f, QStringLiteral("  "), {}).isEmpty());
        f.choices.append(f.choices.first());
        QVERIFY(!FeatureFieldEditor::validate(f, f.name, {}).isEmpty());
        f.choices.clear();
        QVERIFY(!FeatureFieldEditor::validate(f, f.name, {}).isEmpty());

        FeatureFieldEditor editor;
        editor.setField(choiceField());
        QCOMPARE(editor.field(), choiceField());
        QVERIFY(editor.validationError().isEmpty());
    }

    void theZSectionFollowsTheRole()
    {
        NewFeatureLayerDialog dlg(nullptr);
        dlg.setRole(FeatureLayerRole::Region);
        QVERIFY(!dlg.zSectionExpanded());
        QVERIFY(!dlg.zPolicy().isThreeD());
        dlg.setRole(FeatureLayerRole::Breakline);
        QVERIFY(dlg.zSectionExpanded());
        QCOMPARE(dlg.zPolicy().source, ZPolicy::Source::Raster);   // nudged to 3D
        dlg.setRole(FeatureLayerRole::General);
        QVERIFY(dlg.zSectionExpanded());
    }
};

QTEST_MAIN(TestNewFeatureLayerDialog)
#include "test_newfeaturelayerdialog.moc"
