// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      test_modelannotations.cpp
 * @brief     Review G-01: reading and writing the model's [LABELS], [BACKDROP] and [PROFILES].
 * @details   Text parsing and writing, then the engine: the fixture's sections are read, an
 *            edited profile list is written back under the file's own [PROFILE] spelling, and a
 *            saved copy (tests/output/model_annotations_g01/) keeps the sections nobody edited
 *            exactly as they were.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "project/modelannotations.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTest>

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_model.h>

using namespace openswmmvis::project;

namespace
{
QString dataDir()
{
    return QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral(".")))
        .absoluteFilePath(QStringLiteral("model_annotations"));
}

QString outputDir()
{
    return QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral(".")))
        .absoluteFilePath(QStringLiteral("../../output/model_annotations_g01"));
}

/** The data lines of one section of an .inp file, as written. */
QStringList sectionLines(const QString &path, const QString &section)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    QStringList lines;
    bool inside = false;
    for (const QString &line : QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'))) {
        if (line.startsWith(QLatin1Char('['))) {
            inside = line.trimmed().compare(QLatin1Char('[') + section + QLatin1Char(']'),
                                            Qt::CaseInsensitive) == 0;
            continue;
        }
        if (inside && !line.trimmed().isEmpty())
            lines.append(line);
    }
    return lines;
}

int headerCount(const QString &path, const QString &section)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return -1;
    return int(QString::fromUtf8(file.readAll())
                   .split(QLatin1Char('\n'))
                   .filter(QRegularExpression(QStringLiteral("^\\[%1\\]\\s*$").arg(section),
                                              QRegularExpression::CaseInsensitiveOption))
                   .size());
}

struct Engine {
    SWMM_Engine handle = swmm_engine_create();
    ~Engine()
    {
        swmm_engine_close(handle);
        swmm_engine_destroy(handle);
    }
};
} // namespace

class TestModelAnnotations : public QObject
{
    Q_OBJECT

private slots:
    void labelsAreReadInEveryStyle();
    void labelsWriteAndReadBack();
    void backdropIsRead();
    void profilesContinueAcrossRows();
    void engineSectionsAreReadAndWrittenBack();
    void writingProfilesClearsTheOtherSpelling();
    void profileLinksJoinEndToEnd();
    void savingAProfileAddsOrReplaces();
    void renamedLinksFollowIntoProfiles();
};

void TestModelAnnotations::labelsAreReadInEveryStyle()
{
    QStringList warnings;
    const auto labels = parseLabels(QStringLiteral(
                                        ";;X-Coord   Y-Coord   Label\n"
                                        "50.5  20  \"Upper reach\" \"\" \"Arial\" 12 1 0\n"
                                        "\n"
                                        "250 -15.25 \"Outfall\" \"O1\" \"Times New Roman\" 9 NO YES\n"
                                        "10 40 \"Old style label\"\n"
                                        "not-a-number 4 \"Broken\"\n"
                                        "7 8\n"),
                                    &warnings);
    QCOMPARE(labels.size(), 3);
    QCOMPARE(labels[0], (ModelLabel{50.5, 20.0, QStringLiteral("Upper reach"), QString(),
                                    QStringLiteral("Arial"), 12.0, true, false}));
    QCOMPARE(labels[1], (ModelLabel{250.0, -15.25, QStringLiteral("Outfall"), QStringLiteral("O1"),
                                    QStringLiteral("Times New Roman"), 9.0, false, true}));
    QCOMPARE(labels[2], (ModelLabel{10.0, 40.0, QStringLiteral("Old style label"), QString(),
                                    QString(), 10.0, false, false}));
    QCOMPARE(warnings.size(), 2);
    QVERIFY(warnings[0].contains(QStringLiteral("Broken")));
}

void TestModelAnnotations::labelsWriteAndReadBack()
{
    const QList<ModelLabel> labels{
        {218.836, 1149.697, QStringLiteral("S1"), QString(), QStringLiteral("Arial"), 14, true,
         false}, // bold only, so bold and italic cannot trade places unnoticed
        {-0.5, 1e6, QStringLiteral("Say \"hi\" here"), QStringLiteral("J 1"),
         QStringLiteral("Times New Roman"), 8.5, true, true}};
    const QString text = labelsText(labels);
    QVERIFY(text.startsWith(QStringLiteral(";;X-Coord")));
    QVERIFY2(text.contains(QStringLiteral("218.836 ")), qPrintable(text)); // shortest form
    QVERIFY2(text.contains(QStringLiteral("\"Say 'hi' here\"")), qPrintable(text));
    auto back = parseLabels(text);
    QCOMPARE(back.size(), 2);
    QCOMPARE(back[0], labels[0]);
    back[1].text.replace(QLatin1Char('\''), QLatin1Char('"'));
    QCOMPARE(back[1], labels[1]);
    QVERIFY(labelsText({}).isEmpty());
}

