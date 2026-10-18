/*!
 * \file   test_filefilterregistry.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  The built-in .inp writer must be a Save As target.
 *
 *         The engine SDK advertises the input plugin with INPUT_READ only, and
 *         the GUI registry used to leave `canWrite` false for it. Save As then
 *         never offered *.inp, the dialog appended the .oswp filter's suffix
 *         to a typed "model.inp", and the path normalizer — whose writable
 *         set also lacked "inp" — turned "model.inp.oswp" into
 *         "model.inp.inp". These cases drive the LIVE registry, not a
 *         hand-built extension set like tests/unit/test_saveaspathnormalizer.
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

    void saveAsFilterOffersInpAndProject()
    {
        const QString filter = FileFilterRegistry::instance()->saveAsFilter();
        QVERIFY2(filter.contains(QStringLiteral("*.inp")), qPrintable(filter));
        QVERIFY2(filter.contains(QStringLiteral("*.oswp")), qPrintable(filter));
    }

    void builtinInputEntryIsWritable()
    {
        bool found = false;
        for (const auto &e : FileFilterRegistry::instance()->entriesFor(FilterKind::InputRead)) {
            if (!e.patterns.contains(QStringLiteral("*.inp"))) continue;
            found = true;
            QVERIFY2(e.canWrite, "the engine writes .inp through this plugin");
        }
        QVERIFY2(found, "engine discovery must register the built-in .inp input plugin");
    }

    void saveAsExtensionsMatchTheDropdown()
    {
        const QSet<QString> exts = FileFilterRegistry::instance()->saveAsExtensions();
        QVERIFY(exts.contains(QStringLiteral("inp")));
        QVERIFY(exts.contains(QStringLiteral("oswp")));
    }

    void nativeDialogSuffixStackCollapsesWithLiveSet()
    {
        const QSet<QString> exts = FileFilterRegistry::instance()->saveAsExtensions();

        // macOS appends the selected filter's suffix to a typed "model.inp".
        const auto project = openswmmvis::normalizeSaveAsPath(
            QStringLiteral("/p/model.inp.oswp"), exts);
        QCOMPARE(project.inpPath, QStringLiteral("/p/model.inp"));
        QVERIFY(project.isProject);

        const auto input = openswmmvis::normalizeSaveAsPath(
            QStringLiteral("/p/model.inp.inp"), exts);
        QCOMPARE(input.inpPath, QStringLiteral("/p/model.inp"));
        QVERIFY(!input.isProject);
    }
};

QTEST_GUILESS_MAIN(TestFileFilterRegistry)
#include "test_filefilterregistry.moc"
