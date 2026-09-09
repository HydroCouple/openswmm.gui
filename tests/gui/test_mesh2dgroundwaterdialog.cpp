/*!
 * \file   test_mesh2dgroundwaterdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * GG1 (2026-09-07): this dialog is no longer a preview. The two-zone kernel
 * landed in the engine, so the dialog now edits [2D_AQUIFER_OPTIONS], the
 * per-scope [2D_AQUIFER] rows and the [2D_AQUIFER_NODE] beds against a live
 * engine, and carries a read-only state page.
 *
 * Two contracts survive that change, and they are what these tests hold:
 *
 *   1. WITHOUT an engine nothing is editable. The dialog is reachable from the
 *      mesh toolbar before a model is open, and a field that accepted values
 *      with nowhere to write them would discard them silently.
 *   2. The soil and closure vocabularies are INP tokens AND the wire order of
 *      the engine's enums, so they are asserted against the engine's own
 *      SWMM_GW2D_SOIL_* / SWMM_GW2D_CLOSURE_* codes rather than against
 *      whatever the dialog happens to return.
 */
#include <QtTest>

#include "ui/dialogs/mesh2dgroundwaterdialog.h"

#include <QAbstractSpinBox>
#include <QComboBox>
#include <QTabWidget>

using openswmmvis::ui::Mesh2DGroundwaterDialog;

class TestMesh2DGroundwaterDialog : public QObject
{
    Q_OBJECT

private slots:
    /*! With no engine there is nowhere to write, so every input stays off. */
    void allInputsAreDisabledWithoutAnEngine()
    {
        Mesh2DGroundwaterDialog dlg(nullptr);
        const auto spins = dlg.findChildren<QAbstractSpinBox *>();
        QVERIFY2(!spins.isEmpty(), "expected the parameter fields to exist");
        for (QAbstractSpinBox *s : spins)
            QVERIFY2(!s->isEnabled(),
                     qPrintable(QStringLiteral("spin box '%1' is editable")
                                    .arg(s->objectName())));

        const auto combos = dlg.findChildren<QComboBox *>();
        QVERIFY2(!combos.isEmpty(), "expected the model/closure selectors");
        for (QComboBox *c : combos)
            QVERIFY2(!c->isEnabled(),
                     qPrintable(QStringLiteral("combo '%1' is editable")
                                    .arg(c->objectName())));
    }

    /*! Each toolbar entry opens the dialog on its own page. Asserted by tab
     *  TEXT: the Page enum and the tab order are deliberately independent
     *  (Options is built first but is not the default page), so comparing
     *  indices here would only re-state the switch it is meant to check. */
    void opensOnTheRequestedPage()
    {
        Mesh2DGroundwaterDialog params(
            nullptr, nullptr, Mesh2DGroundwaterDialog::Page::AquiferProperties);
        auto *tabsA = params.findChild<QTabWidget *>();
        QVERIFY(tabsA);
        QCOMPARE(tabsA->tabText(tabsA->currentIndex()),
                 QStringLiteral("Aquifer"));

        Mesh2DGroundwaterDialog state(
            nullptr, nullptr, Mesh2DGroundwaterDialog::Page::State);
        auto *tabsB = state.findChild<QTabWidget *>();
        QVERIFY(tabsB);
        QCOMPARE(tabsB->tabText(tabsB->currentIndex()),
                 QStringLiteral("State"));
    }

    /*! The vocabularies are the engine's enums in the engine's order — they go
     *  out as INP tokens and come back as indices, so a reordering here would
     *  silently re-label every existing aquifer row. */
    void vocabulariesMatchTheEngineEnums()
    {
        // openswmm_gw2d.h: RUSSO 0, GARDNER 1, BROOKS_COREY 2, VAN_GENUCHTEN 3.
        QCOMPARE(Mesh2DGroundwaterDialog::soilModelTokens(),
                 (QStringList{QStringLiteral("RUSSO"), QStringLiteral("GARDNER"),
                              QStringLiteral("BROOKS_COREY"),
                              QStringLiteral("VAN_GENUCHTEN")}));
        // openswmm_gw2d.h: AUTO -1, CLOSED_FORM 0, ENSLAVED 1, SIGMA 2. AUTO
        // leads the list because it is the sentinel the engine resolves.
        QCOMPARE(Mesh2DGroundwaterDialog::closureTokens(),
                 (QStringList{QStringLiteral("AUTO"),
                              QStringLiteral("CLOSED_FORM"),
                              QStringLiteral("ENSLAVED"),
                              QStringLiteral("SIGMA")}));
    }
};

QTEST_MAIN(TestMesh2DGroundwaterDialog)
#include "test_mesh2dgroundwaterdialog.moc"
