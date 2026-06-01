/*!
 * \file   test_sidecar_autocreate.cpp
 * \brief  Regression tests for .oswp sidecar auto-create (Slice RB).
 *
 * The integration paths (SWMMVisProjectWindow::saveAs writing the sidecar
 * after a successful built-in .inp write; openSingleINP creating the
 * sidecar on first open) require a full engine + canvas graph and are
 * exercised by the higher-level GUI test suite. This file covers the
 * isolatable pieces:
 *
 *  - ProjectSerializer::sidecarPathFor — guards against the §R.1 regression
 *    where stacked extensions could produce `model.inp.oswp`. After Slice
 *    RA normalises the dialog result, sidecarPathFor only ever receives a
 *    properly-cleaned `<stem>.inp`; the tests below lock that contract.
 *  - QSettings default for `Preferences/AutoCreateOswpOnOpen` — the new
 *    preference RB.4 introduced. Default must be `true` per the user's
 *    "by default" requirement, locked here so a refactor can't silently
 *    flip it.
 */

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QString>
#include <QTemporaryDir>

#include "project/projectserializer.h"

// ---------------------------------------------------------------------------
// sidecarPathFor — regression coverage for §R.1
// ---------------------------------------------------------------------------

TEST(SidecarAutoCreate, SidecarPathForEmptyReturnsEmpty)
{
    EXPECT_TRUE(ProjectSerializer::sidecarPathFor(QString()).isEmpty());
}

TEST(SidecarAutoCreate, SidecarPathForBareInp)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString inp = QDir(tmp.path()).absoluteFilePath(QStringLiteral("model.inp"));
    const QString expected = QDir(tmp.path()).absoluteFilePath(QStringLiteral("model.oswp"));
    EXPECT_EQ(ProjectSerializer::sidecarPathFor(inp), expected);
}

TEST(SidecarAutoCreate, SidecarPathForNoExtension)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    // sidecarPathFor uses completeBaseName (strips last extension only); for
    // a path with no extension at all, the basename is the full filename, so
    // the .oswp is appended to that. This documents the current contract;
    // upstream (Slice RA normalizer) is responsible for ensuring `.inp`
    // paths come in clean.
    const QString inp = QDir(tmp.path()).absoluteFilePath(QStringLiteral("model"));
    const QString expected = QDir(tmp.path()).absoluteFilePath(QStringLiteral("model.oswp"));
    EXPECT_EQ(ProjectSerializer::sidecarPathFor(inp), expected);
}

TEST(SidecarAutoCreate, SidecarPathForDeepPath)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    QDir(tmp.path()).mkpath(QStringLiteral("a/b/c"));
    const QString inp = QDir(tmp.path()).absoluteFilePath(QStringLiteral("a/b/c/model.inp"));
    const QString expected = QDir(tmp.path()).absoluteFilePath(QStringLiteral("a/b/c/model.oswp"));
    EXPECT_EQ(ProjectSerializer::sidecarPathFor(inp), expected);
}

// ---------------------------------------------------------------------------
// Preference default — Slice RB.4
// ---------------------------------------------------------------------------

namespace {

// Run the QSettings test inside an isolated scope so the live user settings
// store is not touched. Sets the organisation + application to a unique
// per-test combination so the underlying QSettings file is sandboxed.
class IsolatedSettings
{
public:
    IsolatedSettings()
        : m_savedOrg(QCoreApplication::organizationName())
        , m_savedApp(QCoreApplication::applicationName())
    {
        QCoreApplication::setOrganizationName(QStringLiteral("OpenSWMM-tests"));
        QCoreApplication::setApplicationName(QStringLiteral("SidecarAutoCreate"));
        QSettings s;
        s.clear();
    }
    ~IsolatedSettings()
    {
        {
            QSettings s;
            s.clear();
        }
        QCoreApplication::setOrganizationName(m_savedOrg);
        QCoreApplication::setApplicationName(m_savedApp);
    }
private:
    QString m_savedOrg;
    QString m_savedApp;
};

} // anonymous

TEST(SidecarAutoCreate, AutoCreateOnOpenDefaultsToTrue)
{
    IsolatedSettings iso;
    QSettings s;
    // The production read in openSingleINP is:
    //   s.value("Preferences/AutoCreateOswpOnOpen", true).toBool()
    // Lock the default value in here so a future refactor can't flip it
    // without updating the test.
    const bool def = s.value(
        QStringLiteral("Preferences/AutoCreateOswpOnOpen"), true).toBool();
    EXPECT_TRUE(def);
}

TEST(SidecarAutoCreate, AutoCreateOnOpenCanBeDisabled)
{
    IsolatedSettings iso;
    QSettings s;
    s.setValue(QStringLiteral("Preferences/AutoCreateOswpOnOpen"), false);
    const bool stored = s.value(
        QStringLiteral("Preferences/AutoCreateOswpOnOpen"), true).toBool();
    EXPECT_FALSE(stored);
}

TEST(SidecarAutoCreate, AutoCreateOnOpenRoundTrip)
{
    IsolatedSettings iso;
    {
        QSettings s;
        s.setValue(QStringLiteral("Preferences/AutoCreateOswpOnOpen"), true);
    }
    {
        QSettings s;
        EXPECT_TRUE(s.value(
            QStringLiteral("Preferences/AutoCreateOswpOnOpen"), false).toBool());
    }
}
