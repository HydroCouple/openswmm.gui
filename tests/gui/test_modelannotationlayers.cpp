// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      test_modelannotationlayers.cpp
 * @brief     Review G-01: the model's [LABELS] and [BACKDROP] as map layers.
 * @details   A project window opens tests/gui/data/model_annotations/annotations.inp (copied with
 *            its backdrop image to tests/output/model_labels_g01/). The model's three labels
 *            become one "Model labels" text layer; moving, restyling, adding, deleting and undoing
 *            labels change [LABELS] in the engine, while changes [LABELS] cannot hold write
 *            nothing. A saved model and a reopened project show the labels once, with their text
 *            styles while the model agrees. The backdrop image is placed at its DIMENSIONS
 *            without files beside it, a missing image is reported and its entry kept, and a
 *            project keeps only how the backdrop is shown. Saved profiles follow link renames,
 *            the profile plot saves its links, and a model saved as a GeoPackage keeps all three.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "layers/annotationlayer.h"
#include "layers/annotationtextitem.h"
#include "layers/gisrasterlayer.h"
#include "layers/modelbackdroplayer.h"
#include "layers/modellabelslayer.h"
#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "map/textlayercommands.h"
#include "project/modelannotations.h"
#include "project/openswmmvisworkspace.h"
#include "project/projectserializer.h"
#include "swmmvisprojectwindow.h"
#include "ui/dialogs/profileplotdialog.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTest>
#include <QUndoStack>

using namespace openswmmvis::layers;
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
        .absoluteFilePath(QStringLiteral("../../output/model_labels_g01"));
}

/** A fresh copy of the fixture under @p name (and its backdrop image, unless told not to). */
QString freshDeck(const QString &name, bool withImage = true)
{
    const QString path = outputDir() + QLatin1Char('/') + name;
    const QString image = outputDir() + QStringLiteral("/backdrop image.png");
    QFile::remove(path);
    QFile::remove(image);
    if (withImage && !QFile::copy(dataDir() + QStringLiteral("/backdrop image.png"), image))
        return {};
    return QFile::copy(dataDir() + QStringLiteral("/annotations.inp"), path) ? path : QString();
}

/** The backdrop layer once its asynchronous open has finished. */
GISRasterLayer *openedBackdrop(SWMMVisProjectWindow *window)
{
    GISRasterLayer *backdrop = findModelBackdropLayer(window->canvas()->layers());
    if (backdrop && backdrop->filePath().isEmpty()) {
        QSignalSpy opened(backdrop, &GISRasterLayer::openFinished);
        if (!opened.wait(15000) || !opened.first().first().toBool())
            return nullptr;
    }
    return backdrop;
}

int rasterLayerCount(SWMMVisProjectWindow *window)
{
    int count = 0;
    for (OpenSWMMVisLayer *layer : window->canvas()->layers())
        count += qobject_cast<GISRasterLayer *>(layer) ? 1 : 0;
    return count;
}

SWMMVisProjectWindow *openWindow(const QString &deck, QStringList *warningsOut = nullptr)
{
    auto *workspace = OpenSWMMVisWorkspace::newInstance(QString(), nullptr);
    if (!workspace)
        return nullptr;
    auto *window = new SWMMVisProjectWindow(workspace, deck, nullptr);
    QList<QString> warnings, errors;
    if (!window->loadModel(warnings, errors)) {
        delete window;
        return nullptr;
    }
    if (warningsOut)
        *warningsOut = warnings;
    return window;
}

QList<OpenSWMMVisAnnotationLayer *> textLayers(SWMMVisProjectWindow *window)
{
    QList<OpenSWMMVisAnnotationLayer *> out;
    for (OpenSWMMVisLayer *layer : window->canvas()->layers())
        if (auto *text = qobject_cast<OpenSWMMVisAnnotationLayer *>(layer))
            out.append(text);
    return out;
}

QList<ModelLabel> engineLabels(SWMMVisProjectWindow *window)
{
    return parseLabels(readAnnotation(window->modelLayer()->engine(), "LABELS"));
}

QString engineLabelsText(SWMMVisProjectWindow *window)
{
    return readAnnotation(window->modelLayer()->engine(), "LABELS");
}
} // namespace

