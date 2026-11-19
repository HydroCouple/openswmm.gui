// GPL-3.0-or-later
// Replacement-plan phases 0–1 and composition feasibility experiment.

/**
 * @file      flowparticleprototype.cpp
 * @brief     Tests for flowparticleprototype.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "render/flowparticles.h"
#include "map/flowparticlenode.h"
#include <QApplication>
#include <QQuickWidget>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTransformNode>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QTimer>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <utility>
using namespace OpenSWMM::Render;

/**
 *
 * @brief Build the immutable synthetic particle field for the visible prototype.
 * @param curved True selects an 80x80 rotational field with angular speed .1/s;
 * false selects a 200x60 uniform channel with velocity (3.5,0) scene units/second.
 * @return Shared owned snapshot with unit-grid triangles, prepared edge topology and
 * spatial index. Geometry ID is 1 for curved/2 for straight, scene XY has no physical
 * CRS. Mesh preparation result is unchecked; no engine data or files are touched.
 *
 */
static std::shared_ptr<FlowParticleSnapshot> makeField(bool curved)
{
    const int columns = curved ? 80 : 200, rows = curved ? 80 : 60;
    auto mesh = std::make_shared<FlowParticleMesh>();
    QVector<QRectF> boxes;
    for (int y = 0; y <= rows; ++y)
        for (int x = 0; x <= columns; ++x)
            mesh->vertices.append({double(x), double(y)});
    for (int y = 0; y < rows; ++y)
        for (int x = 0; x < columns; ++x) {
            const int a = y * (columns + 1) + x, b = a + 1, c = a + columns + 2, d = c - 1;
            mesh->triangles.push_back({a, b, c});
            mesh->triangles.push_back({a, c, d});
            boxes.append({double(x), double(y), 1, 1});
            boxes.append(boxes.back());
        }
    mesh->coverage = {0, 0, double(columns), double(rows)};
    mesh->grid.rebuild(boxes);
    std::atomic_bool cancel = false;
    mesh->prepare(cancel);
    auto field = std::make_shared<FlowParticleSnapshot>();
    field->mesh = mesh;
    field->geometry = curved ? 1 : 2;
    for (const auto &tri : mesh->triangles) {
        FlowParticleField f;
        for (int i = 0; i < 3; ++i) {
            const auto p = mesh->vertices[tri[i]] - mesh->coverage.center();
            f.velocity[i] = curved ? QPointF(-p.y() * .1, p.x() * .1) : QPointF(3.5, 0);
        }
        field->fields.push_back(f);
    }
    return field;
}
/**
 *
 * @brief Rasterize a static blue triangle-wireframe background at 12 pixels/unit.
 * @param mesh Borrowed valid mesh with positive coverage and in-range triangle indices.
 * @return Owned RGB image using scene XY, without axis inversion. Coverage dimensions
 * must fit int pixel sizes; no validation/allocation status check. Does not upload
 * GPU textures or alter the mesh.
 *
 */
