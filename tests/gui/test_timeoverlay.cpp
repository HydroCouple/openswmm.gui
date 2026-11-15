/**
 * @file test_timeoverlay.cpp
 * @brief The map's animation date/time box: its style, text, drawing, placement, the animation
 *        toolbar's "Show date/time" checkbox, project save/restore and Export Map.
 *
 * Images for review go to TIME_OVERLAY_OUTPUT (tests/output/time_overlay_2026-11-14).
 */

#include <QtTest/QtTest>

#include <QApplication>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QFontInfo>
#include <QMdiArea>
#include <QMetaProperty>
#include <QMouseEvent>
#include <QTimeZone>

#include "animation/animationcontroller.h"
#include "map/mapcanvas.h"
#include "project/projectserializer.h"
#include "render/timeoverlaystyle.h"
#include "swmmvis.h"
#include "swmmvisprojectwindow.h"
#include "ui/widgets/timeoverlay.h"
#include "render/legendoverlaystyle.h"
#include "ui/dialogs/legendpropertiesdialog.h"
#include "ui/panels/propertiespanel.h"
#include "ui/widgets/legendoverlay.h"

#include <QAction>
#include <QDialog>
#include <QTreeView>

using OpenSWMM::Render::TimeOverlayStyle;
using openswmmvis::ui::TimeOverlay;
using Anchor = TimeOverlayStyle::Anchor;
using Mode = TimeOverlayStyle::BackgroundMode;

