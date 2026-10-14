// GPL-3.0-or-later
// Replacement-plan phases 0–1 and composition feasibility experiment.
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

static std::shared_ptr<FlowParticleSnapshot> makeField(bool curved) {
    const int columns=curved?80:200,rows=curved?80:60;
    auto mesh=std::make_shared<FlowParticleMesh>();QVector<QRectF> boxes;
    for(int y=0;y<=rows;++y)for(int x=0;x<=columns;++x)mesh->vertices.append({double(x),double(y)});
    for(int y=0;y<rows;++y)for(int x=0;x<columns;++x) {
        const int a=y*(columns+1)+x,b=a+1,c=a+columns+2,d=c-1;
        mesh->triangles.push_back({a,b,c});mesh->triangles.push_back({a,c,d});
        boxes.append({double(x),double(y),1,1});boxes.append(boxes.back());
    }
    mesh->coverage={0,0,double(columns),double(rows)};mesh->grid.rebuild(boxes);
    std::atomic_bool cancel=false;mesh->prepare(cancel);
    auto field=std::make_shared<FlowParticleSnapshot>();field->mesh=mesh;field->geometry=curved?1:2;
    for(const auto &tri:mesh->triangles) {
        FlowParticleField f;
        for(int i=0;i<3;++i) {
            const auto p=mesh->vertices[tri[i]]-mesh->coverage.center();
            f.velocity[i]=curved?QPointF(-p.y()*.1,p.x()*.1):QPointF(3.5,0);
        }field->fields.push_back(f);
    }return field;
}
static QImage background(const FlowParticleMesh &mesh) {
    const auto extent=mesh.coverage;const double scale=12;
    QImage image(int(extent.width()*scale),int(extent.height()*scale),QImage::Format_RGB32);
    image.fill(QColor(20,68,130));QPainter painter(&image);painter.scale(scale,scale);
    painter.setPen(QPen(QColor(34,91,151),.035));
    for(const auto &tri:mesh.triangles)painter.drawPolygon(QPolygonF{mesh.vertices[tri[0]],mesh.vertices[tri[1]],mesh.vertices[tri[2]]});
    return image;
}
class Preview:public QQuickItem {
public:
    std::shared_ptr<const FlowParticleFrame> frame;
    QImage base;QRectF extent;double alpha=1,zoom=1;QPointF pan;
    int backgroundUploads=0,particleUploads=0;
    std::vector<double> geometryTimes;
    explicit Preview(QQuickItem *parent):QQuickItem(parent) {setFlag(ItemHasContents);setAcceptedMouseButtons(Qt::LeftButton);}
protected:
    struct Root:QSGTransformNode {QSGSimpleTextureNode *base=nullptr;FlowParticleNode *trails=nullptr;qint64 imageKey=0;};
    QSGNode *updatePaintNode(QSGNode *old,UpdatePaintNodeData *) override {
        auto *root=old?static_cast<Root*>(old):new Root;
        if(!root->trails) {root->trails=new FlowParticleNode;root->appendChildNode(root->trails);}
        if(!base.isNull() && root->imageKey!=base.cacheKey()) {
            if(root->base) {root->removeChildNode(root->base);delete root->base;}
            root->base=new QSGSimpleTextureNode;root->base->setTexture(window()->createTextureFromImage(base));
            root->base->setOwnsTexture(true);root->base->setRect(extent);root->prependChildNode(root->base);
            root->imageKey=base.cacheKey();++backgroundUploads;
        }
        if(extent.isValid()) {
            const double scale=std::min(width()/extent.width(),height()/extent.height())*zoom;
            QMatrix4x4 matrix;matrix.translate(float(width()/2+pan.x()),float(height()/2+pan.y()));
            matrix.scale(float(scale));matrix.translate(float(-extent.center().x()),float(-extent.center().y()));root->setMatrix(matrix);
            const int before=root->trails->uploads();root->trails->sync(frame,alpha,scale,1,1.5,2,.5,1);
            particleUploads=root->trails->uploads();if(particleUploads!=before)geometryTimes.push_back(root->trails->geometryBuildMs());
        }return root;
    }
    void mousePressEvent(QMouseEvent *event) override {last=event->position();event->accept();}
    void mouseMoveEvent(QMouseEvent *event) override {pan+=event->position()-last;last=event->position();update();event->accept();}
    void wheelEvent(QWheelEvent *event) override {zoom=std::clamp(zoom*std::exp(event->angleDelta().y()/1200.),.5,4.);update();event->accept();}
private:QPointF last;
};
class VisibleCanvas:public QQuickWidget {
public:
    std::vector<double> paintIntervals;QElapsedTimer clock;quint64 paints=0;
    VisibleCanvas() {clock.start();setResizeMode(SizeRootObjectToView);setClearColor(QColor(16,25,38));}
protected:
    void paintEvent(QPaintEvent *event) override {
        const double now=clock.nsecsElapsed()/1e6;
        if(previous>0)paintIntervals.push_back(now-previous);previous=now;++paints;QQuickWidget::paintEvent(event);
    }
private:double previous=-1;
};
struct Packet {
    std::shared_ptr<const FlowParticleFrame> frame;
    std::shared_ptr<const FlowParticleSnapshot> field;
    double alpha=1,lag=0,workerMs=0;
    int steps=0;
};
struct WorkerState {
    FlowParticleSystem system;
    std::shared_ptr<const FlowParticleSnapshot> field;
    double birthsDue=0;
    quint64 seed=1;
    bool curved=true;
    void reset(bool curve) {
        curved=curve;system=FlowParticleSystem();field=makeField(curve);system.setField(field);birthsDue=1;seed=1;
    }
    void spawn() {
        const double a=double((seed*7919)%65521)/65521.,b=double((seed*104729)%65521)/65521.;
        const double theta=a*2*3.14159265358979323846,radius=12+20*b;
        const QPointF position=curved?QPointF(40+radius*std::cos(theta),40+radius*std::sin(theta)):QPointF(5+45*a,5+50*b);
        system.spawn(position,seed++);
    }
};
class Prototype:public QWidget {
public:
    Prototype(double verifySeconds,QString output,bool startCurved,int particles):duration(verifySeconds),outputDir(std::move(output)),targetParticles(particles) {
        setWindowTitle("Continuous flow particles — first-stage prototype");resize(1440,900);
        if(duration>0)setWindowFlags(windowFlags()|Qt::WindowTransparentForInput|Qt::WindowDoesNotAcceptFocus|Qt::WindowStaysOnTopHint);
        QDir().mkpath(outputDir);trace.setFileName(outputDir+"/tracked-particle.csv");trace.open(QIODevice::WriteOnly);
        trace.write("clock,particle_id,age,lifetime,cell,crossings,x,y,status\n");
        auto *layout=new QVBoxLayout(this);auto *controls=new QHBoxLayout;layout->addLayout(controls);
        auto *scene=new QComboBox;scene->addItems({"Curved flow","Uniform channel"});scene->setCurrentIndex(startCurved?0:1);controls->addWidget(scene);
        auto *pause=new QPushButton("Pause");controls->addWidget(pause);
        auto *reverse=new QPushButton("Reverse field");controls->addWidget(reverse);controls->addStretch();
        if(duration>0) {scene->setEnabled(false);pause->setEnabled(false);reverse->setEnabled(false);}
        controls->addWidget(new QLabel("Orange: particle 1 · drag to pan · scroll to zoom"));
        canvas=new VisibleCanvas;canvas->setMinimumSize(640,480);layout->addWidget(canvas,1);
        canvas->setContent(QUrl(),nullptr,new QQuickItem);
        preview=new Preview(canvas->rootObject());
        connect(canvas->quickWindow(),&QQuickWindow::widthChanged,this,[this]{resizePreview();});
        connect(canvas->quickWindow(),&QQuickWindow::heightChanged,this,[this]{resizePreview();});resizePreview();
        status=new QLabel("Preparing immutable field…");layout->addWidget(status);
        tracked=new QLabel("Particle 1 will keep its ID and recorded trail until death.");layout->addWidget(tracked);
        connect(scene,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int i){reset=i;pendingElapsed=0;preview->pan={};preview->zoom=1;});
        connect(pause,&QPushButton::clicked,this,[this,pause]{paused=!paused;pause->setText(paused?"Resume":"Pause");elapsed.restart();});
        connect(reverse,&QPushButton::clicked,this,[this]{reversePending=!reversePending;});
        pool.setMaxThreadCount(1);worker=std::make_shared<WorkerState>();reset=startCurved?0:1;
        connect(&watcher,&QFutureWatcher<Packet>::finished,this,[this] {
            busy=false;
            const auto packet=watcher.result();
            if(displayedMesh!=packet.field->mesh) {
                displayedMesh=packet.field->mesh;preview->base=background(*displayedMesh);preview->extent=displayedMesh->coverage;
            }
            preview->frame=packet.frame;preview->alpha=packet.alpha;preview->update();
            workerTimes.push_back(packet.workerMs);steps+=packet.steps;lag=packet.lag;
            const auto &frame=*packet.frame;
            status->setText(QString("%1 particles · %2 births · %3 deaths · %4 s integrated · worker %5 ms · lag %6 s")
                .arg(frame.particles.size()).arg(frame.births).arg(frame.deaths).arg(frame.clock,0,'f',2).arg(packet.workerMs,0,'f',2).arg(lag,0,'f',3));
            for(const auto &p:frame.particles)if(p.id==1) {
                tracked->setText(QString("Particle %1 · cell %2 · %3 crossings · age %4 / %5 s · %6")
                    .arg(p.id).arg(p.cell).arg(p.crossings).arg(p.age,0,'f',2).arg(p.lifetime,0,'f',2)
                    .arg(p.waitingForData?"waiting for data":p.historyLimited?"shortened tail":flowParticleStopName(p.stop)));
                QTextStream stream(&trace);stream<<frame.clock<<','<<p.id<<','<<p.age<<','<<p.lifetime<<','<<p.cell<<','<<p.crossings<<','
                    <<p.position.x()<<','<<p.position.y()<<','<<flowParticleStopName(p.stop)<<'\n';
                trackedCrossings=std::max(trackedCrossings,p.crossings);trackedDied|=p.stop==FlowParticleStop::Lifetime;
            }
            if(finishing)finish();
        });
        timer.setInterval(8);timer.setTimerType(Qt::PreciseTimer);elapsed.start();wall.start();
        connect(&timer,&QTimer::timeout,this,[this] {
            const double dt=elapsed.nsecsElapsed()/1e9;elapsed.restart();
            if(!paused && isVisible() && !isMinimized())pendingElapsed+=dt;
            if(duration>0 && wall.elapsed()/1000.>=duration) {finishing=true;timer.stop();if(!busy)finish();return;}
            if(busy)return;
            const double requested=pendingElapsed;pendingElapsed=0;const int resetScene=std::exchange(reset,-1);
            const bool reverse=std::exchange(reversePending,false);
            busy=true;
            watcher.setFuture(QtConcurrent::run(&pool,[state=worker,requested,resetScene,reverse,target=targetParticles] {
                QElapsedTimer work;work.start();if(resetScene>=0)state->reset(resetScene==0);
                if(reverse) {
                    auto next=std::make_shared<FlowParticleSnapshot>(*state->field);++next->revision;
                    for(auto &f:next->fields)for(auto &v:f.velocity)v=-v;
                    state->field=next;state->system.setField(next);
                }
                state->system.eraseFinished();
                state->birthsDue+=requested*(target/4.);
                while(state->birthsDue>=1 && int(state->system.particles().size())<target) {state->spawn();state->birthsDue-=1;}
                state->birthsDue=std::min(1.,state->birthsDue);
                const auto result=state->system.advance(requested,16384);
                return Packet{state->system.frame(),state->field,state->system.interpolation(),state->system.lagSeconds(),work.nsecsElapsed()/1e6,result.steps};
            }));
        });timer.start();
    }
    ~Prototype() override {timer.stop();disconnect(&watcher,nullptr,this,nullptr);watcher.waitForFinished();}
