#include "ui/dialogs/layerstyledialog.h"
#include "layers/swmm2dresultslayer.h"
#include "layers/openswmmvislayer.h"
#include <QLineEdit>
#include "render/sublayers/resultscalarsublayer.h"
#include "ui/dialogs/ilayerstylesubject.h"
#include "ui/widgets/classificationeditor.h"
#include <QTableView>
#include "render/sublayers/contourbandsublayer.h"
#include <QDialogButtonBox>
#include <QPushButton>
#include <QUndoStack>
#include <QtTest>
#include <memory>

using openswmmvis::ui::LayerStyleDialog;

class GeneralStyleTestLayer : public OpenSWMMVisLayer
{
public:
    using OpenSWMMVisLayer::OpenSWMMVisLayer;
    void populateScene(QGraphicsScene *, const MapExtent &, const SpatialReferenceSystem *) override {}
};

class NumberStyleTestObject : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double amount MEMBER amount)
public:
    double amount = 0.0;
};

class ReorderedStyleTestLayer : public GeneralStyleTestLayer
{
public:
    NumberStyleTestObject first, second;
    bool reverse = false;
    std::vector<std::unique_ptr<openswmmvis::ui::ILayerStyleSubject>> styleSubjects() override
    {
        std::vector<std::unique_ptr<openswmmvis::ui::ILayerStyleSubject>> result;
        auto add = [&](NumberStyleTestObject *object, const QString &id) {
            result.push_back(std::make_unique<openswmmvis::ui::LayerStyleSubject>(id, object, id));
        };
        auto *self = this;
        if (reverse) { add(&self->second, "second"); add(&self->first, "first"); }
        else { add(&self->first, "first"); add(&self->second, "second"); }
        return result;
    }
};

class TestLayerStyleDialogTransactions : public QObject
{
    Q_OBJECT
private slots:
    void dynamicSublayerCancelRestoresStructure()
    {
        SWMM2DResultsLayer layer; QUndoStack undo;
        const int before=layer.sublayers().size();
        LayerStyleDialog dialog(&layer,{},nullptr,&undo);
        auto *sub=layer.addResultSublayer("surface:unavailable"); QVERIFY(sub);
        dialog.reject(); QCOMPARE(layer.sublayers().size(),before); QCOMPARE(undo.count(),0);
    }
    void dynamicSublayerApplyCancelUndoRestoresIdentity()
    {
        SWMM2DResultsLayer layer; QUndoStack undo;
        const int before=layer.sublayers().size();
        LayerStyleDialog dialog(&layer,{},nullptr,&undo);
        auto *sub=layer.addResultSublayer("surface:unavailable"); QVERIFY(sub);
        const QString id=sub->id(); sub->setOpacity(.37); sub->setVisible(false);
        sub->fillStyle()->setMissingColor(Qt::red);
        dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->click();
        QVERIFY(layer.removeResultSublayer(id)); dialog.reject();
        auto *restored=qobject_cast<OpenSWMM::Render::ResultScalarSublayer *>(
            OpenSWMM::Render::ISublayerHost::findSublayer(layer,id));
        QVERIFY(restored); QCOMPARE(restored->opacity(),.37); QVERIFY(!restored->isVisible());
        QCOMPARE(restored->fillStyle()->missingColor(),QColor(Qt::red));
        QCOMPARE(undo.count(),1); undo.undo(); QCOMPARE(layer.sublayers().size(),before);
        undo.redo(); restored=qobject_cast<OpenSWMM::Render::ResultScalarSublayer *>(
            OpenSWMM::Render::ISublayerHost::findSublayer(layer,id));
        QVERIFY(restored); QCOMPARE(restored->variableKey(),QStringLiteral("surface:unavailable"));
        QCOMPARE(restored->opacity(),.37);
    }

