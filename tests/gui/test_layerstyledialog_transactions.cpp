/**
 * @file      test_layerstyledialog_transactions.cpp
 * @brief     Tests for layerstyledialog transactions.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

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
    /**
     *
     * @brief Provide a deliberate no-op scene hook for general-property dialog tests.
     * @param scene Borrowed scene, unused and not retained.
     * @param extent Borrowed map bounds, unused.
     * @param canvasSrs Borrowed optional destination CRS, unused and not retained.
     * Creates no items, caches or file output and does not mutate the fixture layer.
     *
     */
    void populateScene(QGraphicsScene *scene, const MapExtent &extent,
                       const SpatialReferenceSystem *canvasSrs) override
    {
    }
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
    /**
     *
     * @brief Build fresh property subjects in normal/reversed fixture order.
     * @return Owned wrappers borrowing member objects first/second, keyed by stable
     * IDs first/second. reverse changes enumeration order only; objects remain members
     * of this layer and must outlive wrappers. No values, files or undo history are edited.
     *
     */
    std::vector<std::unique_ptr<openswmmvis::ui::ILayerStyleSubject>> styleSubjects() override
    {
        std::vector<std::unique_ptr<openswmmvis::ui::ILayerStyleSubject>> result;
        auto add = [&](NumberStyleTestObject *object, const QString &id) {
            result.push_back(std::make_unique<openswmmvis::ui::LayerStyleSubject>(id, object, id));
        };
        auto *self = this;
        if (reverse) {
            add(&self->second, "second");
            add(&self->first, "first");
        } else {
            add(&self->first, "first");
            add(&self->second, "second");
        }
        return result;
    }
};

class TestLayerStyleDialogTransactions : public QObject
{
    Q_OBJECT
private slots:
    /**
     *
     * @brief Require rejecting a dialog after adding an unavailable scalar sublayer to restore
     * original sublayer count without undo entries.
     *
     */
    void dynamicSublayerCancelRestoresStructure()
    {
        SWMM2DResultsLayer layer;
        QUndoStack undo;
        const int before = layer.sublayers().size();
        LayerStyleDialog dialog(&layer, {}, nullptr, &undo);
        auto *sub = layer.addResultSublayer("surface:unavailable");
        QVERIFY(sub);
        dialog.reject();
        QCOMPARE(layer.sublayers().size(), before);
        QCOMPARE(undo.count(), 0);
    }
    /**
     *
     * @brief Require Apply to commit scalar sublayer identity/style, later Cancel to restore that
     * checkpoint and undo/redo to remove/recreate it.
     *
     */
    void dynamicSublayerApplyCancelUndoRestoresIdentity()
    {
        SWMM2DResultsLayer layer;
        QUndoStack undo;
        const int before = layer.sublayers().size();
        LayerStyleDialog dialog(&layer, {}, nullptr, &undo);
        auto *sub = layer.addResultSublayer("surface:unavailable");
        QVERIFY(sub);
        const QString id = sub->id();
        sub->setOpacity(.37);
        sub->setVisible(false);
        sub->fillStyle()->setMissingColor(Qt::red);
        dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->click();
        QVERIFY(layer.removeResultSublayer(id));
        dialog.reject();
        auto *restored = qobject_cast<OpenSWMM::Render::ResultScalarSublayer *>(
            OpenSWMM::Render::ISublayerHost::findSublayer(layer, id));
        QVERIFY(restored);
        QCOMPARE(restored->opacity(), .37);
        QVERIFY(!restored->isVisible());
        QCOMPARE(restored->fillStyle()->missingColor(), QColor(Qt::red));
        QCOMPARE(undo.count(), 1);
        undo.undo();
        QCOMPARE(layer.sublayers().size(), before);
        undo.redo();
        restored = qobject_cast<OpenSWMM::Render::ResultScalarSublayer *>(
            OpenSWMM::Render::ISublayerHost::findSublayer(layer, id));
        QVERIFY(restored);
        QCOMPARE(restored->variableKey(), QStringLiteral("surface:unavailable"));
        QCOMPARE(restored->opacity(), .37);
    }