namespace
{

QString outDir()
{
    const QString dir = QStringLiteral(TIME_OVERLAY_OUTPUT);
    QDir().mkpath(dir);
    return dir;
}

QDateTime utc(int y, int mo, int d, int h, int mi, int s)
{
    return QDateTime(QDate(y, mo, d), QTime(h, mi, s), QTimeZone::UTC);
}

/** The box as drawn, on a transparent image the size of the box. */
QImage drawn(TimeOverlay *box)
{
    QImage image(box->size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    box->render(&image, QPoint(), QRegion(), QWidget::RenderFlags(QWidget::DrawChildren));
    return image.convertToFormat(QImage::Format_ARGB32);
}

/** A pixel in the left padding, halfway down: background only. */
QColor paddingPixel(const QImage &image)
{
    return image.pixelColor(4, image.height() / 2);
}

bool near(const QColor &a, const QColor &b, int tol = 4)
{
    return std::abs(a.red() - b.red()) <= tol && std::abs(a.green() - b.green()) <= tol &&
           std::abs(a.blue() - b.blue()) <= tol && std::abs(a.alpha() - b.alpha()) <= tol;
}

QString describe(const QColor &c)
{
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(c.red())
        .arg(c.green())
        .arg(c.blue())
        .arg(c.alpha());
}

TimeOverlay *boxOf(SWMMVisProjectWindow *window)
{
    return window->canvas()->findChild<TimeOverlay *>(QString(), Qt::FindDirectChildrenOnly);
}

/** The first row of @p tree's model, at any depth, whose name is @p name. */
QModelIndex rowNamed(const QAbstractItemModel *model, const QString &name,
                     const QModelIndex &parent = {})
{
    for (int r = 0; r < model->rowCount(parent); ++r) {
        const QModelIndex row = model->index(r, 0, parent);
        if (row.data().toString() == name)
            return row;
        const QModelIndex inner = rowNamed(model, name, row);
        if (inner.isValid())
            return inner;
    }
    return {};
}

/** The value of part @p part of the font row @p font. */
QVariant partValue(const QModelIndex &font, const QString &part)
{
    const QAbstractItemModel *model = font.model();
    for (int r = 0; r < model->rowCount(font); ++r) {
        const QModelIndex row = model->index(r, 0, font);
        if (row.data().toString() != part)
            continue;
        // Check-box parts (Bold, Italic, …) report a check state.
        const QModelIndex value = row.siblingAtColumn(1);
        const QVariant check = value.data(Qt::CheckStateRole);
        return check.isValid() ? QVariant(check.toInt() == Qt::Checked) : value.data(Qt::EditRole);
    }
    return {};
}

QStringList partNames(const QModelIndex &font)
{
    QStringList names;
    for (int r = 0; r < font.model()->rowCount(font); ++r)
        names << font.model()->index(r, 0, font).data().toString();
    return names;
}

const QStringList kFontParts = {QStringLiteral("Family"),    QStringLiteral("Point Size"),
                                QStringLiteral("Bold"),      QStringLiteral("Italic"),
                                QStringLiteral("Underline"), QStringLiteral("Strikeout"),
                                QStringLiteral("Kerning"),   QStringLiteral("Style Strategy")};

} // namespace

class TestTimeOverlay : public QObject
{
    Q_OBJECT

private slots:
    /** @brief Every field is saved and read back; Reset restyles without hiding; the enum rows
     *  of the Properties tree are real enums that accept the integer the tree writes. */
    void styleSavesEveryFieldAndResetKeepsItShown()
    {
        TimeOverlayStyle a;
        a.setShown(true);
        a.setFormat(QStringLiteral("'Time: 'yyyy-MM-dd HH:mm"));
        QFont font;
        font.setPointSizeF(19.0);
        font.setBold(true);
        a.setFont(font);
        a.setTextColor(QColor(1, 2, 3));
        a.setPadding(5);
        a.setAnchor(Anchor::BottomRight);
        a.setOpacity(0.5);
        a.setFreePosition(QPoint(40, 50));
        a.setShowFrame(false);
        a.setFrameColor(QColor(4, 5, 6, 7));
        a.setFrameWidth(2.5);
        a.setCornerRadius(3);
        a.setBackgroundMode(Mode::Gradient);
        a.setBackgroundColor(QColor(10, 20, 30, 40));
        a.setGradientEndColor(QColor(50, 60, 70, 80));
        a.setGradientOrientation(Qt::Horizontal);
        a.setAnchorMarginX(20);
        a.setAnchorMarginY(30);
        a.setMarginRight(9);
        a.setMarginBottom(11);
        a.setFrameStyle(TimeOverlayStyle::FrameStyle::Dash);
        a.setShowShadow(true);
        a.setShadowColor(QColor(9, 8, 7, 6));
        a.setShadowOffsetX(-4);
        a.setShadowOffsetY(5);

        // Every saved field differs from the default here, so none can be dropped unnoticed.
        const QJsonObject defaults = TimeOverlayStyle().toJson();
        const QJsonObject saved = a.toJson();
        for (const QString &key : defaults.keys())
            QVERIFY2(saved.value(key) != defaults.value(key), qPrintable(key));

        TimeOverlayStyle b;
        b.fromJson(saved);
        QCOMPARE(b.toJson(), saved);
        QVERIFY(b.isShown());
        QCOMPARE(b.anchor(), Anchor::BottomRight);
        QCOMPARE(b.backgroundMode(), Mode::Gradient);
        QCOMPARE(b.font().pointSizeF(), 19.0);
        QVERIFY(b.font().bold());
        QCOMPARE(b.freePosition(), QPoint(40, 50));

        b.resetToDefaults();
        QVERIFY(b.isShown());
        QJsonObject reset = b.toJson();
        QJsonObject expected = defaults;
        reset.remove(QStringLiteral("shown"));
        expected.remove(QStringLiteral("shown"));
        QCOMPARE(reset, expected);

        const QMetaObject &meta = TimeOverlayStyle::staticMetaObject;
        for (const char *name : {"anchor", "backgroundMode"}) {
            const QMetaProperty property = meta.property(meta.indexOfProperty(name));
            QVERIFY2(property.isEnumType() && property.enumerator().isValid(), name);
        }
        QVERIFY(meta.property(meta.indexOfProperty("anchor")).write(&b, int(Anchor::Bottom)));
        QCOMPARE(b.anchor(), Anchor::Bottom);
        QVERIFY(meta.property(meta.indexOfProperty("backgroundMode")).write(&b, int(Mode::None)));
        QCOMPARE(b.backgroundMode(), Mode::None);
        QCOMPARE(b.displayLabelFor(QStringLiteral("format")),
                 QStringLiteral("General — Date/time format"));
    }