    void undoTracksSubjectsByStableIdentity()
    {
        ReorderedStyleTestLayer layer;
        layer.first.amount = 1.0; layer.second.amount = 2.0;
        QUndoStack undo;
        LayerStyleDialog dialog(&layer, {}, nullptr, &undo);
        layer.first.amount = 10.0; layer.second.amount = 20.0;
        dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(undo.count(), 1);
        layer.reverse = true;
        undo.undo();
        QCOMPARE(layer.first.amount, 1.0); QCOMPARE(layer.second.amount, 2.0);
        undo.redo();
        QCOMPARE(layer.first.amount, 10.0); QCOMPARE(layer.second.amount, 20.0);
    }
    void invalidClassificationDraftCannotBeAccepted()
    {
        SWMM2DResultsLayer layer;
        QUndoStack undo;
        LayerStyleDialog dialog(&layer, {}, nullptr, &undo);
        QTableView *table = nullptr;
        for (auto *editor : dialog.findChildren<openswmmvis::ui::ClassificationEditor *>()) {
            auto *candidate = editor->findChild<QTableView *>();
            if (candidate && candidate->model()->rowCount() > 1) { table = candidate; break; }
        }
        QVERIFY(table);
        const QModelIndex index = table->model()->index(0, 1);
        const QString original = index.data().toString();
        QVERIFY(table->model()->setData(index, QStringLiteral("nan")));
        dialog.show();
        dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        QVERIFY(dialog.isVisible()); QCOMPARE(undo.count(), 0);
        QVERIFY(table->model()->setData(index, original));
        dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        QVERIFY(!dialog.isVisible());
    }
    void externalGeneralChangesArePreserved_data()
    {
        QTest::addColumn<bool>("accept");
        QTest::newRow("cancel") << false;
        QTest::newRow("accept-untouched") << true;
    }
    void externalGeneralChangesArePreserved()
    {
        QFETCH(bool, accept);
        GeneralStyleTestLayer layer("Original");
        QUndoStack undo;
        LayerStyleDialog dialog(&layer, {}, nullptr, &undo);
        layer.setName("External name"); layer.setVisible(false); layer.setOpacity(0.47);
        dialog.findChild<QDialogButtonBox *>()->button(accept ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)->click();
        QCOMPARE(layer.name(), QStringLiteral("External name"));
        QVERIFY(!layer.isVisible()); QCOMPARE(layer.opacity(), 0.47);
        QCOMPARE(undo.count(), 0);
    }
    void untouchedGeneralOpacityRetainsPrecision()
    {
        GeneralStyleTestLayer layer("Precision"); layer.setOpacity(0.6543210987654);
        LayerStyleDialog dialog(&layer);
        dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->click();
        QCOMPARE(layer.opacity(), 0.6543210987654);
    }
    void generalEditIsUndoableWithoutStyleSubjects()
    {
        GeneralStyleTestLayer layer("Original");
        QUndoStack undo;
        LayerStyleDialog dialog(&layer, {}, nullptr, &undo);
        QLineEdit *name = nullptr;
        for (auto *candidate : dialog.findChildren<QLineEdit *>())
            if (candidate->text() == "Original") { name = candidate; break; }
        QVERIFY(name); name->setText("Accepted name");
        dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(layer.name(), QStringLiteral("Accepted name"));
        QCOMPARE(undo.count(), 1);
        undo.undo(); QCOMPARE(layer.name(), QStringLiteral("Original"));
        undo.redo(); QCOMPARE(layer.name(), QStringLiteral("Accepted name"));
    }
    void cancelPathsRestorePreview_data()
    {
        QTest::addColumn<QString>("path");
        QTest::newRow("button") << QStringLiteral("button");
        QTest::newRow("escape") << QStringLiteral("escape");
        QTest::newRow("window-close") << QStringLiteral("close");
        QTest::newRow("reject-api") << QStringLiteral("reject");
    }
    void cancelPathsRestorePreview()
    {
        QFETCH(QString, path);
        SWMM2DResultsLayer layer;
        QUndoStack undo;
        auto *style = layer.contourBandSublayer()->bandStyle();
        const QColor original = style->lowColor();
        LayerStyleDialog dialog(&layer, {}, nullptr, &undo);
        dialog.show();
        style->setLowColor(QColor("#d34a7f"));
        if (path == "button") dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->click();
        else if (path == "escape") QTest::keyClick(&dialog, Qt::Key_Escape);
        else if (path == "close") dialog.close();
        else dialog.reject();
        QCOMPARE(style->lowColor(), original);
        QCOMPARE(undo.count(), 0);
    }
    void applyThenCancelHasSingleReversibleCommit()
    {
        SWMM2DResultsLayer layer;
        QUndoStack undo;
        auto *style = layer.contourBandSublayer()->bandStyle();
        const QColor original = style->lowColor();
        LayerStyleDialog dialog(&layer, {}, nullptr, &undo);
        auto *buttons = dialog.findChild<QDialogButtonBox *>(); QVERIFY(buttons);
        style->setLowColor(QColor("#d34a7f"));
        buttons->button(QDialogButtonBox::Apply)->click();
        style->setLowColor(QColor("#1c527e"));
        buttons->button(QDialogButtonBox::Cancel)->click();
        QCOMPARE(style->lowColor(), QColor("#d34a7f"));
        QCOMPARE(undo.count(), 1);
        undo.undo(); QCOMPARE(style->lowColor(), original);
        undo.redo(); QCOMPARE(style->lowColor(), QColor("#d34a7f"));
    }
    void repeatedApplyAndAcceptAreOneTransaction()
    {
        SWMM2DResultsLayer layer;
        QUndoStack undo;
        auto *style = layer.contourBandSublayer()->bandStyle();
        const QColor original = style->lowColor();
        LayerStyleDialog dialog(&layer, {}, nullptr, &undo);
        auto *buttons = dialog.findChild<QDialogButtonBox *>(); QVERIFY(buttons);
        style->setLowColor(QColor("#d34a7f")); buttons->button(QDialogButtonBox::Apply)->click();
        style->setLowColor(QColor("#1c527e")); buttons->button(QDialogButtonBox::Apply)->click();
        buttons->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(undo.count(), 1);
        undo.undo(); QCOMPARE(style->lowColor(), original);
        undo.redo(); QCOMPARE(style->lowColor(), QColor("#1c527e"));
    }
    void deletedLayerDisablesEditor()
    {
        auto layer = std::make_unique<SWMM2DResultsLayer>();
        QUndoStack undo;
        LayerStyleDialog dialog(layer.get(), {}, nullptr, &undo);
        dialog.show(); layer.reset();
        QVERIFY(!dialog.isEnabled()); QVERIFY(!dialog.isVisible());
    }
    void deletedUndoOwnerClosesSafely()
    {
        SWMM2DResultsLayer layer;
        auto undo = std::make_unique<QUndoStack>();
        LayerStyleDialog dialog(&layer, {}, nullptr, undo.get());
        dialog.show(); undo.reset();
        QVERIFY(!dialog.isEnabled()); QVERIFY(!dialog.isVisible());
    }
};
QTEST_MAIN(TestLayerStyleDialogTransactions)
#include "test_layerstyledialog_transactions.moc"
