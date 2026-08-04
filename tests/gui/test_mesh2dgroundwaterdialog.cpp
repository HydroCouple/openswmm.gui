/*!
 * \file   test_mesh2dgroundwaterdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * The 2D two-zone groundwater dialog is a PREVIEW: it lays out the parameter
 * surface of the engine's draft [2D_AQUIFER] design so the eventual wiring is
 * mechanical, but nothing in it may be editable while the engine kernel is
 * missing. These tests hold that contract — an enabled input here would let a
 * user type values that are silently discarded.
 */
#include <QtTest>

#include "ui/dialogs/mesh2dgroundwaterdialog.h"

#include <QAbstractSpinBox>
#include <QComboBox>
#include <QLabel>
#include <QStackedWidget>
#include <QTabWidget>

using openswmmvis::ui::Mesh2DGroundwaterDialog;

class TestMesh2DGroundwaterDialog : public QObject
{
    Q_OBJECT

private slots:
    /*! Every numeric field and combo stays disabled — the dialog previews a
     *  kernel the engine does not have yet. */
    void allInputsAreDisabled()
    {
        Mesh2DGroundwaterDialog dlg;
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

    /*! The banner must say why, so the preview is never mistaken for a bug. */
    void bannerExplainsThePreviewState()
    {
        Mesh2DGroundwaterDialog dlg;
        bool explained = false;
        for (QLabel *l : dlg.findChildren<QLabel *>()) {
            const QString t = l->text();
            if (t.contains(QStringLiteral("2D_AQUIFER"))
                || t.contains(QStringLiteral("Preview"), Qt::CaseInsensitive)) {
                explained = true;
                break;
            }
        }
        QVERIFY2(explained, "no label explains that this is a preview");
    }

    /*! Both ribbon buttons open the same dialog on their own page. */
    void opensOnTheRequestedPage()
    {
        Mesh2DGroundwaterDialog params(
            nullptr, Mesh2DGroundwaterDialog::Page::AquiferProperties);
        auto *tabsA = params.findChild<QTabWidget *>();
        QVERIFY(tabsA);
        QCOMPARE(tabsA->currentIndex(), 0);

        Mesh2DGroundwaterDialog init(
            nullptr, Mesh2DGroundwaterDialog::Page::InitialConditions);
        auto *tabsB = init.findChild<QTabWidget *>();
        QVERIFY(tabsB);
        QCOMPARE(tabsB->currentIndex(), 1);
    }

    /*! The soil / closure vocabularies must match the draft section exactly —
     *  they become INP tokens the moment the kernel lands. */
    void vocabulariesMatchTheDraftSection()
    {
        QCOMPARE(Mesh2DGroundwaterDialog::soilModelTokens(),
                 (QStringList{QStringLiteral("GARDNER"), QStringLiteral("RUSSO"),
                              QStringLiteral("BROOKS_COREY"),
                              QStringLiteral("VAN_GENUCHTEN")}));
        QCOMPARE(Mesh2DGroundwaterDialog::closureTokens(),
                 (QStringList{QStringLiteral("CLOSED_FORM"),
                              QStringLiteral("KINEMATIC"),
                              QStringLiteral("ENSLAVED"),
                              QStringLiteral("AUTO")}));
    }

    /*! Each soil model's extra parameters must be laid out, so enabling them
     *  later is a flag flip rather than new UI work. */
    void everySoilModelHasAnExtraParameterPage()
    {
        Mesh2DGroundwaterDialog dlg;
        // By name: an unqualified search would find the tab widget's own
        // internal stack first.
        auto *stack = dlg.findChild<QStackedWidget *>(
            QStringLiteral("soilExtraStack"));
        QVERIFY(stack);
        QCOMPARE(stack->count(),
                 Mesh2DGroundwaterDialog::soilModelTokens().size());
    }
};

QTEST_MAIN(TestMesh2DGroundwaterDialog)
#include "test_mesh2dgroundwaterdialog.moc"