    /** @brief One box per map; it shows the UTC wall clock in the style's format, a placeholder
     *  without a time, and keeps one width while the time plays. */
    void textIsTheUtcWallClockInTheFormat()
    {
        MapCanvas canvas;
        canvas.resize(600, 400);
        TimeOverlay *box = TimeOverlay::forCanvas(&canvas);
        QVERIFY(box);
        QCOMPARE(TimeOverlay::forCanvas(&canvas), box);
        QVERIFY(box->isHidden());
        QCOMPARE(box->text(), QStringLiteral("No animation time"));

        box->setDateTime(utc(2026, 3, 14, 6, 30, 0));
        QCOMPARE(box->text(), QStringLiteral("03/14/2026 06:30:00"));
        QCOMPARE(box->accessibleDescription(), box->text());
        // The same instant given with an offset still reads as the UTC clock, as on the toolbar.
        box->setDateTime(QDateTime());
        QCOMPARE(box->text(), QStringLiteral("No animation time"));
        box->setDateTime(
            QDateTime(QDate(2026, 3, 14), QTime(7, 30), QTimeZone::fromSecondsAheadOfUtc(3600)));
        QCOMPARE(box->text(), QStringLiteral("03/14/2026 06:30:00"));

        box->style()->setFormat(QStringLiteral("'Time: 'yyyy-MM-dd HH:mm"));
        QCOMPARE(box->text(), QStringLiteral("Time: 2026-03-14 06:30"));
        box->style()->setFormat(QString());
        QCOMPARE(box->text(), QStringLiteral("03/14/2026 06:30:00"));

        // With a font whose digits differ in width, the box keeps one width while time plays.
        QFont proportional;
        bool found = false;
        QStringList families = {QStringLiteral("Georgia"), QStringLiteral("Hoefler Text"),
                                QStringLiteral("Palatino"), QStringLiteral("Optima")};
        families += QFontDatabase::families();
        for (const QString &family : std::as_const(families)) {
            const QFont font(family, 14);
            const QFontMetrics fm(font);
            if (QFontInfo(font).family() == family &&
                fm.horizontalAdvance(QStringLiteral("11/11/2011 11:11:11")) !=
                    fm.horizontalAdvance(QStringLiteral("08/08/2088 08:08:08"))) {
                proportional = font;
                found = true;
                break;
            }
        }
        if (!found)
            QSKIP("No installed font has digits of different widths");
        box->style()->setFont(proportional);
        box->setDateTime(utc(2011, 11, 11, 11, 11, 11));
        const int width = box->width();
        box->setDateTime(utc(2088, 8, 8, 8, 8, 8));
        QCOMPARE(box->width(), width);
    }

    /** @brief The box draws its background (solid, none, gradient), frame, text color and
     *  opacity. */
    void boxDrawsItsBackgroundFrameTextAndOpacity()
    {
        MapCanvas canvas;
        canvas.resize(600, 400);
        TimeOverlay *box = TimeOverlay::forCanvas(&canvas);
        box->setDateTime(utc(2026, 3, 14, 6, 30, 0));
        TimeOverlayStyle *style = box->style();
        style->setShowFrame(false);
        style->setBackgroundColor(QColor(255, 0, 255));
        style->setTextColor(QColor(255, 0, 0));

        QImage image = drawn(box);
        image.save(outDir() + QStringLiteral("/box_solid.png"));
        QVERIFY2(near(paddingPixel(image), QColor(255, 0, 255)),
                 qPrintable(describe(paddingPixel(image))));
        int red = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x) {
                const QColor c = image.pixelColor(x, y);
                if (c.red() > 200 && c.green() < 60 && c.blue() < 60)
                    ++red;
            }
        QVERIFY2(red > 10, "the text is not drawn in the text color");

        style->setBackgroundMode(Mode::None);
        image = drawn(box);
        QCOMPARE(paddingPixel(image).alpha(), 0);

        style->setBackgroundMode(Mode::Gradient);
        style->setBackgroundColor(QColor(255, 0, 0));
        style->setGradientEndColor(QColor(0, 0, 255));
        image = drawn(box);
        image.save(outDir() + QStringLiteral("/box_gradient.png"));
        const QColor top = image.pixelColor(4, 3);
        const QColor bottom = image.pixelColor(4, image.height() - 4);
        QVERIFY2(top.red() > top.blue() && bottom.blue() > bottom.red(),
                 qPrintable(describe(top) + QStringLiteral(" / ") + describe(bottom)));

        style->setBackgroundMode(Mode::Solid);
        style->setBackgroundColor(QColor(255, 255, 255));
        style->setShowFrame(true);
        style->setFrameColor(QColor(0, 160, 0));
        style->setFrameWidth(3.0);
        image = drawn(box);
        image.save(outDir() + QStringLiteral("/box_frame.png"));
        const QColor edge = image.pixelColor(1, image.height() / 2);
        QVERIFY2(edge.green() > 120 && edge.red() < 80, qPrintable(describe(edge)));