static QImage background(const FlowParticleMesh &mesh)
{
    const auto extent = mesh.coverage;
    const double scale = 12;
    QImage image(int(extent.width() * scale), int(extent.height() * scale), QImage::Format_RGB32);
    image.fill(QColor(20, 68, 130));
    QPainter painter(&image);
    painter.scale(scale, scale);
    painter.setPen(QPen(QColor(34, 91, 151), .035));
    for (const auto &tri : mesh.triangles)
        painter.drawPolygon(
            QPolygonF{mesh.vertices[tri[0]], mesh.vertices[tri[1]], mesh.vertices[tri[2]]});
    return image;
}
class Preview : public QQuickItem
{
public:
    std::shared_ptr<const FlowParticleFrame> frame;
    QImage base;
    QRectF extent;
    double alpha = 1, zoom = 1;
    QPointF pan;
    int backgroundUploads = 0, particleUploads = 0;
    std::vector<double> geometryTimes;
    /**
     *
     * @brief Construct an interactive Quick particle preview.
     * @param parent Borrowed visual/QObject parent owning the new item, normally the
     * Quick widget root. GUI thread only. Enables scene contents and left-button events;
     * no frame, field or texture is supplied until the prototype publishes them.
     *
     */
    explicit Preview(QQuickItem *parent) : QQuickItem(parent)
    {
        setFlag(ItemHasContents);
        setAcceptedMouseButtons(Qt::LeftButton);
    }

protected:
    struct Root : QSGTransformNode {
        QSGSimpleTextureNode *base = nullptr;
        FlowParticleNode *trails = nullptr;
        qint64 imageKey = 0;
    };
    /**
     *
     * @brief Synchronize particle trails and cached background into the Qt scene graph.
     * @param old Nullable prior Root node belonging to this item, borrowed from Qt;
     * reused when supplied. Must have the expected private node type.
     * @param data Unused borrowed Qt callback metadata, neither retained nor dereferenced.
     * @return Qt-owned root with owned background texture and FlowParticleNode children.
     * Creates/replaces background only on changed nonnull image key; fits valid extent
     * with pan/zoom and synchronizes current frame/alpha. Updates upload counters and
     * geometry timing samples on the render/synchronization callback. Requires live
     * window and Qt-synchronized item state. Allocation failures propagate without rollback;
     * external engine data is not accessed and textures are not read back.
     *
     */
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *data) override
    {
        auto *root = old ? static_cast<Root *>(old) : new Root;
        if (!root->trails) {
            root->trails = new FlowParticleNode(window());
            root->appendChildNode(root->trails);
        }
        if (!base.isNull() && root->imageKey != base.cacheKey()) {
            if (root->base) {
                root->removeChildNode(root->base);
                delete root->base;
            }
            root->base = new QSGSimpleTextureNode;
            root->base->setTexture(window()->createTextureFromImage(base));
            root->base->setOwnsTexture(true);
            root->base->setRect(extent);
            root->prependChildNode(root->base);
            root->imageKey = base.cacheKey();
            ++backgroundUploads;
        }
        if (extent.isValid()) {
            const double scale =
                std::min(width() / extent.width(), height() / extent.height()) * zoom;
            QMatrix4x4 matrix;
            matrix.translate(float(width() / 2 + pan.x()), float(height() / 2 + pan.y()));
            matrix.scale(float(scale));
            matrix.translate(float(-extent.center().x()), float(-extent.center().y()));
            root->setMatrix(matrix);
            const int before = root->trails->uploads();
            root->trails->sync(frame, alpha, scale, 1, 1.5, 2, .5, 1);
            particleUploads = root->trails->uploads();
            if (particleUploads != before)
                geometryTimes.push_back(root->trails->geometryBuildMs());
        }
        return root;
    }
    /**
     *
     * @brief Start a drag by retaining the current local mouse position.
     * @param event Nonnull borrowed Qt mouse event, accepted before return.
     * GUI-thread callback; no ownership retained beyond the copied point or repaint here.
     *
     */
    void mousePressEvent(QMouseEvent *event) override
    {
        last = event->position();
        event->accept();
    }
    /**
     *
     * @brief Accumulate local-pixel drag motion into the preview pan offset.
     * @param event Nonnull borrowed Qt mouse event; accepted after requesting update.
     * Uses the prior press/move point, copies current position and schedules scene sync;
     * does not change particle world coordinates or transfer event ownership.
     *
     */
    void mouseMoveEvent(QMouseEvent *event) override
    {
        pan += event->position() - last;
        last = event->position();
        update();
        event->accept();
    }
    /**
     *
     * @brief Apply bounded exponential wheel zoom and schedule scene synchronization.
     * @param event Nonnull borrowed Qt wheel event; vertical angleDelta controls zoom
     * via exp(delta/1200), clamped to [.5,4], then the event is accepted.
     * GUI thread only; no cursor anchoring, particle-state edits or ownership retention.
     *
     */
    void wheelEvent(QWheelEvent *event) override
    {
        zoom = std::clamp(zoom * std::exp(event->angleDelta().y() / 1200.), .5, 4.);
        update();
        event->accept();
    }

