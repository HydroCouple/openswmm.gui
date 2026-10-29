// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/meshresolutionwidget.h"
#include <QDockWidget>
#include <QHeaderView>
#include <QMainWindow>
#include <QStackedWidget>
#include <QTest>
#include <QTreeWidget>
#include <QDir>
#include <QDoubleSpinBox>
#include <limits>

class TestMeshSettingsDock : public QObject
{
    Q_OBJECT
private slots:
    void readableDockedAndFloatingNavigation()
    {
        MeshResolutionWidget widget;
        widget.addSettingsPage("Quads and regions", new QWidget);
        widget.addSettingsPage("Boundaries", new QWidget);
        widget.resize(900, 620);
        widget.show();
        auto *dock = widget.findChild<QDockWidget *>("meshSettingsNavigationDock");
        auto *categories = widget.findChild<QTreeWidget *>("meshSettingsCategories");
        auto *pages = widget.findChild<QStackedWidget *>("meshSettingsPages");
        auto *host = widget.findChild<QMainWindow *>("meshSettingsDockHost");
        QVERIFY(dock && categories && pages && host);
        QVERIFY(!host->isWindow());
        QVERIFY(!dock->isFloating());
        QVERIFY(dock->features().testFlag(QDockWidget::DockWidgetMovable));
        QVERIFY(dock->features().testFlag(QDockWidget::DockWidgetFloatable));
        // Regression: the viewport used to grow while its hidden text column
        // stayed at 100 px. Check the actual column before any font change.
        QTRY_COMPARE(categories->columnWidth(0), categories->viewport()->width());
        const int initialWidth = categories->columnWidth(0);
        host->resizeDocks({dock}, {initialWidth + 180}, Qt::Horizontal);
        QTRY_VERIFY(categories->columnWidth(0) > initialWidth + 100);
        QTRY_COMPARE(categories->columnWidth(0), categories->viewport()->width());
        widget.resize(1200, 620);
        QTRY_COMPARE(categories->columnWidth(0), categories->viewport()->width());
        for (int points : {12, 24}) {
            auto font = categories->font();
            font.setPointSize(points);
            categories->setFont(font);
            QTRY_COMPARE(categories->columnWidth(0), categories->viewport()->width());
            for (int i = 0; i < categories->topLevelItemCount(); ++i)
                QTRY_VERIFY(categories->viewport()->width() >=
                            categories->fontMetrics().horizontalAdvance(
                                categories->topLevelItem(i)->text(0)) +
                                8);
        }
        dock->setFloating(true);
        QTRY_VERIFY(dock->isFloating());
        categories->setCurrentItem(categories->topLevelItem(2));
        QCOMPARE(pages->currentIndex(), 2);
        host->addDockWidget(Qt::RightDockWidgetArea, dock);
        dock->setFloating(false);
        QTRY_VERIFY(!dock->isFloating());
        QCOMPARE(host->dockWidgetArea(dock), Qt::RightDockWidgetArea);
        categories->setCurrentItem(categories->topLevelItem(1));
        QCOMPARE(pages->currentIndex(), 1);
        QTest::qWait(100); // Allow redocking animation and repaint before the preview.
        QDir().mkpath(MESH_SETTINGS_TEST_OUTPUT);
        QVERIFY(widget.grab().save(QString(MESH_SETTINGS_TEST_OUTPUT) + "/quality-docked.png"));
    }

    void transitionAboveTenSurvivesEditingAndSerialization()
    {
        MeshResolutionWidget widget;
        for (double value : {25., 1000., 1e12, std::numeric_limits<double>::max()}) {
            mesh::ResolutionSettings settings;
            settings.transition = value;
            QVERIFY(settings.validate().isEmpty());
            widget.setSettings(settings);
            QCOMPARE(widget.transition->value(), value);
            QCOMPARE(widget.settings().transition, value);
            const auto restored = mesh::ResolutionSettings::fromJson(widget.settings().toJson());
            QCOMPARE(restored.transition, value);
            QVERIFY(restored.validate().isEmpty());
        }
        auto settings = widget.settings();
        settings.transition = std::numeric_limits<double>::infinity();
        QVERIFY(!settings.validate().isEmpty());
        settings.transition = 1;
        QVERIFY(!settings.validate().isEmpty());
    }
};
QTEST_MAIN(TestMeshSettingsDock)
#include "test_meshsettingsdock.moc"