        style->setShowFrame(false);
        style->setOpacity(0.5);
        image = drawn(box);
        QVERIFY2(std::abs(paddingPixel(image).alpha() - 128) <= 2,
                 qPrintable(describe(paddingPixel(image))));
    }

    /** @brief Anchored boxes sit 12 px in from the map's edge; a drag stores a Free position;
     *  the box stays on a shrunken map and returns to its position when it grows again. */
    void boxAnchorsDragsAndStaysOnTheMap()
    {
        MapCanvas canvas;
        canvas.resize(600, 400);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        TimeOverlay *box = TimeOverlay::forCanvas(&canvas);
        TimeOverlayStyle *style = box->style();
        style->setShown(true);
        QVERIFY(box->isVisible());
        QCOMPARE(box->pos(), QPoint(12, 12));
        style->setAnchor(Anchor::BottomRight);
        QCOMPARE(box->geometry().right(), 600 - 12 - 1);
        QCOMPARE(box->geometry().bottom(), 400 - 12 - 1);

        // Drag 100 px left and 80 px up.
        const QPoint start = box->pos();
        const QPointF grab(5, 5);
        QMouseEvent press(QEvent::MouseButtonPress, grab, box->mapToGlobal(grab), Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(box, &press);
        const QPointF to = grab + QPointF(-100, -80);
        QMouseEvent move(QEvent::MouseMove, to, box->mapToGlobal(to), Qt::NoButton, Qt::LeftButton,
                         Qt::NoModifier);
        QApplication::sendEvent(box, &move);
        QMouseEvent release(QEvent::MouseButtonRelease, to, box->mapToGlobal(to), Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(box, &release);
        const QPoint dropped = start + QPoint(-100, -80);
        QCOMPARE(style->anchor(), Anchor::Free);
        QCOMPARE(style->freePosition(), dropped);
        QCOMPARE(box->pos(), dropped);

        canvas.resize(250, 150);
        QTRY_VERIFY(QRect(QPoint(), canvas.size()).contains(box->geometry()));
        QCOMPARE(style->freePosition(), dropped);
        canvas.resize(600, 400);
        QTRY_COMPARE(box->pos(), dropped);

        style->setAnchor(Anchor::Bottom);
        canvas.resize(500, 300);
        QTRY_COMPARE(box->geometry().bottom(), 300 - 12 - 1);
        QCOMPARE(box->geometry().center().x(), 250 - 1 + (box->width() % 2));
    }

    /** @brief "Show date/time" shows the active map's box; each map keeps its own; the box
     *  follows the animation clock; the box's Hide unchecks the toolbar. */
    void toolbarCheckboxShowsTheActiveMapsBox()
    {
        SWMMVis app;
        app.resize(1600, 900);
        app.show();
        auto *check = app.findChild<QCheckBox *>(QStringLiteral("showAnimationTime"));
        QVERIFY(check);
        QVERIFY(!check->isChecked());
        check->setChecked(true); // no map yet
        QVERIFY(!check->isChecked());

        auto *mdi = app.findChild<QMdiArea *>();
        QVERIFY(QMetaObject::invokeMethod(&app, "onNewProject", Qt::DirectConnection));
        QTRY_COMPARE(app.findChildren<SWMMVisProjectWindow *>().size(), 1);
        QPointer<SWMMVisProjectWindow> a = app.findChildren<SWMMVisProjectWindow *>().first();
        QTRY_COMPARE(mdi->activeSubWindow(), static_cast<QMdiSubWindow *>(a.data()));
        QTRY_VERIFY(boxOf(a));
        TimeOverlay *boxA = boxOf(a);
        QVERIFY(boxA->isHidden());

        check->setChecked(true);
        QVERIFY(boxA->style()->isShown());
        QVERIFY(boxA->isVisibleTo(a->canvas()));

        auto *animation = app.findChild<AnimationController *>();
        QVERIFY(animation);
        emit animation->currentTimeChanged(utc(2026, 3, 14, 6, 30, 0));
        QCOMPARE(boxA->text(), QStringLiteral("03/14/2026 06:30:00"));

        QVERIFY(QMetaObject::invokeMethod(&app, "onNewProject", Qt::DirectConnection));
        QTRY_COMPARE(app.findChildren<SWMMVisProjectWindow *>().size(), 2);
        QPointer<SWMMVisProjectWindow> b;
        for (auto *w : app.findChildren<SWMMVisProjectWindow *>())
            if (w != a)
                b = w;
        QTRY_COMPARE(mdi->activeSubWindow(), static_cast<QMdiSubWindow *>(b.data()));
        QTRY_VERIFY(boxOf(b));
        TimeOverlay *boxB = boxOf(b);
        QVERIFY(!check->isChecked());
        QVERIFY(boxB->isHidden());
        emit animation->currentTimeChanged(utc(2026, 3, 15, 0, 0, 0));
        QCOMPARE(boxB->text(), QStringLiteral("03/15/2026 00:00:00"));
        QCOMPARE(boxA->text(), QStringLiteral("03/14/2026 06:30:00")); // A's map is not playing

        mdi->setActiveSubWindow(a);
        QTRY_VERIFY(check->isChecked());
        boxA->style()->setShown(false); // what the box's "Hide date and time" does
        QVERIFY(!check->isChecked());
        QVERIFY(boxA->isHidden());
    }

    /** @brief The project saves the box's style and whether it is shown, and restores both
     *  into an active or an inactive window; a hidden default box is not written. */
    void projectRemembersTheBoxStyleAndWhetherItIsShown()
    {
        SWMMVis app;
        app.resize(1600, 900);
        app.show();
        auto *check = app.findChild<QCheckBox *>(QStringLiteral("showAnimationTime"));
        auto *mdi = app.findChild<QMdiArea *>();
        const auto newProject = [&]() -> SWMMVisProjectWindow * {
            const auto before = app.findChildren<SWMMVisProjectWindow *>();
            if (!QMetaObject::invokeMethod(&app, "onNewProject", Qt::DirectConnection))
                return nullptr;
            for (auto *w : app.findChildren<SWMMVisProjectWindow *>())
                if (!before.contains(w))
                    return w;
            return nullptr;
        };

        SWMMVisProjectWindow *plain = newProject();
        QVERIFY(plain);
        QTRY_COMPARE(mdi->activeSubWindow(), static_cast<QMdiSubWindow *>(plain));
        QTRY_VERIFY(boxOf(plain));
        const QString plainFile = outDir() + QStringLiteral("/hidden_default_box.oswp");
        QString error;
        QVERIFY2(ProjectSerializer::saveToFile(plainFile, plain, &error), qPrintable(error));
        QFile plainSaved(plainFile);
        QVERIFY(plainSaved.open(QIODevice::ReadOnly));
        QVERIFY(!plainSaved.readAll().contains("timeOverlay"));

        SWMMVisProjectWindow *source = newProject();
        QVERIFY(source);
        QTRY_COMPARE(mdi->activeSubWindow(), static_cast<QMdiSubWindow *>(source));
        QTRY_VERIFY(boxOf(source));
        check->setChecked(true);
        TimeOverlayStyle *style = boxOf(source)->style();
        style->setFormat(QStringLiteral("'Run time: 'yyyy-MM-dd HH:mm"));
        style->setBackgroundMode(Mode::Gradient);
        style->setFreePosition(QPoint(30, 40));
        style->setAnchor(Anchor::Free);
        const QString file = outDir() + QStringLiteral("/shown_box.oswp");
        QVERIFY2(ProjectSerializer::saveToFile(file, source, &error), qPrintable(error));

        // Restored into the active window: the toolbar checkbox follows at once.
        SWMMVisProjectWindow *active = newProject();
        QVERIFY(active);
        QTRY_COMPARE(mdi->activeSubWindow(), static_cast<QMdiSubWindow *>(active));
        QTRY_VERIFY(!check->isChecked());
        QVERIFY2(ProjectSerializer::applyFromFile(file, active, &error), qPrintable(error));
        QVERIFY(boxOf(active));
        QCOMPARE(boxOf(active)->style()->toJson(), style->toJson());
        QVERIFY(boxOf(active)->isVisibleTo(active->canvas()));
        QVERIFY(check->isChecked());

        // Restored into an inactive window: the checkbox shows it once that window is active.
        QVERIFY2(ProjectSerializer::applyFromFile(file, plain, &error), qPrintable(error));
        QCOMPARE(boxOf(plain)->style()->toJson(), style->toJson());
        mdi->setActiveSubWindow(source);
        QTRY_COMPARE(mdi->activeSubWindow(), static_cast<QMdiSubWindow *>(source));
        boxOf(source)->style()->setShown(false);
        QVERIFY(!check->isChecked());
        mdi->setActiveSubWindow(plain);
        QTRY_VERIFY(check->isChecked());
    }

    /** @brief The legend and date/time Properties dialogs follow edits made elsewhere; the
     *  date/time dialog's font row shows its parts when selected (others stay closed). */
    void dialogsShowFontPartsWhenSelectedAndFollowEdits()
    {
        // The legend's dialog has its own controls for each font part, following edits made
        // elsewhere.
        OpenSWMM::Render::LegendOverlayStyle legendStyle;
        openswmmvis::ui::LegendPropertiesDialog legendDialog(&legendStyle);
        legendDialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&legendDialog));
        legendDialog.grab().save(outDir() + QStringLiteral("/legend_dialog.png"));
        QFont bold = legendStyle.itemFont();
        bold.setBold(true);
        bold.setPointSize(15);
        legendStyle.setItemFont(bold); // e.g. from the Properties panel
        QTRY_VERIFY(
            legendDialog.findChild<QCheckBox *>(QStringLiteral("itemFont.bold"))->isChecked());
        QCOMPARE(legendDialog.findChild<QDoubleSpinBox *>(QStringLiteral("itemFont.size"))->value(),
                 15.0);

        MapCanvas canvas;
        canvas.resize(600, 400);
        TimeOverlay *box = TimeOverlay::forCanvas(&canvas);
        box->openPropertiesDialog();
        auto *dialog = box->findChild<QDialog *>(QStringLiteral("TimeOverlayPropertiesDialog"));
        QVERIFY(dialog);
        auto *boxTree = dialog->findChild<QTreeView *>();
        const QModelIndex font = rowNamed(boxTree->model(), QStringLiteral("General — Font"));
        QCOMPARE(partNames(font), kFontParts);
        QVERIFY(!boxTree->isExpanded(font));
        boxTree->setCurrentIndex(font.siblingAtColumn(1));
        QVERIFY(boxTree->isExpanded(font));
        QFont italic = box->style()->font();
        italic.setItalic(true);
        box->style()->setFont(italic);
        QTRY_COMPARE(partValue(font, QStringLiteral("Italic")).toBool(), true);
        dialog->close();
    }