class TestModelAnnotationLayers : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void modelLabelsBecomeOneTextLayer();
    void editsAreWrittenToTheModel();
    void changesLabelsCannotHoldWriteNothing();
    void addDeleteAndUndoKeepTheModelInStep();
    void savedModelHoldsTheEditedLabels();
    void reopenedProjectShowsTheLabelsOnce();
    void projectStylesGiveWayWhenTheModelChanged();
    void backdropIsPlacedAtItsDimensions();
    void missingBackdropIsReportedAndKept();
    void projectKeepsHowTheBackdropIsShown();
    void renamedLinksFollowIntoSavedProfiles();
    void profilePlotSavesItsLinks();
    void geoPackageModelKeepsLabelsBackdropAndProfiles();
};

void TestModelAnnotationLayers::initTestCase()
{
    QVERIFY(QDir().mkpath(outputDir()));
}

void TestModelAnnotationLayers::modelLabelsBecomeOneTextLayer()
{
    const QString deck = freshDeck(QStringLiteral("open.inp"));
    QVERIFY(!deck.isEmpty());
    std::unique_ptr<SWMMVisProjectWindow> window(openWindow(deck));
    QVERIFY(window);
    const auto layers = textLayers(window.get());
    QCOMPARE(layers.size(), 1);
    OpenSWMMVisAnnotationLayer *labels = layers.first();
    QCOMPARE(labels, findModelLabelsLayer(window->canvas()->layers()));
    QCOMPARE(labels->name(), QStringLiteral("Model labels"));
    QCOMPARE(labels->annotations().size(), 3);
    const AnnotationTextItem *outfall = labels->annotations().at(1);
    QCOMPARE(outfall->text(), QStringLiteral("Outfall"));
    QCOMPARE(outfall->x(), 250.0);
    QCOMPARE(outfall->y(), -15.25);
    QCOMPARE(outfall->font().family(), QStringLiteral("Times New Roman"));
    QCOMPARE(outfall->font().pointSizeF(), 9.0);
    QVERIFY(!outfall->font().bold());
    QVERIFY(outfall->font().italic());
    QCOMPARE(labels->modelAnchor(outfall->id()), QStringLiteral("O1"));
    QVERIFY(sameModelLabels(modelLabelsOf(*labels), engineLabels(window.get())));
}

void TestModelAnnotationLayers::editsAreWrittenToTheModel()
{
    const QString deck = freshDeck(QStringLiteral("edit.inp"));
    std::unique_ptr<SWMMVisProjectWindow> window(openWindow(deck));
    QVERIFY(window);
    OpenSWMMVisAnnotationLayer *labels = findModelLabelsLayer(window->canvas()->layers());
    QVERIFY(labels);
    QSignalSpy edited(window->modelLayer(), &SWMMModelLayer::modelEdited);

    AnnotationTextItem *upper = labels->annotations().first();
    upper->setX(75.25);
    upper->setText(QStringLiteral("Upper reach (moved)"));
    QFont font = upper->font();
    font.setFamily(QStringLiteral("Courier New"));
    font.setPointSizeF(16);
    font.setItalic(true);
    upper->setFont(font);

    const auto written = engineLabels(window.get());
    QCOMPARE(written.size(), 3);
    QCOMPARE(written[0], (ModelLabel{75.25, 20.0, QStringLiteral("Upper reach (moved)"), QString(),
                                     QStringLiteral("Courier New"), 16.0, true, true}));
    QCOMPARE(written[1].anchor, QStringLiteral("O1")); // the anchor the GUI does not show survives
    QCOMPARE(written[1].text, QStringLiteral("Outfall"));
    QCOMPARE(written[2].text, QStringLiteral("Old style label"));
    QVERIFY(edited.count() > 0);
}

void TestModelAnnotationLayers::changesLabelsCannotHoldWriteNothing()
{
    const QString deck = freshDeck(QStringLiteral("style.inp"));
    std::unique_ptr<SWMMVisProjectWindow> window(openWindow(deck));
    QVERIFY(window);
    OpenSWMMVisAnnotationLayer *labels = findModelLabelsLayer(window->canvas()->layers());
    QVERIFY(labels);
    const QString before = engineLabelsText(window.get());
    QSignalSpy edited(window->modelLayer(), &SWMMModelLayer::modelEdited);

    labels->setAutoLayout(true);
    labels->annotations().first()->setFillColor(Qt::red);
    labels->annotations().last()->setHaloEnabled(true);

    QCOMPARE(engineLabelsText(window.get()), before); // byte for byte
    QCOMPARE(edited.count(), 0);
}

