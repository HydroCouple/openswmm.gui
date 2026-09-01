/*!
 * \file   test_meshmincelldialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * The "Minimum Cell Size" group on the mesh-generation dialog's Quality tab
 * (MIN_CELL_SIZE_TESTING_HANDOFF_2026-08-17.md Phase 6, items 1-3).
 *
 * The invariant that matters most here is the DEFAULT: a project that never
 * touches this group must generate exactly the mesh it generated before the
 * feature existed, which means the spin box has to come up at 0 and every
 * dependent control has to be inert until it does not.
 *
 * The widgets carry no object names, so they are located structurally — the
 * group box by title, then its children in layout order.  That is deliberate:
 * naming them purely for a test would be a production change for no runtime
 * benefit, and the structure IS the thing under test.
 *
 * NOT covered here: collectInputs() is private and there is no read-back path
 * for PipelineInputs, so "every new field round-trips" (handoff Phase 6 item
 * 4) cannot be asserted without widening the dialog's API.  The policy fields
 * those widgets feed are covered directly in test_pslgminsize.cpp.
 */
#include "project/openswmmvisworkspace.h"
#include "swmmvisprojectwindow.h"
#include "ui/dialogs/meshgenerationdialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QTest>

#include <cmath>

namespace {

QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

QString projectFixturePath()
{
    return QDir(dataDir()).filePath(QStringLiteral("typed_selection_fixture.inp"));
}

/*! The "Minimum Cell Size" group, located by its title. */
QGroupBox *minCellGroup(QWidget *dlg)
{
    for (QGroupBox *g : dlg->findChildren<QGroupBox *>())
        if (g->title() == QLatin1String("Minimum Cell Size")) return g;
    return nullptr;
}

/*! The dialog-wide "Max triangle area" spin, identified by the tooltip
 *  updateUnitDisplay() gives it. */
QDoubleSpinBox *maxAreaSpin(QWidget *dlg)
{
    for (QDoubleSpinBox *s : dlg->findChildren<QDoubleSpinBox *>())
        if (s->toolTip().contains(QLatin1String("Upper bound on triangle area")))
            return s;
    return nullptr;
}

/*! The derived read-out under the group: the only wrapped label in it. */
QLabel *derivedLabel(QGroupBox *g)
{
    for (QLabel *l : g->findChildren<QLabel *>())
        if (l->wordWrap()) return l;
    return nullptr;
}

} // namespace

class TestMeshMinCellDialog : public QObject
{
    Q_OBJECT

private slots:

    void initTestCase()
    {
        m_workspace = OpenSWMMVisWorkspace::newInstance(QString(), nullptr);
        QVERIFY(m_workspace != nullptr);
        QVERIFY(QFile::exists(projectFixturePath()));
        m_window = new SWMMVisProjectWindow(m_workspace, projectFixturePath(),
                                            nullptr);
        QVERIFY(m_window != nullptr);
        QTest::qWait(50);

        m_dlg = new MeshGenerationDialog(m_window, m_window);
        QVERIFY(m_dlg != nullptr);
        m_group = minCellGroup(m_dlg);
        QVERIFY2(m_group != nullptr, "the Minimum Cell Size group is missing");

        m_spins  = m_group->findChildren<QDoubleSpinBox *>();
        m_boxes  = m_group->findChildren<QCheckBox *>();
        m_button = m_group->findChild<QPushButton *>();
        QCOMPARE(m_spins.size(), 2);      // minimum cell size, trim angle
        QCOMPARE(m_boxes.size(), 4);      // enforce, trim at nodes, drop holes, cleanup
        QVERIFY(m_button != nullptr);
        m_minCell   = m_spins[0];
        m_trimAngle = m_spins[1];
        for (QCheckBox *b : std::as_const(m_boxes))
            if (b->text().startsWith(QLatin1String("Enforce"))) m_enforce = b;
        QVERIFY2(m_enforce != nullptr, "the Enforce checkbox is missing");
    }

    void cleanupTestCase()
    {
        delete m_dlg;
        if (m_window) { m_window->close(); delete m_window; }
    }

    /*! Item 1 — off by default, and everything downstream inert. */
    void defaultsToOffWithDependentsDisabled()
    {
        QCOMPARE(m_minCell->value(), 0.0);
        QCOMPARE(m_minCell->specialValueText(), QStringLiteral("(off)"));
        QVERIFY2(!m_trimAngle->isEnabled(), "trim angle live with h = 0");
        for (QCheckBox *b : std::as_const(m_boxes))
            QVERIFY2(!b->isEnabled(),
                     qPrintable(QStringLiteral("'%1' live with h = 0").arg(b->text())));

        // The trim angle still carries the policy default, ready for when it
        // is switched on.
        QCOMPARE(m_trimAngle->value(), mesh::pslg::MinSizePolicy{}.trimAngleDeg);
    }

    void enablingASizeEnablesTheDependents()
    {
        m_minCell->setValue(3.0);
        QVERIFY(m_trimAngle->isEnabled());
        for (QCheckBox *b : std::as_const(m_boxes)) QVERIFY(b->isEnabled());

        m_minCell->setValue(0.0);
        QVERIFY(!m_trimAngle->isEnabled());
        for (QCheckBox *b : std::as_const(m_boxes)) QVERIFY(!b->isEnabled());
    }