    /** @brief The Properties panel shows a style's font parts (closed until selected), edits
     *  write through, edits made elsewhere show, and it lets go of a deleted style. */
    void panelShowsFontPartsAndFollowsTheStyle()
    {
        PropertiesPanel panel;
        panel.resize(420, 700);
        panel.show();
        auto *style = new TimeOverlayStyle;
        panel.showStyleObject(style, QStringLiteral("Date and Time"));
        QCOMPARE(panel.windowTitle(), QStringLiteral("Date and Time"));
        auto *tree = panel.findChild<QTreeView *>();
        QAbstractItemModel *model = tree->model();
        const QModelIndex font = rowNamed(model, QStringLiteral("General — Font"));
        QVERIFY(font.isValid());
        QCOMPARE(partNames(font), kFontParts);
        QVERIFY(!tree->isExpanded(font));
        tree->setCurrentIndex(font);
        QVERIFY(tree->isExpanded(font));
        panel.grab().save(outDir() + QStringLiteral("/panel_font_selected.png"));

        // Edits in the panel write the style.
        const QModelIndex margin = rowNamed(model, QStringLiteral("Margins — Left"));
        QVERIFY(model->setData(margin.siblingAtColumn(1), 20, Qt::EditRole));
        QCOMPARE(style->marginLeft(), 20);
        QVERIFY(model->setData(rowNamed(model, QStringLiteral("Bold"), font).siblingAtColumn(1),
                               true, Qt::EditRole));
        QVERIFY(style->font().bold());

        // Edits made elsewhere (the dialog, a drag) show in the panel, font parts included.
        QFont larger = style->font();
        larger.setPointSize(17);
        style->setFont(larger);
        QTRY_COMPARE(partValue(font, QStringLiteral("Point Size")).toInt(), 17);
        style->setFormat(QStringLiteral("HH:mm"));
        QTRY_COMPARE(rowNamed(model, QStringLiteral("General — Date/time format"))
                         .siblingAtColumn(1)
                         .data()
                         .toString(),
                     QStringLiteral("HH:mm"));

        // Showing something else stops following; a deleted style clears the panel.
        panel.clear();
        style->setPadding(3);
        QCoreApplication::processEvents();
        panel.showStyleObject(style, QStringLiteral("Date and Time"));
        QVERIFY(model->rowCount() > 0);
        delete style;
        QCOMPARE(model->rowCount(), 0);
        QCoreApplication::processEvents();
    }