    /**
     *
     * @brief Require undo/redo to restore the correct property object after subject enumeration
     * order reverses.
     *
     */
    void undoTracksSubjectsByStableIdentity()
    {
        ReorderedStyleTestLayer layer;
        layer.first.amount = 1.0;
        layer.second.amount = 2.0;
        QUndoStack undo;
        LayerStyleDialog dialog(&layer, {}, nullptr, &undo);
        layer.first.amount = 10.0;
        layer.second.amount = 20.0;
        dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(undo.count(), 1);
        layer.reverse = true;
        undo.undo();
        QCOMPARE(layer.first.amount, 1.0);
        QCOMPARE(layer.second.amount, 2.0);
        undo.redo();
        QCOMPARE(layer.first.amount, 10.0);
        QCOMPARE(layer.second.amount, 20.0);
    }
    /**
     *
     * @brief Require a NaN class-edge draft to keep the live dialog open with no undo command,
     * then permit acceptance after correction.
     *
     */
    void invalidClassificationDraftCannotBeAccepted()
    {
        SWMM2DResultsLayer layer;
        QUndoStack undo;
        LayerStyleDialog dialog(&layer, {}, nullptr, &undo);
        QTableView *table = nullptr;
        for (auto *editor : dialog.findChildren<openswmmvis::ui::ClassificationEditor *>()) {
            auto *candidate = editor->findChild<QTableView *>();
            if (candidate && candidate->model()->rowCount() > 1) {
                table = candidate;
                break;
            }
        }
        QVERIFY(table);
        const QModelIndex index = table->model()->index(0, 1);
        const QString original = index.data().toString();
        QVERIFY(table->model()->setData(index, QStringLiteral("nan")));
        dialog.show();
        dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        QVERIFY(dialog.isVisible());
        QCOMPARE(undo.count(), 0);
        QVERIFY(table->model()->setData(index, original));
        dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        QVERIFY(!dialog.isVisible());
    }
    /**
     *
     * @brief Supply Cancel and untouched-Accept variants for external general-property
     * preservation.
     *
     */
    void externalGeneralChangesArePreserved_data()
    {
        QTest::addColumn<bool>("accept");
        QTest::newRow("cancel") << false;
        QTest::newRow("accept-untouched") << true;
    }
    /**
     *
     * @brief Require externally changed name/visibility/opacity to survive dialog closure without
     * an undo command when its general controls were untouched.
     *
     */
    void externalGeneralChangesArePreserved()
    {
        QFETCH(bool, accept);
        GeneralStyleTestLayer layer("Original");
        QUndoStack undo;
        LayerStyleDialog dialog(&layer, {}, nullptr, &undo);
        layer.setName("External name");
        layer.setVisible(false);
        layer.setOpacity(0.47);
        dialog.findChild<QDialogButtonBox *>()
            ->button(accept ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)
            ->click();
        QCOMPARE(layer.name(), QStringLiteral("External name"));
        QVERIFY(!layer.isVisible());
        QCOMPARE(layer.opacity(), 0.47);
        QCOMPARE(undo.count(), 0);
    }
    /**
     *
     * @brief Require Apply with untouched general opacity to preserve the full stored fractional
     * value.
     *
     */
    void untouchedGeneralOpacityRetainsPrecision()
    {
        GeneralStyleTestLayer layer("Precision");
        layer.setOpacity(0.6543210987654);
        LayerStyleDialog dialog(&layer);
        dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->click();
        QCOMPARE(layer.opacity(), 0.6543210987654);
    }
    /**
     *
     * @brief Require a general layer-name edit to create one reversible command even without
     * style subjects.
     *
     */
    void generalEditIsUndoableWithoutStyleSubjects()
    {
        GeneralStyleTestLayer layer("Original");
        QUndoStack undo;
        LayerStyleDialog dialog(&layer, {}, nullptr, &undo);
        QLineEdit *name = nullptr;
        for (auto *candidate : dialog.findChildren<QLineEdit *>())
            if (candidate->text() == "Original") {
                name = candidate;
                break;
            }
        QVERIFY(name);
        name->setText("Accepted name");
        dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(layer.name(), QStringLiteral("Accepted name"));
        QCOMPARE(undo.count(), 1);
        undo.undo();
        QCOMPARE(layer.name(), QStringLiteral("Original"));
        undo.redo();
        QCOMPARE(layer.name(), QStringLiteral("Accepted name"));
    }
    /**
     *
     * @brief Supply Cancel button, Escape, window close and reject API closure paths.
     *
     */
    void cancelPathsRestorePreview_data()
    {
        QTest::addColumn<QString>("path");
        QTest::newRow("button") << QStringLiteral("button");
        QTest::newRow("escape") << QStringLiteral("escape");
        QTest::newRow("window-close") << QStringLiteral("close");
        QTest::newRow("reject-api") << QStringLiteral("reject");
    }
    /**
     *
     * @brief Require every cancellation path to restore preview color and create no undo entries.
     * Shows native dialog and delivers the selected closure event; requires GUI support.
     *
     */
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
        if (path == "button")
            dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->click();
        else if (path == "escape")
            QTest::keyClick(&dialog, Qt::Key_Escape);
        else if (path == "close")
            dialog.close();
        else
            dialog.reject();
        QCOMPARE(style->lowColor(), original);
        QCOMPARE(undo.count(), 0);
    }
    /**
     *
     * @brief Require Apply then further preview/Cancel to retain one committed color transaction
     * reversible to the original.
     *
     */
    void applyThenCancelHasSingleReversibleCommit()
    {
        SWMM2DResultsLayer layer;
        QUndoStack undo;
        auto *style = layer.contourBandSublayer()->bandStyle();
        const QColor original = style->lowColor();
        LayerStyleDialog dialog(&layer, {}, nullptr, &undo);
        auto *buttons = dialog.findChild<QDialogButtonBox *>();
        QVERIFY(buttons);
        style->setLowColor(QColor("#d34a7f"));
        buttons->button(QDialogButtonBox::Apply)->click();
        style->setLowColor(QColor("#1c527e"));
        buttons->button(QDialogButtonBox::Cancel)->click();
        QCOMPARE(style->lowColor(), QColor("#d34a7f"));
        QCOMPARE(undo.count(), 1);
        undo.undo();
        QCOMPARE(style->lowColor(), original);
        undo.redo();
        QCOMPARE(style->lowColor(), QColor("#d34a7f"));
    }
    /**
     *
     * @brief Require repeated Apply plus Ok to coalesce into one undo transaction from original
     * to final color.
     *
     */
    void repeatedApplyAndAcceptAreOneTransaction()
    {
        SWMM2DResultsLayer layer;
        QUndoStack undo;
        auto *style = layer.contourBandSublayer()->bandStyle();
        const QColor original = style->lowColor();
        LayerStyleDialog dialog(&layer, {}, nullptr, &undo);
        auto *buttons = dialog.findChild<QDialogButtonBox *>();
        QVERIFY(buttons);
        style->setLowColor(QColor("#d34a7f"));
        buttons->button(QDialogButtonBox::Apply)->click();
        style->setLowColor(QColor("#1c527e"));
        buttons->button(QDialogButtonBox::Apply)->click();
        buttons->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(undo.count(), 1);
        undo.undo();
        QCOMPARE(style->lowColor(), original);
        undo.redo();
        QCOMPARE(style->lowColor(), QColor("#1c527e"));
    }
    /**
     *
     * @brief Require destruction of the edited layer to hide/disable its open style dialog
     * safely.
     *
     */
    void deletedLayerDisablesEditor()
    {
        auto layer = std::make_unique<SWMM2DResultsLayer>();
        QUndoStack undo;
        LayerStyleDialog dialog(layer.get(), {}, nullptr, &undo);
        dialog.show();
        layer.reset();
        QVERIFY(!dialog.isEnabled());
        QVERIFY(!dialog.isVisible());
    }
    /**
     *
     * @brief Require destruction of the borrowed undo stack to hide/disable the open style dialog
     * safely.
     *
     */
    void deletedUndoOwnerClosesSafely()
    {
        SWMM2DResultsLayer layer;
        auto undo = std::make_unique<QUndoStack>();
        LayerStyleDialog dialog(&layer, {}, nullptr, undo.get());
        dialog.show();
        undo.reset();
        QVERIFY(!dialog.isEnabled());
        QVERIFY(!dialog.isVisible());
    }
};
QTEST_MAIN(TestLayerStyleDialogTransactions)
#include "test_layerstyledialog_transactions.moc"
