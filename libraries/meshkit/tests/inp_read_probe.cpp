// SPDX-License-Identifier: Apache-2.0
// Opt-in timing probe for InpMeshReader::read on large [2D_*] meshes. It measures the reader
// only: no engine open, no layer/scene build, no GUI.
//
//   meshkit_inp_read_probe read   <model.inp>   whole InpMeshReader::read call
//   meshkit_inp_read_probe steps  <mesh.2dm>    the reader's text stages, re-enacted in order
//   meshkit_inp_read_probe bcfill <cells>       the default edge-BC array alone
//
// One mode per process so peak RSS belongs to that mode. `steps` and `bcfill` repeat what
// inpmeshreader.cpp does internally (its helpers are file-local), so they attribute cost; `read`
// is the number that counts.
#include "mesh/inpmeshreader.h"
#include "mesh/meshcellgeom.h"
#include "mesh/meshedgebc.h"
#include "mesh/meshresult.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <iostream>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

namespace
{
double peakBytes()
{
    double peak = 0;
#if defined(__unix__) || defined(__APPLE__)
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        peak = double(usage.ru_maxrss);
#ifndef __APPLE__
        peak *= 1024;
#endif
    }
#endif
    return peak;
}

int report(QJsonObject o)
{
    o.insert("peakBytes", peakBytes());
    std::cout << QJsonDocument(o).toJson(QJsonDocument::Compact).constData() << '\n';
    return 0;
}

// Same splitting rule as inpmeshreader.cpp's tokenize().
QStringList tokenize(const QString &raw)
{
    const qsizetype semi = raw.indexOf(QChar(';'));
    const QStringView line = QStringView(raw).left(semi >= 0 ? semi : raw.size());
    QStringList out;
    qsizetype i = 0;
    const qsizetype n = line.size();
    while (i < n) {
        while (i < n && line[i].isSpace())
            ++i;
        if (i >= n)
            break;
        const qsizetype start = i;
        while (i < n && !line[i].isSpace())
            ++i;
        out.append(line.mid(start, i - start).toString());
    }
    return out;
}

int runRead(const QString &path)
{
    QElapsedTimer t;
    t.start();
    const mesh::InpMeshReadResult r = mesh::InpMeshReader::read(path);
    const qint64 ms = t.elapsed();
    if (!r.hasMesh) {
        std::cerr << "no mesh: " << qPrintable(r.errorMsg) << '\n';
        return 1;
    }
    return report({{"mode", "read"},
                 {"vertices", qint64(r.mesh.vertices.size())},
                 {"cells", qint64(r.mesh.triangles.size())},
                 {"edgeBCs", qint64(r.edgeBCs.size())},
                 {"readMs", ms},
                 {"sizeofVertex", int(sizeof(mesh::MeshVertex))},
                 {"sizeofCell", int(sizeof(mesh::MeshTriangle))},
                 {"sizeofEdgeBC", int(sizeof(mesh::MeshEdgeBC))}});
}

