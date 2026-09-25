#include "ui/widgets/colorbutton.h"
#include "ui/widgets/sunpositionthumb.h"
#include "ui/widgets/labelconfigeditor.h"
#include "ui/dialogs/colorrampeditordialog.h"
#include "ui/dialogs/swmm2dmeshstylepanel.h"
#include "ui/theme/thememanager.h"
#include "layers/swmm2dmeshlayer.h"

#include <QAccessible>
#include <QColorDialog>
#include <QDir>
#include <QFormLayout>
#include <QImage>
#include <QSettings>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTimer>
#include <QtTest>

using namespace openswmmvis::ui;

namespace {
QString outputDir()
{
    return qEnvironmentVariable("SWMMVIS_STYLE_TEST_OUTPUT",
                                QStringLiteral("test_stylecontrols_output"));
}

class ObservedSunThumb : public SunPositionThumb
{
public:
    int paints = 0;
protected:
    void paintEvent(QPaintEvent *event) override
    {
        ++paints;
        SunPositionThumb::paintEvent(event);
    }
};
}

class TestStyleControls : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QVERIFY(QDir().mkpath(outputDir()));
        QCoreApplication::setOrganizationName(QStringLiteral("openswmm-test"));
        QCoreApplication::setApplicationName(QStringLiteral("stylecontrols"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, outputDir());
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    }

    void colorDescription_tracksValue()
    {
        ColorButton button;
        auto *iface = QAccessible::queryAccessibleInterface(&button);
        QVERIFY(iface);
        QCOMPARE(iface->role(), QAccessible::Button);
        QVERIFY(iface->text(QAccessible::Description).contains("#000000"));
        QVERIFY(iface->text(QAccessible::Description).contains("255"));
        QSignalSpy changed(&button, &ColorButton::colorChanged);
        button.setColor(QColor(18, 52, 86, 128));
        QVERIFY(iface->text(QAccessible::Description).contains("#123456"));
        QVERIFY(iface->text(QAccessible::Description).contains("128"));
        QCOMPARE(changed.count(), 1);
        button.setColor(button.color());
        QCOMPARE(changed.count(), 1);
        button.setColor(QColor(18, 52, 86, 0));
        QVERIFY(iface->text(QAccessible::Description).contains("0%"));
        button.setColor(QColor());
        QVERIFY(iface->text(QAccessible::Description).contains("No colour"));

        ColorButton initial(QColor(171, 205, 239, 64));
        auto *initialIface = QAccessible::queryAccessibleInterface(&initial);
        QVERIFY(initialIface->text(QAccessible::Description).contains("#abcdef"));
        QVERIFY(initialIface->text(QAccessible::Description).contains("64"));
    }

    void colorButton_preservesBuddyNameAndAction()
    {
        QWidget host;
        QFormLayout form(&host);
        auto *button = new ColorButton(&host);
        form.addRow(tr("&Water colour:"), button);
        auto *iface = QAccessible::queryAccessibleInterface(button);
        QVERIFY(iface);
        QVERIFY(iface->text(QAccessible::Name).contains("Water colour"));
        QVERIFY(iface->actionInterface());
        QVERIFY(iface->actionInterface()->actionNames().contains(
            QAccessibleActionInterface::pressAction()));
        QVERIFY(button->focusPolicy() & Qt::TabFocus);
    }

    void keyboardPicker_data()
    {
        QTest::addColumn<bool>("accept");
        QTest::newRow("accept") << true;
        QTest::newRow("cancel") << false;
    }

    void keyboardPicker()
    {
        QFETCH(bool, accept);
        ColorButton button(QColor(30, 60, 90));
        button.show();
        button.setFocus();
        QSignalSpy changed(&button, &ColorButton::colorChanged);
        const QColor selected(15, 45, 75, 128);
        bool opened = false;
        QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, &button, [&] {
            auto *picker = qobject_cast<QColorDialog *>(QApplication::activeModalWidget());
            if (!picker) return;
            opened = true;
            picker->setCurrentColor(selected);
            if (accept) picker->accept();
            else picker->reject();
        });
        dismiss.start(10);
        QTest::keyClick(&button, Qt::Key_Space);
        dismiss.stop();
        QVERIFY(opened);
        QCOMPARE(button.color(), accept ? selected : QColor(30, 60, 90));
        QCOMPARE(changed.count(), accept ? 1 : 0);
        auto *iface = QAccessible::queryAccessibleInterface(&button);
        QVERIFY(iface->text(QAccessible::Description).contains(button.color().name()));
    }

    void rampStops_haveDistinctNames()
    {
        RasterColorRamp ramp;
        ramp.stops = {{0.0, Qt::blue}, {1.0, Qt::red}};
        ColorRampEditorDialog dialog(ramp);
        auto *table = dialog.findChild<QTableWidget *>();
        QVERIFY(table);
        QCOMPARE(table->rowCount(), 2);
        QSet<QString> names;
        for (int i = 0; i < table->rowCount(); ++i) {
            auto *button = qobject_cast<ColorButton *>(table->cellWidget(i, 1));
            QVERIFY(button);
            auto *iface = QAccessible::queryAccessibleInterface(button);
            const QString name = iface->text(QAccessible::Name);
            QVERIFY2(name.contains(QString::number(i + 1)), qPrintable(name));
            QVERIFY(name.contains("colour", Qt::CaseInsensitive));
            names.insert(name);
        }
        QCOMPARE(names.size(), 2);
    }

    void labelColors_haveNames()
    {
        LabelConfigEditor editor;
        const auto buttons = editor.findChildren<ColorButton *>();
        QCOMPARE(buttons.size(), 3);
        QSet<QString> names;
        for (auto *button : buttons) {
            auto *iface = QAccessible::queryAccessibleInterface(button);
            const QString name = iface->text(QAccessible::Name);
            QVERIFY2(!name.isEmpty(), "Every label colour control needs a name");
            names.insert(name);
        }
        QCOMPARE(names.size(), 3);
        QVERIFY(names.contains(QStringLiteral("Halo colour")));
        QVERIFY(names.contains(QStringLiteral("Background colour")));
    }

    void meshBoundaryColors_haveNames()
    {
        SWMM2DMeshLayer layer(mesh::MeshResult{});
        Swmm2DMeshStylePanel panel(&layer);
        const auto buttons = panel.findChildren<ColorButton *>();
        QVERIFY(!buttons.isEmpty());
        int boundaryNames = 0;
        for (auto *button : buttons) {
            auto *iface = QAccessible::queryAccessibleInterface(button);
            const QString name = iface->text(QAccessible::Name);
            QVERIFY2(!name.isEmpty(), "Every mesh style colour control needs a name");
            if (name.contains("boundary colour")) ++boundaryNames;
        }
        QVERIFY(boundaryNames > 1);
    }

    void sunDial_tracksLiveTheme()
    {
        auto *theme = ThemeManager::instance();
        theme->setMode(ThemeManager::Mode::Light);
        ObservedSunThumb thumb;
        thumb.resize(110, 110);
        thumb.show();
        QTRY_VERIFY(thumb.paints > 0);
        auto verifyImage = [&](const QString &name) {
            const QImage image = thumb.grab().toImage();
            QVERIFY(image.save(outputDir() + "/" + name + ".png"));
            const qreal dpr = image.devicePixelRatio();
            // A point inside the dial, away from the sun, arrow and text.
            QCOMPARE(image.pixelColor(qRound(75 * dpr), qRound(55 * dpr)),
                     thumb.palette().color(QPalette::Base));
        };
        verifyImage("sun_light");
        const int beforeDark = thumb.paints;
        theme->setMode(ThemeManager::Mode::Dark);
        QTRY_VERIFY(thumb.paints > beforeDark);
        verifyImage("sun_dark");
        const int beforeLight = thumb.paints;
        theme->setMode(ThemeManager::Mode::Light);
        QTRY_VERIFY(thumb.paints > beforeLight);
        verifyImage("sun_light_again");
    }
};

QTEST_MAIN(TestStyleControls)
#include "test_stylecontrols.moc"
