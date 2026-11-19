/**
 * @file      test_projectserializer.cpp
 * @brief     Direct Qt JSON round-trip examples and dormant ProjectSerializer path placeholder.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
// #include "project/projectserializer.h"   // uncomment when implemented

// ---- Extent round-trip ---------------------------------------------------

/**
 *
 * @brief Verify Qt JSON serialization preserves xmin/xmax of the authored extent object.
 * ProjectSerializer is not invoked, and this body does not assert the y coordinates.
 *
 */
TEST(ProjectSerializer, ExtentRoundTrip)
{
    QJsonObject extent;
    extent["xmin"] = 0.0;
    extent["ymin"] = 0.0;
    extent["xmax"] = 5000.0;
    extent["ymax"] = 5000.0;

    QJsonDocument doc(extent);
    QJsonDocument parsed = QJsonDocument::fromJson(doc.toJson());

    EXPECT_DOUBLE_EQ(parsed.object()["xmin"].toDouble(), 0.0);
    EXPECT_DOUBLE_EQ(parsed.object()["xmax"].toDouble(), 5000.0);
}

/**
 *
 * @brief Verify an empty sessions array remains empty through a direct Qt JSON round trip.
 * The production ProjectSerializer implementation is not invoked.
 *
 */
TEST(ProjectSerializer, EmptySessionList)
{
    QJsonObject project;
    project["version"] = 2;
    project["sessions"] = QJsonArray();

    QJsonDocument doc(project);
    QJsonDocument parsed = QJsonDocument::fromJson(doc.toJson());

    EXPECT_EQ(parsed.object()["sessions"].toArray().size(), 0);
}

// ---- Session path portability --------------------------------------------

/**
 *
 * @brief Reserve relative-project-path coverage; current body only asserts true.
 * The intended production serialization/path checks remain commented out.
 *
 */
TEST(ProjectSerializer, RelativePathsPreserved)
{
    // When implemented: serialize a session with an absolute path, verify that
    // ProjectSerializer stores a path relative to the .oswp file location.
    EXPECT_TRUE(true); // placeholder
}