void TestModelAnnotationLayers::addDeleteAndUndoKeepTheModelInStep()
{
    const QString deck = freshDeck(QStringLiteral("undo.inp"));
    std::unique_ptr<SWMMVisProjectWindow> window(openWindow(deck));
    QVERIFY(window);
    OpenSWMMVisAnnotationLayer *labels = findModelLabelsLayer(window->canvas()->layers());
    QVERIFY(labels);
    QUndoStack stack;

    auto *added = new AnnotationTextItem;
    added->setText(QStringLiteral("New label"));
    added->setX(5);
    added->setY(-5);
    stack.push(new AddAnnotationCommand(labels, added, window->canvas()));
    QCOMPARE(engineLabels(window.get()).size(), 4);
    QCOMPARE(engineLabels(window.get()).last().text, QStringLiteral("New label"));

    const QJsonObject before = labels->layerJson();
    labels->removeAnnotation(labels->annotations().at(1)->id()); // the anchored Outfall label
    stack.push(new EditTextLayerCommand(labels, before, labels->layerJson(),
                                        QStringLiteral("Delete label")));
    QCOMPARE(engineLabels(window.get()).size(), 3);

    stack.undo(); // the delete
    const auto restored = engineLabels(window.get());
    QCOMPARE(restored.size(), 4);
    QCOMPARE(restored[1].text, QStringLiteral("Outfall"));
    QCOMPARE(restored[1].anchor, QStringLiteral("O1"));
    stack.undo(); // the add
    QCOMPARE(engineLabels(window.get()).size(), 3);
    stack.redo();
    QCOMPARE(engineLabels(window.get()).size(), 4);
}

void TestModelAnnotationLayers::savedModelHoldsTheEditedLabels()
{
    const QString deck = freshDeck(QStringLiteral("save_source.inp"));
    const QString saved = outputDir() + QStringLiteral("/saved.inp");
    QFile::remove(saved);
    {
        std::unique_ptr<SWMMVisProjectWindow> window(openWindow(deck));
        QVERIFY(window);
        OpenSWMMVisAnnotationLayer *labels = findModelLabelsLayer(window->canvas()->layers());
        labels->annotations().last()->setY(44.5);
        QString error;
        QVERIFY2(window->saveAs(saved, &error), qPrintable(error));
    }
    std::unique_ptr<SWMMVisProjectWindow> reopened(openWindow(saved));
    QVERIFY(reopened);
    OpenSWMMVisAnnotationLayer *labels = findModelLabelsLayer(reopened->canvas()->layers());
    QVERIFY(labels);
    QCOMPARE(labels->annotations().size(), 3);
    QCOMPARE(labels->annotations().last()->y(), 44.5);
    QCOMPARE(labels->modelAnchor(labels->annotations().at(1)->id()), QStringLiteral("O1"));
    // The sections the labels do not touch come back as they were.
    const auto backdrop = parseBackdrop(readAnnotation(reopened->modelLayer()->engine(), "BACKDROP"));
    QVERIFY(backdrop);
    QCOMPARE(backdrop->file, QStringLiteral("backdrop image.png"));
    QCOMPARE(readProfiles(reopened->modelLayer()->engine()).size(), 1);
}