void TestModelAnnotations::backdropIsRead()
{
    const auto backdrop = parseBackdrop(QStringLiteral(
        "UNITS None\nFILE      \t\"backdrop image.png\"\nDIMENSIONS\t-10.000\t-50.000\t310\t50\n"
        "OFFSET 0 0"));
    QVERIFY(backdrop);
    QCOMPARE(backdrop->file, QStringLiteral("backdrop image.png"));
    QVERIFY(backdrop->hasDimensions);
    QCOMPARE(backdrop->x1, -10.0);
    QCOMPARE(backdrop->y1, -50.0);
    QCOMPARE(backdrop->x2, 310.0);
    QCOMPARE(backdrop->y2, 50.0);
    QVERIFY(!parseBackdrop(QStringLiteral("DIMENSIONS 0 0 1 1")));
    QVERIFY(!parseBackdrop(QStringLiteral("FILE \"a.png\"\nDIMENSIONS 0 0 1"))->hasDimensions);
}

void TestModelAnnotations::profilesContinueAcrossRows()
{
    QStringList warnings;
    const auto profiles = parseProfiles(QStringLiteral(";;Name   Links\n"
                                                       "\"Main line       \"\tC1\tC2\n"
                                                       "Branch C9\n"
                                                       "\"Main line       \"\tC3\n"
                                                       "\"Lonely\"\n"),
                                        &warnings);
    QCOMPARE(profiles.size(), 2);
    QCOMPARE(profiles[0], (ModelProfile{QStringLiteral("Main line"),
                                        {QStringLiteral("C1"), QStringLiteral("C2"),
                                         QStringLiteral("C3")}}));
    QCOMPARE(profiles[1], (ModelProfile{QStringLiteral("Branch"), {QStringLiteral("C9")}}));
    QCOMPARE(warnings.size(), 1);

    const ModelProfile seven{QStringLiteral("Long one"),
                             {"L1", "L2", "L3", "L4", "L5", "L6", "L7"}};
    const QString text = profilesText({seven, profiles[1]});
    const QStringList rows = text.split(QLatin1Char('\n'));
    QCOMPARE(rows.size(), 4); // comment, two rows of the long profile, Branch
    QCOMPARE(rows[1], QStringLiteral("\"Long one\" L1 L2 L3 L4 L5"));
    QCOMPARE(rows[2], QStringLiteral("\"Long one\" L6 L7"));
    QCOMPARE(parseProfiles(text), (QList<ModelProfile>{seven, profiles[1]}));
    QVERIFY(profilesText({}).isEmpty());
}

void TestModelAnnotations::engineSectionsAreReadAndWrittenBack()
{
    const QString inp = dataDir() + QStringLiteral("/annotations.inp");
    Engine engine;
    QVERIFY(engine.handle);
    QCOMPARE(swmm_engine_open(engine.handle, inp.toUtf8().constData(), nullptr, nullptr, nullptr),
             SWMM_OK);

    QCOMPARE(parseLabels(readAnnotation(engine.handle, "LABELS")).size(), 3);
    const auto backdrop = parseBackdrop(readAnnotation(engine.handle, "BACKDROP"));
    QVERIFY(backdrop);
    QCOMPARE(backdrop->file, QStringLiteral("backdrop image.png"));
    QString tag;
    auto profiles = readProfiles(engine.handle, &tag);
    QCOMPARE(tag, QStringLiteral("PROFILE"));
    QCOMPARE(profiles, (QList<ModelProfile>{{QStringLiteral("Main line"),
                                             {QStringLiteral("C1"), QStringLiteral("C2"),
                                              QStringLiteral("C3")}}}));

    profiles.append({QStringLiteral("Lower reach"), {QStringLiteral("C3")}});
    QString error;
    QVERIFY2(writeProfiles(engine.handle, profiles, tag, &error), qPrintable(error));
    QCOMPARE(readProfiles(engine.handle), profiles);

    QVERIFY(QDir().mkpath(outputDir()));
    const QString written = outputDir() + QStringLiteral("/written.inp");
    QFile::remove(written);
    QCOMPARE(swmm_model_write(engine.handle, written.toUtf8().constData()), SWMM_OK);
    QCOMPARE(headerCount(written, QStringLiteral("PROFILE")), 1);
    QCOMPARE(headerCount(written, QStringLiteral("PROFILES")), 0);
    QCOMPARE(sectionLines(written, QStringLiteral("PROFILE")).size(), 3); // comment + 2 rows
    // Sections nobody edited are written back as they were read.
    QCOMPARE(sectionLines(written, QStringLiteral("LABELS")),
             sectionLines(inp, QStringLiteral("LABELS")));
    QCOMPARE(sectionLines(written, QStringLiteral("BACKDROP")),
             sectionLines(inp, QStringLiteral("BACKDROP")));
}

