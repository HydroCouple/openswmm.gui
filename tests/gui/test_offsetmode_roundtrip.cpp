/*!
 * \file   test_offsetmode_roundtrip.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Open → Save must preserve what the user authored: the LINK_OFFSETS
 *         convention of every offset/crest and the From/To orientation of
 *         adverse-slope conduits.
 *
 *         The engine normalises both at open (elevations become depths;
 *         adverse conduits are reversed for DYNWAVE/FV). Before the fix the
 *         GUI saved the live context verbatim, so an ELEVATION deck came back
 *         with depths under an ELEVATION header (clamped to 0 on the next
 *         open) and adverse conduits pointed the other way in the file.
 *         Legacy SWMM-GUI never showed either because it exports its own
 *         object model (Uexport.pas).
 *
 *         Fixture: offset_authored_fixture.inp — LINK_OFFSETS ELEVATION;
 *           C_ADV J1(10)→J2(12) adverse, offsets 10.5/12.25, InitFlow 0.75,
 *                 losses 0.1/0.2; C_OK J2→J3 12.5/11.0;
 *           OR1 offset 11.5, W1 crest 12.0, L1 offset 11.25 (all from J3=11).
 *         Output is written next to the fixture (reviewable, CLAUDE.md §4.1).
 */
#include "layers/swmmmodellayer.h"
#include "ui/linkoffsetdisplay.h"

#include <QDir>
#include <QFile>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTest>

#include <memory>

#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_nodes.h>

namespace {

QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

std::unique_ptr<SWMMModelLayer> openLayer(const QString &file)
{
    auto layer = std::make_unique<SWMMModelLayer>(QDir(dataDir()).filePath(file), nullptr);
    QList<QString> warnings, errors;
    if (!layer->loadModel(warnings, errors)) return nullptr;
    return layer;
}

//! Whitespace-split row of `section` whose first token is `id`.
QStringList row(const QString &path, const QString &section, const QString &id)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    bool inside = false;
    for (const QString &raw : QString::fromUtf8(f.readAll()).split('\n'))
    {
        const QString line = raw.trimmed();
        if (line.startsWith('['))
        {
            inside = line.startsWith(QStringLiteral("[%1]").arg(section));
            continue;
        }
        if (!inside || line.isEmpty() || line.startsWith(';')) continue;
        const QStringList c = line.split(' ', Qt::SkipEmptyParts);
        if (!c.isEmpty() && c.first() == id) return c;
    }
    return {};
}

QString nodeName(SWMM_Engine e, int idx)
{
    return idx >= 0 ? QString::fromUtf8(swmm_node_id(e, idx)) : QString();
}

} // namespace

class TestOffsetModeRoundTrip : public QObject
{
    Q_OBJECT

private slots:

    //! After open the edit context shows the authored J1→J2, not the solver's
    //! reversed J2→J1, and the offsets travelled back with their nodes.
    void adverseConduitLoadsWithAuthoredOrientation()
    {
        auto layer = openLayer(QStringLiteral("offset_authored_fixture.inp"));
        QVERIFY(layer);
        SWMM_Engine e = layer->engine();
        const int idx = layer->linkIndex(QStringLiteral("C_ADV"));
        QVERIFY(idx >= 0);

        int from = -1, to = -1;
        swmm_link_get_from_node(e, idx, &from);
        swmm_link_get_to_node(e, idx, &to);
        QCOMPARE(nodeName(e, from), QStringLiteral("J1"));
        QCOMPARE(nodeName(e, to),   QStringLiteral("J2"));
        QCOMPARE(layer->linkFromNodeIdx(idx), from);
        QCOMPARE(layer->linkToNodeIdx(idx),   to);

        double up = 0, dn = 0;
        swmm_link_get_offset_up(e, idx, &up);
        swmm_link_get_offset_dn(e, idx, &dn);
        QCOMPARE(up, 0.5);    // 10.5 − 10
        QCOMPARE(dn, 0.25);   // 12.25 − 12
        // InitFlow (q0) has no C-API getter — swmm_link_get_initial_flow reads
        // the live flow state, which is 0 before a run. Its restored sign is
        // asserted through the file in saveWithoutEditsPreservesAuthoredForm.
    }

    //! The editors present offsets in the file's convention: elevations here.
    void editorsShowElevationsInElevationMode()
    {
        auto layer = openLayer(QStringLiteral("offset_authored_fixture.inp"));
        QVERIFY(layer);
        SWMM_Engine e = layer->engine();
        QVERIFY(linkoffsetdisplay::elevationMode(e));

        double v = 0;
        linkoffsetdisplay::getOffsetUp(e, layer->linkIndex(QStringLiteral("C_OK")), &v);
        QCOMPARE(v, 12.5);
        linkoffsetdisplay::getOffsetDn(e, layer->linkIndex(QStringLiteral("C_OK")), &v);
        QCOMPARE(v, 11.0);
        linkoffsetdisplay::getCrestHeight(e, layer->linkIndex(QStringLiteral("W1")), &v);
        QCOMPARE(v, 12.0);
        linkoffsetdisplay::getOffsetUp(e, layer->linkIndex(QStringLiteral("OR1")), &v);
        QCOMPARE(v, 11.5);

        // An elevation typed back lands as a depth in the store.
        const int ok = layer->linkIndex(QStringLiteral("C_OK"));
        QCOMPARE(linkoffsetdisplay::setOffsetUp(e, ok, 13.0), SWMM_OK);
        swmm_link_get_offset_up(e, ok, &v);
        QCOMPARE(v, 1.0);
        // Below the invert clamps to 0 (legacy GetOffsetDepth).
        QCOMPARE(linkoffsetdisplay::setOffsetUp(e, ok, 9.0), SWMM_OK);
        swmm_link_get_offset_up(e, ok, &v);
        QCOMPARE(v, 0.0);
    }