private:
    QPointF last;
};
class VisibleCanvas : public QQuickWidget
{
public:
    std::vector<double> paintIntervals;
    QElapsedTimer clock;
    quint64 paints = 0;
    /**
     *
     * @brief Construct the embedded Quick canvas and start visible-paint timing.
     * GUI thread only; root follows view size and clear color is fixed dark blue.
     * Records widget paint cadence rather than OS presentation timing; no field is loaded.
     *
     */
    VisibleCanvas()
    {
        clock.start();
        setResizeMode(SizeRootObjectToView);
        setClearColor(QColor(16, 25, 38));
    }

protected:
    /**
     *
     * @brief Record wall-clock widget paint intervals, then run normal Quick painting.
     * @param event Borrowed nonnull Qt paint event passed to QQuickWidget's handler.
     * Appends milliseconds since prior paint and increments count; first interval is
     * omitted. GUI callback only; vector growth can allocate. No presentation guarantee.
     *
     */
    void paintEvent(QPaintEvent *event) override
    {
        const double now = clock.nsecsElapsed() / 1e6;
        if (previous > 0)
            paintIntervals.push_back(now - previous);
        previous = now;
        ++paints;
        QQuickWidget::paintEvent(event);
    }

private:
    double previous = -1;
};
struct Packet {
    std::shared_ptr<const FlowParticleFrame> frame;
    std::shared_ptr<const FlowParticleSnapshot> field;
    double alpha = 1, lag = 0, workerMs = 0;
    int steps = 0;
};
struct WorkerState {
    FlowParticleSystem system;
    std::shared_ptr<const FlowParticleSnapshot> field;
    double birthsDue = 0;
    quint64 seed = 1;
    bool curved = true;
    /**
     *
     * @brief Replace worker simulation state with a freshly prepared synthetic scene.
     * @param curve True selects rotation, false uniform channel.
     * Worker's serialized thread only: clears particles/history via a new system, builds/
     * sets field, resets seed to 1 and requests one initial birth. Allocation/preparation
     * failure is not caught or transactional across member assignments.
     *
     */
    void reset(bool curve)
    {
        curved = curve;
        system = FlowParticleSystem();
        field = makeField(curve);
        system.setField(field);
        birthsDue = 1;
        seed = 1;
    }
    /**
     *
     * @brief Attempt one deterministic worker particle birth and advance the seed.
     * Requires reset/setField and exclusive worker access. Rotation seeds radii 12..32
     * around (40,40); uniform seeds an interior rectangle. Passes the preincrement seed
     * to production spawn; result is ignored, so the seed advances even for refusal.
     * Does not replenish birthsDue or publish a GUI frame itself.
     *
     */
    void spawn()
    {
        const double a = double((seed * 7919) % 65521) / 65521.,
                     b = double((seed * 104729) % 65521) / 65521.;
        const double theta = a * 2 * 3.14159265358979323846, radius = 12 + 20 * b;
        const QPointF position =
            curved ? QPointF(40 + radius * std::cos(theta), 40 + radius * std::sin(theta))
                   : QPointF(5 + 45 * a, 5 + 50 * b);
        system.spawn(position, seed++);
    }
};
class Prototype : public QWidget
{
public:
    /**
     *
     * @brief Construct and start the visible worker/render composition experiment.
     * @param verifySeconds Positive wall seconds enables timed noninteractive verification;
     * zero/negative runs interactively until closed. Finite values intended.
     * @param output By-value writable persistent directory path, moved into the window.
     * Attempts creation and truncates tracked-particle.csv; IO status is unchecked.
     * @param startCurved True starts rotational scene; false uniform channel.
     * @param particles Positive target population, normally main-clamped to 1..2000.
     * GUI thread required. Owns widgets/timer and shared worker state, serializes particle
     * integration through a one-thread pool and publishes immutable frame packets via a
     * watcher. 8-ms timer accrues elapsed visible/unpaused time, limits integration work,
     * and handles scene/reset/reverse controls. Constructor failures have no file rollback;
     * this is a synthetic visible prototype, not an engine or large-mesh benchmark.
     *
     */
    Prototype(double verifySeconds, QString output, bool startCurved, int particles)
        : duration(verifySeconds), outputDir(std::move(output)), targetParticles(particles)
    {
        setWindowTitle("Continuous flow particles — first-stage prototype");
        resize(1440, 900);
        if (duration > 0)
            setWindowFlags(windowFlags() | Qt::WindowTransparentForInput |
                           Qt::WindowDoesNotAcceptFocus | Qt::WindowStaysOnTopHint);
        QDir().mkpath(outputDir);
        trace.setFileName(outputDir + "/tracked-particle.csv");
        trace.open(QIODevice::WriteOnly);
        trace.write("clock,particle_id,age,lifetime,cell,crossings,x,y,status\n");
        auto *layout = new QVBoxLayout(this);
        auto *controls = new QHBoxLayout;
        layout->addLayout(controls);
        auto *scene = new QComboBox;
        scene->addItems({"Curved flow", "Uniform channel"});
        scene->setCurrentIndex(startCurved ? 0 : 1);
        controls->addWidget(scene);
        auto *pause = new QPushButton("Pause");
        controls->addWidget(pause);
        auto *reverse = new QPushButton("Reverse field");
        controls->addWidget(reverse);
        controls->addStretch();
        if (duration > 0) {
            scene->setEnabled(false);
            pause->setEnabled(false);
            reverse->setEnabled(false);
        }
        controls->addWidget(new QLabel("Orange: particle 1 · drag to pan · scroll to zoom"));
        canvas = new VisibleCanvas;
        canvas->setMinimumSize(640, 480);
        layout->addWidget(canvas, 1);
        canvas->setContent(QUrl(), nullptr, new QQuickItem);
        preview = new Preview(canvas->rootObject());
        connect(canvas->quickWindow(), &QQuickWindow::widthChanged, this,
                [this] { resizePreview(); });
        connect(canvas->quickWindow(), &QQuickWindow::heightChanged, this,
                [this] { resizePreview(); });
        resizePreview();
        status = new QLabel("Preparing immutable field…");
        layout->addWidget(status);
        tracked = new QLabel("Particle 1 will keep its ID and recorded trail until death.");
        layout->addWidget(tracked);
        connect(scene, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int i) {
            reset = i;
            pendingElapsed = 0;
            preview->pan = {};
            preview->zoom = 1;
        });
        connect(pause, &QPushButton::clicked, this, [this, pause] {
            paused = !paused;
            pause->setText(paused ? "Resume" : "Pause");
            elapsed.restart();
        });
        connect(reverse, &QPushButton::clicked, this, [this] { reversePending = !reversePending; });
        pool.setMaxThreadCount(1);
        worker = std::make_shared<WorkerState>();
        reset = startCurved ? 0 : 1;
        connect(&watcher, &QFutureWatcher<Packet>::finished, this, [this] {
            busy = false;
            const auto packet = watcher.result();
            if (displayedMesh != packet.field->mesh) {
                displayedMesh = packet.field->mesh;
                preview->base = background(*displayedMesh);
                preview->extent = displayedMesh->coverage;
            }
            preview->frame = packet.frame;
            preview->alpha = packet.alpha;
            preview->update();
            workerTimes.push_back(packet.workerMs);
            steps += packet.steps;
            lag = packet.lag;
            const auto &frame = *packet.frame;
            status->setText(
                QString(
                    "%1 particles · %2 births · %3 deaths · %4 s integrated · worker %5 ms · lag %6 s")
                    .arg(frame.particles.size())
                    .arg(frame.births)
                    .arg(frame.deaths)
                    .arg(frame.clock, 0, 'f', 2)
                    .arg(packet.workerMs, 0, 'f', 2)
                    .arg(lag, 0, 'f', 3));
            for (const auto &p : frame.particles)
                if (p.id == 1) {
                    tracked->setText(
                        QString("Particle %1 · cell %2 · %3 crossings · age %4 / %5 s · %6")
                            .arg(p.id)
                            .arg(p.cell)
                            .arg(p.crossings)
                            .arg(p.age, 0, 'f', 2)
                            .arg(p.lifetime, 0, 'f', 2)
                            .arg(p.waitingForData   ? "waiting for data"
                                 : p.historyLimited ? "shortened tail"
                                                    : flowParticleStopName(p.stop)));
                    QTextStream stream(&trace);
                    stream << frame.clock << ',' << p.id << ',' << p.age << ',' << p.lifetime << ','
                           << p.cell << ',' << p.crossings << ',' << p.position.x() << ','
                           << p.position.y() << ',' << flowParticleStopName(p.stop) << '\n';
                    trackedCrossings = std::max(trackedCrossings, p.crossings);
                    trackedDied |= p.stop == FlowParticleStop::Lifetime;
                }
            if (finishing)
                finish();
        });
        timer.setInterval(8);
        timer.setTimerType(Qt::PreciseTimer);
        elapsed.start();
        wall.start();
        connect(&timer, &QTimer::timeout, this, [this] {
            const double dt = elapsed.nsecsElapsed() / 1e9;
            elapsed.restart();
            if (!paused && isVisible() && !isMinimized())
                pendingElapsed += dt;
            if (duration > 0 && wall.elapsed() / 1000. >= duration) {
                finishing = true;
                timer.stop();
                if (!busy)
                    finish();
                return;
            }
            if (busy)
                return;
            const double requested = pendingElapsed;
            pendingElapsed = 0;
            const int resetScene = std::exchange(reset, -1);
            const bool reverse = std::exchange(reversePending, false);
            busy = true;
            watcher.setFuture(QtConcurrent::run(&pool, [state = worker, requested, resetScene,
                                                        reverse, target = targetParticles] {
                QElapsedTimer work;
                work.start();
                if (resetScene >= 0)
                    state->reset(resetScene == 0);
                if (reverse) {
                    auto next = std::make_shared<FlowParticleSnapshot>(*state->field);
                    ++next->revision;
                    for (auto &f : next->fields)
                        for (auto &v : f.velocity)
                            v = -v;
                    state->field = next;
                    state->system.setField(next);
                }
                state->system.eraseFinished();
                state->birthsDue += requested * (target / 4.);
                while (state->birthsDue >= 1 && int(state->system.particles().size()) < target) {
                    state->spawn();
                    state->birthsDue -= 1;
                }
                state->birthsDue = std::min(1., state->birthsDue);
                const auto result = state->system.advance(requested, 16384);
                return Packet{state->system.frame(),         state->field,
                              state->system.interpolation(), state->system.lagSeconds(),
                              work.nsecsElapsed() / 1e6,     result.steps};
            }));
        });
        timer.start();
    }
    /**
     *
     * @brief Stop scheduling, disconnect result publication and wait for active work.
     * GUI-thread destruction joins the owned watcher before member/shared-state cleanup;
     * Qt parent ownership releases child widgets. Persistent CSV/JSON/PNG files remain.
     *
     */
    ~Prototype() override
    {
        timer.stop();
        disconnect(&watcher, nullptr, this, nullptr);
        watcher.waitForFinished();
    }

