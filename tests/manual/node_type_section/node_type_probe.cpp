/*!
 * \file node_type_probe.cpp
 * \brief Renders one node profile per node type and per connecting-link type,
 *        so the type-distinguishing drawing can be reviewed as pixels.
 *
 * Not a test: the assertions that guard this behaviour live in
 * tests/gui/test_sectionmodelbuilders.cpp. This exists because "a storage unit
 * should not look like a junction" is a claim about a picture, and the only way
 * to check a picture is to look at it.
 *
 * Fixtures are built in memory with the object/setter API, so no .inp is
 * needed and every case is exactly as extreme as it needs to be.
 *
 * Build + run:  sh tests/manual/node_type_section/build_and_run.sh
 * Output:       tests/manual/node_type_section/out/*.png
 */

#include "ui/sectionview/sectiondiagram.h"
#include "ui/sectionview/sectionmodelbuilders.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_tables.h>

#include <QApplication>
#include <QDir>
#include <QImage>
#include <QPainter>
#include <QPalette>
#include <QTextStream>

using namespace openswmmvis::sectionview;

namespace {

QString gOut;
const DiagramUnits kUnits{ QStringLiteral("ft"), false };

QPalette darkPalette()
{
    QPalette p;
    p.setColor(QPalette::Base,       QColor(0x1e, 0x1e, 0x1e));
    p.setColor(QPalette::Window,     QColor(0x1e, 0x1e, 0x1e));
    p.setColor(QPalette::Text,       QColor(0xe0, 0xe0, 0xe0));
    p.setColor(QPalette::WindowText, QColor(0xe0, 0xe0, 0xe0));
    p.setColor(QPalette::Highlight,  QColor(0x3d, 0x8b, 0xfd));
    p.setColor(QPalette::Mid,        QColor(0x6a, 0x6a, 0x6a));
    p.setColor(QPalette::Dark,       QColor(0x9a, 0x9a, 0x9a));
    p.setColor(QPalette::Midlight,   QColor(0x4a, 0x4a, 0x4a));
    p.setColor(QPalette::Button,     QColor(0x2d, 0x2d, 0x2d));
    return p;
}

void save(const SectionDiagramModel &m, const QString &file, const QPalette &pal,
          QSize size = QSize(520, 420))
{
    QImage img(size, QImage::Format_ARGB32_Premultiplied);
    img.fill(pal.color(QPalette::Base));
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing, true);
        paintSectionDiagram(p, QRectF(QPointF(0, 0), QSizeF(size)), m, pal);
    }
    const QString path = gOut + QLatin1Char('/') + file;
    QTextStream(stdout) << (img.save(path) ? "  wrote " : "  FAILED ") << path
                        << "\n      subtitle=\"" << m.subtitle << "\""
                        << "\n      footer=\"" << m.footer << "\""
                        << "  polys=" << m.polys.size()
                        << " symbols=" << m.symbols.size()
                        << " arrows=" << m.arrows.size() << Qt::endl;
}

