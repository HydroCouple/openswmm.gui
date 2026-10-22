/**
 * @file      test_gw_page_keys.cpp
 * @brief     U5 (2026-09-07) — the Simulation Options 2D page Groundwater group:
 *        [2D_OPTIONS] GROUNDWATER / GW_ET, and the infiltration-destination
 *        mutual exclusion the engine validates shown as a state change.
 *
 * Contract:
 *   - Defaults seed AUTO / None and an unedited Apply writes nothing.
 *   - Enabling Groundwater locks the Infiltration destination to the 2D
 *     aquifer (and disables the method combo), because that is what the
 *     engine's D-I4 exclusion means for the user.
 *   - Apply writes both keys with the engine's spelling; re-open + Apply is
 *     the identity.
 *   - The Transport summary counts the [GW_*] rows the model carries.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
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
#include <QPushButton>
#include <QTest>

#include <memory>

namespace
{

/**
 *
 * @brief Select SWMMVIS_GUI_TEST_DATA or current-directory dot for groundwater-page inputs.
 * @return Owned unvalidated directory string; no file IO.
 *
 */
QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

/**
 *
 * @brief Resolve a groundwater-page fixture path.
 * @param name Borrowed input filename resolved under dataDir(), without sanitization.
 * @return Owned path; does not check existence or open the file.
 *
 */
QString fixture(const QString &name)
{
    return QDir(dataDir()).filePath(name);
}

/**
 *
 * @brief Allocate and load a fresh groundwater-page model layer.
 * @param path Borrowed input filename copied into the layer.
 * @return Unique owned loaded layer or nullptr after failed-load cleanup; warnings
 * and errors stay local. Loading may create engine reports; tests do not save input edits.
 *
 */
std::unique_ptr<SWMMModelLayer> openLayer(const QString &path)
{
    auto layer = std::make_unique<SWMMModelLayer>(path, nullptr);
    QList<QString> warnings, errors;
    if (!layer->loadModel(warnings, errors))
        return nullptr;
    return layer;
}

/**
 *
 * @brief Read and trim a fixed-buffer extended option for assertions.
 * @param e Borrowed valid loaded engine.
 * @param key Borrowed null-terminated engine option/alias name.
 * @return Owned UTF-8 option text from a 256-byte buffer, empty on getter failure
 * or empty option. Long-value behavior follows the engine buffer policy; no writes.
 *
 */
QString ext(SWMM_Engine e, const char *key)
{
    char buf[256] = {};
    if (swmm_options_get_ext(e, key, buf, sizeof buf) != SWMM_OK)
        return {};
    return QString::fromUtf8(buf).trimmed();
}

/**
 *
 * @brief Find the first named descendant of a fixture object.
 * @tparam W QObject-derived target widget type.
 * @param root Borrowed non-null QObject searched recursively.
 * @param name Borrowed null-terminated Latin-1 objectName.
 * @return Borrowed matching widget or nullptr; root retains ownership and callers
 * check or dereference it. No widget is allocated or mutated.
 *
 */
template <class W> W *child(QObject *root, const char *name)
{
    return root->findChild<W *>(QLatin1String(name));
}

} // namespace

class TestGwPageKeys : public QObject
{
    Q_OBJECT

private slots:
    /**
     *
     * @brief Register AUTO/YES/NO groundwater enable modes for the unsupported editor shortcut
     * gate.
     *
     */
    void subsurfaceEditorDoesNotOpenNodeLinkEditor_data()
    {
        QTest::addColumn<QString>("mode");
        QTest::newRow("automatic") << QStringLiteral("AUTO");
        QTest::newRow("enabled") << QStringLiteral("YES");
        QTest::newRow("disabled") << QStringLiteral("NO");
    }

    /**
     *
     * @brief Require the node/link initial-quality editor shortcut to stay disabled for each
     * groundwater mode.
     * Consumes mode; edits only the dialog draft and does not Apply.
     *
     */
    void subsurfaceEditorDoesNotOpenNodeLinkEditor()
    {
        QFETCH(QString, mode);
        auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
        QVERIFY(layer);
        SimulationOptionsDialog dlg(layer->engine(), layer.get(), QStringLiteral("6.0.0"), nullptr,
                                    nullptr);
        auto *gw = child<QComboBox>(&dlg, "gw2DEnableCombo");
        auto *edit = child<QPushButton>(&dlg, "gw2DEditBtn");
        QVERIFY(gw && edit);
        gw->setCurrentIndex(gw->findData(mode));
        QVERIFY2(!edit->isEnabled(),
                 "The node/link initial-quality editor cannot author groundwater rows");
    }

    /**
     *
     * @brief Require AUTO/NONE defaults to accept without writes, explicit groundwater YES to
     * lock the aquifer destination and BOTH ET to round-trip.
     * Checks reopening and unchanged Apply are an identity; authored changes remain in memory.
     *
     */
    void defaultsAndRoundTrip()
    {
        auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
        QVERIFY(layer);
        SWMM_Engine e = layer->engine();
        SimulationOptionsDialog dlg(e, layer.get(), QStringLiteral("6.0.0"), nullptr, nullptr);

        auto *gw = child<QComboBox>(&dlg, "gw2DEnableCombo");
        auto *et = child<QComboBox>(&dlg, "gw2DEtCombo");
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

    /**
     *
     * @brief Require two API-authored subsurface quality/sorption rows to update the status count
     * and survive an unrelated Apply.
     * Also checks the initial no-subsurface summary; does not execute transport.
     *
     */
    void transportSummaryCountsAuthoredRows()
    {
        auto layer = openLayer(fixture(QStringLiteral("mini_2d.inp")));
        QVERIFY(layer);
        SWMM_Engine e = layer->engine();
        {
            SimulationOptionsDialog dlg(e, layer.get(), QStringLiteral("6.0.0"), nullptr, nullptr);
            auto *status = child<QLabel>(&dlg, "gw2DStatusLabel");
            QVERIFY(status);
            QVERIFY2(status->text().contains(QStringLiteral("No subsurface")),
                     qPrintable(status->text()));
        }
        // Author two rows through the C API, then re-open the dialog.
        QCOMPARE(swmm_gw_init_quality_set(e, SWMM_GW_SCOPE_GLOBAL, nullptr, -1, SWMM_GW_ZONE_SAT,
                                          -1, "__TEMPERATURE__", 12.0),
                 SWMM_OK);
        QCOMPARE(swmm_gw_sorption_set(e, SWMM_GW_SCOPE_GLOBAL, nullptr, -1, "__TEMPERATURE__", 0.0,
                                      -1.0),
                 SWMM_OK);
        {
            SimulationOptionsDialog dlg(e, layer.get(), QStringLiteral("6.0.0"), nullptr, nullptr);
            auto *status = child<QLabel>(&dlg, "gw2DStatusLabel");
            QVERIFY(status);
            QVERIFY2(status->text().startsWith(QStringLiteral("2 subsurface")),
                     qPrintable(status->text()));
            // Imported/API-authored GW rows are not edited by this dialog:
            // Apply must preserve them.
            QVERIFY(QMetaObject::invokeMethod(&dlg, "onApply", Qt::DirectConnection));
            QCOMPARE(swmm_gw_init_quality_count(e), 1);
            QCOMPARE(swmm_gw_sorption_count(e), 1);
        }
    }
};

QTEST_MAIN(TestGwPageKeys)
#include "test_gw_page_keys.moc"