private:
    /**
     *
     * @brief Make the preview item match the embedded canvas logical size.
     * GUI-thread helper requires constructed nonnull canvas/preview. Schedules no explicit
     * particle integration and preserves pan/zoom/frame state.
     *
     */
    void resizePreview() { preview->setSize(canvas->size()); }
    /**
     *
     * @brief Write final continuity/timing evidence, capture one image and exit the app.
     * Requires GUI thread, completed worker publication and constructed preview/canvas;
     * timer/watcher coordination must prevent concurrent worker mutation. Flushes trace,
     * writes prototype.json, saves prototype.png and requests exit 0 only for tracked
     * particle crossings>=100, own-lifetime death, lag<1 second and no unexpected stops;
     * otherwise exit 1. IO statuses do not affect that result. Timing is worker/widget
     * cadence, not OS presentation or engine-scale performance; screenshot readback
     * occurs after measured animation. Replaces existing outputs without transaction.
     *
     */
    void finish()
    {
        trace.flush();
        QJsonObject report;
        const auto frame = preview->frame;
        report["wallSeconds"] = wall.elapsed() / 1000.;
        report["integratedSeconds"] = frame ? frame->clock : 0;
        report["births"] = frame ? double(frame->births) : 0;
        report["deaths"] = frame ? double(frame->deaths) : 0;
        report["trackedParticleCrossings"] = trackedCrossings;
        report["trackedParticleDiedAtOwnLifetime"] = trackedDied;
        report["workerSteps"] = double(steps);
        report["integrationLagSeconds"] = lag;
        report["backgroundTextureUploads"] = preview->backgroundUploads;
        report["historyGeometryUploads"] = preview->particleUploads;
        report["animationReadbacks"] = 0;
        report["visibleWidgetPaints"] = double(canvas->paints);
        report["logicalWidth"] = canvas->width();
        report["logicalHeight"] = canvas->height();
        report["devicePixelRatio"] = canvas->devicePixelRatioF();
        auto distribution = [](std::vector<double> values) {
            std::sort(values.begin(), values.end());
            if (values.empty())
                return QJsonObject{};
            return QJsonObject{{"medianMs", values[values.size() / 2]},
                               {"p95Ms", values[size_t((values.size() - 1) * .95)]},
                               {"maxMs", values.back()}};
        };
        report["workerTime"] = distribution(workerTimes);
        report["visibleWidgetPaintInterval"] = distribution(canvas->paintIntervals);
        report["historyGeometryBuildTime"] = distribution(preview->geometryTimes);
        report["note"] =
            "Visible Qt widget paint cadence; not a claim of OS display presentation or six-million-cell performance. Screenshot is the only explicit readback, taken after measurements.";
        const bool passed = frame && trackedCrossings >= 100 && trackedDied && lag < 1;
        report["continuityPassed"] = passed;
        if (frame) {
            QJsonObject stops;
            quint64 unexpected = 0;
            for (size_t i = 1; i < frame->stopCounts.size(); ++i) {
                stops[flowParticleStopName(FlowParticleStop(i))] = double(frame->stopCounts[i]);
                if (i != size_t(FlowParticleStop::Lifetime))
                    unexpected += frame->stopCounts[i];
            }
            report["stopReasons"] = stops;
            report["unexpectedStops"] = double(unexpected);
            report["continuityPassed"] = passed && unexpected == 0;
        }
        QFile file(outputDir + "/prototype.json");
        if (file.open(QIODevice::WriteOnly))
            file.write(QJsonDocument(report).toJson());
        canvas->grabFramebuffer().save(outputDir + "/prototype.png");
        QApplication::exit(report["continuityPassed"].toBool() ? 0 : 1);
    }
    VisibleCanvas *canvas = nullptr;
    Preview *preview = nullptr;
    QLabel *status = nullptr, *tracked = nullptr;
    QFutureWatcher<Packet> watcher;
    QThreadPool pool;
    std::shared_ptr<WorkerState> worker;
    std::shared_ptr<const FlowParticleMesh> displayedMesh;
    QTimer timer;
    QElapsedTimer elapsed, wall;
    QFile trace;
    std::vector<double> workerTimes;
    double pendingElapsed = 0, duration = 0, lag = 0;
    QString outputDir;
    int reset = 0, trackedCrossings = 0, targetParticles = 256;
    bool paused = false, reversePending = false, finishing = false, trackedDied = false,
         busy = false;
    quint64 steps = 0;
};
/**
 *
 * @brief Run the interactive or timed visible particle composition prototype.
 * @param argc Process argument count, consumed by QApplication before option scanning.
 * @param argv Borrowed Qt/process strings; supports --verify-seconds value, --output
 * path, --uniform and --particles value. Numeric conversion errors are unchecked;
 * particles clamp to 1..2000 and unknown/incomplete options are ignored.
 * @return Qt event-loop result: timed finish uses continuity exit 0/1; ordinary window
 * close follows Qt's result. Creates/replaces persistent trace/report/image outputs
 * under explicit path or FLOW_PARTICLE_PROTOTYPE_OUTPUT. Requires native GUI/Quick
 * support and does not run an engine model or establish OS display throughput.
 *
 */
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("Flow Particle Prototype");
    const auto args = app.arguments();
    double seconds = 0;
    QString out = QStringLiteral(FLOW_PARTICLE_PROTOTYPE_OUTPUT);
    bool curved = true;
    int particles = 256;
    for (int i = 1; i < args.size(); ++i) {
        if (args[i] == "--verify-seconds" && i + 1 < args.size())
            seconds = args[++i].toDouble();
        else if (args[i] == "--output" && i + 1 < args.size())
            out = args[++i];
        else if (args[i] == "--uniform")
            curved = false;
        else if (args[i] == "--particles" && i + 1 < args.size())
            particles = std::clamp(args[++i].toInt(), 1, 2000);
    }
    Prototype prototype(seconds, out, curved, particles);
    prototype.show();
    return app.exec();
}
