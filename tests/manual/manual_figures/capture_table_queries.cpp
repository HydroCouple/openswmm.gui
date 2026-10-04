// Capture the production AttributeTablePanel using a disposable drainage model.
// Render native widgets at 2x, as FigureCapture does; include the actual popup
// at its widget position so the suggestion list retains its query-bar context.
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
    if (popup) popup->render(&painter, offset - region.topLeft());
    painter.end();
    auto image = pixels.toImage();
    image.setDevicePixelRatio(1);
    if (image.width() > 1600) return false;
    return image.save(path);
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setOrganizationName("SWMMVisManualFigures");
    app.setApplicationName("TableQueries");
    app.setStyle("Fusion");
    openswmmvis::ui::ThemeManager::instance()->setMode(openswmmvis::ui::ThemeManager::Mode::Light);
    if (argc != 3) return 1;
    SWMMModelLayer model(QString::fromLocal8Bit(argv[1]), nullptr);
    QList<QString> warnings, errors;
    if (!model.loadModel(warnings, errors)) { qCritical() << errors; return 2; }
    UnitSystem units;
    units.syncFromEngine(model.engine());
    UnitSystem::setActiveProject(&units);
    SelectionManager selection;
    AttributeTablePanel panel;
    panel.setProject(&model, &selection, nullptr);
    auto *source = panel.findChild<QComboBox *>();
    auto *table = panel.findChild<QTableView *>();
    auto *query = panel.findChild<QLineEdit *>("attributeQuery");
    if (!source || !table || !query) return 3;
    source->setCurrentIndex(source->findData(static_cast<int>(SWMMModelLayer::CatConduits)));
    panel.resize(790, 245);
    panel.show();
    QTest::qWait(100);
    int length = -1;
    for (int col = 0; col < table->model()->columnCount(); ++col)
        if (table->model()->headerData(col, Qt::Horizontal).toString().startsWith("Length")) length = col;
    if (length < 0) return 4;
    table->horizontalHeader()->resizeSection(length, 130);
    query->setText("Name NOT LIKE 'TEMP%' AND Length BETWEEN 10 AND 100");
    QMetaObject::invokeMethod(&panel, "onQueryApplyClicked");
    table->sortByColumn(length, Qt::AscendingOrder);
    table->scrollTo(table->model()->index(0, length), QAbstractItemView::EnsureVisible);
    QTest::qWait(100);
    if (table->model()->rowCount() != 2 || selection.size() != 2
        || table->model()->index(0, 0).data().toString() != "C11"
        || table->model()->index(1, 0).data().toString() != "C7") return 5;
    const QString output = QString::fromLocal8Bit(argv[2]);
    QDir().mkpath(output);
    if (!capture(panel, nullptr, output + "/11_query_exclusion_sorted.png")) return 6;
    query->setFocus();
    query->clear();
    QTest::keyClicks(query, "Len");
    auto *completer = query->findChild<QCompleter *>();
    QTest::keyClick(query, Qt::Key_Space, Qt::ControlModifier);
    QTest::qWait(100);
    if (!completer || !completer->popup()->isVisible() || completer->completionCount() < 1) return 7;
    if (!capture(panel, completer->popup(), output + "/11_query_completion.png")) return 8;
    qInfo() << "Captured actual panel and completion popup; two of eleven conduits matched and selected."
            << "Sorted lengths:" << table->model()->index(0, length).data() << table->model()->index(1, length).data();
    UnitSystem::setActiveProject(nullptr);
    return 0;
}
