// Capture the production AttributeTablePanel using a disposable drainage model.
// Render native widgets at 2x, as FigureCapture does; include the actual popup
// at its widget position so the suggestion list retains its query-bar context.

/**
 * @file      capture_table_queries.cpp
 * @brief     Tests for capture table queries.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "layers/swmmmodellayer.h"
#include "selection/selectionmanager.h"
#include "ui/panels/attributetablepanel.h"
#include "ui/theme/thememanager.h"
#include "core/unitsystem.h"
#include <QApplication>
#include <QComboBox>
#include <QCompleter>
#include <QDir>
#include <QHeaderView>
#include <QLineEdit>
#include <QPainter>
#include <QTableView>
#include <QTest>
#include <QDebug>

/**
 *
 * @brief Render the panel and optional visible popup into a two-times-resolution PNG.
 * @param panel Borrowed live widget, painted synchronously on its GUI thread.
 * @param popup Optional borrowed widget whose global position is mapped into panel
 * coordinates; must remain valid throughout rendering.
 * @param path Borrowed writable output image path; parent must already exist.
 * @return False when resulting pixel width exceeds 1600 or image save fails, true
 * on save success. Extends capture bounds to contain popup, resets image DPR to 1
 * and may replace the file. Does not show widgets, process events or change ownership.
 *
 */
bool capture(QWidget &panel, QWidget *popup, const QString &path)
{
    QRect region(QPoint(), panel.size());
    QPoint offset;
    if (popup) {
        offset = panel.mapFromGlobal(popup->mapToGlobal(QPoint()));
        region = region.united(QRect(offset, popup->size()));
    }
    QPixmap pixels(region.size() * 2);
    pixels.setDevicePixelRatio(2);
    pixels.fill(Qt::transparent);
    QPainter painter(&pixels);
    panel.render(&painter, -region.topLeft());
    if (popup)
        popup->render(&painter, offset - region.topLeft());
    painter.end();
    auto image = pixels.toImage();
    image.setDevicePixelRatio(1);
    if (image.width() > 1600)
        return false;
    return image.save(path);
}

/**
 *
 * @brief Capture production attribute-table query and completion screenshots.
 * @param argc Exactly 3 required after QApplication argument handling.
 * @param argv Process strings: program, disposable drainage model .inp and writable
 * output directory, decoded in local 8-bit encoding. Qt may consume its own options.
 * @return 0 after expected two-row query/selection/order and both captures; 1 usage,
 * 2 model load, 3 missing controls, 4 missing Length column, 5 query mismatch,
 * 6 first capture, 7 completion popup or 8 second capture failure.
 * Uses Fusion/light theme and manual-figure settings identity, processes GUI events,
 * creates output directory and replaces two PNGs. Requires the intended eleven-conduit
 * fixture; does not save/edit its engine model. Active UnitSystem is cleared only
 * on success; early returns terminate the standalone process with it still registered.
 *
 */
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setOrganizationName("SWMMVisManualFigures");
    app.setApplicationName("TableQueries");
    app.setStyle("Fusion");
    openswmmvis::ui::ThemeManager::instance()->setMode(openswmmvis::ui::ThemeManager::Mode::Light);
    if (argc != 3)
        return 1;
    SWMMModelLayer model(QString::fromLocal8Bit(argv[1]), nullptr);
    QList<QString> warnings, errors;
    if (!model.loadModel(warnings, errors)) {
        qCritical() << errors;
        return 2;
    }
    UnitSystem units;
    units.syncFromEngine(model.engine());
    UnitSystem::setActiveProject(&units);
    SelectionManager selection;
    AttributeTablePanel panel;
    panel.setProject(&model, &selection, nullptr);
    auto *source = panel.findChild<QComboBox *>();
    auto *table = panel.findChild<QTableView *>();
    auto *query = panel.findChild<QLineEdit *>("attributeQuery");
    if (!source || !table || !query)
        return 3;
    source->setCurrentIndex(source->findData(static_cast<int>(SWMMModelLayer::CatConduits)));
    panel.resize(790, 245);
    panel.show();
    QTest::qWait(100);
    int length = -1;
    for (int col = 0; col < table->model()->columnCount(); ++col)
        if (table->model()->headerData(col, Qt::Horizontal).toString().startsWith("Length"))
            length = col;
    if (length < 0)
        return 4;
    table->horizontalHeader()->resizeSection(length, 130);
    query->setText("Name NOT LIKE 'TEMP%' AND Length BETWEEN 10 AND 100");
    QMetaObject::invokeMethod(&panel, "onQueryApplyClicked");
    table->sortByColumn(length, Qt::AscendingOrder);
    table->scrollTo(table->model()->index(0, length), QAbstractItemView::EnsureVisible);
    QTest::qWait(100);
    if (table->model()->rowCount() != 2 || selection.size() != 2 ||
        table->model()->index(0, 0).data().toString() != "C11" ||
        table->model()->index(1, 0).data().toString() != "C7")
        return 5;
    const QString output = QString::fromLocal8Bit(argv[2]);
    QDir().mkpath(output);
    if (!capture(panel, nullptr, output + "/11_query_exclusion_sorted.png"))
        return 6;
    query->setFocus();
    query->clear();
    QTest::keyClicks(query, "Len");
    auto *completer = query->findChild<QCompleter *>();
    QTest::keyClick(query, Qt::Key_Space, Qt::ControlModifier);
    QTest::qWait(100);
    if (!completer || !completer->popup()->isVisible() || completer->completionCount() < 1)
        return 7;
    if (!capture(panel, completer->popup(), output + "/11_query_completion.png"))
        return 8;
    qInfo()
        << "Captured actual panel and completion popup; two of eleven conduits matched and selected."
        << "Sorted lengths:" << table->model()->index(0, length).data()
        << table->model()->index(1, length).data();
    UnitSystem::setActiveProject(nullptr);
    return 0;
}
