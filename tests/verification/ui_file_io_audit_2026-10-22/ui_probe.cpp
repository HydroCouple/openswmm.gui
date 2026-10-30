#include "ui/dialogs/simulationoptionsdialog.h"
#include "ui/dialogs/simoptions/hydraulicspage.h"
#include "ui/dialogs/simoptions/qualitypage.h"
#include "ui/dialogs/simoptions/twodpage.h"
#include "ui/dialogs/simoptions/filespage.h"
#include "ui/dialogs/simoptions/datespage.h"
#include <openswmm/engine/openswmm_model.h>
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSettings>
#include <iostream>

QString opt(SWMM_Engine e, const char *key, bool ext=false) {
    char b[65536]{};
    int rc = ext ? swmm_options_get_ext(e,key,b,sizeof b) : swmm_options_get(e,key,b,sizeof b);
    return rc == 0 ? QString::fromUtf8(b) : QString("ERROR %1").arg(rc);
}
int main(int argc,char **argv) {
    QApplication app(argc,argv);
    QCoreApplication::setOrganizationName("OpenSWMMFileIOAudit");
    QCoreApplication::setApplicationName("ReviewOnly");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,argv[2]);
    SWMM_Engine e=swmm_engine_create();
    if(swmm_engine_open(e,argv[1],"","",nullptr)!=0) return 2;
    const QList<QPair<QByteArray,QByteArray>> values={
        {"MINIMUM_STEP","0.001"}, {"FV_CFL","0.025"}, {"QUALITY_STEP","0.001"},
        {"ROUTING_STEP","0.123456789"}, {"DRY_DAYS","1.2345"}};
    QJsonObject before, displayed, after;
    for(auto p:values) { swmm_options_set(e,p.first,p.second); before[p.first]=opt(e,p.first); }
    swmm_options_set_ext(e,"LIMITER_EPSILON","0");
    {
        SimulationOptionsDialog dlg(e);
        for(auto *w:dlg.findChildren<QWidget*>()) {
            const QString key=w->property("optionKey").toString();
            if(before.contains(key)) {
                if(auto *s=qobject_cast<QDoubleSpinBox*>(w)) displayed[key]=s->value();
            }
            if(key=="LIMITER_EPSILON")
                if(auto *s=qobject_cast<QDoubleSpinBox*>(w)) s->setValue(1e-9);
        }
        dlg.findChild<openswmmvis::ui::HydraulicsPage*>()->write();
        dlg.findChild<openswmmvis::ui::QualityPage*>()->write();
        dlg.findChild<openswmmvis::ui::DatesPage*>()->write();
        dlg.findChild<openswmmvis::ui::TwoDPage*>()->write();
        for(auto p:values) after[p.first]=opt(e,p.first);
        after["LIMITER_EPSILON edited to 1e-9"]=opt(e,"LIMITER_EPSILON",true);
    }
    const char *gpkg="org.hydrocouple.openswmm.plugins.geopackage";
    swmm_plugin_set(e,gpkg,nullptr);
    QJsonArray writersBefore, writersAfter;
    {
        SimulationOptionsDialog dlg(e);
        auto *page=dlg.findChild<openswmmvis::ui::FilesPage*>();
        for(auto *c:page->findChildren<QComboBox*>())
            if(c->itemText(0).startsWith("(built-in:")) {
                writersBefore.append(c->currentData().toString());
                c->setCurrentIndex(0);
            }
        page->write();
    }
    {
        SimulationOptionsDialog dlg(e);
        auto *page=dlg.findChild<openswmmvis::ui::FilesPage*>();
        for(auto *c:page->findChildren<QComboBox*>())
            if(c->itemText(0).startsWith("(built-in:")) writersAfter.append(c->currentData().toString());
    }
    QJsonObject out{{"before_unedited_apply",before},{"ui_display",displayed},{"after_apply",after},
                    {"writers_before_setting_builtin",writersBefore},{"writers_after_reopen",writersAfter}};
    QFile file(QString(argv[2])+"/ui_probe_results.json");
    if(!file.open(QIODevice::WriteOnly)) return 3;
    auto data=QJsonDocument(out).toJson();file.write(data);
    std::cout << data.constData();
    swmm_engine_close(e);swmm_engine_destroy(e);
}