void TestModelAnnotationLayers::reopenedProjectShowsTheLabelsOnce()
{
    const QString deck = freshDeck(QStringLiteral("project.inp"));
    const QString oswp = outputDir() + QStringLiteral("/project.oswp");
    QFile::remove(oswp);
    QString error;
    {
        std::unique_ptr<SWMMVisProjectWindow> window(openWindow(deck));
        QVERIFY(window);
        OpenSWMMVisAnnotationLayer *labels = findModelLabelsLayer(window->canvas()->layers());
        labels->setName(QStringLiteral("Street names"));
        labels->setOpacity(0.5);
        labels->annotations().first()->setFillColor(QColor(10, 120, 200));
        QVERIFY2(ProjectSerializer::saveToFile(oswp, window.get(), &error), qPrintable(error));
    }
    std::unique_ptr<SWMMVisProjectWindow> window(openWindow(deck));
    QVERIFY(window);
    QStringList warnings;
    QVERIFY2(ProjectSerializer::applyFromFile(oswp, window.get(), &error, &warnings),
             qPrintable(error));
    const auto layers = textLayers(window.get());
    QCOMPARE(layers.size(), 1);
    OpenSWMMVisAnnotationLayer *labels = layers.first();
    QCOMPARE(labels->modelSection(), QStringLiteral("LABELS"));
    QCOMPARE(labels->name(), QStringLiteral("Street names"));
    QCOMPARE(labels->opacity(), 0.5);
    QCOMPARE(labels->annotations().size(), 3);
    QCOMPARE(labels->annotations().first()->fillColor(), QColor(10, 120, 200));
    QCOMPARE(labels->modelAnchor(labels->annotations().at(1)->id()), QStringLiteral("O1"));
    QVERIFY2(!warnings.join(QLatin1Char('\n')).contains(QStringLiteral("map labels")),
             qPrintable(warnings.join(QLatin1Char('\n'))));
}

void TestModelAnnotationLayers::projectStylesGiveWayWhenTheModelChanged()
{
    const QString deck = freshDeck(QStringLiteral("changed.inp"));
    const QString oswp = outputDir() + QStringLiteral("/changed.oswp");
    QFile::remove(oswp);
    QString error;
    {
        std::unique_ptr<SWMMVisProjectWindow> window(openWindow(deck));
        QVERIFY(window);
        findModelLabelsLayer(window->canvas()->layers())
            ->annotations()
            .first()
            ->setFillColor(QColor(10, 120, 200));
        QVERIFY2(ProjectSerializer::saveToFile(oswp, window.get(), &error), qPrintable(error));
    }
    // Another program renames a label in the .inp.
    QFile file(deck);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    QString text = QString::fromUtf8(file.readAll());
    file.close();
    QVERIFY(text.contains(QStringLiteral("\"Upper reach\"")));
    text.replace(QStringLiteral("\"Upper reach\""), QStringLiteral("\"Upper channel\""));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text));
    file.write(text.toUtf8());
    file.close();

    std::unique_ptr<SWMMVisProjectWindow> window(openWindow(deck));
    QVERIFY(window);
    QStringList warnings;
    QVERIFY2(ProjectSerializer::applyFromFile(oswp, window.get(), &error, &warnings),
             qPrintable(error));
    const auto layers = textLayers(window.get());
    QCOMPARE(layers.size(), 1);
    QCOMPARE(layers.first()->annotations().first()->text(), QStringLiteral("Upper channel"));
    QVERIFY(layers.first()->annotations().first()->fillColor() != QColor(10, 120, 200));
    QVERIFY2(warnings.join(QLatin1Char('\n')).contains(QStringLiteral("map labels")),
             qPrintable(warnings.join(QLatin1Char('\n'))));
    QCOMPARE(engineLabels(window.get()).first().text, QStringLiteral("Upper channel"));
}

void TestModelAnnotationLayers::backdropIsPlacedAtItsDimensions()
{
    const QString deck = freshDeck(QStringLiteral("backdrop.inp"));
    QVERIFY(!deck.isEmpty());
    std::unique_ptr<SWMMVisProjectWindow> window(openWindow(deck));
    QVERIFY(window);
    QCOMPARE(rasterLayerCount(window.get()), 1);
    GISRasterLayer *backdrop = openedBackdrop(window.get());
    QVERIFY(backdrop);
    QCOMPARE(backdrop->name(), QStringLiteral("Backdrop"));
    const MapExtent extent = backdrop->extent();
    QCOMPARE(extent.xMin(), -10.0);
    QCOMPARE(extent.yMin(), -50.0);
    QCOMPARE(extent.xMax(), 310.0);
    QCOMPARE(extent.yMax(), 50.0);
    // The image's top-left quadrant is red and its bottom-left green: the right way up.
    bool ok = false;
    QCOMPARE(backdrop->valueAt(30, 30, nullptr, 1, &ok), 255.0);
    QVERIFY(ok);
    QCOMPARE(backdrop->valueAt(30, 30, nullptr, 2, &ok), 0.0);
    QCOMPARE(backdrop->valueAt(30, -30, nullptr, 2, &ok), 255.0);
    QCOMPARE(backdrop->valueAt(290, 30, nullptr, 3, &ok), 255.0); // top-right blue
    // Nothing was written beside the image.
    const QStringList beside = QDir(outputDir()).entryList(
        {QStringLiteral("backdrop image*")}, QDir::Files);
    QCOMPARE(beside, QStringList{QStringLiteral("backdrop image.png")});
}