    /*! Item 2 — the derived read-out tracks the spin, live. */
    void derivedLabelReportsAreaAndWeldRadius()
    {
        QLabel *lab = derivedLabel(m_group);
        QVERIFY(lab != nullptr);

        m_minCell->setValue(0.0);
        QVERIFY2(lab->text().contains(QLatin1String("Off")),
                 qPrintable(lab->text()));

        m_minCell->setValue(4.0);
        mesh::pslg::MinSizePolicy p;
        p.minCellSize = 4.0;
        p.resolveDefaults();
        const QString area = QString::number(p.minTriangleArea(), 'g', 4);
        const QString weld = QString::number(p.weldRadius, 'g', 4);
        QVERIFY2(lab->text().contains(area),
                 qPrintable(QStringLiteral("no A_min (%1) in: %2").arg(area, lab->text())));
        QVERIFY2(lab->text().contains(weld),
                 qPrintable(QStringLiteral("no weld radius (%1) in: %2").arg(weld, lab->text())));

        // It really is live: a new value must produce new numbers.
        const QString before = lab->text();
        m_minCell->setValue(9.0);
        QVERIFY(lab->text() != before);
    }

    /*! Item 2 — the min-angle hint appears only above 28 degrees. */
    void minAngleHintAppearsOnlyAboveTwentyEight()
    {
        QLabel *lab = derivedLabel(m_group);
        QVERIFY(lab != nullptr);
        m_minCell->setValue(4.0);

        // The min-angle spin is the dialog-wide one, outside this group.
        // QStringLiteral, not QLatin1String: the degree sign is two bytes in
        // this UTF-8 source and a Latin-1 view of it never matches.
        QDoubleSpinBox *angle = nullptr;
        for (QDoubleSpinBox *s : m_dlg->findChildren<QDoubleSpinBox *>())
            if (s != m_trimAngle && s->suffix().contains(QStringLiteral("°"))
                && s->maximum() <= 40.0 && s->maximum() > 28.0)
            { angle = s; break; }
        QVERIFY2(angle != nullptr, "min-angle spin not found");

        angle->setValue(26.0);
        QVERIFY(!lab->text().contains(QLatin1String("Min angle is")));
        angle->setValue(32.0);
        QVERIFY2(lab->text().contains(QLatin1String("Min angle is")),
                 qPrintable(lab->text()));
    }

    /*! V2 — Enforce defaults OFF (advisory), and the derived read-out says
     *  what turning it on means. */
    void enforceDefaultsOffAndUpdatesTheDerivedLabel()
    {
        QLabel *lab = derivedLabel(m_group);
        QVERIFY(lab != nullptr);

        QVERIFY2(!m_enforce->isChecked(), "Enforce must default off");

        m_minCell->setValue(4.0);
        QVERIFY(m_enforce->isEnabled());
        QVERIFY2(lab->text().contains(QLatin1String("never move")),
                 qPrintable(lab->text()));

        m_enforce->setChecked(true);
        QVERIFY2(lab->text().contains(QLatin1String("Enforce")),
                 qPrintable(lab->text()));
        QVERIFY(!lab->text().contains(QLatin1String("never move")));

        m_enforce->setChecked(false);
        m_minCell->setValue(0.0);
    }

    /*! V2 — the size-gradation spin (Triangle quality group) defaults to
     *  uniform and is live only when a max-area cap exists to relax. */
    void gradationDefaultsUniformAndNeedsAMaxArea()
    {
        QDoubleSpinBox *grad = nullptr;
        for (QDoubleSpinBox *s : m_dlg->findChildren<QDoubleSpinBox *>())
            if (s->specialValueText() == QLatin1String("(uniform)"))
            { grad = s; break; }
        QVERIFY2(grad != nullptr, "size-gradation spin not found");
        QCOMPARE(grad->value(), 0.0);

        QDoubleSpinBox *area = maxAreaSpin(m_dlg);
        QVERIFY(area != nullptr);
        area->setValue(0.0);
        QVERIFY2(!grad->isEnabled(), "gradation live with no area cap");
        area->setValue(50.0);
        QVERIFY(grad->isEnabled());
        area->setValue(0.0);
    }

    /*! Item 3 — Suggest derives h from the max triangle area. */
    void suggestComputesFromMaxArea()
    {
        QDoubleSpinBox *area = maxAreaSpin(m_dlg);
        QVERIFY2(area != nullptr, "Max triangle area spin not found");

        // With no cap there is nothing to derive from: leave the value alone.
        area->setValue(0.0);
        m_minCell->setValue(1.5);
        m_button->click();
        QCOMPARE(m_minCell->value(), 1.5);

        area->setValue(100.0);
        m_minCell->setValue(0.0);
        m_button->click();
        // A third of the side of the equilateral triangle with that area.
        const double side = std::sqrt(4.0 * 100.0 / std::sqrt(3.0));
        QVERIFY(std::abs(m_minCell->value() - side / 3.0) < 1e-3);
        QVERIFY(m_minCell->value() > 0.0);

        m_minCell->setValue(0.0);        // leave the dialog as we found it
    }

private:
    OpenSWMMVisWorkspace  *m_workspace = nullptr;
    SWMMVisProjectWindow  *m_window    = nullptr;
    MeshGenerationDialog  *m_dlg       = nullptr;
    QGroupBox             *m_group     = nullptr;
    QList<QDoubleSpinBox *> m_spins;
    QList<QCheckBox *>      m_boxes;
    QPushButton           *m_button    = nullptr;
    QDoubleSpinBox        *m_minCell   = nullptr;
    QDoubleSpinBox        *m_trimAngle = nullptr;
    QCheckBox             *m_enforce   = nullptr;
};

QTEST_MAIN(TestMeshMinCellDialog)
#include "test_meshmincelldialog.moc"