    /** @brief In the app, clicking the date/time box or the legend on the map shows its style in
     *  the Properties panel. */
    void clickingABoxShowsItsStyleInThePropertiesPanel()
    {
        SWMMVis app;
        app.resize(1600, 900);
        app.show();
        auto *mdi = app.findChild<QMdiArea *>();
        QVERIFY(QMetaObject::invokeMethod(&app, "onNewProject", Qt::DirectConnection));
        QTRY_COMPARE(app.findChildren<SWMMVisProjectWindow *>().size(), 1);
        SWMMVisProjectWindow *project = app.findChildren<SWMMVisProjectWindow *>().first();
        QTRY_COMPARE(mdi->activeSubWindow(), static_cast<QMdiSubWindow *>(project));
        QTRY_VERIFY(boxOf(project));
        auto *panel = app.findChild<PropertiesPanel *>();
        QVERIFY(panel);
        auto *tree = panel->findChild<QTreeView *>();

        const auto click = [](QWidget *widget) {
            const QPointF at(6, 6);
            QMouseEvent press(QEvent::MouseButtonPress, at, widget->mapToGlobal(at), Qt::LeftButton,
                              Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(widget, &press);
            QMouseEvent release(QEvent::MouseButtonRelease, at, widget->mapToGlobal(at),
                                Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(widget, &release);
        };

        TimeOverlay *box = boxOf(project);
        box->style()->setShown(true);
        click(box);
        QCOMPARE(panel->windowTitle(), QStringLiteral("Date and Time"));
        QVERIFY(rowNamed(tree->model(), QStringLiteral("General — Date/time format")).isValid());

        auto *showLegend = app.findChild<QAction *>(QStringLiteral("actionShowLegend"));
        QVERIFY(showLegend);
        showLegend->trigger();
        auto *legend = project->canvas()->findChild<openswmmvis::ui::LegendOverlay *>(
            QString(), Qt::FindDirectChildrenOnly);
        QVERIFY(legend && legend->isVisibleTo(project->canvas()));
        click(legend);
        QCOMPARE(panel->windowTitle(), QStringLiteral("Legend"));
        QVERIFY(rowNamed(tree->model(), QStringLiteral("Text — Item font")).isValid());
    }

    /** @brief Export Map (no GPU map here, so the canvas is grabbed) holds the box exactly once:
     *  a half-transparent background blends over the map one time. */
    void exportMapHoldsTheBoxOnce()
    {
        MapCanvas canvas;
        canvas.resize(600, 400);
        canvas.setExtent({0, 0, 100, 100}, false);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        TimeOverlay *box = TimeOverlay::forCanvas(&canvas);
        box->setDateTime(utc(2026, 3, 14, 6, 30, 0));
        box->style()->setShowFrame(false);
        box->style()->setBackgroundColor(QColor(255, 0, 255, 128));

        QString error;
        const QPixmap without = canvas.captureMap(&error);
        QVERIFY2(!without.isNull(), qPrintable(error));
        box->style()->setShown(true);
        const QPixmap with = canvas.captureMap(&error);
        QVERIFY2(!with.isNull(), qPrintable(error));
        with.save(outDir() + QStringLiteral("/export_map_offscreen.png"));

        const qreal dpr = with.devicePixelRatio();
        const QPoint at = (box->pos() + QPoint(4, box->height() / 2)) * dpr;
        const QColor map = without.toImage().pixelColor(at);
        const QColor got = with.toImage().pixelColor(at);
        const double a = 128.0 / 255.0;
        const QColor once(int(std::lround(255 * a + map.red() * (1 - a))),
                          int(std::lround(map.green() * (1 - a))),
                          int(std::lround(255 * a + map.blue() * (1 - a))));
        QVERIFY2(near(got, once),
                 qPrintable(describe(got) + QStringLiteral(" vs ") + describe(once) +
                            QStringLiteral(" over ") + describe(map)));
    }
};

QTEST_MAIN(TestTimeOverlay)
#include "test_timeoverlay.moc"
