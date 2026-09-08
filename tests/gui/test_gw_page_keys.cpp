/*!
 * \file test_gw_page_keys.cpp
 * \brief U5 (2026-09-07) — the Simulation Options 2D page Groundwater group:
 *        [2D_OPTIONS] GROUNDWATER / GW_ET, and the infiltration-destination
 *        mutual exclusion the engine validates shown as a state change.
 *
 * Contract:
 *   - Defaults seed OFF / None and an unedited Apply writes nothing.
 *   - Enabling Groundwater locks the Infiltration destination to the 2D
 *     aquifer (and disables the method combo), because that is what the
 *     engine's D-I4 exclusion means for the user.
 *   - Apply writes both keys with the engine's spelling; re-open + Apply is
 *     the identity.
 *   - The Transport summary counts the [GW_*] rows the model carries.
 */
#include "layers/swmmmodellayer.h"
#include "ui/dialogs/simulationoptionsdialog.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_gw_transport.h>
#include <openswmm/engine/openswmm_model.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QTest>

#include <memory>

namespace {

QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

QString fixture(const QString &name) { return QDir(dataDir()).filePath(name); }

std::unique_ptr<SWMMModelLayer> openLayer(const QString &path)
{
    auto layer = std::make_unique<SWMMModelLayer>(path, nullptr);
    QList<QString> warnings, errors;
    if (!layer->loadModel(warnings, errors)) return nullptr;
    return layer;
}

QString ext(SWMM_Engine e, const char *key)
{
    char buf[256] = {};
    if (swmm_options_get_ext(e, key, buf, sizeof buf) != SWMM_OK) return {};
    return QString::fromUtf8(buf).trimmed();
}

template <class W>
W *child(QObject *root, const char *name)
{
    return root->findChild<W *>(QLatin1String(name));
}

} // namespace

class TestGwPageKeys : public QObject
{
    Q_OBJECT

private slots:
    void defaultsAndRoundTrip()
    {
        auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
        QVERIFY(layer);
        SWMM_Engine e = layer->engine();
        SimulationOptionsDialog dlg(e, layer.get(), QStringLiteral("6.0.0"), nullptr, nullptr);

        auto *gw   = child<QComboBox>(&dlg, "gw2DEnableCombo");
        auto *et   = child<QComboBox>(&dlg, "gw2DEtCombo");
        auto *dest = child<QComboBox>(&dlg, "infil2DDestCombo");
        QVERIFY(gw && et && dest);
        // AUTO is the default and must be the seeded state, or an unedited
        // Apply would write an explicit enable over a silent deck.
        QCOMPARE(gw->currentData().toString(), QStringLiteral("AUTO"));
        QCOMPARE(et->currentData().toString(), QStringLiteral("NONE"));
        QVERIFY2(et->isEnabled(), "under AUTO the subsurface may still run");
        QVERIFY(dest->isEnabled());

        QVERIFY(QMetaObject::invokeMethod(&dlg, "onApply", Qt::DirectConnection));
        QVERIFY(!dlg.wroteAnyChanges());

        // An explicit On locks the destination to the 2D aquifer.
        gw->setCurrentIndex(gw->findData(QStringLiteral("YES")));
        QVERIFY(et->isEnabled());
        QVERIFY(!dest->isEnabled());
        QCOMPARE(dest->currentData().toString(), QStringLiteral("AQUIFER_2D"));

        et->setCurrentIndex(et->findData(QStringLiteral("BOTH")));
        QVERIFY(QMetaObject::invokeMethod(&dlg, "onApply", Qt::DirectConnection));
        QVERIFY(dlg.wroteAnyChanges());
        QCOMPARE(ext(e, "GROUNDWATER"), QStringLiteral("YES"));
        // GW_ET is an alias: it is stored in [2D_AQUIFER_OPTIONS], and reads
        // back through the same key.
        QCOMPARE(ext(e, "GW_ET"), QStringLiteral("BOTH"));

        SimulationOptionsDialog dlg2(e, layer.get(), QStringLiteral("6.0.0"), nullptr, nullptr);
        QCOMPARE(child<QComboBox>(&dlg2, "gw2DEnableCombo")->currentData().toString(),
                 QStringLiteral("YES"));
        QCOMPARE(child<QComboBox>(&dlg2, "gw2DEtCombo")->currentData().toString(),
                 QStringLiteral("BOTH"));
        QVERIFY(QMetaObject::invokeMethod(&dlg2, "onApply", Qt::DirectConnection));
        QVERIFY(!dlg2.wroteAnyChanges());
    }

    void transportSummaryCountsAuthoredRows()
    {
        auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
        QVERIFY(layer);
        SWMM_Engine e = layer->engine();
        {
            SimulationOptionsDialog dlg(e, layer.get(), QStringLiteral("6.0.0"),
                                        nullptr, nullptr);
            auto *status = child<QLabel>(&dlg, "gw2DStatusLabel");
            QVERIFY(status);
            QVERIFY2(status->text().contains(QStringLiteral("No subsurface")),
                     qPrintable(status->text()));
        }
        // Author two rows through the C API, then re-open the dialog.
        QCOMPARE(swmm_gw_init_quality_set(e, SWMM_GW_SCOPE_GLOBAL, nullptr, -1,
                                          SWMM_GW_ZONE_SAT, -1,
                                          "__TEMPERATURE__", 12.0), SWMM_OK);
        QCOMPARE(swmm_gw_sorption_set(e, SWMM_GW_SCOPE_GLOBAL, nullptr, -1,
                                      "__TEMPERATURE__", 0.0, -1.0), SWMM_OK);
        {
            SimulationOptionsDialog dlg(e, layer.get(), QStringLiteral("6.0.0"),
                                        nullptr, nullptr);
            auto *status = child<QLabel>(&dlg, "gw2DStatusLabel");
            QVERIFY(status);
            QVERIFY2(status->text().startsWith(QStringLiteral("2 subsurface")),
                     qPrintable(status->text()));
            // The rows are the GW editor's, not this dialog's: Apply is the
            // identity for them.
            QVERIFY(QMetaObject::invokeMethod(&dlg, "onApply", Qt::DirectConnection));
            QCOMPARE(swmm_gw_init_quality_count(e), 1);
            QCOMPARE(swmm_gw_sorption_count(e), 1);
        }
    }
};

QTEST_MAIN(TestGwPageKeys)
#include "test_gw_page_keys.moc"