//! An upstream junction + a conduit into \p target, so every case has a pipe.
int addFeeder(SWMM_Engine e, const char *name, int target, double invert,
              double diameter)
{
    swmm_node_add(e, name, SWMM_NODE_JUNCTION);
    const int up = swmm_node_index(e, name);
    swmm_node_set_invert_elev(e, up, invert + 4.0);
    swmm_node_set_max_depth(e, up, 8.0);

    const QByteArray linkId = QByteArray("C_") + name;
    swmm_link_add(e, linkId.constData(), SWMM_LINK_CONDUIT);
    const int c = swmm_link_index(e, linkId.constData());
    swmm_link_set_nodes(e, c, up, target);
    swmm_link_set_length(e, c, 300.0);
    swmm_link_set_xsect(e, c, SWMM_XSECT_CIRCULAR, diameter, 0, 0, 0);
    return c;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    gOut = (argc > 1) ? QString::fromLocal8Bit(argv[1])
                      : QStringLiteral("tests/manual/node_type_section/out");
    QDir().mkpath(gOut);

    const QPalette light;
    const QPalette dark = darkPalette();

    // ── 1. Junction: corbelled chamber, frame & cover, two pipes ────────────
    {
        SWMM_Engine e = swmm_engine_new();
        swmm_node_add(e, "J1", SWMM_NODE_JUNCTION);
        const int j = swmm_node_index(e, "J1");
        swmm_node_set_invert_elev(e, j, 90.0);
        swmm_node_set_max_depth(e, j, 12.0);
        addFeeder(e, "UP", j, 90.5, 1.5);

        swmm_node_add(e, "DN", SWMM_NODE_JUNCTION);
        const int dn = swmm_node_index(e, "DN");
        swmm_node_set_invert_elev(e, dn, 88.0);
        swmm_link_add(e, "OUT", SWMM_LINK_CONDUIT);
        const int out = swmm_link_index(e, "OUT");
        swmm_link_set_nodes(e, out, j, dn);
        swmm_link_set_length(e, out, 400.0);
        swmm_link_set_xsect(e, out, SWMM_XSECT_CIRCULAR, 2.0, 0, 0, 0);

        save(buildNodeProfile(e, j, kUnits), QStringLiteral("1_junction.png"), light);
        save(buildNodeProfile(e, j, kUnits), QStringLiteral("1_junction_dark.png"), dark);
        swmm_engine_destroy(e);
    }

    // ── 2. Storage, one image per shape ─────────────────────────────────────
    struct StorageCase { const char *tag; int shape; double p1, p2, p3; };
    static const StorageCase kStorage[] = {
        { "cylindrical", SWMM_STORAGE_CYLINDRICAL, 40.0, 30.0, 0.0 },
        { "conical",     SWMM_STORAGE_CONICAL,     20.0, 16.0, 3.0 },
        { "pyramidal",   SWMM_STORAGE_PYRAMIDAL,   24.0, 18.0, 2.5 },
        { "paraboloid",  SWMM_STORAGE_PARABOLOID,  60.0, 40.0, 10.0 },
    };
    for (const auto &sc : kStorage) {
        SWMM_Engine e = swmm_engine_new();
        swmm_node_add(e, "ST1", SWMM_NODE_STORAGE);
        const int s = swmm_node_index(e, "ST1");
        swmm_node_set_invert_elev(e, s, 100.0);
        swmm_node_set_max_depth(e, s, 10.0);
        swmm_node_set_initial_depth(e, s, 4.0);
        swmm_node_set_storage_shape(e, s, sc.shape);
        swmm_node_set_storage_geometry(e, s, sc.p1, sc.p2, sc.p3);
        swmm_node_set_storage_seep_rate(e, s, 0.02);
        addFeeder(e, "IN", s, 106.0, 2.0);

        save(buildNodeProfile(e, s, kUnits),
             QStringLiteral("2_storage_%1.png").arg(QLatin1String(sc.tag)), light);
        swmm_engine_destroy(e);
    }

    // Functional + tabular, which derive their silhouette from data rather than
    // from a shape code.
    {
        SWMM_Engine e = swmm_engine_new();
        swmm_node_add(e, "ST2", SWMM_NODE_STORAGE);
        const int s = swmm_node_index(e, "ST2");
        swmm_node_set_invert_elev(e, s, 100.0);
        swmm_node_set_max_depth(e, s, 10.0);
        swmm_node_set_initial_depth(e, s, 2.5);
        swmm_node_set_storage_functional(e, s, 500.0, 1.5, 200.0);
        addFeeder(e, "IN2", s, 105.0, 2.0);
        save(buildNodeProfile(e, s, kUnits),
             QStringLiteral("2_storage_functional.png"), light);
        save(buildNodeProfile(e, s, kUnits),
             QStringLiteral("2_storage_functional_dark.png"), dark);
        swmm_engine_destroy(e);
    }
    {
        SWMM_Engine e = swmm_engine_new();
        swmm_curve_add(e, "SC1", 3 /* storage curve */);
        const int cv = swmm_table_index(e, "SC1");
        // A pond that widens fast near the bottom then flattens — a shape a
        // rectangle would completely hide.
        swmm_table_add_point(e, cv,  0.0,  400.0);
        swmm_table_add_point(e, cv,  2.0, 2600.0);
        swmm_table_add_point(e, cv,  5.0, 4200.0);
        swmm_table_add_point(e, cv, 10.0, 5000.0);

        swmm_node_add(e, "ST3", SWMM_NODE_STORAGE);
        const int s = swmm_node_index(e, "ST3");
        swmm_node_set_invert_elev(e, s, 100.0);
        swmm_node_set_max_depth(e, s, 10.0);
        swmm_node_set_initial_depth(e, s, 3.0);
        swmm_node_set_storage_curve(e, s, cv);
        addFeeder(e, "IN3", s, 106.0, 2.5);
        save(buildNodeProfile(e, s, kUnits),
             QStringLiteral("2_storage_tabular.png"), light);
        swmm_engine_destroy(e);
    }

    // ── 3. Outfalls: free discharge, fixed tailwater, tidal ─────────────────
    struct OutfallCase { const char *tag; int type; double stage; int flap; };
    static const OutfallCase kOutfalls[] = {
        { "free",       0, 0.0,  0 },
        { "fixed",      2, 95.0, 1 },
        { "tidal",      3, 0.0,  0 },
    };
    for (const auto &oc : kOutfalls) {
        SWMM_Engine e = swmm_engine_new();
        swmm_node_add(e, "OF1", SWMM_NODE_OUTFALL);
        const int o = swmm_node_index(e, "OF1");
        swmm_node_set_invert_elev(e, o, 90.0);
        swmm_node_set_max_depth(e, o, 10.0);
        swmm_node_set_outfall_type(e, o, oc.type);
        if (oc.type == 2) swmm_node_set_outfall_stage(e, o, oc.stage);
        swmm_node_set_outfall_flap_gate(e, o, oc.flap);
        addFeeder(e, "OUTF", o, 91.0, 3.0);

        save(buildNodeProfile(e, o, kUnits),
             QStringLiteral("3_outfall_%1.png").arg(QLatin1String(oc.tag)), light);
        if (oc.type == 0)
            save(buildNodeProfile(e, o, kUnits),
                 QStringLiteral("3_outfall_free_dark.png"), dark);
        swmm_engine_destroy(e);
    }

    // ── 4. Divider ──────────────────────────────────────────────────────────
    {
        SWMM_Engine e = swmm_engine_new();
        swmm_node_add(e, "DV1", SWMM_NODE_DIVIDER);
        const int d = swmm_node_index(e, "DV1");
        swmm_node_set_invert_elev(e, d, 95.0);
        swmm_node_set_max_depth(e, d, 8.0);
        swmm_node_set_divider_type(e, d, SWMM_DIVIDER_CUTOFF);
        addFeeder(e, "IND", d, 96.0, 2.0);
        save(buildNodeProfile(e, d, kUnits), QStringLiteral("4_divider.png"), light);
        swmm_engine_destroy(e);
    }

    // ── 5. Device connections on one junction: pump, orifice, weir, outlet ──
    {
        SWMM_Engine e = swmm_engine_new();
        swmm_node_add(e, "WW", SWMM_NODE_JUNCTION);
        const int ww = swmm_node_index(e, "WW");
        swmm_node_set_invert_elev(e, ww, 80.0);
        swmm_node_set_max_depth(e, ww, 14.0);
        addFeeder(e, "SEW", ww, 84.0, 2.0);

        swmm_node_add(e, "FM", SWMM_NODE_JUNCTION);
        const int fm = swmm_node_index(e, "FM");
        swmm_node_set_invert_elev(e, fm, 95.0);
        swmm_link_add(e, "PMP", SWMM_LINK_PUMP);
        const int pmp = swmm_link_index(e, "PMP");
        swmm_link_set_nodes(e, pmp, ww, fm);

        swmm_node_add(e, "OR_DN", SWMM_NODE_JUNCTION);
        swmm_link_add(e, "ORF", SWMM_LINK_ORIFICE);
        const int orf = swmm_link_index(e, "ORF");
        swmm_link_set_nodes(e, orf, ww, swmm_node_index(e, "OR_DN"));
        swmm_link_set_xsect(e, orf, SWMM_XSECT_RECT_CLOSED, 1.5, 2.0, 0, 0);
        swmm_link_set_offset_up(e, orf, 3.0);
        swmm_link_set_orifice_type(e, orf, SWMM_ORIFICE_SIDE);

        swmm_node_add(e, "WR_DN", SWMM_NODE_JUNCTION);
        swmm_link_add(e, "WEIR", SWMM_LINK_WEIR);
        const int wr = swmm_link_index(e, "WEIR");
        swmm_link_set_nodes(e, wr, ww, swmm_node_index(e, "WR_DN"));
        swmm_link_set_xsect(e, wr, SWMM_XSECT_RECT_OPEN, 2.0, 6.0, 0, 0);
        swmm_link_set_offset_up(e, wr, 9.0);
        swmm_link_set_weir_type(e, wr, SWMM_WEIR_TRANSVERSE);

        swmm_node_add(e, "OL_DN", SWMM_NODE_JUNCTION);
        swmm_link_add(e, "OTL", SWMM_LINK_OUTLET);
        const int ol = swmm_link_index(e, "OTL");
        swmm_link_set_nodes(e, ol, ww, swmm_node_index(e, "OL_DN"));
        swmm_link_set_offset_up(e, ol, 1.0);

        save(buildNodeProfile(e, ww, kUnits), QStringLiteral("5_devices.png"),
             light, QSize(640, 460));
        save(buildNodeProfile(e, ww, kUnits), QStringLiteral("5_devices_dark.png"),
             dark, QSize(640, 460));

        // Same wet well, now as a link profile of the pump itself.
        save(buildLinkProfile(e, pmp, kUnits),
             QStringLiteral("6_link_profile_pump.png"), light, QSize(640, 400));
        swmm_engine_destroy(e);
    }

    // ── 6. Link profile into a storage unit ─────────────────────────────────
    {
        SWMM_Engine e = swmm_engine_new();
        swmm_node_add(e, "J9", SWMM_NODE_JUNCTION);
        swmm_node_add(e, "POND", SWMM_NODE_STORAGE);
        const int j = swmm_node_index(e, "J9");
        const int s = swmm_node_index(e, "POND");
        swmm_node_set_invert_elev(e, j, 104.0);
        swmm_node_set_max_depth(e, j, 8.0);
        swmm_node_set_invert_elev(e, s, 100.0);
        swmm_node_set_max_depth(e, s, 10.0);
        swmm_node_set_storage_shape(e, s, SWMM_STORAGE_CONICAL);
        swmm_node_set_storage_geometry(e, s, 30.0, 20.0, 3.0);

        swmm_link_add(e, "INLET", SWMM_LINK_CONDUIT);
        const int c = swmm_link_index(e, "INLET");
        swmm_link_set_nodes(e, c, j, s);
        swmm_link_set_length(e, c, 250.0);
        swmm_link_set_xsect(e, c, SWMM_XSECT_CIRCULAR, 2.5, 0, 0, 0);
        save(buildLinkProfile(e, c, kUnits),
             QStringLiteral("6_link_profile_storage.png"), light, QSize(640, 400));
        swmm_engine_destroy(e);
    }

    // ── 7. From an .inp: the storage water surface and a gated outfall ──────
    // The initial depth of a storage unit is only reachable through the parser
    // (see storage_water.inp), so this is the only case that can show water
    // standing in a tank at the depth the model starts from.
    {
        const QString here = QStringLiteral(__FILE__).section(QLatin1Char('/'), 0, -2);
        const QByteArray inp = (here + QStringLiteral("/storage_water.inp")).toUtf8();
        const QByteArray rpt = (gOut + QStringLiteral("/storage_water.rpt")).toUtf8();
        const QByteArray out = (gOut + QStringLiteral("/storage_water.out")).toUtf8();

        SWMM_Engine e = swmm_engine_create();
        const int rc = swmm_engine_open(e, inp.constData(), rpt.constData(),
                                       out.constData(), nullptr);
        if (rc != 0) {
            QTextStream(stderr) << "  inp open failed rc=" << rc << ": "
                                << swmm_get_last_error_msg(e) << Qt::endl;
        } else {
            save(buildNodeProfile(e, swmm_node_index(e, "POND"), kUnits),
                 QStringLiteral("7_inp_storage_water.png"), light, QSize(600, 440));
            save(buildNodeProfile(e, swmm_node_index(e, "POND"), kUnits),
                 QStringLiteral("7_inp_storage_water_dark.png"), dark, QSize(600, 440));
            save(buildNodeProfile(e, swmm_node_index(e, "OF_FIX"), kUnits),
                 QStringLiteral("7_inp_outfall_gated.png"), light, QSize(600, 440));
            swmm_engine_close(e);
        }
        swmm_engine_destroy(e);
    }

    // ── 8. A real model, at a realistic dock size ───────────────────────────
    // structures_demo.inp hangs a pump, an orifice and a weir off ONE face of
    // J4 at the SAME invert, which is the case that broke the first attempt at
    // device drawing. Rendered wide because that is the size the Section View
    // dock actually gets when a user opens it.
    {
        const QString here = QStringLiteral(__FILE__).section(QLatin1Char('/'), 0, -2);
        const QByteArray inp = (here + QStringLiteral("/structures_demo.inp")).toUtf8();
        const QByteArray rpt = (gOut + QStringLiteral("/structures_demo.rpt")).toUtf8();
        const QByteArray out = (gOut + QStringLiteral("/structures_demo.out")).toUtf8();

        SWMM_Engine e = swmm_engine_create();
        if (swmm_engine_open(e, inp.constData(), rpt.constData(),
                             out.constData(), nullptr) != 0) {
            QTextStream(stderr) << "  structures_demo open failed: "
                                << swmm_get_last_error_msg(e) << Qt::endl;
        } else {
            struct Case { const char *node; const char *file; };
            static const Case kCases[] = {
                { "J4",   "8_demo_J4_devices.png" },
                { "ST1",  "8_demo_ST1_storage.png" },
                { "OUT2", "8_demo_OUT2_gated_outfall.png" },
                { "J2",   "8_demo_J2_junction.png" },
            };
            for (const auto &c : kCases) {
                const int idx = swmm_node_index(e, c.node);
                if (idx < 0) continue;
                save(buildNodeProfile(e, idx, kUnits),
                     QLatin1String(c.file), light, QSize(900, 620));
            }
            save(buildNodeProfile(e, swmm_node_index(e, "J4"), kUnits),
                 QStringLiteral("8_demo_J4_devices_dark.png"), dark, QSize(900, 620));
            swmm_engine_close(e);
        }
        swmm_engine_destroy(e);
    }

    QTextStream(stdout) << "done → " << gOut << Qt::endl;
    return 0;
}
