/**
 * @file extensionmanager.cpp
 * @brief Implementation of metadata-first native GUI extension loading.
 * @details Discovery reads embedded Qt metadata without executing native plugin code.
 * Canonical library paths and validated plugin IDs are deduplicated in search-path order;
 * SDK, Qt, compiler and architecture checks precede QPluginLoader::instance().
 *
 * Startup persists the initializing plugin identity before calling third-party code. Workers
 * and native edit scopes use additional operation markers. A surviving marker disables its
 * owner on the next startup. Safe mode skips initialization while retaining the inventory.
 *
 * Partial initialization joins workers and tears down host contributions. Failure diagnostics
 * survive metadata rescans, and loaded libraries remain pinned until process exit to protect
 * retained Qt callbacks. Enable changes and new libraries take effect after restart.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "plugins/extensionmanager.h"
#include "sdk-build-manifest.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLibrary>
#include <QPluginLoader>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>
#include <algorithm>
#include <exception>

using namespace OpenSWMM::Sdk;
namespace openswmmvis
{
/** @brief One successfully initialized extension and its ordered owned lifetime resources. */
struct ExtensionManager::Loaded {
    std::unique_ptr<QPluginLoader> loader; ///< Owning loader with PreventUnloadHint enabled.
    std::unique_ptr<ExtensionHost> host; ///< Owned service facade, released after plugin shutdown.
    IGuiExtension *extension = nullptr;  ///< Borrowed QObject plugin instance managed by Qt.
    QString id;                          ///< Validated embedded plugin identity.
    QList<ExtensionInfo> children;       ///< Owning child metadata cached for safe rescans.
};

ExtensionManager::ExtensionManager(CompatibilityManifest manifest, QSettings *settings,
                                   QObject *parent)
    : QObject(parent), mManifest(std::move(manifest)), mSettings(settings)
{
    Q_ASSERT(mSettings);
    if (auto *application = QCoreApplication::instance())
        connect(application, &QCoreApplication::aboutToQuit, this, &ExtensionManager::shutdown);
}
ExtensionManager::~ExtensionManager()
{
    shutdown();
}

ExtensionManager *ExtensionManager::instance()
{
    static auto *settings = new QSettings(QCoreApplication::instance());
    static auto *manager =
        new ExtensionManager(buildManifest(), settings, QCoreApplication::instance());
    return manager;
}

CompatibilityManifest ExtensionManager::buildManifest()
{
    return CompatibilityManifest::fromJson(
        QJsonDocument::fromJson(QByteArray(OPENSWMMVIS_SDK_MANIFEST_JSON)).object());
}

QString ExtensionManager::compatibilityError(const CompatibilityManifest &h,
                                             const CompatibilityManifest &p, bool requiresEngineAbi,
                                             QString *warning)
{
    if (warning)
        warning->clear();
    const auto mismatch = [](const QString &field, const QString &host, const QString &plugin) {
        return QStringLiteral("%1 mismatch: host '%2', plugin '%3'.").arg(field, host, plugin);
    };
    if (p.sdkApiVersion.isEmpty() ||
        h.sdkApiVersion.section('.', 0, 0) != p.sdkApiVersion.section('.', 0, 0) ||
        p.sdkApiVersion.section('.', 1, 1).toInt() > h.sdkApiVersion.section('.', 1, 1).toInt())
        return mismatch("sdkApiVersion", h.sdkApiVersion, p.sdkApiVersion);
    if (p.qtVersion.isEmpty() || h.qtVersion.section('.', 0, 1) != p.qtVersion.section('.', 0, 1))
        return mismatch("qt", h.qtVersion, p.qtVersion);
    if (p.compilerAbi.isEmpty() || h.compilerAbi != p.compilerAbi)
        return mismatch("compilerAbi", h.compilerAbi, p.compilerAbi);
    if (p.arch.isEmpty() || h.arch != p.arch)
        return mismatch("arch", h.arch, p.arch);
    if (p.guiVersion.isEmpty() || p.engineVersion.isEmpty())
        return QStringLiteral("Missing guiVersion or engineVersion in compatibility manifest.");
    if (p.engineAbiTag.isEmpty() || h.engineAbiTag.isEmpty() || h.engineAbiTag != p.engineAbiTag) {
        const QString reason = mismatch("engineAbiTag", h.engineAbiTag, p.engineAbiTag);
        if (requiresEngineAbi)
            return reason;
        if (warning)
            *warning = reason;
    }
    return {};
}

