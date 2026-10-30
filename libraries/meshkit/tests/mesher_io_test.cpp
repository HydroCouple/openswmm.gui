// SPDX-License-Identifier: GPL-3.0-or-later
#include "mesh/inpmeshreader.h"
#include "mesh/inpmeshwriter.h"
#include "mesh/meshgenerator.h"
#include "mesh/sms2dmreader.h"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <cmath>
#include <cstdlib>
#include <iostream>

static void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    mesh::MeshGenerator generator;
    generator.setDomain(QPolygonF{{0, 0}, {20, 0}, {20, 20}, {0, 20}, {0, 0}});
    mesh::GenerationOptions options;
    options.maxArea = 4.0;
    generator.setOptions(options);
    const auto generated = generator.generate();
    require(generated.ok, qPrintable(generated.errorMsg));
    require(generated.triangles.size() > 10, "independent mesher must refine the domain");
    require(!generated.boundaryEdges.empty(), "domain constraints must be retained");

    // Sparse IDs, mixed connectivity and material tags exercise the standalone
    // codec surface without any application/engine or GDAL dependency.
    const auto original = mesh::Sms2dmReader::parse(
        QStringLiteral("MESH2D\nND 10 0 0 1\nND 20 2 0 2\nND 30 2 2 3\nND 40 0 2 4\n"
                       "ND 50 4 0 5\nE3T 7 20 50 30 3\nE4Q 8 10 20 30 40 4\n"));
    require(original.ok, qPrintable(original.errorMsg));
    require(original.vertices.size() == 5 && original.triangles.size() == 2,
            "mixed SMS mesh must preserve node/cell counts");
    require(original.triangles[0].v3 < 0 && original.triangles[1].v3 >= 0,
            "triangles and quadrilaterals must retain their cell types");
    QTemporaryDir dir;
    require(dir.isValid(), "temporary test directory required");
    const auto path = dir.filePath("roundtrip.2dm");
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), "roundtrip file must open");
    mesh::InpMeshWriter::UnitInfo units;
    units.linearUnitName = "SI (m)";
    const auto text = mesh::InpMeshWriter::buildSectionText(original, {}, .035, units).toUtf8();
    require(file.write(text) == text.size(), "complete mesh text must be written");
    file.close();
    const auto read = mesh::InpMeshReader::read(path);
    require(read.hasMesh && read.errorMsg.isEmpty(), qPrintable(read.errorMsg));
    require(read.mesh.vertices.size() == original.vertices.size() &&
                read.mesh.triangles.size() == original.triangles.size(),
            "roundtrip counts must match");
    require(mesh::unitsHeaderIsSI(read.unitsHeader), "unit metadata must survive the roundtrip");
    for (int i = 0; i < original.vertices.size(); ++i) {
        require(read.mesh.vertices[i].xy == original.vertices[i].xy, "roundtrip XY changed");
        require(read.mesh.vertices[i].z == original.vertices[i].z, "roundtrip elevation changed");
    }
    for (int i = 0; i < original.triangles.size(); ++i) {
        const auto &a = original.triangles[i], &b = read.mesh.triangles[i];
        require(a.v0 == b.v0 && a.v1 == b.v1 && a.v2 == b.v2 && a.v3 == b.v3,
                "roundtrip connectivity changed");
    }
    const auto smsPath = dir.filePath("streamed.2dm");
    QFile smsFile(smsPath); require(smsFile.open(QIODevice::WriteOnly), "SMS fixture open");
    smsFile.write("MESH2D\nE3T 8 10 20 30 earth\nND 10 0 0 1\nND 20 2 0 2\nND 30 0 2 3\nNS 10 -20\nUNSUPPORTED data\n");smsFile.close();
    QStringList warnings;const auto streamed=mesh::Sms2dmReader::read(smsPath,false,&warnings);
    require(streamed.ok,qPrintable(streamed.errorMsg));require(streamed.triangles[0].tag=="matearth","legacy tag retained");
    require(streamed.boundaryEdges.size()==1,"node string retained");require(!warnings.empty(),"unknown card reported");
    std::cout << "Standalone generation and mixed-cell SMS/native mesh codec roundtrip passed.\n";
}
