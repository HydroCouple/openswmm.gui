/**
 * @file      gwresults_fixture.h
 * @brief     Tiny HDF5 writers for the saved-run groundwater ledger and node
 *            exchange tests (V3/V4).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Fixtures mimic the engine's Default2DOutputPlugin layout
 * (`/time`, `/groundwater_ledger`, `/groundwater_species_ledger`,
 * `/groundwater_node_exchange_cum` and their string attributes). They are
 * written to GW_RESULTS_TEST_OUTPUT (a reviewable folder under the source
 * tree, set by tests/gui/CMakeLists.txt) — never a temp directory.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_TESTS_GWRESULTS_FIXTURE_H
#define OPENSWMMVIS_TESTS_GWRESULTS_FIXTURE_H

#include <hdf5.h>

#include <QDir>
#include <QString>

#include <vector>

namespace gwfixture
{

/** @return Output folder for fixture files, created on demand. */
inline QString outputDir()
{
    const QString dir = QStringLiteral(GW_RESULTS_TEST_OUTPUT);
    QDir().mkpath(dir);
    return dir;
}

/** @brief Create (truncate) \p name in outputDir(); the caller closes the handle. */
inline hid_t create(const QString &name)
{
    return H5Fcreate(QDir(outputDir()).filePath(name).toUtf8().constData(), H5F_ACC_TRUNC,
                     H5P_DEFAULT, H5P_DEFAULT);
}

/** @return Full path of fixture \p name. */
inline QString path(const QString &name)
{
    return QDir(outputDir()).filePath(name);
}

/** @brief Write a scalar fixed-length string attribute on root object \p object. */
inline void stringAttr(hid_t file, const char *object, const char *attr, const QString &value)
{
    const QByteArray text = value.toUtf8();
    hid_t obj = H5Oopen(file, object, H5P_DEFAULT);
    hid_t type = H5Tcopy(H5T_C_S1);
    H5Tset_size(type, std::size_t(text.size()) + 1);
    H5Tset_strpad(type, H5T_STR_NULLTERM);
    hid_t space = H5Screate(H5S_SCALAR);
    hid_t a = H5Acreate2(obj, attr, type, space, H5P_DEFAULT, H5P_DEFAULT);
    H5Awrite(a, type, text.constData());
    H5Aclose(a);
    H5Sclose(space);
    H5Tclose(type);
    H5Oclose(obj);
}

/** @brief Write a row-major double dataset \p name with extents \p dims. */
inline void doubles(hid_t file, const char *name, const std::vector<hsize_t> &dims,
                    const std::vector<double> &values)
{
    hid_t space = H5Screate_simple(int(dims.size()), dims.data(), nullptr);
    hid_t data = H5Dcreate2(file, name, H5T_NATIVE_DOUBLE, space, H5P_DEFAULT, H5P_DEFAULT,
                            H5P_DEFAULT);
    H5Dwrite(data, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, values.data());
    H5Dclose(data);
    H5Sclose(space);
}

/** @brief Write `/time` in SWMM DateTime days ("days since 1899-12-30"). */
inline void times(hid_t file, const std::vector<double> &days)
{
    doubles(file, "time", {hsize_t(days.size())}, days);
    stringAttr(file, "time", "units", QStringLiteral("days since 1899-12-30 00:00:00"));
}

} // namespace gwfixture

#endif // OPENSWMMVIS_TESTS_GWRESULTS_FIXTURE_H