QStringList ExtensionManager::directories() const
{
    QStringList paths{QDir(QCoreApplication::applicationDirPath()).filePath("plugins/gui"),
                      QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                          .filePath("plugins")};
    paths.append(mSettings->value("extensions/directories").toStringList());
    paths.append(qEnvironmentVariable("SWMMVIS_GUI_EXTENSION_PATH")
                     .split(QDir::listSeparator(), Qt::SkipEmptyParts));
    paths.removeDuplicates();
    return paths;
}
void ExtensionManager::setEngineRuntimeAbiTag(const QString &tag)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!mStarted)
        mManifest.engineAbiTag = tag;
}
void ExtensionManager::addDirectory(const QString &path)
{
    if (!QFileInfo(path).isDir())
        return;
    auto paths = mSettings->value("extensions/directories").toStringList();
    paths.append(QFileInfo(path).canonicalFilePath());
    paths.removeDuplicates();
    mSettings->setValue("extensions/directories", paths);
    mSettings->sync();
    rescan();
}
void ExtensionManager::setEnabled(const QString &id, bool enabled)
{
    const auto it =
        std::find_if(mRecords.cbegin(), mRecords.cend(),
                     [&](const GuiExtensionRecord &record) { return record.info.id == id; });
    if (it == mRecords.cend())
        return;
    mSettings->setValue("extensions/enabled/" + id, enabled);
    mSettings->sync();
    rescan();
}

void ExtensionManager::rescan()
{
    Q_ASSERT(QThread::currentThread() == thread());
    mRecords.clear();
    QSet<QString> seenPaths, seenIds;
    const QRegularExpression validId(QStringLiteral("^[A-Za-z][A-Za-z0-9_.-]*$"));
    for (const auto &directory : directories()) {
        const QDir dir(directory);
        for (const auto &file : dir.entryInfoList(QDir::Files, QDir::Name)) {
            if (!QLibrary::isLibrary(file.fileName()))
                continue;
            const auto path = file.canonicalFilePath();
            if (seenPaths.contains(path))
                continue;
            seenPaths.insert(path);
            QPluginLoader probe(path);
            const auto metadata = probe.metaData();
            // Ignore unrelated Qt plugins, including platform and image-format plugins.
            if (metadata.value("IID").toString() != QLatin1String(GUI_EXTENSION_IID))
                continue;
            const auto json = metadata.value("MetaData").toObject();
            GuiExtensionRecord record;
            record.path = path;
            record.info = {json["id"].toString(),          json["caption"].toString(),
                           json["description"].toString(), json["version"].toString(),
                           json["vendor"].toString(),      json["url"].toString(),
                           json["licenseType"].toString(), {}};
            record.builtAgainst =
                CompatibilityManifest::fromJson(json.value("builtAgainst").toObject());
            record.requiresEngineAbi = json.value("requiresEngineAbi").toBool();
            const auto id = record.info.id;
            if (!validId.match(id).hasMatch() || id.contains("..") ||
                QStringList{"enabled", "operations", "directories", "loading", "network"}.contains(
                    id))
                record.error = tr("Invalid extension id.");
            else if (seenIds.contains(id))
                record.error = tr("Duplicate extension id; another directory has priority.");
            else {
                seenIds.insert(id);
                record.error = compatibilityError(mManifest, record.builtAgainst,
                                                  record.requiresEngineAbi, &record.warning);
            }
            record.enabled = mSettings->value("extensions/enabled/" + id, true).toBool();
            record.state = !record.error.isEmpty() ? tr("Incompatible")
                           : mSafeMode             ? tr("Safe mode")
                           : !record.enabled       ? tr("Disabled")
                                                   : tr("Available");
            if (record.error.isEmpty() && mFailures.contains(path)) {
                record.error = mFailures.value(path);
                record.state = tr("Failed");
            }
            if (mWarnings.contains(id))
                record.warning = mWarnings.value(id);
            for (const auto &loaded : mLoaded) {
                if (loaded->loader->fileName() == path) {
                    record.loaded = true;
                    record.state =
                        record.enabled ? tr("Loaded") : tr("Loaded — restart to disable");
                    record.children = loaded->children;
                    break;
                }
            }
            if (mStarted && !record.loaded && record.error.isEmpty() && record.enabled &&
                !mSafeMode)
                record.state = tr("Restart to load");
            mRecords.append(record);
        }
    }
    emit changed();
}

