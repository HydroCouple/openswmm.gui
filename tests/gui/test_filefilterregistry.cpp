/**
 * @file      test_filefilterregistry.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     The built-in .inp writer must be a Save As target.
 *
 *         The engine SDK advertises the input plugin with INPUT_READ only, and
 *         the GUI registry used to leave `canWrite` false for it. Save As then
 *         never offered *.inp, the dialog appended the .oswp filter's suffix
 *         to a typed "model.inp", and the path normalizer — whose writable
 *         set also lacked "inp" — turned "model.inp.oswp" into
 *         "model.inp.inp". These cases drive the LIVE registry, not a
 *         hand-built extension set like tests/unit/test_saveaspathnormalizer.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "plugins/filefilterregistry.h"
#include "project/saveaspathnormalizer.h"

#include <QObject>
#include <QSet>
#include <QString>
#include <QTest>

using openswmmvis::FileFilterRegistry;
using openswmmvis::FilterKind;

class TestFileFilterRegistry : public QObject
{
    Q_OBJECT

private slots:

    /**
     *
     * @brief Require the live discovered Save As filter to advertise both input and project
     * extensions.
     *
     */
    void saveAsFilterOffersInpAndProject()
    {
        const QString filter = FileFilterRegistry::instance()->saveAsFilter();
        QVERIFY2(filter.contains(QStringLiteral("*.inp")), qPrintable(filter));
        QVERIFY2(filter.contains(QStringLiteral("*.oswp")), qPrintable(filter));
    }

    /**
     *
     * @brief Require a discovered input plugin entry for .inp to be writable and marked built-in
     * for staged writer routing.
     *
     */
    void builtinInputEntryIsWritable()
    {
        bool found = false;
        for (const auto &e : FileFilterRegistry::instance()->entriesFor(FilterKind::InputRead)) {
            if (!e.patterns.contains(QStringLiteral("*.inp")))
                continue;
            found = true;
            QVERIFY2(e.canWrite, "the engine writes .inp through this plugin");
            // ...but Save must route it to the staged built-in writer.
            QVERIFY2(FileFilterRegistry::isBuiltinPlugin(e.pluginId), qPrintable(e.pluginId));
        }
        QVERIFY2(found, "engine discovery must register the built-in .inp input plugin");
    }

    /**
     *
     * @brief Require the live writable-extension set to include inp and oswp.
     *
     */
    void saveAsExtensionsMatchTheDropdown()
    {
        const QSet<QString> exts = FileFilterRegistry::instance()->saveAsExtensions();
        QVERIFY(exts.contains(QStringLiteral("inp")));
        QVERIFY(exts.contains(QStringLiteral("oswp")));
    }

    /**
     *
     * @brief Normalize stacked project/input suffixes using the live extension registry and
     * require the expected path/type.
     * Uses path strings only; does not open a native file dialog or write model files.
     *
     */
    void nativeDialogSuffixStackCollapsesWithLiveSet()
    {
        const QSet<QString> exts = FileFilterRegistry::instance()->saveAsExtensions();

        // macOS appends the selected filter's suffix to a typed "model.inp".
        const auto project =
            openswmmvis::normalizeSaveAsPath(QStringLiteral("/p/model.inp.oswp"), exts);
        QCOMPARE(project.inpPath, QStringLiteral("/p/model.inp"));
        QVERIFY(project.isProject);

        const auto input =
            openswmmvis::normalizeSaveAsPath(QStringLiteral("/p/model.inp.inp"), exts);
        QCOMPARE(input.inpPath, QStringLiteral("/p/model.inp"));
        QVERIFY(!input.isProject);
    }
};

QTEST_GUILESS_MAIN(TestFileFilterRegistry)
#include "test_filefilterregistry.moc"
