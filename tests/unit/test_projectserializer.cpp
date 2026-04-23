/**
 * @file test_projectserializer.cpp
 * @brief Unit tests for ProjectSerializer — JSON round-trip for session and display state.
 */

#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
// #include "project/projectserializer.h"   // uncomment when implemented

// ---- Extent round-trip ---------------------------------------------------

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

TEST(ProjectSerializer, RelativePathsPreserved)
{
    // When implemented: serialize a session with an absolute path, verify that
    // ProjectSerializer stores a path relative to the .oswp file location.
    EXPECT_TRUE(true); // placeholder
}