void TestModelAnnotations::writingProfilesClearsTheOtherSpelling()
{
    const QString inp = dataDir() + QStringLiteral("/annotations.inp");
    Engine engine;
    QCOMPARE(swmm_engine_open(engine.handle, inp.toUtf8().constData(), nullptr, nullptr, nullptr),
             SWMM_OK);
    QVERIFY(writeAnnotation(engine.handle, "PROFILES", QStringLiteral("\"Second\" C2")));
    QString tag;
    const auto both = readProfiles(engine.handle, &tag);
    QCOMPARE(tag, QStringLiteral("PROFILES")); // the plural spelling wins once it holds rows
    QCOMPARE(both.size(), 2);
    QVERIFY(writeProfiles(engine.handle, both, tag));
    QVERIFY(readAnnotation(engine.handle, "PROFILE").isEmpty());
    QCOMPARE(readProfiles(engine.handle), both);
    QVERIFY(writeProfiles(engine.handle, {}, tag));
    QVERIFY(readAnnotation(engine.handle, "PROFILES").isEmpty());
}

void TestModelAnnotations::profileLinksJoinEndToEnd()
{
    const QString inp = dataDir() + QStringLiteral("/annotations.inp");
    Engine engine;
    QCOMPARE(swmm_engine_open(engine.handle, inp.toUtf8().constData(), nullptr, nullptr, nullptr),
             SWMM_OK);
    const auto node = [&](const char *id) { return swmm_node_index(engine.handle, id); };
    const auto link = [&](const char *id) { return swmm_link_index(engine.handle, id); };
    QVector<int> nodes, links;
    QString error;
    QVERIFY2(profilePath(engine.handle, {"C1", "C2", "C3"}, &nodes, &links, &error),
             qPrintable(error));
    QCOMPARE(nodes, (QVector<int>{node("J1"), node("J2"), node("J3"), node("O1")}));
    QCOMPARE(links, (QVector<int>{link("C1"), link("C2"), link("C3")}));
    // Against the flow: the links are walked from the outfall up.
    QVERIFY2(profilePath(engine.handle, {"C3", "C2", "C1"}, &nodes, &links, &error),
             qPrintable(error));
    QCOMPARE(nodes, (QVector<int>{node("O1"), node("J3"), node("J2"), node("J1")}));
    QVERIFY(profilePath(engine.handle, {"C2"}, &nodes, &links));
    QCOMPARE(nodes, (QVector<int>{node("J2"), node("J3")}));

    QVERIFY(!profilePath(engine.handle, {"C1", "C3"}, &nodes, &links, &error));
    QVERIFY2(error.contains(QStringLiteral("C3")), qPrintable(error));
    QVERIFY(!profilePath(engine.handle, {"C1", "Gone"}, &nodes, &links, &error));
    QVERIFY2(error.contains(QStringLiteral("Gone")), qPrintable(error));
    QVERIFY(!profilePath(engine.handle, {}, &nodes, &links, &error));
}

void TestModelAnnotations::savingAProfileAddsOrReplaces()
{
    const QString inp = dataDir() + QStringLiteral("/annotations.inp");
    Engine engine;
    QCOMPARE(swmm_engine_open(engine.handle, inp.toUtf8().constData(), nullptr, nullptr, nullptr),
             SWMM_OK);
    QVERIFY(saveProfile(engine.handle, {QStringLiteral("Lower"), {QStringLiteral("C3")}}));
    QVERIFY(saveProfile(engine.handle,
                        {QStringLiteral("Main line"), {QStringLiteral("C1"), QStringLiteral("C2")}}));
    QString tag;
    const auto profiles = readProfiles(engine.handle, &tag);
    QCOMPARE(tag, QStringLiteral("PROFILE")); // the file's spelling is kept
    QCOMPARE(profiles,
             (QList<ModelProfile>{
                 {QStringLiteral("Main line"), {QStringLiteral("C1"), QStringLiteral("C2")}},
                 {QStringLiteral("Lower"), {QStringLiteral("C3")}}}));
}

void TestModelAnnotations::renamedLinksFollowIntoProfiles()
{
    const QString inp = dataDir() + QStringLiteral("/annotations.inp");
    Engine engine;
    QCOMPARE(swmm_engine_open(engine.handle, inp.toUtf8().constData(), nullptr, nullptr, nullptr),
             SWMM_OK);
    const QString before = readAnnotation(engine.handle, "PROFILE");
    QVERIFY(!renameProfileLink(engine.handle, QStringLiteral("C9"), QStringLiteral("C10")));
    QCOMPARE(readAnnotation(engine.handle, "PROFILE"), before); // nothing rewritten
    QVERIFY(renameProfileLink(engine.handle, QStringLiteral("C2"), QStringLiteral("C2b")));
    QCOMPARE(readProfiles(engine.handle).first().links,
             (QStringList{QStringLiteral("C1"), QStringLiteral("C2b"), QStringLiteral("C3")}));
}

QTEST_GUILESS_MAIN(TestModelAnnotations)
#include "test_modelannotations.moc"