void ExtensionManager::start(HostFactory factory, bool safeMode)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (mStarted)
        return;
    mSafeMode = safeMode;
    const auto crashed = mSettings->value("extensions/loading").toString();
    mSettings->beginGroup("extensions/operations");
    const auto interrupted = mSettings->childGroups();
    mSettings->endGroup();
    for (const auto &id : interrupted) {
        mSettings->setValue("extensions/enabled/" + id, false);
        mWarnings[id] = tr("Disabled after an interrupted extension job. Re-enable to retry.");
    }
    mSettings->remove("extensions/operations");
    if (!crashed.isEmpty()) {
        mSettings->setValue("extensions/enabled/" + crashed, false);
        mSettings->remove("extensions/loading");
        mSettings->sync();
        mWarnings[crashed] =
            tr("Disabled after an interrupted extension operation. Re-enable to retry.");
    }
    rescan();
    mStarted = true;
    for (auto &record : mRecords) {
        if (record.info.id == crashed)
            record.warning =
                tr("Disabled after an interrupted extension operation. Re-enable to retry.");
        if (mSafeMode || !record.enabled || !record.error.isEmpty())
            continue;
        auto loaded = std::make_unique<Loaded>();
        loaded->id = record.info.id;
        loaded->loader = std::make_unique<QPluginLoader>(record.path);
        loaded->loader->setLoadHints(QLibrary::PreventUnloadHint);
        mSettings->setValue("extensions/loading", record.info.id);
        mSettings->sync();
        try {
            loaded->extension = qobject_cast<IGuiExtension *>(loaded->loader->instance());
            if (!loaded->extension)
                record.error = loaded->loader->errorString();
            else if (loaded->extension->info().id != record.info.id)
                record.error = tr("Extension id differs from its metadata.");
            else {
                loaded->host = factory(record.info.id);
                if (!loaded->host)
                    record.error = tr("Could not create the extension host.");
                else if (!loaded->extension->initialize(loaded->host.get(), &record.error) &&
                         record.error.isEmpty())
                    record.error = tr("Extension initialization failed.");
            }
        } catch (const std::exception &error) {
            record.error = QString::fromUtf8(error.what());
        } catch (...) {
            record.error = tr("Extension initialization threw an unknown exception.");
        }
        mSettings->remove("extensions/loading");
        mSettings->sync();
        if (!record.error.isEmpty()) {
            record.state = tr("Failed");
            if (loaded->extension && loaded->host) {
                loaded->host->prepareShutdown();
                try {
                    loaded->extension->shutdown();
                } catch (...) {
                }
            }
            // The host destroys partial contributions before the pinned loader is released.
            continue;
        }
        record.loaded = true;
        record.state = tr("Loaded");
        try {
            loaded->children = loaded->extension->children();
        } catch (...) {
        }
        record.children = loaded->children;
        mLoaded.push_back(std::move(loaded));
    }
    for (const auto &record : mRecords)
        if (!record.error.isEmpty() && record.state == tr("Failed"))
            mFailures[record.path] = record.error;
    emit changed();
}

void ExtensionManager::shutdown()
{
    // Reverse dependency order. No QPluginLoader::unload: callbacks and Qt meta-types may
    // still contain code pointers even after their widgets have gone away.
    for (auto it = mLoaded.rbegin(); it != mLoaded.rend(); ++it) {
        (*it)->host->prepareShutdown();
        try {
            (*it)->extension->shutdown();
        } catch (...) {
        }
        (*it)->host.reset();
    }
    mLoaded.clear();
}

void ExtensionManager::refreshModels()
{
    for (const auto &loaded : mLoaded)
        loaded->host->projects();
}
}
