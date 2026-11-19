// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/meshrunlogwidget.h"
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QTableView>
#include <QTest>
#include <thread>

class TestMeshRunLogWidget : public QObject
{
    Q_OBJECT
private slots:
    void concurrentUpdatesFiltersExportAndRetention()
    {
        MeshRunLogWidget widget;
        widget.resize(1080, 500);
        widget.show();
        auto channel = std::make_shared<meshkit::EventChannel>();
        widget.beginRun(channel);
        std::thread worker([&] {
            meshkit::Event event;
            event.kind = meshkit::EventKind::Stage;
            event.stage = "Constraints";
            event.message = "All 308 holes retained; redundant boundary vertices simplified.";
            channel->publish(event);
            event.kind = meshkit::EventKind::Progress;
            event.stage = "Pass 1: Refinement";
            event.counts.triangles = 5'900'000;
            event.counts.quads = 100'000;
            for (int i = 0; i < 100000; ++i) {
                event.message = "6,000,000 cells (5,900,000 triangles, 100,000 quads)";
                event.counts.workingVertices = 3'100'000 + i;
                channel->publish(event);
            }
            event.kind = meshkit::EventKind::Diagnostic;
            event.severity = meshkit::Severity::Warning;
            event.stage = "Validation";
            event.message = "24 cells below target size beside protected passage P-18.";
            channel->publish(event);
            event.kind = meshkit::EventKind::Completed;
            event.status = meshkit::RunStatus::CompleteWithExceptions;
            event.message = "Completed with exceptions; all constraints retained.";
            channel->publish(event);
        });
        worker.join();
        widget.finish();
        auto *view = widget.findChild<QTableView *>("meshRunLogView");
        QVERIFY(view);
        QCOMPARE(view->model()->rowCount(), 4);
        auto *filter = widget.findChild<QComboBox *>("meshRunLogSeverity");
        filter->setCurrentIndex(1);
        QCOMPARE(view->model()->rowCount(), 2);
        auto *search = widget.findChild<QLineEdit *>("meshRunLogSearch");
        search->setText("P-18");
        QCOMPARE(view->model()->rowCount(), 1);
        search->clear();
        filter->setCurrentIndex(0);
        QDir output(QStringLiteral(MESH_RUN_LOG_TEST_OUTPUT));
        QVERIFY(QDir().mkpath(output.path()));
        QString error;
        const QString path = output.filePath("run-log.jsonl");
        QVERIFY2(widget.exportLog(path, &error), qPrintable(error));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto lines = file.readAll().trimmed().split('\n');
        QCOMPARE(lines.size(), 5);
        const auto counts = QJsonDocument::fromJson(lines[2]).object()["counts"].toObject();
        QCOMPARE(counts["cells"].toInteger(), qint64(6'000'000));
        QCOMPARE(counts["vertices"].toInteger(), qint64(-1));
        QVERIFY(widget.grab().save(output.filePath("run-log.png")));
        // A second run must never receive the first run's events or counters.
        auto next = std::make_shared<meshkit::EventChannel>();
        widget.beginRun(next);
        QCOMPARE(view->model()->rowCount(), 0);
        meshkit::Event note;
        note.message = "New run";
        for (int batch = 0; batch < 11; ++batch) {
            for (int i = 0; i < 1000; ++i)
                next->publish(note);
            widget.drain();
        }
        QCOMPARE(view->model()->rowCount(), 10000);
        QVERIFY(widget.text().contains("1000 earlier messages omitted"));
        widget.finish();
    }
};
QTEST_MAIN(TestMeshRunLogWidget)
#include "test_meshrunlogwidget.moc"
