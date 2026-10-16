#include "core/attributevalue.h"
#include "core/queryparser.h"
#include "layers/swmm2dmeshlayer.h"
#include "layers/tabulardatalayer.h"
#include "layers/gisvectorlayer.h"
#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include "map/mapcanvas.h"
#include "mesh/meshcellparams.h"
#include "ui/panels/attributedelegates.h"
#include "ui/models/userflagsmodel.h"
#include <openswmm/engine/openswmm_pollutants.h>
#include "ui/panels/attributetablepanel.h"
#include "ui/panels/attributevalueproxy.h"
#include "ui/panels/dataobjectattributetablemodel.h"
#include "ui/panels/meshattributetablemodel.h"
#include "ui/panels/tabulardatatablemodel.h"
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QSettings>
#include <QStandardItemModel>
#include <QTableView>
#include <QTest>
#include <memory>

using namespace openswmmvis;
namespace {
QString outputDir() {
    return qEnvironmentVariable("TABLE_NULL_TEST_OUTPUT", QStringLiteral("tests/output/attribute_nulls"));
}
std::unique_ptr<SWMMModelLayer> modelLayer(const char *fixture = "typed_selection_fixture.inp") {
    auto layer = std::make_unique<SWMMModelLayer>(QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", "tests/gui/data")).filePath(fixture));
    QList<QString> warnings,errors;
    if (!layer->loadModel(warnings,errors)) return {};
    return layer;
}
int column(const QList<ColumnSpec> &specs, const QString &key) {
    for(int i=0;i<specs.size();++i) if(specs[i].key==key)return i;
    return -1;
}
mesh::MeshResult fixture() {
    mesh::MeshResult m;
    // Distinct areas; the fourth cell is a quad with the same area as cell 1.
    for (int i=0;i<4;++i) {
        const double area = QList<double>{2,10,100,10}[i];
        const double x = i*1000.0;
        const int b=m.vertices.size();
        for (auto p : {QPointF(x,0),QPointF(x+area*2,0),QPointF(x+area*2,1),QPointF(x,1)}) {
            mesh::MeshVertex v;v.xy=p;m.vertices.append(v);
        }
        mesh::MeshTriangle t;t.v0=b;t.v1=b+1;t.v2=b+2;
        if(i==3) {t.v3=b+3;m.vertices[b+1].xy.setX(x+area);m.vertices[b+2].xy.setX(x+area);}
        m.triangles.append(t);
    }
    for(int i=0;i<3;++i) {
        const int row=i*2;
        m.vertices[row].coupledNode="J1";
        m.vertices[row].couplingCd=QList<double>{1.0,0.1,0.65}[i];
        m.vertices[row].couplingArea=QList<double>{100,2,10}[i];
    }
    m.ok=true;return m;
}
}
class TestAttributeNulls : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir().mkpath(outputDir());
        QCoreApplication::setOrganizationName("OpenSWMMTableNullTests");
        QCoreApplication::setApplicationName("AttributeNulls");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,outputDir()+"/settings");
        QSettings().clear();
    }
    void actualPanelHeadersQueriesAndIdentity() {
        auto swmm=modelLayer();QVERIFY(swmm);
        SWMM2DMeshLayer layer(fixture());
        MapCanvas canvas;canvas.addLayer(&layer,false);
        SelectionManager selection;
        AttributeTablePanel panel;
        panel.setProject(swmm.get(),&selection,&canvas);
        panel.resize(1200,700);panel.show();
        auto *view=panel.findChild<QTableView *>();
        auto *query=panel.findChild<QLineEdit *>("attributeQuery");
        auto *source=panel.findChild<MeshAttributeTableModel *>();
        QVERIFY(view&&query&&source);
        panel.showMeshTable(&layer,0);
        QCOMPARE(view->model()->rowCount(),16);
        auto apply=[&](const QString &text) {
            query->setText(text);QTest::keyClick(query,Qt::Key_Return);
            return view->model()->rowCount();
        };
        QCOMPARE(apply("couplingCd IS NULL"),13);
        QCOMPARE(view->model()->index(0,column(source->columnSpecs(),"couplingCd")).data().toString(),QString("NULL"));
        QVERIFY(!panel.selectionAsTsv().contains("NULL"));
        QCOMPARE(apply("couplingCd >= 0.65"),2);
        QCOMPARE(apply("couplingArea BETWEEN 2 AND 10"),2);
        QCOMPARE(apply("[Coupled Node] IS NULL"),13); // bound engine => Compound picker
        QCOMPARE(apply("[Coupled Node] = 'J1'"),3);
        QVERIFY(query->styleSheet().isEmpty());
        QCOMPARE(apply("[No Such Field] = 0"),3);
        QVERIFY(!query->styleSheet().isEmpty());
        QCOMPARE(apply(""),16);
        for(int kind : {0,1,2}) {
            panel.showMeshTable(&layer,kind);
            QCOMPARE(int(source->kind()),kind);
            for(int c=0;c<source->columnCount();++c) {
                // Actual header interaction, including every numeric cell parameter.
                view->scrollTo(view->model()->index(0,c),QAbstractItemView::PositionAtCenter);
                QCoreApplication::processEvents();
                auto *header=view->horizontalHeader();
                for(int click=0;click<2;++click) {
                    const QPoint p(header->sectionViewportPosition(c)+header->sectionSize(c)/2,header->height()/2);
                    QTest::mouseClick(header->viewport(),Qt::LeftButton,Qt::NoModifier,p);
                    auto *proxy=qobject_cast<QSortFilterProxyModel *>(view->model());QVERIFY(proxy);
                    QCOMPARE(proxy->sortColumn(),c);
                    bool seenNull=false;QVariant previous;
                    for(int r=0;r<proxy->rowCount();++r) {
                        const auto value=proxy->index(r,c).data(kAttributeSortRole);
                        if(isNullAttributeValue(value)) {seenNull=true;continue;}
                        QVERIFY2(!seenNull,qPrintable(source->columnSpecs()[c].key));
                        if(isNumericValue(previous)&&isNumericValue(value)) {
                            const int cmp=compareAttributeNumbers(previous,value);
                            QVERIFY2(proxy->sortOrder()==Qt::AscendingOrder?cmp<=0:cmp>=0,qPrintable(source->columnSpecs()[c].key));
                        }
                        previous=value;
                    }
                }
            }
        }
        const int area=column(source->columnSpecs(),"Area");
        view->sortByColumn(area,Qt::DescendingOrder);
        QCOMPARE(view->model()->index(0,area).data(kAttributeValueRole).toDouble(),100.0);
        QCOMPARE(apply("Area > 10"),1);
        const auto cell=source->refForRow(2);
        view->selectRow(0);QCOMPARE(selection.selection(),QSet<SWMMObjectRef>{cell});
        const int tag=column(source->columnSpecs(),"tag");
        QVERIFY(view->model()->setData(view->model()->index(0,tag),"largest"));
        QCOMPARE(layer.mesh().triangles[2].tag,QString("largest"));
        QCOMPARE(apply(""),4);
        // Persist the area sort, switch to a shorter schema, and restore it.
        panel.showMeshTable(&layer,0);panel.showMeshTable(&layer,2);
        QCOMPARE(view->horizontalHeader()->sortIndicatorSection(),area);
        QCOMPARE(view->model()->index(0,area).data(kAttributeValueRole).toDouble(),100.0);
        panel.setProject(nullptr,nullptr,nullptr);
        canvas.takeLayer(canvas.layers().indexOf(&layer),false);
    }
    void cellRegistryAvailabilityAndInheritance() {
        for (const auto method : {mesh::InfilMethod::None,mesh::InfilMethod::Horton,mesh::InfilMethod::ModHorton,
             mesh::InfilMethod::GreenAmpt,mesh::InfilMethod::ModGreenAmpt,mesh::InfilMethod::CurveNumber,mesh::InfilMethod::Constant}) {
            auto m=fixture();mesh::InfilRow row;row.method=method;
            for(int p=0;p<mesh::kInfilMaxParams;++p)row.p[p]=p+1;
            m.infilDefaults.append({"*",row});
            m.infiltrationOwner={1,0,2,1};m.infiltrationConflict={0,0,1,0};
            m.surfaceOwnershipStatus={0,1,2,3};
            m.surfaceMeshWeatherPercent={100,50,0,0};
            m.surfaceSourcePerviousPercent={0,50,100,100};
            SWMM2DMeshLayer layer(m);MeshAttributeTableModel model;model.setSource(&layer,MeshAttributeTableModel::Kind::Cell);
            // Binding deliberately clears stale diagnostics without an engine.
            // Publish a small resolved snapshot here to isolate the table's
            // read-only value contract from the engine's ownership resolver.
            auto &resolved=const_cast<mesh::MeshResult &>(layer.mesh());
            resolved.infiltrationOwner=m.infiltrationOwner;resolved.infiltrationConflict=m.infiltrationConflict;
            resolved.surfaceOwnershipStatus=m.surfaceOwnershipStatus;
            resolved.surfaceMeshWeatherPercent=m.surfaceMeshWeatherPercent;
            resolved.surfaceSourcePerviousPercent=m.surfaceSourcePerviousPercent;
            for(const auto &spec:mesh::cellParamSpecs()) {
                const int col=column(model.columnSpecs(),spec.key);QVERIFY(col>=0);
                const QVariant value=model.index(0,col).data(kAttributeValueRole);
                if(spec.key.startsWith("snow.")||spec.key.startsWith("gw.")) {
                    QVERIFY(!value.isValid());QVERIFY(!model.index(0,col).data(Qt::ToolTipRole).toString().isEmpty());
                } else if(spec.key.startsWith("infil.")&&spec.key!="infil.owner"&&spec.key!="infil.conflict") {
                    const bool applicable=spec.key=="infil.method"||mesh::infilSlotForKey(method,spec.key)>=0;
                    QCOMPARE(value.isValid(),applicable);
                    if(applicable && spec.key!="infil.method") QCOMPARE(value.toDouble(),row.p[mesh::infilSlotForKey(method,spec.key)]);
                    QVERIFY(!model.index(1,col).data(kAttributeValueRole).isValid());
                    QVERIFY(!model.index(2,col).data(kAttributeValueRole).isValid());
                } else QVERIFY2(value.isValid(),spec.key.constData());
            }
            const int weather=column(model.columnSpecs(),"surface.meshWeather");
            QCOMPARE(model.index(2,weather).data(kAttributeValueRole).toDouble(),0.0);
            QVERIFY(!(model.flags(model.index(2,weather))&Qt::ItemIsEditable));
        }
        SWMM2DMeshLayer empty(fixture());MeshAttributeTableModel model;model.setSource(&empty,MeshAttributeTableModel::Kind::Cell);
        for(const auto &key:{"infil.owner","infil.conflict","surface.owner","surface.meshWeather","surface.sourcePervious"})
            QVERIFY(!model.index(0,column(model.columnSpecs(),key)).data(kAttributeValueRole).isValid());
    }
    void optionalSwmmValuesAndReferenceQueries() {
        auto swmm=modelLayer();QVERIFY(swmm);
        auto *flags=swmm->ensureUserFlagsModel();QVERIFY(flags);
        using FT=openswmmvis::ui::UserFlagsModel::FlagType;
        QVERIFY(flags->define("NUMBER",FT::Integer,QString()));
        QVERIFY(flags->define("BOOLEAN",FT::Boolean,QString()));
        QVERIFY(flags->define("TEXT",FT::String,QString()));
        QVERIFY(flags->setValue("NODE","J1","NUMBER","2147483647"));
        QVERIFY(flags->setValue("NODE","J1","BOOLEAN","NO"));
        QCOMPARE(swmm_pollutant_add(swmm->engine(),"TSS",0),SWMM_OK);
        SWMMAttributeTableModel model;model.setSource(swmm.get(),SWMMModelLayer::CatJunctions);
        const int row=model.rowForName("J1");QVERIFY(row>=0);
        const int number=column(model.columnSpecs(),"userflag:NUMBER");QVERIFY(number>=0);
        QCOMPARE(model.index(row,number).data(kAttributeValueRole).toLongLong(),qlonglong(2147483647));
        const int boolean=column(model.columnSpecs(),"userflag:BOOLEAN");
        QCOMPARE(model.index(row,boolean).data(kAttributeValueRole).toString(),QString("NO"));
        QVERIFY(!model.index(row,column(model.columnSpecs(),"userflag:TEXT")).data(kAttributeValueRole).isValid());
        const int quality=column(model.columnSpecs(),"initq:TSS");QVERIFY(quality>=0);
        QVERIFY(!model.index(row,quality).data(kAttributeValueRole).isValid());
        QCOMPARE(model.index(row,quality).data(Qt::EditRole).metaType().id(),int(QMetaType::QString));
        QVERIFY(model.setData(model.index(row,quality),"0"));
        QCOMPARE(model.index(row,quality).data(kAttributeValueRole).metaType().id(),int(QMetaType::Double));
        QVERIFY(model.setData(model.index(row,quality),QString()));
        QVERIFY(!model.index(row,quality).data(kAttributeValueRole).isValid());
        for(int c=0;c<model.columnCount();++c)
            if(model.columnSpecs()[c].setter.contains("_stat_")) QVERIFY(!model.index(row,c).data(kAttributeValueRole).isValid());
        auto objects=modelLayer("data_object_table_fixture.inp");QVERIFY(objects);
        AttributeTablePanel panel;panel.setProject(objects.get(),nullptr,nullptr);
        panel.showDataCategory(SWMMModelLayer::DataInlets);
        auto *edit=panel.findChild<QLineEdit *>("attributeQuery");auto *view=panel.findChild<QTableView *>();QVERIFY(edit&&view);
        edit->setText("grateLength IS NULL");QTest::keyClick(edit,Qt::Key_Return);
        QVERIFY(edit->styleSheet().isEmpty());QVERIFY(view->model()->rowCount()>0);
        edit->setText("grateLength >= 0");QTest::keyClick(edit,Qt::Key_Return);
        QVERIFY(edit->styleSheet().isEmpty());QVERIFY(view->model()->rowCount()>0);
    }
    void nullableEditorDoesNotInventZero() {
        QStandardItemModel model(1,1);QWidget parent;NumericDelegate delegate;
        std::unique_ptr<QWidget> editor(delegate.createEditor(&parent,{},model.index(0,0)));
        delegate.setEditorData(editor.get(),model.index(0,0));
        delegate.setModelData(editor.get(),&model,model.index(0,0));
        QVERIFY(!model.index(0,0).data(Qt::EditRole).isValid());
        auto *spin=qobject_cast<QDoubleSpinBox *>(editor.get());QVERIFY(spin);
        QTest::keyClicks(spin,"0");delegate.setModelData(editor.get(),&model,model.index(0,0));
        QVERIFY(model.index(0,0).data(Qt::EditRole).isValid());
        QCOMPARE(model.index(0,0).data(Qt::EditRole).toDouble(),0.0);
    }
    void gisNullsAndLargeIdentifiers() {
        GDALAllRegister();
        auto *driver=GetGDALDriverManager()->GetDriverByName("GPKG");QVERIFY(driver);
        const auto path=(outputDir()+"/nulls.gpkg").toUtf8();
        if(QFile::exists(QString::fromUtf8(path))) QCOMPARE(driver->Delete(path.constData()),CE_None);
        auto *dataset=driver->Create(path.constData(),0,0,0,GDT_Unknown,nullptr);QVERIFY(dataset);
        auto *ogr=dataset->CreateLayer("points",nullptr,wkbPoint,nullptr);QVERIFY(ogr);
        OGRFieldDefn field("amount",OFTReal);QCOMPARE(ogr->CreateField(&field),OGRERR_NONE);
        for(int i=0;i<3;++i) {
            auto *feature=OGRFeature::CreateFeature(ogr->GetLayerDefn());
            feature->SetFID(9007199254740992LL+i);
            if(i!=1)feature->SetField("amount",i==0?0.0:10.0);
            OGRPoint point(i,0);feature->SetGeometry(&point);
            const auto result=ogr->CreateFeature(feature);OGRFeature::DestroyFeature(feature);
            QCOMPARE(result,OGRERR_NONE);
        }
        GDALClose(dataset);
        GISVectorLayer layer(QString::fromUtf8(path),"points");
        MapCanvas canvas;canvas.addLayer(&layer,false);SelectionManager selection;
        AttributeTablePanel panel;panel.setProject(nullptr,&selection,&canvas);panel.showLayerSource(&layer);
        auto *view=panel.findChild<QTableView *>();auto *query=panel.findChild<QLineEdit *>("attributeQuery");QVERIFY(view&&query);
        QCOMPARE(view->model()->rowCount(),3);view->sortByColumn(0,Qt::DescendingOrder);
        QCOMPARE(view->model()->index(0,0).data(kAttributeValueRole).toLongLong(),qlonglong(9007199254740994LL));
        query->setText("amount IS NULL AND FID = 9007199254740993");QTest::keyClick(query,Qt::Key_Return);
        QVERIFY(query->styleSheet().isEmpty());QCOMPARE(view->model()->rowCount(),1);
        QCOMPARE(view->model()->index(0,1).data().toString(),QString("NULL"));
        query->setText("amount >= 0");QTest::keyClick(query,Qt::Key_Return);QCOMPARE(view->model()->rowCount(),2);
        panel.setProject(nullptr,nullptr,nullptr);canvas.takeLayer(0,false);
    }
    void delimitedValuesAndInventory() {
        QFile csv(outputDir()+"/values.csv");QVERIFY(csv.open(QIODevice::WriteOnly));
        csv.write("id,number,literal,empty,missing\n001,9007199254740993,NULL,\n002,10,-,\n003,2.5,text,,present\n");csv.close();
        TabularDataLayer layer;QString error;QVERIFY2(layer.loadFromFile(csv.fileName(),&error),qPrintable(error));
        TabularDataTableModel model;model.setLayer(&layer);
        QCOMPARE(model.index(0,0).data(kAttributeValueRole).toString(),QString("001"));
        QCOMPARE(model.index(0,1).data(kAttributeValueRole).toLongLong(),qlonglong(9007199254740993LL));
        QCOMPARE(model.index(0,2).data(kAttributeValueRole).toString(),QString("NULL"));
        QVERIFY(model.index(0,3).data(kAttributeValueRole).isValid());
        QVERIFY(!model.index(0,4).data(kAttributeValueRole).isValid());
        QJsonArray inventory;
        auto record=[&](const QString &source,const QAbstractItemModel &model,const QList<ColumnSpec> &specs) {
            for(int col=0;col<model.columnCount();++col) {
                QJsonObject entry{{"source",source},{"header",model.headerData(col,Qt::Horizontal).toString()}};
                if(col<specs.size()) {entry["key"]=specs[col].key;entry["queryable"]=specs[col].queryable;}
                QJsonArray rows;
                for(int r=0;r<qMin(model.rowCount(),16);++r) {
                    const QVariant v=model.index(r,col).data(kAttributeValueRole);
                    rows.append(QJsonObject{{"row",r},{"null",!v.isValid()},{"type",QString::fromLatin1(v.metaType().name()?v.metaType().name():"NULL")},{"text",v.toString()}});
                }
                entry["samples"]=rows;inventory.append(entry);
            }
        };
        record("CSV",model,{});
        SWMM2DMeshLayer mesh(fixture());MeshAttributeTableModel meshModel;
        for(int kind:{0,1,2}) {meshModel.setSource(&mesh,static_cast<MeshAttributeTableModel::Kind>(kind));record(QString("mesh:%1").arg(kind),meshModel,meshModel.columnSpecs());}
        auto swmm=modelLayer();QVERIFY(swmm);SWMMAttributeTableModel spatial;
        for(auto cat:{SWMMModelLayer::CatJunctions,SWMMModelLayer::CatOutfalls,SWMMModelLayer::CatStorage,SWMMModelLayer::CatDividers,
            SWMMModelLayer::CatConduits,SWMMModelLayer::CatPumps,SWMMModelLayer::CatOrifices,SWMMModelLayer::CatWeirs,
            SWMMModelLayer::CatOutlets,SWMMModelLayer::CatSubcatchments,SWMMModelLayer::CatRainGages}) {
            spatial.setSource(swmm.get(),cat);record(QString("SWMM:%1").arg(int(cat)),spatial,spatial.columnSpecs());
        }
        auto objects=modelLayer("data_object_table_fixture.inp");QVERIFY(objects);DataObjectAttributeTableModel data;
        for(auto cat:{SWMMModelLayer::DataPollutants,SWMMModelLayer::DataLandUses,SWMMModelLayer::DataAquifers,SWMMModelLayer::DataStreets,SWMMModelLayer::DataInlets}) {
            data.setSource(objects.get(),cat);record(QString("data:%1").arg(int(cat)),data,data.columnSpecs());
        }
        QFile report(outputDir()+"/column-inventory.json");QVERIFY(report.open(QIODevice::WriteOnly));
        report.write(QJsonDocument(inventory).toJson());
    }
};
QTEST_MAIN(TestAttributeNulls)
#include "test_attributenulls.moc"
