/**
 * @file      test_swmmunits.cpp
 * @brief     The GUI's shared unit factors equal the engine's legacy tables, bit for bit.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "core/swmmunits.h"

#include <QObject>
#include <QTest>

#include <cmath>
#include <numbers>

namespace swmmunits = openswmmvis::core::units;

class TestSwmmUnits : public QObject
{
    Q_OBJECT
private slots:
    /// swmm5.c Qcf[] and Ucf[VOLUME], consts.h MperFT / LperFT3 / SECperDAY /
    /// MSECperDAY, copied verbatim: the GUI's copies must be these bits, not
    /// a more precise neighbour (hot start conversions once used 0.64631689).
    void legacyFamilyMatchesSwmm5c()
    {
        const double qcf[6] = {1.0, 448.831, 0.64632, 0.02832, 28.317, 2.4466};
        for (int i = 0; i < 6; ++i)
            QVERIFY2(swmmunits::kQcf[i] == qcf[i], qPrintable(QString::number(i)));
        QVERIFY(swmmunits::kUcfVolume[0] == 1.0);
        QVERIFY(swmmunits::kUcfVolume[1] == 0.02832);
        QVERIFY(swmmunits::kUcfRainfall[0] == 43200.0);
        QVERIFY(swmmunits::kUcfRainfall[1] == 1097280.0);
        QVERIFY(swmmunits::kUcfLandArea[0] == 2.2956e-5);
        QVERIFY(swmmunits::kUcfLandArea[1] == 0.92903e-5);
        QVERIFY(swmmunits::kFtToM == 0.3048);
        QVERIFY(swmmunits::kFt3ToL == 28.317);
        QVERIFY(swmmunits::kFt2PerAcre == 43560.0);
        QVERIFY(swmmunits::kSecPerDay == 86400.0);
        QVERIFY(swmmunits::kMsecPerDay == 86400000.0);
        QVERIFY(double(swmmunits::kSecPerDayInt) == swmmunits::kSecPerDay);
        QVERIFY(double(swmmunits::kMsecPerDayInt) == swmmunits::kMsecPerDay);
        QVERIFY(swmmunits::kKelvinOffset == 273.15);
    }

    /// The exact family is the standard value or derived from kFtToM, and stays
    /// distinct from the legacy family.
    void exactFamilyIsDerivedAndDistinct()
    {
        QVERIFY(swmmunits::kPiExact == std::numbers::pi);
        QVERIFY(swmmunits::kMToFt == 3.280839895013123);
        QVERIFY(std::abs(swmmunits::kFt3ToM3Exact -
                         swmmunits::kFtToM * swmmunits::kFtToM * swmmunits::kFtToM) < 1e-17);
        QVERIFY(std::abs(swmmunits::kFt3ToLExact - 1000.0 * swmmunits::kFt3ToM3Exact) < 1e-12);
        QVERIFY(std::abs(swmmunits::kSqMPerAcre -
                         swmmunits::kFt2PerAcre * swmmunits::kFtToM * swmmunits::kFtToM) < 1e-9);
        QVERIFY(swmmunits::kFt3ToL != swmmunits::kFt3ToLExact);
        QVERIFY(swmmunits::kUcfVolume[1] != swmmunits::kFt3ToM3Exact);
    }
};

QTEST_MAIN(TestSwmmUnits)
#include "test_swmmunits.moc"
