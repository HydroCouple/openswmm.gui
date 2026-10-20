/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <QtTest>
#include <openswmm/engine/openswmm_model.h>
#include "simulation/twodbackendpolicy.h"
class TestTwoDBackendPolicy : public QObject {
    Q_OBJECT
private slots:
#ifndef OPENSWMM_HAS_2D
    void noTwoDPolicyIsInert() {
        QString notice;QCOMPARE(openswmmvis::simulation::applyTwoDBackendPolicy(nullptr,notice),SWMM_OK);
        QVERIFY(notice.isEmpty());
    }
#else
    void preservesAuthoredBackend() {
        const auto prior=qgetenv("OPENSWMM_2D_BACKEND");
        const auto restore=qScopeGuard([&] {qputenv("OPENSWMM_2D_BACKEND",prior);});
        qunsetenv("OPENSWMM_2D_BACKEND");
        auto engine=swmm_engine_new();QVERIFY(engine);
        const auto cleanup=qScopeGuard([&] {swmm_engine_destroy(engine);});
        for(const char* requested:{"AUTO","CPU","OMP","CUDA","HIP","SYCL"}) {
            QCOMPARE(swmm_options_set_ext(engine,"BACKEND",requested),SWMM_OK);
            QString notice;
            QCOMPARE(openswmmvis::simulation::applyTwoDBackendPolicy(engine,notice),SWMM_OK);
            QVERIFY(notice.contains("CPU"));
            char value[32]={};QCOMPARE(swmm_options_get_ext(engine,"BACKEND",value,sizeof value),SWMM_OK);
            QCOMPARE(QByteArray(value),QByteArray(requested));
        }
    }
    void validatesAndReportsEnvironmentOverride() {
        const auto prior=qgetenv("OPENSWMM_2D_BACKEND");
        const auto restore=qScopeGuard([&] {qputenv("OPENSWMM_2D_BACKEND",prior);});
        auto engine=swmm_engine_new();QVERIFY(engine);
        const auto cleanup=qScopeGuard([&] {swmm_engine_destroy(engine);});
        QString notice;
        qputenv("OPENSWMM_2D_BACKEND","cudaa");
        QCOMPARE(openswmmvis::simulation::applyTwoDBackendPolicy(engine,notice),SWMM_ERR_BADPARAM);
        QVERIFY(notice.contains("cudaa"));
        qputenv("OPENSWMM_2D_BACKEND","CuDa");
        QCOMPARE(openswmmvis::simulation::applyTwoDBackendPolicy(engine,notice),SWMM_OK);
        QVERIFY(notice.contains("override"));
    }
#endif
};
QTEST_GUILESS_MAIN(TestTwoDBackendPolicy)
#include "test_twodbackendpolicy.moc"