void TestModelAnnotationLayers::missingBackdropIsReportedAndKept()
{
    const QString deck = freshDeck(QStringLiteral("no_image.inp"), /*withImage=*/false);
    QVERIFY(!deck.isEmpty());
    const QString saved = outputDir() + QStringLiteral("/no_image_saved.inp");
    QFile::remove(saved);
    QStringList warnings;
    std::unique_ptr<SWMMVisProjectWindow> window(openWindow(deck, &warnings));
    QVERIFY(window);
    QVERIFY(!findModelBackdropLayer(window->canvas()->layers()));
    QVERIFY2(warnings.join(QLatin1Char('\n')).contains(QStringLiteral("was not found")),
             qPrintable(warnings.join(QLatin1Char('\n'))));
    QString error;
    QVERIFY2(window->saveAs(saved, &error), qPrintable(error));
    const auto backdrop = parseBackdrop(readAnnotation(window->modelLayer()->engine(), "BACKDROP"));
    QVERIFY(backdrop);
    QCOMPARE(backdrop->file, QStringLiteral("backdrop image.png"));
    QFile file(saved);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    QVERIFY(QString::fromUtf8(file.readAll()).contains(QStringLiteral("\"backdrop image.png\"")));
}

void TestModelAnnotationLayers::projectKeepsHowTheBackdropIsShown()
{
    const QString deck = freshDeck(QStringLiteral("backdrop_project.inp"));
    const QString oswp = outputDir() + QStringLiteral("/backdrop_project.oswp");
    QFile::remove(oswp);
    QString error;
    {
        std::unique_ptr<SWMMVisProjectWindow> window(openWindow(deck));
        QVERIFY(window);
        GISRasterLayer *backdrop = openedBackdrop(window.get());
        QVERIFY(backdrop);
        backdrop->setName(QStringLiteral("Site plan"));
        backdrop->setOpacity(0.4);
        QVERIFY2(ProjectSerializer::saveToFile(oswp, window.get(), &error), qPrintable(error));
    }
    QFile file(oswp);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QString saved = QString::fromUtf8(file.readAll());
    QVERIFY(saved.contains(QStringLiteral("modelBackdrop")));
    QVERIFY(saved.contains(QStringLiteral("model|BACKDROP"))); // its place in the layer order
    QVERIFY2(!saved.contains(QStringLiteral("/vsimem/")), "the in-memory image must not be saved");

    std::unique_ptr<SWMMVisProjectWindow> window(openWindow(deck));
    QVERIFY(window);
    QStringList warnings;
    QVERIFY2(ProjectSerializer::applyFromFile(oswp, window.get(), &error, &warnings),
             qPrintable(error));
    QCOMPARE(rasterLayerCount(window.get()), 1);
    GISRasterLayer *backdrop = openedBackdrop(window.get());
    QVERIFY(backdrop);
    QCOMPARE(backdrop->name(), QStringLiteral("Site plan"));
    QCOMPARE(backdrop->opacity(), 0.4);
}