    //! Save with no edits reproduces the authored file: elevations under the
    //! ELEVATION header, J1→J2 with its own offsets, losses and InitFlow.
    void saveWithoutEditsPreservesAuthoredForm()
    {
        auto layer = openLayer(QStringLiteral("offset_authored_fixture.inp"));
        QVERIFY(layer);
        const QString out = QDir(dataDir()).filePath(QStringLiteral("offset_authored_out.inp"));
        QCOMPARE(swmm_model_write(layer->engine(), out.toUtf8().constData()), 0);

        QCOMPARE(row(out, "OPTIONS", "LINK_OFFSETS").value(1), QStringLiteral("ELEVATION"));

        const QStringList adv = row(out, "CONDUITS", "C_ADV");
        QVERIFY(adv.size() >= 8);
        QCOMPARE(adv.at(1), QStringLiteral("J1"));
        QCOMPARE(adv.at(2), QStringLiteral("J2"));
        QCOMPARE(adv.at(5).toDouble(), 10.5);
        QCOMPARE(adv.at(6).toDouble(), 12.25);
        QCOMPARE(adv.at(7).toDouble(), 0.75);

        const QStringList ok = row(out, "CONDUITS", "C_OK");
        QCOMPARE(ok.at(5).toDouble(), 12.5);
        QCOMPARE(ok.at(6).toDouble(), 11.0);

        QCOMPARE(row(out, "ORIFICES", "OR1").at(4).toDouble(), 11.5);
        QCOMPARE(row(out, "WEIRS",    "W1").at(4).toDouble(),  12.0);
        QCOMPARE(row(out, "OUTLETS",  "L1").at(3).toDouble(),  11.25);

        const QStringList loss = row(out, "LOSSES", "C_ADV");
        QCOMPARE(loss.at(1).toDouble(), 0.1);
        QCOMPARE(loss.at(2).toDouble(), 0.2);

        // And a second open of the written file sees the same depths.
        auto again = openLayer(QStringLiteral("offset_authored_out.inp"));
        QVERIFY(again);
        double up = 0;
        swmm_link_get_offset_up(again->engine(), again->linkIndex(QStringLiteral("C_OK")), &up);
        QCOMPARE(up, 0.5);
    }

    //! Toggle to DEPTH + "Yes" keeps the physics: file offsets become depths.
    //! Toggle + "No" keeps the numbers: the old elevation 12.5 is now a depth.
    void togglePromptSemanticsMatchLegacy()
    {
        auto layer = openLayer(QStringLiteral("offset_authored_fixture.inp"));
        QVERIFY(layer);
        SWMM_Engine e = layer->engine();
        const int ok = layer->linkIndex(QStringLiteral("C_OK"));

        QCOMPARE(swmm_options_set(e, "LINK_OFFSETS", "DEPTH"), 0);
        layer->convertLinkOffsets(/*toElevation*/ false, /*convertValues*/ true);
        double up = 0;
        swmm_link_get_offset_up(e, ok, &up);
        QCOMPARE(up, 0.5);
        linkoffsetdisplay::getOffsetUp(e, ok, &up);
        QCOMPARE(up, 0.5);

        const QString out = QDir(dataDir()).filePath(QStringLiteral("offset_authored_depth_out.inp"));
        QCOMPARE(swmm_model_write(e, out.toUtf8().constData()), 0);
        QCOMPARE(row(out, "OPTIONS", "LINK_OFFSETS").value(1), QStringLiteral("DEPTH"));
        QCOMPARE(row(out, "CONDUITS", "C_OK").at(5).toDouble(), 0.5);

        // Back to ELEVATION, answering "No": the depth 0.5 is now read as an
        // elevation 0.5, which is below the invert → clamped to 0.
        QCOMPARE(swmm_options_set(e, "LINK_OFFSETS", "ELEVATION"), 0);
        layer->convertLinkOffsets(true, false);
        swmm_link_get_offset_up(e, ok, &up);
        QCOMPARE(up, 0.0);

        // And ELEVATION → DEPTH answering "No": elevation 12.0 (0 + J2 invert)
        // becomes a depth of 12.0.
        QCOMPARE(swmm_options_set(e, "LINK_OFFSETS", "DEPTH"), 0);
        layer->convertLinkOffsets(false, false);
        swmm_link_get_offset_up(e, ok, &up);
        QCOMPARE(up, 12.0);
    }
};

QTEST_MAIN(TestOffsetModeRoundTrip)
#include "test_offsetmode_roundtrip.moc"