private:
    void resizePreview() {preview->setSize(canvas->size());}
    void finish() {
        trace.flush();QJsonObject report;
        const auto frame=preview->frame;
        report["wallSeconds"]=wall.elapsed()/1000.;report["integratedSeconds"]=frame?frame->clock:0;
        report["births"]=frame?double(frame->births):0;report["deaths"]=frame?double(frame->deaths):0;
        report["trackedParticleCrossings"]=trackedCrossings;report["trackedParticleDiedAtOwnLifetime"]=trackedDied;
        report["workerSteps"]=double(steps);report["integrationLagSeconds"]=lag;
        report["backgroundTextureUploads"]=preview->backgroundUploads;report["historyGeometryUploads"]=preview->particleUploads;
        report["animationReadbacks"]=0;report["visibleWidgetPaints"]=double(canvas->paints);
        report["logicalWidth"]=canvas->width();report["logicalHeight"]=canvas->height();
        report["devicePixelRatio"]=canvas->devicePixelRatioF();
        auto distribution=[](std::vector<double> values) {
            std::sort(values.begin(),values.end());if(values.empty())return QJsonObject{};
            return QJsonObject{{"medianMs",values[values.size()/2]},{"p95Ms",values[size_t((values.size()-1)*.95)]},{"maxMs",values.back()}};
        };
        report["workerTime"]=distribution(workerTimes);report["visibleWidgetPaintInterval"]=distribution(canvas->paintIntervals);
        report["historyGeometryBuildTime"]=distribution(preview->geometryTimes);
        report["note"]="Visible Qt widget paint cadence; not a claim of OS display presentation or six-million-cell performance. Screenshot is the only explicit readback, taken after measurements.";
        const bool passed=frame && trackedCrossings>=100 && trackedDied && lag<1;
        report["continuityPassed"]=passed;
        if(frame) {
            QJsonObject stops;quint64 unexpected=0;
            for(size_t i=1;i<frame->stopCounts.size();++i) {
                stops[flowParticleStopName(FlowParticleStop(i))]=double(frame->stopCounts[i]);
                if(i!=size_t(FlowParticleStop::Lifetime))unexpected+=frame->stopCounts[i];
            }
            report["stopReasons"]=stops;report["unexpectedStops"]=double(unexpected);
            report["continuityPassed"]=passed && unexpected==0;
        }
        QFile file(outputDir+"/prototype.json");if(file.open(QIODevice::WriteOnly))file.write(QJsonDocument(report).toJson());
        canvas->grabFramebuffer().save(outputDir+"/prototype.png");QApplication::exit(report["continuityPassed"].toBool()?0:1);
    }
    VisibleCanvas *canvas=nullptr;Preview *preview=nullptr;QLabel *status=nullptr,*tracked=nullptr;
    QFutureWatcher<Packet> watcher;QThreadPool pool;std::shared_ptr<WorkerState> worker;
    std::shared_ptr<const FlowParticleMesh> displayedMesh;
    QTimer timer;QElapsedTimer elapsed,wall;QFile trace;std::vector<double> workerTimes;
    double pendingElapsed=0,duration=0,lag=0;QString outputDir;int reset=0,trackedCrossings=0,targetParticles=256;
    bool paused=false,reversePending=false,finishing=false,trackedDied=false,busy=false;quint64 steps=0;
};
int main(int argc,char **argv) {
    QApplication app(argc,argv);app.setApplicationName("Flow Particle Prototype");
    const auto args=app.arguments();double seconds=0;QString out=QStringLiteral(FLOW_PARTICLE_PROTOTYPE_OUTPUT);bool curved=true;int particles=256;
    for(int i=1;i<args.size();++i) {
        if(args[i]=="--verify-seconds" && i+1<args.size())seconds=args[++i].toDouble();
        else if(args[i]=="--output" && i+1<args.size())out=args[++i];
        else if(args[i]=="--uniform")curved=false;
        else if(args[i]=="--particles" && i+1<args.size())particles=std::clamp(args[++i].toInt(),1,2000);
    }
    Prototype prototype(seconds,out,curved,particles);prototype.show();return app.exec();
}