void TestModelAnnotationLayers::renamedLinksFollowIntoSavedProfiles()
{
    const QString deck = freshDeck(QStringLiteral("rename.inp"));
    std::unique_ptr<SWMMVisProjectWindow> window(openWindow(deck));
    QVERIFY(window);
    QSignalSpy edited(window->modelLayer(), &SWMMModelLayer::modelEdited);
    QVERIFY(window->modelLayer()->applyRename(QStringLiteral("C2"), QStringLiteral("C2b"),
                                              SWMMModelLayer::kKindLink));
    QCOMPARE(readProfiles(window->modelLayer()->engine()).first().links,
             (QStringList{QStringLiteral("C1"), QStringLiteral("C2b"), QStringLiteral("C3")}));
    QVERIFY(edited.count() > 0);
    // A renamed node is not a link: the profiles are left alone.
    const QString before = readAnnotation(window->modelLayer()->engine(), "PROFILE");
    QVERIFY(window->modelLayer()->applyRename(QStringLiteral("J3"), QStringLiteral("C1"),
                                              SWMMModelLayer::kKindNode));
    QCOMPARE(readAnnotation(window->modelLayer()->engine(), "PROFILE"), before);
}

void TestModelAnnotationLayers::profilePlotSavesItsLinks()
{
    const QString deck = freshDeck(QStringLiteral("plot_profile.inp"));
    std::unique_ptr<SWMMVisProjectWindow> window(openWindow(deck));
    QVERIFY(window);
    const SWMM_Engine engine = window->modelLayer()->engine();
    ProfileRouter::Path path;
    QString error;
    QVERIFY2(profilePath(engine, {QStringLiteral("C2"), QStringLiteral("C3")}, &path.nodes,
                         &path.linkIds, &error),
             qPrintable(error));
    ProfilePlotDialog plot(window->modelLayer(), nullptr, path, window.get());
    QSignalSpy edited(window->modelLayer(), &SWMMModelLayer::modelEdited);
    QVERIFY2(plot.saveProfileAs(QStringLiteral("  Lower reach "), &error), qPrintable(error));
    QVERIFY(edited.count() > 0);
    QVERIFY2(plot.saveProfileAs(QStringLiteral("Main line"), &error), qPrintable(error));
    const auto profiles = readProfiles(engine);
    QCOMPARE(profiles.size(), 2); // "Main line" was replaced, not added again
    QCOMPARE(profiles[0], (ModelProfile{QStringLiteral("Main line"),
                                        {QStringLiteral("C2"), QStringLiteral("C3")}}));
    QCOMPARE(profiles[1], (ModelProfile{QStringLiteral("Lower reach"),
                                        {QStringLiteral("C2"), QStringLiteral("C3")}}));
    QVERIFY(!plot.saveProfileAs(QStringLiteral("   "), &error));
    QVERIFY(plot.findChild<QAction *>(QStringLiteral("saveProfile")));
}

void TestModelAnnotationLayers::geoPackageModelKeepsLabelsBackdropAndProfiles()
{
    const QString deck = freshDeck(QStringLiteral("to_gpkg.inp"));
    const QString gpkg = outputDir() + QStringLiteral("/saved.gpkg");
    QFile::remove(gpkg);
    QString error;
    {
        std::unique_ptr<SWMMVisProjectWindow> window(openWindow(deck));
        QVERIFY(window);
        findModelLabelsLayer(window->canvas()->layers())->annotations().first()->setX(80);
        QVERIFY(saveProfile(window->modelLayer()->engine(),
                            {QStringLiteral("Lower"), {QStringLiteral("C3")}}));
        QVERIFY2(window->saveAs(gpkg, &error), qPrintable(error));
    }
    std::unique_ptr<SWMMVisProjectWindow> window(openWindow(gpkg));
    QVERIFY(window);
    const SWMM_Engine engine = window->modelLayer()->engine();
    OpenSWMMVisAnnotationLayer *labels = findModelLabelsLayer(window->canvas()->layers());
    QVERIFY(labels);
    QCOMPARE(labels->annotations().size(), 3);
    QCOMPARE(labels->annotations().first()->x(), 80.0);
    QCOMPARE(labels->modelAnchor(labels->annotations().at(1)->id()), QStringLiteral("O1"));
    // Written as the GUI wrote it, column comment included, with no blank line added.
    QVERIFY(readAnnotation(engine, "LABELS").startsWith(QStringLiteral(";;X-Coord")));
    QVERIFY(!readAnnotation(engine, "LABELS").endsWith(QLatin1Char('\n')));
    QCOMPARE(readProfiles(engine).size(), 2);
    QVERIFY(openedBackdrop(window.get())); // the image beside the GeoPackage is found
}

QTEST_MAIN(TestModelAnnotationLayers)
#include "test_modelannotationlayers.moc"