int runSteps(const QString &path)
{
    QElapsedTimer t;
    t.start();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::cerr << "cannot open " << qPrintable(path) << '\n';
        return 1;
    }
    const QByteArray bytes = f.readAll();
    const qint64 readAllMs = t.restart();
    const QString text = QString::fromUtf8(bytes);
    const qint64 utf16Ms = t.restart();
    const QStringList lines = text.split(QChar('\n'), Qt::KeepEmptyParts);
    const qint64 splitMs = t.restart();
    qint64 kept = 0;
    for (const QString &raw : lines)
        kept += raw.trimmed().size();
    const qint64 trimMs = t.restart();
    qint64 tokens = 0;
    for (const QString &raw : lines)
        tokens += tokenize(raw).size();
    const qint64 tokenizeMs = t.restart();
    // Tokenize again and convert, so (tokenizeConvertMs - tokenizeMs) is the conversion cost.
    qint64 numeric = 0;
    double sum = 0;
    for (const QString &raw : lines) {
        const QStringList tok = tokenize(raw);
        for (const QString &s : tok) {
            bool ok = false;
            const double v = s.toDouble(&ok);
            if (ok) {
                ++numeric;
                sum += v;
            }
        }
    }
    const qint64 tokenizeConvertMs = t.restart();

    // Byte-level alternatives over the same file, for the replacement parser's design: split on
    // ASCII whitespace without building strings, then convert every token with strtod and (where
    // the standard library has it) floating-point std::from_chars. Comment lines are not
    // stripped here, so token counts differ slightly from the QString pass.
    const char *const begin = bytes.constData(), *const end = begin + bytes.size();
    const auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    qint64 byteTokens = 0;
    for (const char *p = begin; p < end;) {
        while (p < end && isSpace(*p))
            ++p;
        if (p >= end)
            break;
        while (p < end && !isSpace(*p))
            ++p;
        ++byteTokens;
    }
    const qint64 byteTokenizeMs = t.restart();
    double strtodSum = 0;
    for (const char *p = begin; p < end;) {
        while (p < end && isSpace(*p))
            ++p;
        if (p >= end)
            break;
        char *stop = nullptr;
        const double v = std::strtod(p, &stop); // QByteArray data is NUL-terminated
        if (stop != p)
            strtodSum += v;
        while (p < end && !isSpace(*p))
            ++p;
    }
    const qint64 byteStrtodMs = t.restart();
    qint64 byteFromCharsMs = -1; // -1: floating-point from_chars unavailable in this toolchain
    double fromCharsSum = 0;
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
    for (const char *p = begin; p < end;) {
        while (p < end && isSpace(*p))
            ++p;
        if (p >= end)
            break;
        const char *q = p;
        while (q < end && !isSpace(*q))
            ++q;
        double v = 0;
        if (std::from_chars(p, q, v).ec == std::errc{})
            fromCharsSum += v;
        p = q;
    }
    byteFromCharsMs = t.restart();
#endif
    return report({{"mode", "steps"},
                 {"fileBytes", qint64(bytes.size())},
                 {"lines", qint64(lines.size())},
                 {"tokens", tokens},
                 {"numericTokens", numeric},
                 {"checksum", sum + double(kept)},
                 {"readAllMs", readAllMs},
                 {"utf16Ms", utf16Ms},
                 {"splitMs", splitMs},
                 {"trimMs", trimMs},
                 {"tokenizeMs", tokenizeMs},
                 {"tokenizeConvertMs", tokenizeConvertMs},
                 {"byteTokens", byteTokens},
                 {"byteTokenizeMs", byteTokenizeMs},
                 {"byteStrtodMs", byteStrtodMs},
                 {"byteFromCharsMs", byteFromCharsMs},
                 {"byteChecksum", strtodSum + fromCharsSum}});
}

int runBcFill(qint64 cells)
{
    QElapsedTimer t;
    t.start();
    QVector<mesh::MeshEdgeBC> bcs;
    bcs.resize(mesh::edgeSlotCount(int(cells)));
    std::fill(bcs.begin(), bcs.end(), mesh::MeshEdgeBC{});
    return report({{"mode", "bcfill"},
                 {"cells", cells},
                 {"slots", qint64(bcs.size())},
                 {"fillMs", t.elapsed()},
                 {"sizeofEdgeBC", int(sizeof(mesh::MeshEdgeBC))}});
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString mode = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    const QString arg = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();
    if (mode == QLatin1String("read") && !arg.isEmpty())
        return runRead(arg);
    if (mode == QLatin1String("steps") && !arg.isEmpty())
        return runSteps(arg);
    if (mode == QLatin1String("bcfill") && arg.toLongLong() > 0 && arg.toLongLong() <= 500'000'000)
        return runBcFill(arg.toLongLong());
    std::cerr << "Usage: meshkit_inp_read_probe read <model.inp> | steps <mesh.2dm> | bcfill <cells>\n";
    return 2;
}
